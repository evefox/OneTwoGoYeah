import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_diagnostics.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory directory;
  final loggers = <LocalDiagnostics>[];

  Future<LocalDiagnostics> open({int bytes = 262144, int files = 4}) async {
    final logger = await LocalDiagnostics.openAt(
      directory,
      identity: {'appVersion': 'test', 'apiVersion': 1},
      maxFileBytes: bytes,
      maxFiles: files,
    );
    loggers.add(logger);
    return logger;
  }

  Future<Map<String, Object?>> bundle(LocalDiagnostics logger) =>
      logger.exportBundle(recoveryPayload: Uint8List(0), state: {});

  List<Map<String, dynamic>> records(Map<String, Object?> value) =>
      (value['logs'] as List).cast<Map<String, dynamic>>();

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-diagnostics-');
  });

  tearDown(() async {
    for (final logger in loggers) {
      await logger.flush();
    }
    loggers.clear();
    LocalDiagnostics.instance = null;
    if (await directory.exists()) await directory.delete(recursive: true);
  });

  test('default initialization never requests a support directory', () async {
    if (LocalDiagnostics.detailedEnabled) return;
    var calls = 0;
    const channel = MethodChannel('plugins.flutter.io/path_provider');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (_) async {
          calls++;
          throw StateError('disabled startup must not initialize storage');
        });
    try {
      expect(await LocalDiagnostics.initialize(), isNull);
      expect(LocalDiagnostics.instance, isNull);
      expect(calls, 0);
    } finally {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    }
  });

  test('snapshot fallback has build identity and the full bundle schema', () {
    final payload = Uint8List.fromList([0, 255, 4]);
    final nested = <String, Object?>{'dirty': true};
    final state = <String, Object?>{
      'nested': nested,
      'draftName': 'private title',
    };
    final before = DateTime.now().toUtc();
    final result = LocalDiagnostics.snapshotBundle(
      recoveryPayload: payload,
      state: state,
    );
    payload.fillRange(0, payload.length, 10);
    nested['dirty'] = false;
    expect(result['schemaVersion'], 1);
    expect(result['session'], isA<String>());
    expect(result['logs'], isEmpty);
    expect(
      result['detailedDiagnosticsEnabled'],
      LocalDiagnostics.detailedEnabled,
    );
    expect(
      result['storageFailed'],
      LocalDiagnostics.initializationFailure != null,
    );
    expect(
      result['initializationFailure'],
      LocalDiagnostics.initializationFailure,
    );
    expect(result['corruptLines'], 0);
    expect(result['droppedRecords'], 0);
    expect(base64Decode((result['snapshot'] as Map)['payload'] as String), [
      0,
      255,
      4,
    ]);
    expect(result['captureState'], {
      'nested': {'dirty': true},
      'draftName': '[redacted]',
    });
    final identity = result['identity'] as Map;
    expect(
      identity['appVersion'],
      const String.fromEnvironment('OTGY_APP_VERSION'),
    );
    expect(identity['buildId'], const String.fromEnvironment('OTGY_BUILD_ID'));
    expect(
      identity['buildTime'],
      const String.fromEnvironment('OTGY_BUILD_TIME'),
    );
    expect(identity['operatingSystem'], Platform.operatingSystem);
    expect(identity['buildMode'], LocalDiagnostics.buildMode);
    expect(identity.keys, containsAll(['apiVersion', 'coreVersion']));
    if (identity['nativeVersionFailure'] case final String failure) {
      final summary = jsonDecode(failure) as Map;
      expect(summary['type'], isA<String>());
      expect(
        summary.keys,
        everyElement(isIn(['type', 'code', 'domain', 'status', 'osCode'])),
      );
    }
    final capturedAt = DateTime.parse(result['capturedAt'] as String);
    final createdAt = DateTime.parse(result['createdAt'] as String);
    expect(capturedAt.isBefore(before), isFalse);
    expect(createdAt.isBefore(capturedAt), isFalse);
    expect(() => jsonEncode(result), returnsNormally);
  });

  test(
    'enabled initialization failure is visible in the fallback bundle',
    () async {
      if (!LocalDiagnostics.detailedEnabled) return;
      // No path-provider plugin is registered in the unit-test process.
      expect(await LocalDiagnostics.initialize(), isNull);
      expect(LocalDiagnostics.initializationFailure, isNotNull);
      final failure =
          jsonDecode(LocalDiagnostics.initializationFailure!) as Map;
      expect(
        failure.keys,
        everyElement(isIn(['type', 'code', 'domain', 'status', 'osCode'])),
      );
      expect(failure['type'], isA<String>());
      final result = LocalDiagnostics.snapshotBundle(
        recoveryPayload: Uint8List.fromList([7]),
        state: {},
      );
      expect(result['storageFailed'], isTrue);
      expect(
        result['initializationFailure'],
        LocalDiagnostics.initializationFailure,
      );
      expect(result['failure'], LocalDiagnostics.initializationFailure);
      expect((result['snapshot'] as Map)['payload'], base64Encode([7]));
    },
  );

  test(
    'startup records retain the identity of each build and session',
    () async {
      final first = await open();
      first.record('startup', {
        'identity': {'appVersion': 'wrong'},
      });
      await first.flush();
      final second = await LocalDiagnostics.openAt(
        directory,
        identity: {'appVersion': 'next-build', 'apiVersion': 2},
      );
      loggers.add(second);
      second.record('startup', {});
      final logs = records(await bundle(second));
      expect((logs.first['fields'] as Map)['identity'], first.identity);
      expect((logs.last['fields'] as Map)['identity'], second.identity);
      expect(logs.map((log) => log['session']), [
        first.session,
        second.session,
      ]);
    },
  );

  test(
    'export fixes payload and nested state before its first await',
    () async {
      final logger = await open();
      logger.record('edit', {});
      final payload = Uint8List.fromList([1, 2, 3]);
      final values = <Object?>[
        true,
        {'count': 1},
      ];
      final state = <String, Object?>{'values': values};
      final before = DateTime.now().toUtc();
      final future = logger.exportBundle(
        recoveryPayload: payload,
        state: state,
      );
      final afterCall = DateTime.now().toUtc();
      payload.fillRange(0, payload.length, 9);
      (values[1] as Map)['count'] = 20;
      values[0] = false;
      state['new'] = true;
      final result = await future;
      expect(base64Decode((result['snapshot'] as Map)['payload'] as String), [
        1,
        2,
        3,
      ]);
      expect(result['captureState'], {
        'values': [
          true,
          {'count': 1},
        ],
      });
      final capturedAt = DateTime.parse(result['capturedAt'] as String);
      expect(capturedAt.isBefore(before), isFalse);
      expect(capturedAt.isAfter(afterCall), isFalse);
      expect(
        DateTime.parse(result['createdAt'] as String).isBefore(capturedAt),
        isFalse,
      );
      final fallback = LocalDiagnostics.snapshotBundle(
        recoveryPayload: Uint8List(0),
        state: {},
      );
      expect(result.keys.toSet(), fallback.keys.toSet());
    },
  );

  test('queued records preserve order and structured metadata', () async {
    final logger = await open();
    for (var index = 0; index < 40; index++) {
      logger.record('edit', {
        'index': index,
        'nested': {
          'ok': true,
          'values': [null, 1, 1.25],
        },
      });
    }
    final logs = records(await bundle(logger));
    expect(logs, hasLength(40));
    expect(logs.map((log) => log['sequence']), List.generate(40, (i) => i + 1));
    expect(logs.map((log) => log['session']).toSet(), {logger.session});
    expect(DateTime.parse(logs.first['time'] as String).isUtc, isTrue);
    expect((logs.last['fields'] as Map)['index'], 39);
    expect((logs.first['fields'] as Map)['nested'], {
      'ok': true,
      'values': [null, 1, 1.25],
    });
    expect(logger.failure, isNull);
  });

  test('rotation bounds file count and every file byte length', () async {
    final logger = await open(bytes: 512, files: 3);
    for (var index = 0; index < 30; index++) {
      logger.record('edit', {'index': index, 'detail': 'x' * 100});
      await logger.flush();
    }
    final files = await directory.list().where((e) => e is File).toList();
    expect(files, hasLength(3));
    for (final file in files.cast<File>()) {
      expect(await file.length(), lessThanOrEqualTo(512));
      for (final line in await file.readAsLines()) {
        expect(jsonDecode(line), isA<Map>());
      }
    }
    final logs = records(await bundle(logger));
    final indices = logs.map((log) => (log['fields'] as Map)['index']).toList();
    expect(indices.last, 29);
    expect(
      indices,
      orderedEquals(
        List.generate(indices.length, (i) => 30 - indices.length + i),
      ),
    );
    expect(logs.length, lessThan(30));
  });

  test('one file rotation retains the newest record', () async {
    final logger = await open(bytes: 256, files: 1);
    for (var index = 0; index < 5; index++) {
      logger.record('edit', {'index': index});
      await logger.flush();
    }
    expect(await directory.list().length, 1);
    expect((records(await bundle(logger)).last['fields'] as Map)['index'], 4);
  });

  test('old sessions are exported before new session records', () async {
    final first = await open();
    first.record('first', {});
    await first.flush();
    final second = await open();
    second.record('second', {});
    final logs = records(await bundle(second));
    expect(logs.map((log) => log['event']), ['first', 'second']);
    expect(logs.map((log) => log['session']), [first.session, second.session]);
    expect(first.session, isNot(second.session));
    expect(logs.map((log) => log['sequence']), [1, 1]);
  });

  test('opening cleans excess files and oversized old records', () async {
    final kept = File('${directory.path}/diagnostics-0.jsonl');
    await kept.writeAsString('${jsonEncode({'event': 'old'})}\n${'x' * 1000}');
    await File('${directory.path}/diagnostics-1.jsonl')
        .writeAsString('x' * 1000);
    await File('${directory.path}/diagnostics-4.jsonl').writeAsString('old\n');
    final unrelated = File('${directory.path}/unrelated.txt');
    await unrelated.writeAsString('preserve');
    final logger = await open(bytes: 256, files: 2);
    expect(
      await File('${directory.path}/diagnostics-4.jsonl').exists(),
      isFalse,
    );
    expect(logger.failure, isNull);
    expect(await kept.readAsString(), '${jsonEncode({'event': 'old'})}\n');
    expect(await kept.length(), lessThanOrEqualTo(256));
    expect(await File('${directory.path}/diagnostics-1.jsonl').length(), 0);
    expect(await unrelated.readAsString(), 'preserve');
    expect(records(await bundle(logger)).single['event'], 'old');
  });

  test('queue is bounded and counts rejected records', () async {
    final logger = await open();
    for (var index = 0; index < 300; index++) {
      logger.record('burst', {'index': index});
    }
    expect(logger.droppedRecords, 172);
    final result = await bundle(logger);
    expect(records(result), hasLength(128));
    expect(result['droppedRecords'], 172);
    logger.record('after', {});
    expect(records(await bundle(logger)).last['sequence'], 301);
  });

  test('large UTF-8 records have explicit truncation within 16 KB', () async {
    final logger = await open();
    logger.record('large', {'detail': '谱' * 20000});
    final logs = records(await bundle(logger));
    expect(logs.single['truncated'], isTrue);
    expect(logs.single['originalBytes'], greaterThan(16384));
    final file = File('${directory.path}/diagnostics-0.jsonl');
    expect(await file.length(), lessThanOrEqualTo(16384));
  });

  test(
    'small files truncate explicitly and impossible sizes drop safely',
    () async {
      final logger = await open(bytes: 256);
      logger.record('large', {'detail': 'x' * 2000});
      expect(records(await bundle(logger)).single['truncated'], isTrue);
      expect(
        await File('${directory.path}/diagnostics-0.jsonl').length(),
        lessThanOrEqualTo(256),
      );
      final tinyDirectory = Directory('${directory.path}/tiny');
      final tiny = await LocalDiagnostics.openAt(
        tinyDirectory,
        identity: {},
        maxFileBytes: 8,
      );
      loggers.add(tiny);
      expect(() => tiny.record('edit', {}), returnsNormally);
      expect(records(await bundle(tiny)), isEmpty);
      expect(tiny.droppedRecords, 1);
    },
  );

  test(
    'paths and URIs are removed recursively without losing package frames',
    () async {
      final logger = await open();
      const texts = [
        'failed at /Users/private/Music/secret track.mp3',
        r'failed at "C:\Users\Private User\Music\secret song.mp3" again',
        r"failed at 'D:\Private User\secret.sl k' again",
        r'failed at C:\Users\Private User\secret song.mp3',
        r'failed at "\\server\Private Share\secret song.mp3"',
        'file:///Users/private/secret%20song.mp3',
        'content://media/external/audio/media/123?displayName=secret.mp3',
      ];
      logger.record('privacy', {'nested': texts});
      logger.recordException(
        'read',
        const FileSystemException('cannot read', '/Users/private/secret.slk'),
        StackTrace.fromString(
          '#0 read (/Users/private/lib/read.dart:1:2)\n#1 caller (package:one_two_go_yeah/editor.dart:20:3)',
        ),
      );
      final result = await bundle(logger);
      final encoded = jsonEncode(result);
      for (final forbidden in [
        'secret',
        'Private User',
        'Private Share',
        '/Users/',
        'C:\\',
        'D:\\',
        'content://',
        'file:///',
      ]) {
        expect(encoded, isNot(contains(forbidden)), reason: forbidden);
      }
      expect(encoded, contains('package:one_two_go_yeah/editor.dart:20:3'));
      final nested = (records(result).first['fields'] as Map)['nested'];
      expect(nested, isA<List>());
      expect(nested, hasLength(texts.length));
    },
  );

  test('public sanitizers clean nested fields and preserve package frames', () {
    final fields = LocalDiagnostics.sanitizeFields({
      'draftName': 'private draft',
      'nested': {
        'fileName': 'private-song.mp3',
        'path': '/Users/private/private-song.mp3',
      },
    });
    expect(fields, {
      'draftName': '[redacted]',
      'nested': {'fileName': '[redacted]', 'path': '[redacted path]'},
    });
    final stack = LocalDiagnostics.sanitizeText(
      '#0 read (/Users/private/read.dart:1:2)\n'
      '#1 caller (package:one_two_go_yeah/editor.dart:20:3)',
    );
    expect(stack, isNot(contains('/Users/')));
    expect(stack, contains('package:one_two_go_yeah/editor.dart:20:3'));
  });

  test('exception messages never expose bare names or call toString', () async {
    final logger = await open();
    final errors = <Object>[
      StateError('private draft'),
      const FormatException('private-song.mp3', 'private draft'),
      const FileSystemException('private draft', 'private-song.mp3'),
      const OtgyException(
        status: 2,
        domain: 3,
        code: 4,
        message: 'private-song.mp3 private draft',
      ),
      PlatformException(
        code: 'private-song.mp3',
        message: 'private draft',
        details: 'private-song.mp3',
      ),
      _UnprintableError(),
    ];
    for (final error in errors) {
      logger.recordException(
        'read',
        error,
        StackTrace.fromString(
          '#0 read (/Users/private/private-song.mp3:1:2)\n'
          '#1 caller (package:one_two_go_yeah/editor.dart:20:3)',
        ),
      );
    }
    final logs = records(await bundle(logger));
    expect(logs, hasLength(errors.length));
    for (var index = 0; index < errors.length; index++) {
      final fields = logs[index]['fields'] as Map;
      expect(fields['type'], errors[index].runtimeType.toString());
      expect(fields, isNot(contains('message')));
      expect(
        fields['stack'],
        contains('package:one_two_go_yeah/editor.dart:20:3'),
      );
    }
    expect((logs[4]['fields'] as Map)['code'], '[redacted code]');
    final encoded = jsonEncode(logs);
    expect(encoded, isNot(contains('private-song.mp3')));
    expect(encoded, isNot(contains('private draft')));
    expect(encoded, isNot(contains('/Users/')));
    final persisted = await File('${directory.path}/diagnostics-0.jsonl')
        .readAsString();
    expect(persisted, isNot(contains('private-song.mp3')));
    expect(persisted, isNot(contains('private draft')));
  });

  test('unknown values and map keys are never stringified', () async {
    final unknown = _UnprintableError();
    final input = <String, Object?>{
      'value': unknown,
      'nested': <Object, Object?>{unknown: 'private draft', 'count': 2},
      'message': 'private draft',
      'details': {'title': 'private draft'},
      'error': 'private-song.mp3',
    };
    final cleaned = LocalDiagnostics.sanitizeFields(input);
    expect(cleaned, {
      'value': '[redacted object]',
      'nested': {'count': 2},
      'message': '[redacted]',
      'details': '[redacted]',
      'error': '[redacted]',
    });
    final logger = await open();
    logger.record('metadata', input);
    final logs = records(await bundle(logger));
    expect(logger.droppedRecords, 0);
    expect(logs.single['fields'], cleaned);
    final persisted = await File('${directory.path}/diagnostics-0.jsonl')
        .readAsString();
    expect(persisted, isNot(contains('private draft')));
    expect(persisted, isNot(contains('private-song.mp3')));
  });

  test('historical exception text is excluded from exported logs', () async {
    final historical = [
      {
        'event': 'exception',
        'message': 'private draft',
        'fields': {
          'operation': 'read',
          'type': 'PlatformException',
          'code': 'private-song.mp3',
          'domain': 'private draft',
          'status': 2,
          'message': 'private draft',
          'details': {'title': 'private-song.mp3'},
          'error': 'private draft',
          'stack':
              '#0 read (/Users/private/read.dart:1:2)\n'
              '#1 caller (package:one_two_go_yeah/editor.dart:20:3)',
        },
      },
      {
        'event': 'old',
        'fields': {
          'nested': [
            {
              'message': 'private draft',
              'details': 'private-song.mp3',
              'error': 'private draft',
              'exception': 'private-song.mp3',
              'count': 3,
            },
          ],
        },
      },
      {
        'event': 'exception',
        'fields': {
          'type': 'OtgyException',
          'code': 4,
          'domain': 3,
          'status': 2,
          'operation': {'private': 'private draft'},
          'stack': ['private-song.mp3'],
        },
      },
    ];
    await File('${directory.path}/diagnostics-0.jsonl')
        .writeAsString('${historical.map(jsonEncode).join('\n')}\n');
    final logger = await open();
    final result = await bundle(logger);
    final logs = records(result);
    expect(result['corruptLines'], 0);
    expect(logs, hasLength(3));
    expect(logs.first, isNot(contains('message')));
    final fields = logs.first['fields'] as Map;
    expect(fields['code'], '[redacted code]');
    expect(fields, isNot(contains('domain')));
    expect(fields['status'], 2);
    expect(fields, isNot(contains('message')));
    expect(fields, isNot(contains('details')));
    expect(
      fields['stack'],
      contains('package:one_two_go_yeah/editor.dart:20:3'),
    );
    expect(logs.last['fields'], {
      'type': 'OtgyException',
      'domain': 3,
      'code': 4,
      'status': 2,
    });
    expect(jsonEncode(result), isNot(contains('private draft')));
    expect(jsonEncode(result), isNot(contains('private-song.mp3')));
    expect(jsonEncode(result), isNot(contains('/Users/private')));
  });

  test('exception state retains only typed diagnostic fields', () async {
    final safeState = <String, Object?>{
      'revision': 7,
      'dirty': true,
      'sourceKind': 'savedDraft',
      'bpm': 123.5,
      'offset': -0.25,
      'currentRow': 18.0,
      'markerCount': 4,
      'showtimeMoveCount': 1,
      'hasAudio': true,
      'canUndo': false,
      'canRedo': false,
      'interactionMode': 'pastePending',
      'selectionCount': 2,
      'hasSelection': true,
      'hasClipboard': true,
      'clipboardCount': 2,
      'cutPending': true,
      'pastePreviewAnchorRow': 48,
      'pastePreviewCode': 19,
    };
    await File('${directory.path}/diagnostics-0.jsonl').writeAsString(
      '${jsonEncode({
        'event': 'exception',
        'fields': {
          'type': 'StateError',
          'state': {
            ...safeState,
            'draftName': 'private draft',
            'path': '/Users/private/chart.slk',
            'unknown': {'title': 'private-song.mp3'},
            'selectionMode': true,
            'objectId': 'PRIVATE_OBJECT_ID_9d3e',
            'danceMoveDrillId': 'PRIVATE_DRILL_ID_9d3e',
            'clipboard': 'PRIVATE_CLIPBOARD_PAYLOAD_9d3e',
            'selection': {'active': true, 'danceMoveDrillId': 'PRIVATE_DRILL_ID_9d3e', 'firstRow': 2, 'lastRow': 7, 'message': 'private-song.mp3', 'nested': 'PRIVATE_SELECTION_9d3e'},
          },
        },
      })}\n${jsonEncode({
        'event': 'exception',
        'fields': {
          'state': {
            'revision': 'private draft',
            'markerCount': 1.5,
            'showtimeMoveCount': false,
            'bpm': 'private-song.mp3',
            'offset': null,
            'currentRow': {},
            'dirty': 1,
            'hasAudio': 'private draft',
            'canUndo': [],
            'canRedo': null,
            'selectionMode': 'private-song.mp3',
            'interactionMode': 'private-song.mp3',
            'selectionCount': 1.5,
            'hasSelection': 'private draft',
            'hasClipboard': {},
            'clipboardCount': false,
            'cutPending': [],
            'pastePreviewAnchorRow': 1.5,
            'pastePreviewCode': 'private draft',
            'sourceKind': 'private draft',
            'objectId': 'PRIVATE_OBJECT_ID_9d3e',
            'danceMoveDrillId': 'PRIVATE_DRILL_ID_9d3e',
            'clipboardPayload': 'PRIVATE_CLIPBOARD_PAYLOAD_9d3e',
            'selection': {'active': 'private draft', 'danceMoveDrillId': 'PRIVATE_DRILL_ID_9d3e', 'nested': 'PRIVATE_SELECTION_9d3e'},
          },
        },
      })}\n',
    );
    final logger = await open();
    logger.record('exception', {
      'state': {
        ...safeState,
        'unknown': _UnprintableError(),
        'selectionMode': true,
        'objectId': 'PRIVATE_OBJECT_ID_9d3e',
        'danceMoveDrillId': 'PRIVATE_DRILL_ID_9d3e',
        'clipboardPayload': 'PRIVATE_CLIPBOARD_PAYLOAD_9d3e',
        'selection': {
          'active': true,
          'danceMoveDrillId': 'PRIVATE_DRILL_ID_9d3e',
          'nested': _UnprintableError(),
        },
      },
    });
    logger.record('exception', {
      'state': {'bpm': double.nan, 'offset': double.infinity},
    });
    final logs = records(await bundle(logger));
    expect((logs[0]['fields'] as Map)['state'], safeState);
    expect((logs[1]['fields'] as Map)['state'], isEmpty);
    expect((logs[2]['fields'] as Map)['state'], safeState);
    expect((logs[3]['fields'] as Map)['state'], isEmpty);
    expect(logger.droppedRecords, 0);
    final encoded = jsonEncode(logs);
    expect(encoded, isNot(contains('private draft')));
    expect(encoded, isNot(contains('private-song.mp3')));
    expect(encoded, isNot(contains('/Users/private')));
    expect(encoded, isNot(contains('unknown')));
    for (final forbidden in [
      'PRIVATE_OBJECT_ID_9d3e',
      'PRIVATE_DRILL_ID_9d3e',
      'PRIVATE_CLIPBOARD_PAYLOAD_9d3e',
      'PRIVATE_SELECTION_9d3e',
      'objectId',
      'danceMoveDrillId',
      'clipboardPayload',
      'selectionMode',
      '"selection":',
    ]) {
      expect(encoded, isNot(contains(forbidden)), reason: forbidden);
    }
  });

  test('storage failures retain only types and safe codes', () async {
    final errors = <Object>[
      StateError('private draft'),
      const FileSystemException(
        'private draft',
        'private-song.mp3',
        OSError('private-song.mp3', 13),
      ),
      const OtgyException(
        status: 2,
        domain: 3,
        code: 4,
        message: 'private draft',
      ),
      PlatformException(code: 'private draft', message: 'private-song.mp3'),
      _UnprintableError(),
    ];
    for (final error in errors) {
      final logger = await LocalDiagnostics.openAt(
        _FailingDirectory(error),
        identity: {},
      );
      loggers.add(logger);
      final summary = jsonDecode(logger.failure!) as Map;
      expect(summary['type'], error.runtimeType.toString());
      if (error is FileSystemException) expect(summary['osCode'], 13);
      if (error is OtgyException) {
        expect(summary['domain'], 3);
        expect(summary['code'], 4);
        expect(summary['status'], 2);
      }
      if (error is PlatformException) {
        expect(summary['code'], '[redacted code]');
      }
      expect(logger.failure, isNot(contains('private draft')));
      expect(logger.failure, isNot(contains('private-song.mp3')));
      expect(summary.keys, isNot(contains('message')));
    }
  });

  test(
    'native and platform exceptions keep diagnostic type and codes',
    () async {
      final logger = await open();
      logger.recordException(
        'native',
        const OtgyException(
          status: 2,
          domain: 3,
          code: 4,
          message: 'bad /private/chart.slk',
        ),
        StackTrace.fromString('package:test/source.dart:1:2'),
      );
      logger.recordException(
        'platform',
        PlatformException(code: 'io', message: 'bad content://private/42'),
        StackTrace.empty,
      );
      final logs = records(await bundle(logger));
      expect(logs.first['fields'], containsPair('type', 'OtgyException'));
      expect(logs.first['fields'], containsPair('domain', 3));
      expect(logs.first['fields'], containsPair('code', 4));
      expect(logs.last['fields'], containsPair('type', 'PlatformException'));
      expect(logs.last['fields'], containsPair('code', 'io'));
      expect(jsonEncode(logs), isNot(contains('chart.slk')));
    },
  );

  test(
    'corrupt lines are skipped and counted, old strings are sanitized',
    () async {
      await File('${directory.path}/diagnostics-0.jsonl').writeAsString(
        '{broken}\n${jsonEncode({
          'event': 'old',
          'fields': {'error': '/private/secret.slk'},
        })}\n42\n\n',
      );
      final logger = await open();
      final result = await bundle(logger);
      expect(result['corruptLines'], 2);
      expect(records(result), hasLength(1));
      expect(jsonEncode(result), isNot(contains('secret.slk')));
    },
  );

  test(
    'export snapshots are byte equivalent and never persist in logs',
    () async {
      final logger = await open();
      final payload = Uint8List.fromList(List.generate(1024, (i) => i % 256));
      logger.record('edit', {'changed': true});
      final result = await logger.exportBundle(
        recoveryPayload: payload,
        state: {'dirty': true, 'path': '/private/secret.slk'},
      );
      expect(result['schemaVersion'], 1);
      expect(result['identity'], {'appVersion': 'test', 'apiVersion': 1});
      final snapshot = result['snapshot'] as Map;
      expect(snapshot['kind'], 'recovery');
      expect(snapshot['encoding'], 'base64');
      expect(
        base64Decode(snapshot['payload'] as String),
        orderedEquals(payload),
      );
      expect(result['captureState'], containsPair('dirty', true));
      expect(() => jsonEncode(result), returnsNormally);
      final text = await File('${directory.path}/diagnostics-0.jsonl')
          .readAsString();
      expect(text, isNot(contains(base64Encode(payload))));
      final next = await logger.exportBundle(
        recoveryPayload: Uint8List.fromList([4, 5]),
        state: {},
      );
      expect(base64Decode((next['snapshot'] as Map)['payload'] as String), [
        4,
        5,
      ]);
    },
  );

  test('binary, sensitive metadata and cyclic values are not leaked', () async {
    final logger = await open();
    final cycle = <String, Object?>{};
    cycle['self'] = cycle;
    logger.record('metadata', {
      'fileName': 'secret.mp3',
      'draftName': 'secret draft',
      'sourceName': 'secret source',
      'targetName': 'secret target',
      'nestedNames': {
        'sourceNames': ['secret one', 'secret two'],
      },
      'clipboard': 'secret clipboard',
      'audio': [1, 2],
      'bytes': Uint8List.fromList([1, 2]),
      'cycle': cycle,
      'number': double.nan,
    });
    final result = await bundle(logger);
    expect(jsonEncode(result), isNot(contains('secret')));
    expect(
      (records(result).single['fields'] as Map)['bytes'],
      '[redacted binary]',
    );
    expect((records(result).single['fields'] as Map)['cycle'], {
      'self': '[truncated structure]',
    });
  });

  test('failed initialization and writes do not interrupt export', () async {
    final blocked = File('${directory.path}/blocked');
    await blocked.writeAsString('not a directory');
    final logger = await LocalDiagnostics.openAt(
      Directory(blocked.path),
      identity: {},
    );
    loggers.add(logger);
    expect(logger.failure, isNotNull);
    expect(() => logger.record('edit', {}), returnsNormally);
    expect(
      () => logger.recordException(
        'read',
        StateError('/private/secret.slk'),
        StackTrace.empty,
      ),
      returnsNormally,
    );
    await logger.flush();
    final result = await bundle(logger);
    expect(result['storageFailed'], isTrue);
    expect(result['failure'], isNotNull);
    expect(result['droppedRecords'], 2);
    expect(jsonEncode(result), isNot(contains(directory.path)));
    expect((result['snapshot'] as Map)['payload'], '');
  });

  test(
    'directory disappearing after open produces a queryable failure',
    () async {
      final logger = await open();
      await directory.delete(recursive: true);
      expect(() => logger.record('edit', {}), returnsNormally);
      await logger.flush();
      expect(logger.failure, isNotNull);
      expect(logger.droppedRecords, 1);
      expect((await bundle(logger))['storageFailed'], isTrue);
    },
  );
}

final class _UnprintableError {
  @override
  String toString() => throw StateError('must not stringify private draft');
}

final class _FailingDirectory implements Directory {
  _FailingDirectory(this.error);

  final Object error;

  @override
  Future<Directory> create({bool recursive = false}) async => throw error;

  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}
