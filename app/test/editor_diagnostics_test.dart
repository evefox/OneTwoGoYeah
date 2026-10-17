import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_diagnostics.dart';
import 'package:otgy_native/otgy_native.dart';

NativeChartEditor _seededEditor() {
  final editor = NativeChartEditor.createEmpty();
  for (final row in [0, 2, 4, 6, 6]) {
    expect(editor.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
  }
  for (final row in [8, 10]) {
    expect(editor.click(OTGY_CHART_COLUMN_PLAYER, row).changed, isTrue);
  }
  return editor;
}

List<List<Object>> _selectionState(Iterable<ChartSelection> selections) => [
  for (final selection in selections)
    [
      selection.active,
      selection.danceMoveDrillId,
      selection.firstRow,
      selection.lastRow,
    ],
];

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late List<({String event, Map<String, Object?> fields})> records;
  late EditorController controller;

  setUp(() {
    records = [];
    controller = EditorController(
      editor: _seededEditor(),
      diagnosticRecorder: (event, fields) {
        records.add((event: event, fields: fields));
      },
    );
  });

  tearDown(() => controller.dispose());

  Map<String, Object?> last(String event) =>
      records.lastWhere((record) => record.event == event).fields;

  Map<String, Object?> state(String event) =>
      last(event)['state']! as Map<String, Object?>;

  test('edit diagnostics report native results and committed revisions', () {
    final expected = _seededEditor();
    addTearDown(expected.dispose);
    void check(String event, EditResult result, int revision) {
      expect(last(event)['code'], result.code);
      expect(last(event)['changed'], result.changed);
      expect(last(event).containsKey('primaryEventId'), isFalse);
      expect(state(event)['revision'], revision);
      expect(
        controller.encodeRecoverySnapshot(),
        orderedEquals(expected.encodeRecovery()),
      );
    }

    controller.clickCell(OTGY_CHART_COLUMN_NPC, 32);
    check('click', expected.click(OTGY_CHART_COLUMN_NPC, 32), 1);
    expect(last('click')['column'], OTGY_CHART_COLUMN_NPC);
    expect(last('click')['row'], 32);
    expect(state('click')['dirty'], isTrue);
    controller.undo();
    check('undo', expected.undo(), 2);
    expect(state('undo')['dirty'], isFalse);
    controller.redo();
    check('redo', expected.redo(), 3);
    controller.clearChart();
    check('clear', expected.clear(), 4);
    controller.clearChart();
    check('clear', expected.clear(), 4);
    expect(last('clear')['changed'], isFalse);
  });

  test('selection and clipboard diagnostics follow deferred cut sessions', () {
    final expected = _seededEditor();
    addTearDown(expected.dispose);
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    final selected = expected.select(OTGY_CHART_COLUMN_NPC, 0);
    expect(last('select')['code'], selected.code);
    expect(last('select')['changed'], selected.changed);
    expect(state('select'), {
      ...controller.diagnosticState,
      'interactionMode': 'selection',
      'selectionCount': 1,
      'hasSelection': true,
      'hasClipboard': false,
      'clipboardCount': 0,
      'cutPending': false,
    });
    expect(state('select').containsKey('selection'), isFalse);
    expect(state('select').containsKey('danceMoveDrillId'), isFalse);

    controller.cutSelection();
    final cut = expected.cutSelection();
    expect(last('cut')['code'], cut.code);
    expect(last('cut')['changed'], cut.changed);
    expect(state('cut')['revision'], 0);
    expect(state('cut')['interactionMode'], 'pastePending');
    expect(state('cut')['selectionCount'], 1);
    expect(state('cut')['hasSelection'], isTrue);
    expect(state('cut')['hasClipboard'], isTrue);
    expect(state('cut')['clipboardCount'], 1);
    expect(state('cut')['cutPending'], isTrue);
    expect(state('cut')['canUndo'], isFalse);
    expect(state('cut')['canRedo'], isFalse);
    expect(
      controller.encodeRecoverySnapshot(),
      orderedEquals(expected.encodeRecovery()),
    );

    controller.pasteAtRow(32);
    final pasted = expected.paste(32);
    expect(last('paste')['anchorRow'], 32);
    expect(last('paste')['code'], pasted.code);
    expect(last('paste').containsKey('primaryEventId'), isFalse);
    expect(state('paste')['revision'], 1);
    expect(state('paste')['interactionMode'], 'pastePending');
    expect(state('paste')['selectionCount'], expected.selections.length);
    expect(state('paste')['hasSelection'], expected.selections.isNotEmpty);
    expect(
      _selectionState(controller.selections),
      equals(_selectionState(expected.selections)),
    );
    expect(state('paste')['hasClipboard'], isTrue);
    expect(state('paste')['clipboardCount'], 1);
    expect(state('paste')['cutPending'], isFalse);
    expect(
      controller.encodeRecoverySnapshot(),
      orderedEquals(expected.encodeRecovery()),
    );

    controller.exitSelectionMode();
    expect(state('selectionMode.exit')['interactionMode'], 'normal');
    expect(state('selectionMode.exit')['selectionCount'], 0);
    expect(state('selectionMode.exit')['hasSelection'], isFalse);
    expect(state('selectionMode.exit')['hasClipboard'], isFalse);
    expect(state('selectionMode.exit')['clipboardCount'], 0);
    expect(state('selectionMode.exit')['cutPending'], isFalse);
    expect(last('selection.clear')['code'], 0);
    expect(last('clipboard.clear')['code'], 0);
  });

  test('paste preview diagnostics retain only normalized safe scalars', () {
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    final formal = controller.encodeFormalDraft();
    final recovery = controller.encodeRecoverySnapshot();
    final revision = controller.projectRevision;

    controller.updatePastePreview(-12);

    final preview = controller.pastePreview!;
    expect(controller.pastePreviewAnchorRow, 0);
    expect(last('paste.preview')['anchorRow'], 0);
    expect(last('paste.preview')['code'], preview.code);
    expect(state('paste.preview')['pastePreviewAnchorRow'], 0);
    expect(state('paste.preview')['pastePreviewCode'], preview.code);
    expect(controller.projectRevision, revision);
    expect(controller.encodeFormalDraft(), orderedEquals(formal));
    expect(controller.encodeRecoverySnapshot(), orderedEquals(recovery));
    final encoded = jsonEncode(last('paste.preview'));
    for (final forbidden in [
      'markers',
      'rowDelta',
      'objectId',
      'danceMoveDrillId',
      'clipboardPayload',
      'selections',
      '"selection":',
    ]) {
      expect(encoded, isNot(contains(forbidden)), reason: forbidden);
    }

    controller.clearPastePreview();
    expect(controller.pastePreview, isNull);
    expect(controller.pastePreviewAnchorRow, isNull);
    expect(controller.diagnosticState['pastePreviewAnchorRow'], isNull);
    expect(controller.diagnosticState['pastePreviewCode'], isNull);
    expect(controller.projectRevision, revision);
    expect(controller.encodeFormalDraft(), orderedEquals(formal));
    expect(controller.encodeRecoverySnapshot(), orderedEquals(recovery));
  });

  test('rejected paste is atomic including history and session state', () {
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    final formal = controller.encodeFormalDraft();
    final recovery = controller.encodeRecoverySnapshot();
    controller.updatePastePreview(0);
    final before = controller.diagnosticState;
    controller.pasteAtRow(0);
    expect(last('paste')['anchorRow'], 0);
    expect(last('paste')['code'], 19);
    expect(last('paste')['changed'], isFalse);
    expect(state('paste'), before);
    expect(controller.encodeFormalDraft(), orderedEquals(formal));
    expect(controller.encodeRecoverySnapshot(), orderedEquals(recovery));
    controller.pasteAtRow(32);
    expect(last('paste')['anchorRow'], 32);
    expect(last('paste')['changed'], isTrue);
    expect(controller.projectRevision, 1);
  });

  test('blocked commands preserve the original mode gates', () {
    final before = controller.encodeFormalDraft();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    controller.cutSelection();
    controller.pasteAtRow(24);
    for (final event in ['select', 'copy', 'cut', 'paste']) {
      expect(last(event)['blocked'], isTrue);
      expect(last(event).containsKey('code'), isFalse);
      expect(state(event)['revision'], 0);
    }
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    controller.toggleSelectionMode();
    expect(controller.selectionMode, isTrue);
    controller.toggleSelectionMode();
    expect(controller.selectionMode, isFalse);
  });

  test('diagnostic state and retained record snapshots are immutable', () {
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    final snapshot = controller.diagnosticState;
    final record = last('select');
    final originalRecord = jsonEncode(record);
    expect(
      snapshot.keys,
      unorderedEquals([
        'revision',
        'dirty',
        'sourceKind',
        'hasAudio',
        'bpm',
        'offset',
        'currentRow',
        'markerCount',
        'showtimeMoveCount',
        'canUndo',
        'canRedo',
        'interactionMode',
        'selectionCount',
        'hasSelection',
        'hasClipboard',
        'clipboardCount',
        'cutPending',
        'pastePreviewAnchorRow',
        'pastePreviewCode',
      ]),
    );
    expect(() => snapshot['revision'] = 99, throwsUnsupportedError);
    expect(() => record['code'] = 99, throwsUnsupportedError);
    expect(snapshot['interactionMode'], 'selection');
    expect(snapshot['selectionCount'], 1);
    expect(snapshot['hasSelection'], isTrue);
    expect(snapshot['hasClipboard'], isFalse);
    expect(snapshot['clipboardCount'], 0);
    expect(snapshot['cutPending'], isFalse);
    controller.exitSelectionMode();
    controller.clearChart();
    expect(snapshot['markerCount'], greaterThan(0));
    expect(snapshot['interactionMode'], 'selection');
    expect(snapshot['selectionCount'], 1);
    expect(snapshot['hasSelection'], isTrue);
    expect(jsonEncode(record), originalRecord);
  });

  test('diagnostic reads and helpers never alter persisted precision', () {
    controller.setBpm(127.882123456789);
    controller.setOffset(-0.123456789012345);
    final formal = controller.encodeFormalDraft();
    final recovery = controller.encodeRecoverySnapshot();
    for (var i = 0; i < 3; i++) {
      expect(controller.diagnosticState['bpm'], 127.882123456789);
      expect(controller.diagnosticState['offset'], -0.123456789012345);
      controller.recordDiagnostic('snapshot');
      controller.recordDiagnosticException(
        'snapshot',
        StateError('private'),
        StackTrace.current,
      );
    }
    expect(controller.encodeFormalDraft(), orderedEquals(formal));
    expect(controller.encodeRecoverySnapshot(), orderedEquals(recovery));
    expect(controller.projectRevision, 0);
  });

  test('throwing recorder cannot change editing or exception propagation', () {
    final throwing = EditorController(
      editor: _seededEditor(),
      diagnosticRecorder: (event, fields) =>
          throw StateError('recorder failed'),
    );
    addTearDown(throwing.dispose);
    for (final editor in [controller, throwing]) {
      editor.enterSelectionMode();
      editor.selectAt(OTGY_CHART_COLUMN_NPC, 0);
      editor.copySelection();
      editor.moveToRow(32);
      editor.pasteAtRow(32);
      editor.pasteAtRow(64);
      editor.cutSelection();
      editor.undo();
      editor.redo();
      editor.exitSelectionMode();
      editor.clickCell(OTGY_CHART_COLUMN_NPC, 48);
      editor.clearChart();
      expect(
        () => editor.restoreRecovery(Uint8List(0)),
        throwsA(isA<OtgyException>()),
      );
    }
    expect(
      throwing.encodeFormalDraft(),
      orderedEquals(controller.encodeFormalDraft()),
    );
    expect(
      throwing.encodeRecoverySnapshot(),
      orderedEquals(controller.encodeRecoverySnapshot()),
    );
    expect(throwing.diagnosticState, controller.diagnosticState);
  });

  testWidgets('project replacement logs final state without source identity', (
    tester,
  ) async {
    const secret = 'PRIVATE-DRAFT-/Users/private/music';
    final payload = controller.encodeFormalDraft();
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    final sessionBefore = controller.diagnosticState;
    controller.loadFormalDraft(
      payload: payload,
      draftId: secret,
      draftName: secret,
    );
    expect(last('project.load')['blocked'], isTrue);
    expect(controller.diagnosticState, sessionBefore);
    expect(controller.encodeFormalDraft(), orderedEquals(payload));
    controller.exitChartInteraction();
    await tester.pumpAndSettle();
    controller.loadFormalDraft(
      payload: payload,
      draftId: secret,
      draftName: secret,
    );
    expect(state('project.load')['sourceKind'], 'savedDraft');
    expect(state('project.load')['dirty'], isFalse);
    expect(state('project.load')['revision'], 1);
    expect(state('project.load')['interactionMode'], 'normal');
    expect(state('project.load')['selectionCount'], 0);
    expect(state('project.load')['hasSelection'], isFalse);
    expect(state('project.load')['hasClipboard'], isFalse);
    expect(state('project.load')['clipboardCount'], 0);
    expect(state('project.load')['cutPending'], isFalse);
    controller.markSaved(draftId: secret, draftName: secret, payload: payload);
    expect(state('project.saved')['revision'], 1);
    final recovery = controller.encodeRecoverySnapshot();
    controller.restoreRecovery(recovery);
    expect(state('project.restore')['sourceKind'], 'untitled');
    expect(state('project.restore')['dirty'], isTrue);
    expect(state('project.restore')['revision'], 2);
    controller.importSlkBytes(
      File('../example/z3116_lv5.slk').readAsBytesSync(),
      secret,
    );
    expect(state('project.importSlk')['sourceKind'], 'importedSlk');
    expect(state('project.importSlk')['revision'], 3);
    expect(
      jsonEncode(records.map((r) => r.fields).toList()),
      isNot(contains(secret)),
    );
  });

  testWidgets('failed project decodes log native errors without committing state', (
    tester,
  ) async {
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    final sessionBefore = controller.diagnosticState;
    controller.loadFormalDraft(
      payload: Uint8List(0),
      draftId: 'secret',
      draftName: 'secret',
    );
    controller.restoreRecovery(Uint8List(0));
    controller.importSlkBytes(Uint8List(0), 'secret.slk');
    for (final operation in [
      'project.load',
      'project.restore',
      'project.importSlk',
    ]) {
      expect(last(operation)['blocked'], isTrue);
      expect(state(operation), sessionBefore);
    }
    expect(controller.diagnosticState, sessionBefore);
    controller.exitChartInteraction();
    await tester.pumpAndSettle();
    final before = controller.diagnosticState;
    final formal = controller.encodeFormalDraft();
    final recovery = controller.encodeRecoverySnapshot();
    for (final (operation, action) in <(String, void Function())>[
      (
        'project.load',
        () => controller.loadFormalDraft(
          payload: Uint8List(0),
          draftId: 'secret',
          draftName: 'secret',
        ),
      ),
      ('project.restore', () => controller.restoreRecovery(Uint8List(0))),
      (
        'project.importSlk',
        () => controller.importSlkBytes(Uint8List(0), 'secret.slk'),
      ),
    ]) {
      OtgyException? failure;
      try {
        action();
      } on OtgyException catch (error) {
        failure = error;
      }
      expect(failure, isNotNull);
      expect(last('exception')['operation'], operation);
      expect(last('exception')['type'], 'OtgyException');
      expect(last('exception')['code'], failure!.code);
      expect(last('exception')['domain'], failure.domain);
      expect(last('exception')['status'], failure.status);
      expect(controller.diagnosticState, before);
      expect(controller.encodeFormalDraft(), orderedEquals(formal));
      expect(controller.encodeRecoverySnapshot(), orderedEquals(recovery));
      controller.enterSelectionMode();
      controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
      controller.copySelection();
      expect(controller.diagnosticState, sessionBefore);
      controller.exitChartInteraction();
      await tester.pumpAndSettle();
    }
  });

  test('diagnostic metadata uses a scalar whitelist and omits error text', () {
    controller.recordDiagnostic('privacy', {
      'column': 1,
      'row': 2,
      'changed': false,
      'fileName': 'private.mp3',
      'draftId': 'private-id',
      'path': '/private/file',
      'audioBytes': Uint8List.fromList([1, 2, 3]),
      'clipboard': {
        'payload': 'PRIVATE_CLIPBOARD_PAYLOAD_8f7c',
        'events': [1, 2, 3],
      },
      'clipboardPayload': 'PRIVATE_CLIPBOARD_PAYLOAD_8f7c',
      'objectId': 'PRIVATE_OBJECT_ID_8f7c',
      'danceMoveDrillId': 'PRIVATE_DRILL_ID_8f7c',
      'selection': {
        'active': true,
        'danceMoveDrillId': 'PRIVATE_DRILL_ID_8f7c',
        'nested': 'PRIVATE_SELECTION_8f7c',
      },
      'code': 'private-platform-code',
      'stage': double.nan,
      'state': {'name': 'injected-private-state'},
    });
    expect(
      last('privacy').keys,
      unorderedEquals(['column', 'row', 'changed', 'state']),
    );
    final privacyRecord = jsonEncode(last('privacy'));
    for (final forbidden in [
      'PRIVATE_CLIPBOARD_PAYLOAD_8f7c',
      'PRIVATE_OBJECT_ID_8f7c',
      'PRIVATE_DRILL_ID_8f7c',
      'PRIVATE_SELECTION_8f7c',
      'clipboardPayload',
      'objectId',
      'danceMoveDrillId',
      '"selection":',
    ]) {
      expect(privacyRecord, isNot(contains(forbidden)), reason: forbidden);
    }
    controller.recordDiagnosticException(
      'privacy',
      const OtgyException(
        status: 1,
        domain: 2,
        code: 3,
        message: 'PRIVATE /Users/private/song.mp3',
      ),
      StackTrace.fromString(
        '#0      edit (/Users/PRIVATE/editor.dart:1:2)\n'
        '#1      load (file:///Users/PRIVATE/song.dart:3:4)\n'
        r'#2      save (C:\PRIVATE\editor.dart:5:6)'
        '\n#3      EditorController.clickCell '
        '(package:one_two_go_yeah/src/application/editor_controller.dart:42:7)\n'
        '#4      main (dart:isolate-patch/isolate_patch.dart:9:10)',
      ),
    );
    expect(
      last('exception').keys,
      unorderedEquals([
        'operation',
        'type',
        'stack',
        'domain',
        'code',
        'status',
        'state',
      ]),
    );
    final recordedStack = last('exception')['stack']! as String;
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
    expect(recordedStack, isNot(contains('/Users/')));
    expect(recordedStack, isNot(contains(r'C:\PRIVATE')));
    controller.recordDiagnosticException(
      'privacy',
      PlatformException(code: 'PRIVATE', message: 'PRIVATE'),
      StackTrace.current,
    );
    expect(last('exception').containsKey('code'), isFalse);
    expect(
      jsonEncode(records.map((r) => r.fields).toList()),
      isNot(contains('PRIVATE')),
    );
  });

  test(
    'audio loading and analysis preserve reuse and failure semantics',
    () async {
      await expectLater(controller.analyzeAudio(), throwsStateError);
      expect(last('exception')['operation'], 'audio.analysis');
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(
        audio,
        'PRIVATE-/Users/private/track.mp3',
      );
      expect(state('audio.load')['hasAudio'], isTrue);
      expect(last('audio.load')['changed'], isTrue);
      final before = controller.diagnosticState;
      final formal = controller.encodeFormalDraft();
      await controller.loadAudioBytes(Uint8List(0), 'PRIVATE-broken.mp3');
      expect(last('exception')['operation'], 'audio.load');
      expect(controller.diagnosticState, before);
      expect(controller.encodeFormalDraft(), orderedEquals(formal));
      expect(controller.isBusy, isFalse);
      final pending = controller.analyzeAudio();
      expect(controller.analyzeAudio(), same(pending));
      expect(last('audio.analysis')['reused'], isTrue);
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 32);
      expect(last('click')['blocked'], isTrue);
      final candidates = await pending;
      expect(candidates, isNotEmpty);
      expect(candidates.first.bpm, 120);
      expect(controller.isBusy, isFalse);
      expect(last('audio.analysis')['candidateCount'], candidates.length);
      expect(await controller.analyzeAudio(), same(candidates));
      expect(last('audio.analysis')['cached'], isTrue);
      expect(controller.encodeFormalDraft(), orderedEquals(formal));
      expect(
        jsonEncode(records.map((r) => r.fields).toList()),
        isNot(contains('PRIVATE')),
      );
    },
  );

  test(
    'disabled detailed diagnostics ignore an available default logger',
    () async {
      if (LocalDiagnostics.detailedEnabled) return;
      final directory = await Directory.systemTemp.createTemp(
        'otgy-controller-diagnostics-',
      );
      final previous = LocalDiagnostics.instance;
      final logger = await LocalDiagnostics.openAt(directory, identity: {});
      LocalDiagnostics.instance = logger;
      try {
        final disabled = EditorController();
        try {
          disabled.clickCell(OTGY_CHART_COLUMN_NPC, 0);
          disabled.recordDiagnostic('disabled');
          disabled.recordDiagnosticException(
            'disabled',
            StateError('private'),
            StackTrace.current,
          );
          final bundle = await logger.exportBundle(
            recoveryPayload: disabled.encodeRecoverySnapshot(),
            state: disabled.diagnosticState,
          );
          expect(bundle['logs'], isEmpty);
          expect(disabled.markers, isNotEmpty);
        } finally {
          disabled.dispose();
        }
      } finally {
        LocalDiagnostics.instance = previous;
        await logger.flush();
        await directory.delete(recursive: true);
      }
    },
  );
}
