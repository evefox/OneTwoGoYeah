import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_diagnostics.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/infrastructure/playback_output.dart';
import 'package:one_two_go_yeah/src/ui/editor_screen.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  const filesChannel = MethodChannel('io.onetwogoyeah/files');
  final binding = TestWidgetsFlutterBinding.ensureInitialized();
  final messenger = binding.defaultBinaryMessenger;
  late Directory directory;
  late Directory projectDirectory;
  late Directory logDirectory;
  late LocalProjectStore store;
  late LocalDiagnostics logger;
  late EditorController controller;
  Completer<String?>? picker;

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-diagnostic-life-');
    projectDirectory = Directory('${directory.path}/projects');
    logDirectory = Directory('${directory.path}/diagnostics');
    store = await LocalProjectStore.openAt(projectDirectory);
    logger = await LocalDiagnostics.openAt(logDirectory, identity: {});
    controller = EditorController(playbackOutput: _PlaybackOutput());
    picker = null;
  });

  tearDown(() async {
    messenger.setMockMethodCallHandler(filesChannel, null);
    controller.dispose();
    await directory.delete(recursive: true);
  });

  Future<List<Map<String, dynamic>>> records() async {
    await logger.flush();
    final files = await logDirectory
        .list()
        .where((entry) => entry is File)
        .toList();
    final result = <Map<String, dynamic>>[];
    for (final file in files.cast<File>()) {
      for (final line in await file.readAsLines()) {
        if (line.isNotEmpty) {
          result.add(jsonDecode(line) as Map<String, dynamic>);
        }
      }
    }
    return result;
  }

  Future<List<Map<String, dynamic>>> events(String name) async =>
      (await records()).where((record) => record['event'] == name).toList();

  Future<void> waitUntil(
    WidgetTester tester,
    Future<bool> Function() condition,
  ) async {
    var checking = false;
    var matched = false;
    Object? failure;
    StackTrace? failureStack;
    for (var attempt = 0; attempt < 100; attempt++) {
      await tester.runAsync(() async {
        if (!checking && !matched) {
          checking = true;
          unawaited(
            condition().then(
              (value) {
                matched = value;
                checking = false;
              },
              onError: (Object error, StackTrace stack) {
                failure = error;
                failureStack = stack;
                checking = false;
              },
            ),
          );
        }
        // Do not await a check here: logger.flush can depend on a writer
        // started in fake async. Pump its continuations between I/O turns.
        await Future<void>.delayed(const Duration(milliseconds: 10));
      });
      await tester.pump();
      if (failure != null) {
        Error.throwWithStackTrace(failure!, failureStack!);
      }
      if (matched) break;
    }
    expect(
      matched,
      isTrue,
      reason: 'The real filesystem operation must finish.',
    );
  }

  Future<void> runIo(WidgetTester tester, Future<void> Function() action) =>
      waitUntil(tester, () async {
        await action();
        return true;
      });

  Future<void> drainIo(WidgetTester tester) async {
    await runIo(tester, () async {
      await Future<void>.delayed(const Duration(milliseconds: 20));
      await logger.flush();
    });
  }

  Future<void> pumpScreen(WidgetTester tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: EditorScreen(
          diagnostics: logger,
          controller: controller,
          storeFactory: () async => store,
        ),
      ),
    );
    await waitUntil(
      tester,
      () async => (await events('store.session.started')).isNotEmpty,
    );
  }

  void lifecycleTest(
    String description,
    Future<void> Function(WidgetTester) body,
  ) {
    testWidgets(description, (tester) async {
      final originalPlatform = debugDefaultTargetPlatformOverride;
      debugDefaultTargetPlatformOverride = TargetPlatform.android;
      binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
      try {
        await body(tester);
      } finally {
        try {
          final pending = picker;
          if (pending != null && !pending.isCompleted) pending.complete(null);
          binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
          await drainIo(tester);
          await tester.pumpWidget(const SizedBox.shrink());
          // Flush while the fake-async zone that owns the writer still exists.
          await drainIo(tester);
        } finally {
          debugDefaultTargetPlatformOverride = originalPlatform;
        }
      }
    });
  }

  Future<void> openMenu(WidgetTester tester) async {
    await tester.tap(find.byKey(const ValueKey('chart-menu')));
    await tester.pumpAndSettle();
  }

  MenuItemButton diagnosticItem(WidgetTester tester) {
    final anchor = tester.widget<MenuAnchor>(
      find
          .ancestor(
            of: find.byKey(const ValueKey('chart-menu')),
            matching: find.byType(MenuAnchor),
          )
          .first,
    );
    return anchor.menuChildren.whereType<MenuItemButton>().singleWhere(
      (item) => item.key == const ValueKey('export-diagnostics'),
    );
  }

  List<String> installPicker() {
    picker = Completer<String?>();
    final methods = <String>[];
    messenger.setMockMethodCallHandler(filesChannel, (call) async {
      methods.add(call.method);
      switch (call.method) {
        case 'createDocument':
          return picker!.future;
        case 'openDocumentWrite':
          return 7;
        case 'writeDocumentChunk':
        case 'finishDocumentWrite':
        case 'commitDocument':
        case 'abortDocumentWrite':
        case 'deleteDocument':
          return null;
        default:
          fail('Unexpected method ${call.method}');
      }
    });
    return methods;
  }

  Future<void> startPicker(WidgetTester tester, List<String> methods) async {
    await tester.tap(find.byKey(const ValueKey('export-diagnostics')));
    await tester.pump();
    await waitUntil(tester, () async => methods.contains('createDocument'));
  }

  Map<String, List<int>> projectFiles() => {
    for (final file in projectDirectory.listSync().whereType<File>())
      file.uri.pathSegments.last: file.readAsBytesSync(),
  };

  lifecycleTest(
    'real edited draft recovers at the 500 ms debounce and logs completion',
    (tester) async {
      await pumpScreen(tester);
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 3);
      final payload = controller.encodeRecoverySnapshot();
      final revision = controller.projectRevision;
      expect(controller.isDirty, isTrue);

      await tester.pump(const Duration(milliseconds: 499));
      await runIo(tester, () async {
        expect(await store.readRecovery(), isNull);
        expect(await events('recovery.write.completed'), isEmpty);
      });
      await tester.pump(const Duration(milliseconds: 1));
      await waitUntil(
        tester,
        () async => (await events('recovery.write.completed')).isNotEmpty,
      );
      await runIo(tester, () async {
        expect(await store.readRecovery(), payload);
        final writes = await events('recovery.write.completed');
        expect(writes, hasLength(1));
        expect(writes.single['fields'], {
          'revision': revision,
          'bytes': payload.length,
        });
        final restored = NativeChartEditor.decodeRecovery(
          (await store.readRecovery())!,
        );
        try {
          expect(restored.markers(), hasLength(1));
        } finally {
          restored.dispose();
        }
      });
      expect(tester.takeException(), isNull);
    },
  );

  lifecycleTest(
    'real save persists the draft and logs completion without its name',
    (tester) async {
      const privateName = 'PRIVATE-DRAFT-NAME-SENTINEL';
      await pumpScreen(tester);
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 3);
      await tester.pump(const Duration(milliseconds: 500));
      await waitUntil(
        tester,
        () async => (await events('recovery.write.completed')).isNotEmpty,
      );
      final payload = controller.encodeFormalDraft();

      await tester.tap(find.byKey(const ValueKey('save-chart')));
      await tester.pumpAndSettle();
      expect(find.text('保存草稿'), findsOneWidget);
      await tester.enterText(find.byType(TextFormField), privateName);
      await tester.tap(find.widgetWithText(FilledButton, '保存'));
      await tester.pump();
      await waitUntil(
        tester,
        () async => (await events('draft.save.completed')).isNotEmpty,
      );
      expect(controller.sourceKind, WorkspaceSourceKind.savedDraft);
      expect(controller.isDirty, isFalse);
      expect(controller.chartName, privateName);
      await runIo(tester, () async {
        final drafts = await store.listDrafts();
        expect(drafts, hasLength(1));
        expect(drafts.single.name, privateName);
        expect(drafts.single.payload, payload);
        expect(await store.readRecovery(), isNull);
        final saves = await events('draft.save.completed');
        expect(saves, hasLength(1));
        expect(saves.single['fields'], {
          'saveAs': false,
          'revision': controller.projectRevision,
          'bytes': payload.length,
        });
        expect(jsonEncode(await records()), isNot(contains(privateName)));
        expect(jsonEncode(await records()), isNot(contains(directory.path)));
      });
      expect(tester.takeException(), isNull);
    },
  );

  lifecycleTest(
    'hidden and paused during a diagnostic picker create no recovery write',
    (tester) async {
      await pumpScreen(tester);
      await openMenu(tester);
      final methods = installPicker();
      final before = projectFiles();
      await startPicker(tester, methods);

      binding.handleAppLifecycleStateChanged(AppLifecycleState.hidden);
      binding.handleAppLifecycleStateChanged(AppLifecycleState.paused);
      await tester.pump(const Duration(milliseconds: 600));
      await drainIo(tester);
      expect(projectFiles(), before);
      await runIo(tester, () async {
        expect(await store.readRecovery(), isNull);
        expect(await events('recovery.write.completed'), isEmpty);
      });

      binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
      picker!.complete('content://test/lifecycle.json');
      await waitUntil(
        tester,
        () async => (await events('diagnostics.export.completed')).isNotEmpty,
      );
      expect(methods, [
        'createDocument',
        'openDocumentWrite',
        'writeDocumentChunk',
        'finishDocumentWrite',
        'commitDocument',
      ]);
      expect(projectFiles(), before);
      expect(tester.takeException(), isNull);
    },
  );

  lifecycleTest(
    'an existing recovery timer still fires while the diagnostic picker is paused',
    (tester) async {
      await pumpScreen(tester);
      await openMenu(tester);
      final methods = installPicker();
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 3);
      final payload = controller.encodeRecoverySnapshot();
      final revision = controller.projectRevision;
      await startPicker(tester, methods);

      binding.handleAppLifecycleStateChanged(AppLifecycleState.hidden);
      binding.handleAppLifecycleStateChanged(AppLifecycleState.paused);
      await tester.pump(const Duration(milliseconds: 499));
      await runIo(tester, () async {
        expect(await store.readRecovery(), isNull);
        expect(await events('recovery.write.completed'), isEmpty);
      });
      await tester.pump(const Duration(milliseconds: 1));
      await waitUntil(
        tester,
        () async => (await events('recovery.write.completed')).isNotEmpty,
      );
      await runIo(tester, () async {
        expect(await store.readRecovery(), payload);
        final writes = await events('recovery.write.completed');
        expect(writes, hasLength(1));
        expect((writes.single['fields'] as Map)['revision'], revision);
      });
      expect(methods, ['createDocument']);
      binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
      picker!.complete(null);
      await waitUntil(
        tester,
        () async => (await events('diagnostics.export.cancelled')).isNotEmpty,
      );
      await tester.pump(const Duration(milliseconds: 600));
      await runIo(tester, () async {
        expect(await events('recovery.write.completed'), hasLength(1));
      });
      expect(tester.takeException(), isNull);
    },
  );

  lifecycleTest(
    'detached cancels diagnostic output, cleans the document and flushes recovery',
    (tester) async {
      await pumpScreen(tester);
      await openMenu(tester);
      final methods = installPicker();
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 3);
      final payload = controller.encodeRecoverySnapshot();
      await startPicker(tester, methods);

      binding.handleAppLifecycleStateChanged(AppLifecycleState.detached);
      await waitUntil(
        tester,
        () async => (await events('recovery.write.completed')).isNotEmpty,
      );
      picker!.complete('content://test/detached.json');
      await waitUntil(tester, () async => methods.contains('deleteDocument'));
      await runIo(tester, () async {
        expect(await store.readRecovery(), payload);
        expect(await events('recovery.write.completed'), hasLength(1));
        expect(await events('diagnostics.export.cancelled'), hasLength(1));
        expect(await events('diagnostics.export.completed'), isEmpty);
        expect(
          File('${projectDirectory.path}/active-session').existsSync(),
          isTrue,
        );
      });
      expect(methods, ['createDocument', 'deleteDocument']);
      expect(controller.isDirty, isTrue);
      expect(controller.encodeRecoverySnapshot(), payload);
      binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
      await tester.pump(const Duration(milliseconds: 600));
      await runIo(tester, () async {
        expect(await events('recovery.write.completed'), hasLength(1));
      });
      expect(tester.takeException(), isNull);
    },
  );

  lifecycleTest(
    'diagnostic menu is disabled during real playback and enabled after pause',
    (tester) async {
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');
      await pumpScreen(tester);
      expect(diagnosticItem(tester).onPressed, isNotNull);
      await controller.togglePlayback();
      await tester.pump();
      try {
        expect(controller.isPlaying, isTrue);
        expect(diagnosticItem(tester).onPressed, isNull);
        expect(
          tester
              .widget<TextButton>(find.byKey(const ValueKey('chart-menu')))
              .onPressed,
          isNull,
        );
      } finally {
        await controller.pausePlayback();
      }
      await tester.pump();
      expect(diagnosticItem(tester).onPressed, isNotNull);
      expect(tester.takeException(), isNull);
    },
  );

  lifecycleTest(
    'diagnostic menu is disabled while real audio export is busy saving',
    (tester) async {
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');
      await pumpScreen(tester);
      final saveEntered = Completer<void>();
      final releaseSave = Completer<void>();
      late Future<void> export;
      try {
        await runIo(tester, () async {
          export = controller.exportAudio(
            quality: 1,
            baseName: 'fixture',
            save: (result, isCancelled, onProgress) async {
              saveEntered.complete();
              await releaseSave.future;
            },
          );
          await Future.any<void>([saveEntered.future, export]);
        });
        expect(saveEntered.isCompleted, isTrue);
        await tester.pump();
        expect(controller.isBusy, isTrue);
        expect(controller.isExportingAudio, isTrue);
        expect(diagnosticItem(tester).onPressed, isNull);
        expect(
          tester
              .widget<TextButton>(find.byKey(const ValueKey('chart-menu')))
              .onPressed,
          isNull,
        );
      } finally {
        releaseSave.complete();
        await runIo(tester, () => export);
      }
      await tester.pump();
      expect(controller.isBusy, isFalse);
      expect(diagnosticItem(tester).onPressed, isNotNull);
      expect(tester.takeException(), isNull);
    },
  );
}

final class _PlaybackOutput implements PlaybackOutput {
  bool _playing = false;

  @override
  Future<void> start({
    required int sampleRate,
    required Uint8List interleavedPcm16le,
  }) async {
    _playing = true;
  }

  @override
  Future<PlaybackDeviceState> query() async =>
      PlaybackDeviceState(playedFrames: 0, playing: _playing);

  @override
  Future<PlaybackDeviceState> pause() async {
    _playing = false;
    return const PlaybackDeviceState(playedFrames: 0, playing: false);
  }

  @override
  Future<void> dispose() async {
    _playing = false;
  }
}
