import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_diagnostics.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/ui/editor_screen.dart';
import 'package:otgy_native/otgy_native.dart';

class _SnapshotController extends EditorController {
  Object? snapshotError;

  @override
  Uint8List encodeRecoverySnapshot() {
    final error = snapshotError;
    if (error != null) throw error;
    return super.encodeRecoverySnapshot();
  }
}

Map<String, List<int>> storedFiles(Directory directory) => {
  for (final file in directory.listSync().whereType<File>())
    file.uri.pathSegments.last: file.readAsBytesSync(),
};

List<List<Object?>> markerState(Iterable<ChartMarker> markers) => [
  for (final marker in markers)
    [
      marker.id,
      marker.row,
      marker.column,
      marker.type,
      marker.showNCount,
      marker.ordinal,
    ],
];

List<List<Object>> selectionState(EditorController controller) => [
  for (final selection in controller.selections)
    [
      selection.active,
      selection.danceMoveDrillId,
      selection.firstRow,
      selection.lastRow,
    ],
];

List<Object?> editorState(EditorController controller) => [
  controller.encodeFormalDraft().toList(),
  controller.encodeRecoverySnapshot().toList(),
  controller.isDirty,
  controller.sourceKind,
  controller.draftId,
  controller.projectRevision,
  controller.canUndo,
  controller.canRedo,
  controller.chartInteractionMode,
  selectionState(controller),
  controller.selectionCount,
  controller.hasSelection,
  controller.hasClipboard,
  controller.clipboardCount,
  controller.cutPending,
  controller.pastePreviewAnchorRow,
  controller.pastePreview?.code,
  controller.canPaste,
];

