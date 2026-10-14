import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart' as app;
import 'package:one_two_go_yeah/src/infrastructure/local_diagnostics.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late Directory directory;
  late LocalDiagnostics logger;
  FlutterExceptionHandler? previousFlutterError;
  late FlutterExceptionHandler previousPresenter;
  bool Function(Object, StackTrace)? previousPlatformError;
  LocalDiagnostics? previousLogger;

  setUp(() async {
    previousFlutterError = FlutterError.onError;
    previousPresenter = FlutterError.presentError;
    previousPlatformError = PlatformDispatcher.instance.onError;
    previousLogger = LocalDiagnostics.instance;
    directory = await Directory.systemTemp.createTemp('otgy-main-diagnostics-');
    logger = await LocalDiagnostics.openAt(directory, identity: {});
  });

  tearDown(() async {
    FlutterError.onError = previousFlutterError;
    FlutterError.presentError = previousPresenter;
    PlatformDispatcher.instance.onError = previousPlatformError;
    LocalDiagnostics.instance = previousLogger;
    await logger.flush();
    if (await directory.exists()) await directory.delete(recursive: true);
  });

  Future<List<Map<String, dynamic>>> records() async {
    final bundle = await logger.exportBundle(
      recoveryPayload: Uint8List(0),
      state: {},
    );
    return (bundle['logs'] as List).cast<Map<String, dynamic>>();
  }

  final stack = StackTrace.fromString(
    '#0      load (/Users/PRIVATE/song.dart:1:2)\n'
    '#1      decode (file:///Users/PRIVATE/audio.dart:3:4)\n'
    r'#2      save (C:\PRIVATE\editor.dart:5:6)'
    '\n#3      EditorController.clickCell '
    '(package:one_two_go_yeah/src/application/editor_controller.dart:42:7)\n'
    '#4      main (dart:isolate-patch/isolate_patch.dart:9:10)',
  );

  test(
    'Flutter hook logs sanitized stack and chains original details',
    () async {
      final error = StateError('PRIVATE song.mp3');
      final details = FlutterErrorDetails(exception: error, stack: stack);
      final forwarded = <FlutterErrorDetails>[];
      FlutterError.onError = forwarded.add;
      app.installDiagnosticErrorHandlers(logger);

      FlutterError.onError!(details);

      expect(forwarded, [same(details)]);
      final logs = await records();
      expect(logs, hasLength(1));
      expect(logs.single['event'], 'exception');
      final fields = logs.single['fields'] as Map;
      expect(fields['operation'], 'flutter');
      expect(fields['type'], 'StateError');
      final recordedStack = fields['stack'] as String;
      expect(recordedStack, contains('[redacted path]'));
      expect(recordedStack, contains('[redacted uri]'));
      expect(
        recordedStack,
        contains(
          'package:one_two_go_yeah/src/application/editor_controller.dart:42:7',
        ),
      );
      expect(
        recordedStack,
        contains('dart:isolate-patch/isolate_patch.dart:9:10'),
      );
      expect(jsonEncode(logs), isNot(contains('PRIVATE')));
    },
  );

  test('Flutter hook uses default presenter when no handler exists', () async {
    final presented = <FlutterErrorDetails>[];
    FlutterError.onError = null;
    FlutterError.presentError = presented.add;
    app.installDiagnosticErrorHandlers(logger);
    final details = FlutterErrorDetails(exception: StateError('PRIVATE'));

    FlutterError.onError!(details);

    expect(presented, [same(details)]);
    final logs = await records();
    final fields = logs.single['fields'] as Map;
    expect(fields['operation'], 'flutter');
    expect(fields['stack'], '');
    expect(jsonEncode(logs), isNot(contains('PRIVATE')));
  });

  for (final handled in [true, false]) {
    test('platform hook preserves original return value $handled', () async {
      final error = StateError('PRIVATE song.mp3');
      final forwarded = <({Object error, StackTrace stack})>[];
      PlatformDispatcher.instance.onError = (error, stack) {
        forwarded.add((error: error, stack: stack));
        return handled;
      };
      app.installDiagnosticErrorHandlers(logger);

      expect(PlatformDispatcher.instance.onError!(error, stack), handled);

      expect(forwarded, hasLength(1));
      expect(forwarded.single.error, same(error));
      expect(forwarded.single.stack, same(stack));
      final logs = await records();
      expect(logs, hasLength(1));
      final fields = logs.single['fields'] as Map;
      expect(fields['operation'], 'platform');
      expect(fields['stack'], contains('[redacted path]'));
      expect(
        fields['stack'],
        contains(
          'package:one_two_go_yeah/src/application/editor_controller.dart:42:7',
        ),
      );
      expect(jsonEncode(logs), isNot(contains('PRIVATE')));
    });
  }

  test('platform hook returns false when no handler exists', () async {
    PlatformDispatcher.instance.onError = null;
    app.installDiagnosticErrorHandlers(logger);

    expect(
      PlatformDispatcher.instance.onError!(StateError('PRIVATE'), stack),
      isFalse,
    );

    final logs = await records();
    expect((logs.single['fields'] as Map)['operation'], 'platform');
    expect(jsonEncode(logs), isNot(contains('PRIVATE')));
  });

  test('original handler exceptions propagate unchanged', () async {
    final failure = StateError('original handler failure');
    FlutterError.onError = (_) => throw failure;
    PlatformDispatcher.instance.onError = (_, _) => throw failure;
    app.installDiagnosticErrorHandlers(logger);

    expect(
      () => FlutterError.onError!(
        FlutterErrorDetails(exception: StateError('PRIVATE'), stack: stack),
      ),
      throwsA(same(failure)),
    );
    expect(
      () => PlatformDispatcher.instance.onError!(StateError('PRIVATE'), stack),
      throwsA(same(failure)),
    );

    final logs = await records();
    expect(logs, hasLength(2));
    expect(jsonEncode(logs), isNot(contains('PRIVATE')));
  });

  test(
    'storage failure preserves chaining without recursive logging',
    () async {
      var flutterCalls = 0;
      var platformCalls = 0;
      FlutterError.onError = (_) => flutterCalls++;
      PlatformDispatcher.instance.onError = (_, _) {
        platformCalls++;
        return true;
      };
      app.installDiagnosticErrorHandlers(logger);
      await directory.delete(recursive: true);

      FlutterError.onError!(
        FlutterErrorDetails(exception: StateError('PRIVATE'), stack: stack),
      );
      expect(
        PlatformDispatcher.instance.onError!(StateError('PRIVATE'), stack),
        isTrue,
      );
      await logger.flush();

      expect(flutterCalls, 1);
      expect(platformCalls, 1);
      expect(logger.failure, isNotNull);
      expect(logger.droppedRecords, 2);
    },
  );

  test(
    'disabled app initialization leaves global handlers unchanged',
    () async {
      if (LocalDiagnostics.detailedEnabled) return;
      var directoryCalls = 0;
      const channel = MethodChannel('plugins.flutter.io/path_provider');
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (_) async {
            directoryCalls++;
            throw StateError('disabled startup must not initialize storage');
          });
      LocalDiagnostics.instance = logger;
      void flutterHandler(FlutterErrorDetails _) {}
      bool platformHandler(Object _, StackTrace _) => true;
      FlutterError.onError = flutterHandler;
      PlatformDispatcher.instance.onError = platformHandler;
      try {
        expect(await app.initializeAppDiagnostics(), isNull);
        expect(FlutterError.onError, same(flutterHandler));
        expect(PlatformDispatcher.instance.onError, same(platformHandler));
        expect(directoryCalls, 0);
        expect(await records(), isEmpty);
      } finally {
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
            .setMockMethodCallHandler(channel, null);
      }
    },
  );
}