void main() {
  const filesChannel = MethodChannel('io.onetwogoyeah/files');
  final messenger =
      TestWidgetsFlutterBinding.ensureInitialized().defaultBinaryMessenger;
  late Directory directory;
  late EditorController controller;
  late TargetPlatform? originalPlatform;

  setUp(() {
    directory = Directory.systemTemp.createTempSync('otgy-diagnostic-export-');
    controller = _SnapshotController();
    originalPlatform = debugDefaultTargetPlatformOverride;
    LocalDiagnostics.instance = null;
  });

  tearDown(() {
    controller.dispose();
    messenger.setMockMethodCallHandler(filesChannel, null);
    debugDefaultTargetPlatformOverride = originalPlatform;
    LocalDiagnostics.instance = null;
    directory.deleteSync(recursive: true);
  });

  void diagnosticTest(
    String description,
    Future<void> Function(WidgetTester) body,
  ) {
    testWidgets(description, (tester) async {
      debugDefaultTargetPlatformOverride = TargetPlatform.android;
      try {
        await body(tester);
      } finally {
        await tester.pumpWidget(const SizedBox.shrink());
        debugDefaultTargetPlatformOverride = originalPlatform;
      }
    });
  }

  Future<void> pumpUntil(WidgetTester tester, bool Function() condition) async {
    for (var attempt = 0; attempt < 100 && !condition(); attempt++) {
      await tester.runAsync(() async {
        await Future<void>.delayed(const Duration(milliseconds: 10));
      });
      await tester.pump();
    }
    expect(condition(), isTrue, reason: 'The pending operation must finish.');
  }

  Future<List<Map<String, dynamic>>> diagnosticRecords(
    WidgetTester tester,
    LocalDiagnostics logger,
    Directory logDirectory,
  ) async {
    var flushed = false;
    unawaited(logger.flush().then((_) => flushed = true));
    await pumpUntil(tester, () => flushed);
    return (await tester.runAsync(() async {
      final records = <Map<String, dynamic>>[];
      await for (final file in logDirectory.list().where((f) => f is File)) {
        for (final line in await (file as File).readAsLines()) {
          if (line.isNotEmpty) {
            records.add(jsonDecode(line) as Map<String, dynamic>);
          }
        }
      }
      return records;
    }))!;
  }

  Future<void> pumpScreen(
    WidgetTester tester, {
    bool storage = true,
    LocalDiagnostics? diagnostics,
  }) async {
    final store = storage
        ? await tester.runAsync(() => LocalProjectStore.openAt(directory))
        : null;
    await tester.pumpWidget(
      MaterialApp(
        home: EditorScreen(
          controller: controller,
          diagnostics: diagnostics,
          storeFactory: () async {
            if (store == null) throw const FileSystemException('unavailable');
            return store;
          },
        ),
      ),
    );
    if (store != null) {
      final marker = File('${directory.path}/active-session');
      await pumpUntil(
        tester,
        () => marker.existsSync() && marker.lengthSync() > 0,
      );
    }
    await tester.pump();
  }

  void restoreClipboard(EditorController target, {bool extended = false}) {
    target.enterSelectionMode();
    target.selectAt(OTGY_CHART_COLUMN_NPC, extended ? 0 : 3);
    if (extended) {
      target.selectAt(OTGY_CHART_COLUMN_PLAYER, 40);
    }
    target.cutSelection();
    expect(
      target.chartInteractionMode,
      EditorChartInteractionMode.pastePending,
    );
    expect(target.selectionCount, extended ? 2 : 1);
    expect(target.hasSelection, isTrue);
    expect(target.hasClipboard, isTrue);
    expect(target.clipboardCount, extended ? 2 : 1);
    expect(target.cutPending, isTrue);
  }

  void prepareClipboard(EditorController target, {bool extended = false}) {
    for (final base in extended ? [0, 32] : [3]) {
      for (final row in [0, 2, 4, 6, 6]) {
        target.clickCell(OTGY_CHART_COLUMN_NPC, base + row);
      }
      for (final row in [8, 10]) {
        target.clickCell(OTGY_CHART_COLUMN_PLAYER, base + row);
      }
    }
    restoreClipboard(target, extended: extended);
  }

  Future<void> expectClipboardPreserved(
    WidgetTester tester, {
    bool extended = false,
  }) async {
    // Detach storage listeners before this deliberate test-only edit.
    await tester.pumpWidget(const SizedBox.shrink());
    final reference = EditorController();
    try {
      prepareClipboard(reference, extended: extended);
      final previewAnchorRow = controller.pastePreviewAnchorRow;
      if (previewAnchorRow != null) {
        reference.updatePastePreview(previewAnchorRow);
      }
      final beforePaste = editorState(controller);
      expect(beforePaste, editorState(reference));
      final revision = controller.projectRevision;
      const anchorRow = 96;
      controller.pasteAtRow(anchorRow);
      reference.pasteAtRow(anchorRow);
      expect(controller.projectRevision, revision + 1);
      expect(controller.cutPending, isFalse);
      expect(controller.hasClipboard, isTrue);
      expect(controller.clipboardCount, extended ? 2 : 1);
      expect(editorState(controller), editorState(reference));
      expect(
        controller.encodeRecoverySnapshot(),
        reference.encodeRecoverySnapshot(),
      );
    } finally {
      reference.dispose();
    }
  }

  Future<MenuItemButton> exportItem(WidgetTester tester) async {
    await tester.tap(find.byKey(const ValueKey('chart-menu')));
    await tester.pumpAndSettle();
    return tester.widget<MenuItemButton>(
      find.byKey(const ValueKey('export-diagnostics')),
    );
  }

  for (final outcome in [
    'success',
    'cancel',
    'capture-error',
    'selector-error',
    'write-error',
    'commit-error',
  ]) {
    diagnosticTest(
      'diagnostic $outcome preserves draft, history and stored files',
      (tester) async {
        if (outcome == 'commit-error') {
          debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
        }
        final logDirectory = Directory('${directory.path}/diagnostics');
        final logger = outcome.endsWith('-error')
            ? await tester.runAsync(
                () => LocalDiagnostics.openAt(logDirectory, identity: {}),
              )
            : null;
        await pumpScreen(tester, diagnostics: logger);
        prepareClipboard(controller);
        controller.moveToRow(47);
        await tester.pump();
        await tester.pump();
        expect(controller.pastePreviewAnchorRow, 47);
        expect(controller.pastePreview, isNotNull);
        final sessionBefore = editorState(controller);
        final sessionCaptureState = controller.diagnosticState;
        final sessionPreviewCode = controller.pastePreview!.code;
        expect(controller.generalActionsEnabled, isFalse);
        expect(
          tester
              .widget<TextButton>(find.byKey(const ValueKey('chart-menu')))
              .onPressed,
          isNull,
        );
        controller.exitChartInteraction();
        await tester.pumpAndSettle();
        final before = editorState(controller);
        final markersBefore = markerState(controller.markers);
        final filesBefore = storedFiles(directory);
        final methods = <String>[];
        final written = BytesBuilder();
        messenger.setMockMethodCallHandler(filesChannel, (call) async {
          methods.add(call.method);
          switch (call.method) {
            case 'createDocument':
              if (outcome == 'selector-error') {
                throw PlatformException(
                  code: 'unavailable',
                  message: '/private/selector-sentinel',
                );
              }
              final arguments = call.arguments as Map;
              expect(arguments['mimeType'], 'application/json');
              expect(
                arguments['suggestedName'],
                startsWith('otgy-diagnostics-'),
              );
              expect(arguments['suggestedName'], endsWith('.json'));
              tester.binding.handleAppLifecycleStateChanged(
                AppLifecycleState.hidden,
              );
              tester.binding.handleAppLifecycleStateChanged(
                AppLifecycleState.paused,
              );
              tester.binding.handleAppLifecycleStateChanged(
                AppLifecycleState.resumed,
              );
              return outcome == 'cancel'
                  ? null
                  : 'content://private/diagnostic.json';
            case 'openDocumentWrite':
              return 71;
            case 'writeDocumentChunk':
              if (outcome == 'write-error') {
                throw PlatformException(
                  code: 'write-error',
                  message: '/private/sentinel',
                );
              }
              written.add((call.arguments as Map)['bytes'] as Uint8List);
              return null;
            case 'commitDocument':
              if (outcome == 'commit-error') {
                throw PlatformException(code: 'commit-error');
              }
              return null;
            case 'finishDocumentWrite':
            case 'abortDocumentWrite':
            case 'deleteDocument':
              return null;
            default:
              fail('Unexpected method ${call.method}');
          }
        });
        final item = await exportItem(tester);
        expect(item.onPressed, isNotNull);
        if (outcome == 'capture-error') {
          (controller as _SnapshotController).snapshotError =
              const FileSystemException('/private/capture-sentinel');
        }
        await tester.runAsync(() async {
          item.onPressed!();
          (controller as _SnapshotController).snapshotError = null;
        });
        if (outcome == 'success') {
          await pumpUntil(
            tester,
            () => find.text('诊断包已导出').evaluate().isNotEmpty,
          );
        } else if (outcome == 'cancel') {
          await tester.pump();
          await tester.pump();
          await tester.runAsync(() async {});
          await tester.pump();
        } else {
          await pumpUntil(
            tester,
            () => find.text('诊断包导出失败').evaluate().isNotEmpty,
          );
        }
        expect(editorState(controller), before);
        expect(storedFiles(directory), filesBefore);
        if (outcome == 'success') {
          expect(methods, [
            'createDocument',
            'openDocumentWrite',
            'writeDocumentChunk',
            'finishDocumentWrite',
            'commitDocument',
          ]);
          final bundle = jsonDecode(utf8.decode(written.takeBytes())) as Map;
          expect(bundle['schemaVersion'], 1);
          expect(
            base64Decode((bundle['snapshot'] as Map)['payload'] as String),
            controller.encodeRecoverySnapshot(),
          );
          expect(bundle['captureState'], controller.diagnosticState);
          final captureState = bundle['captureState'] as Map;
          expect(
            captureState.keys,
            unorderedEquals([
              'revision',
              'dirty',
              'sourceKind',
              'hasAudio',
              'bpm',
              'offset',
              'audioEndMode',
              'audioEndRow',
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
          expect(captureState['interactionMode'], 'normal');
          expect(captureState['selectionCount'], 0);
          expect(captureState['hasSelection'], isFalse);
          expect(captureState['hasClipboard'], isFalse);
          expect(captureState['clipboardCount'], 0);
          expect(captureState['cutPending'], isFalse);
          expect(captureState['pastePreviewAnchorRow'], isNull);
          expect(captureState['pastePreviewCode'], isNull);
          expect(sessionCaptureState['interactionMode'], 'pastePending');
          expect(sessionCaptureState['selectionCount'], 1);
          expect(sessionCaptureState['hasSelection'], isTrue);
          expect(sessionCaptureState['hasClipboard'], isTrue);
          expect(sessionCaptureState['clipboardCount'], 1);
          expect(sessionCaptureState['cutPending'], isTrue);
          expect(sessionCaptureState['pastePreviewAnchorRow'], 47);
          expect(
            sessionCaptureState['pastePreviewCode'],
            sessionPreviewCode,
          );
          for (final forbidden in [
            'markers',
            'rowDelta',
            'objectId',
            'danceMoveDrillId',
            'clipboardPayload',
            '"selection":',
          ]) {
            expect(jsonEncode(captureState), isNot(contains(forbidden)));
          }
          final restored = NativeChartEditor.decodeRecovery(
            base64Decode((bundle['snapshot'] as Map)['payload'] as String),
          );
          expect(markerState(restored.markers()), equals(markersBefore));
          restored.dispose();
          expect(find.text('诊断包已导出'), findsOneWidget);
        } else if (outcome == 'cancel') {
          expect(methods, ['createDocument']);
        } else {
          expect(find.text('诊断包导出失败'), findsOneWidget);
          if (outcome == 'write-error' || outcome == 'commit-error') {
            expect(methods.last, 'deleteDocument');
          }
          Future<void> expectSingleExportException() async {
            final records = await diagnosticRecords(
              tester,
              logger!,
              logDirectory,
            );
            final exceptions = records
                .where((record) => record['event'] == 'exception')
                .toList();
            expect(exceptions, hasLength(1));
            expect(
              (exceptions.single['fields'] as Map)['operation'],
              'diagnostics-export',
            );
          }

          await expectSingleExportException();
          expect(find.text('诊断包导出失败'), findsOneWidget);
          await tester.tap(find.text('关闭'));
          await tester.pump();
          await tester.pump();
          await expectSingleExportException();
          switch (outcome) {
            case 'capture-error':
              expect(methods, isEmpty);
            case 'selector-error':
              expect(methods, ['createDocument']);
            case 'write-error':
            case 'commit-error':
              expect(methods.last, 'deleteDocument');
          }
        }
        expect(tester.takeException(), isNull);
        restoreClipboard(controller);
        await tester.pumpAndSettle();
        expect(editorState(controller), sessionBefore);
        await expectClipboardPreserved(tester);
      },
    );
  }

  diagnosticTest(
    'detached while finish is pending cancels without committing or changing workspace',
    (tester) async {
      final logDirectory = Directory('${directory.path}/diagnostics');
      final logger = (await tester.runAsync(
        () => LocalDiagnostics.openAt(logDirectory, identity: {}),
      ))!;
      await pumpScreen(tester, storage: false, diagnostics: logger);
      prepareClipboard(controller, extended: true);
      controller.moveToRow(48);
      await tester.pump();
      await tester.pump();
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.pastePending,
      );
      expect(controller.selectionCount, 2);
      expect(controller.hasSelection, isTrue);
      expect(controller.hasClipboard, isTrue);
      expect(controller.clipboardCount, 2);
      expect(controller.cutPending, isTrue);
      expect(controller.pastePreviewAnchorRow, 48);
      expect(controller.pastePreview, isNotNull);
      expect(controller.canUndo, isFalse);
      expect(controller.canRedo, isFalse);
      final sessionBefore = editorState(controller);
      controller.exitChartInteraction();
      await tester.pumpAndSettle();
      final before = editorState(controller);
      const uri = 'content://test/detached.json';
      final finishEntered = Completer<void>();
      final finishReleased = Completer<void>();
      final methods = <String>[];
      var documentExists = false;
      messenger.setMockMethodCallHandler(filesChannel, (call) async {
        methods.add(call.method);
        switch (call.method) {
          case 'createDocument':
            documentExists = true;
            return uri;
          case 'openDocumentWrite':
            expect(call.arguments, {'uri': uri});
            return 83;
          case 'writeDocumentChunk':
            expect((call.arguments as Map)['session'], 83);
            return null;
          case 'finishDocumentWrite':
            expect(call.arguments, {'session': 83});
            finishEntered.complete();
            await finishReleased.future;
            return null;
          case 'abortDocumentWrite':
            expect(call.arguments, {'session': 83, 'uri': uri});
            return null;
          case 'deleteDocument':
            expect(call.arguments, {'uri': uri});
            documentExists = false;
            return null;
          case 'commitDocument':
            return null;
          default:
            fail('Unexpected method ${call.method}');
        }
      });
      tester.binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
      try {
        final item = await exportItem(tester);
        await tester.runAsync(() async {
          item.onPressed!();
        });
        await pumpUntil(tester, () => finishEntered.isCompleted);
        expect(finishReleased.isCompleted, isFalse);
        expect(documentExists, isTrue);
        expect(methods, [
          'createDocument',
          'openDocumentWrite',
          'writeDocumentChunk',
          'finishDocumentWrite',
        ]);
        expect(editorState(controller), before);
        tester.binding.handleAppLifecycleStateChanged(
          AppLifecycleState.detached,
        );
        await tester.pump();
        expect(methods.last, 'finishDocumentWrite');
        finishReleased.complete();
        await pumpUntil(tester, () => methods.contains('deleteDocument'));
        expect(methods, [
          'createDocument',
          'openDocumentWrite',
          'writeDocumentChunk',
          'finishDocumentWrite',
          'abortDocumentWrite',
          'deleteDocument',
        ]);
        expect(documentExists, isFalse);
        expect(editorState(controller), before);
        final records = await diagnosticRecords(tester, logger, logDirectory);
        expect(
          records.where((r) => r['event'] == 'diagnostics.export.requested'),
          hasLength(1),
        );
        expect(
          records.where((r) => r['event'] == 'diagnostics.export.cancelled'),
          hasLength(1),
        );
        expect(
          records.where((r) => r['event'] == 'diagnostics.export.completed'),
          isEmpty,
        );
        expect(find.text('诊断包已导出'), findsNothing);
        expect(find.text('诊断包导出失败'), findsNothing);
        expect(tester.takeException(), isNull);
        restoreClipboard(controller, extended: true);
        controller.updatePastePreview(48);
        await tester.pumpAndSettle();
        expect(editorState(controller), sessionBefore);
        await expectClipboardPreserved(tester, extended: true);
      } finally {
        if (!finishReleased.isCompleted) finishReleased.complete();
        tester.binding.handleAppLifecycleStateChanged(
          AppLifecycleState.resumed,
        );
        await diagnosticRecords(tester, logger, logDirectory);
      }
    },
  );

  diagnosticTest('empty draft exports without audio or project storage', (
    tester,
  ) async {
    await pumpScreen(tester, storage: false);
    final written = BytesBuilder();
    messenger.setMockMethodCallHandler(filesChannel, (call) async {
      if (call.method == 'createDocument') return 'content://test/empty.json';
      if (call.method == 'openDocumentWrite') return 1;
      if (call.method == 'writeDocumentChunk') {
        written.add((call.arguments as Map)['bytes'] as Uint8List);
      }
      return null;
    });
    final before = editorState(controller);
    (await exportItem(tester)).onPressed!();
    await tester.pump();
    await tester.pumpAndSettle();
    expect(editorState(controller), before);
    final bundle = jsonDecode(utf8.decode(written.takeBytes())) as Map;
    expect((bundle['captureState'] as Map)['markerCount'], 0);
    expect((bundle['captureState'] as Map)['hasAudio'], false);
    expect(find.text('诊断包已导出'), findsOneWidget);
  });

  diagnosticTest('captures before picker and ignores duplicate requests', (
    tester,
  ) async {
    await pumpScreen(tester);
    controller.clickCell(OTGY_CHART_COLUMN_NPC, 3);
    final captured = controller.encodeRecoverySnapshot();
    final capturedRevision = controller.projectRevision;
    final selected = Completer<String?>();
    final written = BytesBuilder();
    var pickerCalls = 0;
    messenger.setMockMethodCallHandler(filesChannel, (call) async {
      if (call.method == 'createDocument') {
        pickerCalls++;
        return selected.future;
      }
      if (call.method == 'openDocumentWrite') return 4;
      if (call.method == 'writeDocumentChunk') {
        written.add((call.arguments as Map)['bytes'] as Uint8List);
      }
      return null;
    });
    final item = await exportItem(tester);
    item.onPressed!();
    item.onPressed!();
    await tester.pump();
    controller.clickCell(OTGY_CHART_COLUMN_NPC, 30);
    selected.complete('content://test/fixed.json');
    await tester.pump();
    await tester.pump();
    expect(pickerCalls, 1);
    final bundle = jsonDecode(utf8.decode(written.takeBytes())) as Map;
    expect(
      base64Decode((bundle['snapshot'] as Map)['payload'] as String),
      captured,
    );
    expect((bundle['captureState'] as Map)['revision'], capturedRevision);
    expect(controller.markers, hasLength(2));
  });
}
