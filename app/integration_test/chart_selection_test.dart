import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  WidgetController.hitTestWarningShouldBeFatal = true;

  late Directory directory;
  late LocalProjectStore store;
  late EditorController controller;
  final screenshotBoundaryKey = GlobalKey();

  bool isMobilePlatform() => Platform.isAndroid || Platform.isIOS;

  Future<void> captureScreenshot(WidgetTester tester, String state) async {
    await tester.pumpAndSettle();
    final boundary = screenshotBoundaryKey.currentContext!.findRenderObject();
    expect(boundary, isA<RenderRepaintBoundary>());
    final image = await (boundary! as RenderRepaintBoundary).toImage(
      pixelRatio: tester.view.devicePixelRatio,
    );
    try {
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      expect(data, isNotNull);
      final file = File(
        '${Directory.systemTemp.path}/otgy-chart-selection-$state.png',
      );
      await file.writeAsBytes(data!.buffer.asUint8List(), flush: true);
      expect(await file.exists(), isTrue);
      // Integration logs expose the isolated screenshot path to the test runner.
      // ignore: avoid_print
      print('OTGY_INTEGRATION_SCREENSHOT[$state]=${file.path}');
    } finally {
      image.dispose();
    }
  }

  void addDanceMove(NativeChartEditor editor, int firstRow) {
    for (final row in [0, 2, 4, 6, 6]) {
      expect(
        editor.click(OTGY_CHART_COLUMN_NPC, firstRow + row).changed,
        isTrue,
      );
    }
    for (final row in [8, 10]) {
      expect(
        editor.click(OTGY_CHART_COLUMN_PLAYER, firstRow + row).changed,
        isTrue,
      );
    }
  }

  Future<void> openEditor(
    WidgetTester tester, {
    bool withSecondMove = true,
    bool withShowtime = false,
    bool withTailMove = false,
  }) async {
    directory = await Directory.systemTemp.createTemp('otgy-device-selection-');
    store = await LocalProjectStore.openAt(directory);
    final editor = NativeChartEditor.createEmpty();
    addDanceMove(editor, 0);
    if (withSecondMove) {
      addDanceMove(editor, 32);
    }
    if (withTailMove) {
      // A later move keeps the timeline longer than the pasted batch, so the
      // fixed judgement line can still reach an anchor beyond that batch.
      addDanceMove(editor, 200);
    }
    if (withShowtime) {
      expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
      expect(editor.showtimeMoves(), hasLength(1));
    }
    controller = EditorController(editor: editor);
    await tester.pumpWidget(
      RepaintBoundary(
        key: screenshotBoundaryKey,
        child: OneTwoGoYeahApp(
          controller: controller,
          storeFactory: () async => store,
        ),
      ),
    );
    await tester.pumpAndSettle();
    addTearDown(() async {
      await tester.pumpWidget(const SizedBox.shrink());
      controller.dispose();
      await directory.delete(recursive: true);
    });
  }

  Offset cellAtRow(WidgetTester tester, String column, int row) {
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final header = tester.getRect(
      find.byKey(ValueKey('column-header-$column')),
    );
    return Offset(
      header.center.dx,
      canvas.top +
          editorCursorY(canvas.size) +
          (row - controller.currentRow) * controller.rowSpacing,
    );
  }

  Future<void> activateRow(
    WidgetTester tester,
    String column,
    int row, {
    bool longPress = false,
  }) async {
    controller.moveToRow(row.toDouble());
    await tester.pumpAndSettle();
    final point = cellAtRow(tester, column, row);
    if (longPress) {
      await tester.longPressAt(point);
    } else {
      await tester.tapAt(point);
    }
    await tester.pumpAndSettle();
  }

  Future<void> tapControl(WidgetTester tester, String key) async {
    final finder = find.byKey(ValueKey(key));
    expect(finder, findsOneWidget);
    expect(tester.widget<IconButton>(finder).onPressed, isNotNull);
    await tester.tap(finder);
    await tester.pumpAndSettle();
  }

  VoidCallback? controlAction(WidgetTester tester, String key) {
    final finder = find.byKey(ValueKey(key));
    expect(finder, findsOneWidget);
    final widget = tester.widget(finder);
    return widget is IconButton
        ? widget.onPressed
        : (widget as ButtonStyleButton).onPressed;
  }

  /// The chart interaction session keeps only its own commands, navigation and
  /// view transforms, and history navigation. Every other entry is disabled.
  void expectSessionToolbars(WidgetTester tester) {
    for (final key in [
      'audio-picker',
      'analyze-audio',
      'export-audio',
      'chart-menu',
      'save-chart',
      'export-slk',
      'playback-sound-button',
    ]) {
      expect(
        controlAction(tester, key) == null,
        isTrue,
        reason: '$key must be disabled inside a session',
      );
    }
    expect(controller.navigationEnabled, isTrue);
    expect(controller.historyNavigationEnabled, isTrue);
    expect(controller.selectionCommandEnabled, isTrue);
    expect(controller.generalActionsEnabled, isFalse);
  }

  /// Moves the timeline to [row]. The mobile anchor is the fixed judgement line,
  /// whose row is the current row, and the current row never passes the timeline
  /// end, so [row] must lie inside the timeline range on mobile callers.
  Future<void> moveToPasteRow(WidgetTester tester, int row) async {
    controller.moveToRow(row.toDouble());
    await tester.pumpAndSettle();
    if (!isMobilePlatform()) {
      return;
    }
    expect(controller.currentRow, row);
    expect(controller.pastePreviewAnchorRow, row);
    expect(controller.pastePreview, isNotNull);
  }

  void expectMobilePasteButton(
    WidgetTester tester, {
    required bool previewValid,
  }) {
    final finder = find.byKey(const ValueKey('paste-at-current-cursor'));
    expect(finder, findsOneWidget);
    expect(tester.widget<FilledButton>(finder).onPressed, isNotNull);
    expect(
      find.descendant(of: finder, matching: find.byIcon(Icons.content_paste)),
      findsOneWidget,
    );
    expect(find.text('粘贴到当前判定线'), findsOneWidget);
    expect(controller.pastePreview!.valid, previewValid);
  }

  Future<void> submitPasteAtRow(
    WidgetTester tester,
    int row, {
    required bool previewValid,
    bool verifyMobileTapDoesNotSubmit = false,
  }) async {
    await moveToPasteRow(tester, row);
    final point = cellAtRow(tester, 'NPC', row);
    if (!isMobilePlatform()) {
      final mouse = await tester.createGesture(
        kind: ui.PointerDeviceKind.mouse,
      );
      await mouse.addPointer(location: point);
      await mouse.moveTo(point);
      await tester.pumpAndSettle();
      expect(controller.pastePreviewAnchorRow, row);
      expect(controller.pastePreview!.valid, previewValid);
      await mouse.down(point);
      await mouse.up();
      await tester.pumpAndSettle();
      await mouse.removePointer();
      await tester.pumpAndSettle();
      return;
    }

    expectMobilePasteButton(tester, previewValid: previewValid);
    if (verifyMobileTapDoesNotSubmit) {
      final before = controller.encodeFormalDraft();
      final revision = controller.projectRevision;
      await tester.tapAt(cellAtRow(tester, 'NPC', row));
      await tester.pumpAndSettle();
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, revision);
      expect(controller.pastePending, isTrue);
      expect(controller.pastePreviewAnchorRow, row);
    }

    await tester.tap(find.byKey(const ValueKey('paste-at-current-cursor')));
    await tester.pumpAndSettle();
  }

  List<Object?> markerGeometry(ChartMarker marker, int firstRow) => [
    marker.row - firstRow,
    marker.column,
    marker.type,
    marker.showNCount,
    marker.ordinal,
  ];

  testWidgets(
    'marker keyboard focus keeps activation separate from long press',
    (tester) async {
      final previousHighlightStrategy = FocusManager.instance.highlightStrategy;
      FocusManager.instance.highlightStrategy =
          FocusHighlightStrategy.alwaysTraditional;
      try {
        await openEditor(tester, withSecondMove: false);
        final before = controller.encodeFormalDraft();
        final markerModel = controller.markers.singleWhere(
          (marker) => marker.column == OTGY_CHART_COLUMN_NPC && marker.row == 0,
        );
        final marker = find.byWidgetPredicate(
          (widget) =>
              widget is Semantics &&
              widget.properties.label == 'NPC 起点，第 0 个十六分行',
        );
        expect(marker, findsOneWidget);
        Focus.of(tester.element(marker)).requestFocus();
        await tester.pumpAndSettle();
        expect(
          find.byKey(ValueKey('marker-focus-${markerModel.id}')),
          findsOneWidget,
        );
        await captureScreenshot(tester, 'marker-keyboard-focus');

        await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
        await tester.sendKeyEvent(LogicalKeyboardKey.enter);
        await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
        await tester.pumpAndSettle();
        expect(
          controller.chartInteractionMode,
          EditorChartInteractionMode.selection,
        );
        expect(controller.selections.map((selection) => selection.firstRow), [
          0,
        ]);
        expect(controller.encodeFormalDraft(), orderedEquals(before));
        expect(controller.projectRevision, 0);

        await tester.sendKeyEvent(LogicalKeyboardKey.escape);
        await tester.pumpAndSettle();
        expect(
          controller.chartInteractionMode,
          EditorChartInteractionMode.normal,
        );
        expect(controller.selections, isEmpty);

        await tester.sendKeyEvent(LogicalKeyboardKey.enter);
        await tester.pumpAndSettle();
        expect(
          controller.chartInteractionMode,
          EditorChartInteractionMode.normal,
        );
        expect(controller.projectRevision, 1);
        expect(controller.encodeFormalDraft(), isNot(orderedEquals(before)));
      } finally {
        FocusManager.instance.highlightStrategy = previousHighlightStrategy;
      }
    },
  );

  testWidgets(
    'long press selects disjoint moves and copy supports repeated placement',
    (tester) async {
      await openEditor(tester, withTailMove: true);
      final source = controller.encodeFormalDraft();
      // Only the two selected moves join the clipboard; the tail move exists so
      // that the timeline stays longer than the batch the first paste lands.
      final sourceMarkers = controller.markers
          .where((marker) => marker.row <= 46)
          .toList();
      final sourceIds = sourceMarkers.map((marker) => marker.id).toSet();

      await activateRow(tester, '玩家', 0, longPress: true);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.selection,
      );
      expect(controller.selections.map((item) => item.firstRow), [0]);
      expectSessionToolbars(tester);
      // Navigation and history navigation stay available so the paste target
      // row remains reachable while the session is active.
      expect(controller.navigationEnabled, isTrue);
      controller.moveToRow(64);
      await tester.pumpAndSettle();
      expect(controller.selections.map((item) => item.firstRow), [0]);
      await activateRow(tester, 'Showtime', 33);
      expect(controller.selections.map((item) => item.firstRow), [0]);
      await activateRow(tester, 'Showtime', 32);
      expect(controller.selections.map((item) => item.firstRow), [0, 32]);
      expect(controller.selections.map((item) => item.lastRow), [14, 46]);
      expect(controller.encodeFormalDraft(), orderedEquals(source));
      expect(controller.projectRevision, 0);
      expect(controller.scale, 4);

      await activateRow(tester, '玩家', 32);
      expect(controller.selections.map((item) => item.firstRow), [0]);
      await activateRow(tester, 'Showtime', 32, longPress: true);
      expect(controller.selections.map((item) => item.firstRow), [0, 32]);
      expect(find.byKey(const ValueKey('selection-controls')), findsOneWidget);
      await captureScreenshot(tester, 'selection-highlight');

      await tapControl(tester, 'selection-copy');
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.pastePending,
      );
      expect(controller.clipboardCount, 2);
      expect(controller.hasClipboard, isTrue);
      expect(controller.projectRevision, 0);
      expect(controller.isDirty, isFalse);
      if (isMobilePlatform()) {
        expect(controller.pastePreviewAnchorRow, 32);
        expect(controller.pastePreview!.valid, isFalse);
        expectMobilePasteButton(tester, previewValid: false);
      }

      await moveToPasteRow(tester, 64);
      if (isMobilePlatform()) {
        expect(controller.pastePreviewAnchorRow, 64);
        expect(controller.pastePreview!.valid, isTrue);
        expect(
          find.byKey(const ValueKey('paste-pending-controls')),
          findsOneWidget,
        );
        expectMobilePasteButton(tester, previewValid: true);
      } else {
        final previewPoint = cellAtRow(tester, 'NPC', 66);
        final previewGesture = await tester.createGesture(
          kind: ui.PointerDeviceKind.mouse,
        );
        await previewGesture.addPointer(location: previewPoint);
        await previewGesture.moveTo(previewPoint);
        await tester.pumpAndSettle();
        expect(controller.currentRow, 64);
        expect(controller.pastePreviewAnchorRow, 66);
        expect(controller.pastePreview!.valid, isTrue);
        expect(
          find.byKey(const ValueKey('paste-pending-controls')),
          findsOneWidget,
        );
        expect(find.text('点击谱面放置 2 个舞步'), findsOneWidget);
        await captureScreenshot(tester, 'paste-pending-preview');
        await previewGesture.removePointer();
        await tester.pumpAndSettle();
      }
      if (isMobilePlatform()) {
        await captureScreenshot(tester, 'paste-pending-preview');
      }
      expect(controller.projectRevision, 0);
      expect(controller.encodeFormalDraft(), orderedEquals(source));

      await submitPasteAtRow(
        tester,
        64,
        previewValid: true,
        verifyMobileTapDoesNotSubmit: true,
      );
      expect(controller.projectRevision, 1);
      expect(controller.pastePending, isTrue);
      final afterFirst = controller.encodeFormalDraft();
      final firstPaste = controller.markers
          .where((marker) => marker.row >= 64 && marker.row <= 110)
          .toList();
      expect(
        firstPaste.map((marker) => markerGeometry(marker, 64)).toList(),
        sourceMarkers.map((marker) => markerGeometry(marker, 0)).toList(),
      );
      expect(
        firstPaste.map((marker) => marker.id).toSet().intersection(sourceIds),
        isEmpty,
      );

      await submitPasteAtRow(tester, 64, previewValid: false);
      expect(controller.encodeFormalDraft(), orderedEquals(afterFirst));
      expect(controller.projectRevision, 1);
      expect(controller.pastePending, isTrue);
      expect(controller.hasClipboard, isTrue);
      expect(find.text('粘贴范围内存在其他标记'), findsOneWidget);

      // The tail move keeps the timeline past the batch above, which is what a
      // fixed judgement line needs before it can anchor the next placement.
      await submitPasteAtRow(tester, 112, previewValid: true);
      expect(controller.projectRevision, 2);
      expect(controller.pastePending, isTrue);
      final secondPaste = controller.markers
          .where((marker) => marker.row >= 112 && marker.row <= 158)
          .toList();
      expect(
        secondPaste.map((marker) => markerGeometry(marker, 112)).toList(),
        sourceMarkers.map((marker) => markerGeometry(marker, 0)).toList(),
      );
      expect(
        secondPaste
            .map((marker) => marker.id)
            .toSet()
            .intersection(
              [
                ...sourceMarkers,
                ...firstPaste,
              ].map((marker) => marker.id).toSet(),
            ),
        isEmpty,
      );

      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );
      expect(controller.selections, isEmpty);
      expect(controller.hasClipboard, isFalse);
    },
  );

  testWidgets('cut defers deletion until a legal canvas placement', (
    tester,
  ) async {
    await openEditor(tester, withSecondMove: false);
    final before = controller.encodeFormalDraft();
    final sourceMarkers = List<ChartMarker>.of(controller.markers);
    final sourceIds = sourceMarkers.map((marker) => marker.id).toSet();

    await activateRow(tester, 'NPC', 0, longPress: true);
    await tapControl(tester, 'selection-cut');
    expect(
      controller.chartInteractionMode,
      EditorChartInteractionMode.pastePending,
    );
    expect(controller.cutPending, isTrue);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 0);

    await submitPasteAtRow(tester, 0, previewValid: true);
    expect(controller.cutPending, isFalse);
    expect(controller.pastePending, isTrue);
    expect(controller.projectRevision, 1);
    expect(
      controller.markers
          .map((marker) => marker.id)
          .toSet()
          .intersection(sourceIds),
      isEmpty,
    );
    final moved = controller.encodeFormalDraft();

    await tester.tap(find.byTooltip('撤销'));
    await tester.pumpAndSettle();
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.cutPending, isFalse);
    expect(controller.pastePending, isTrue);

    await submitPasteAtRow(tester, 32, previewValid: true);
    expect(controller.projectRevision, 3);
    expect(controller.markers, hasLength(sourceMarkers.length * 2));
    for (final anchor in [0, 32]) {
      expect(
        controller.markers
            .where(
              (marker) => marker.row >= anchor && marker.row <= anchor + 14,
            )
            .map((marker) => markerGeometry(marker, anchor)),
        sourceMarkers.map((marker) => markerGeometry(marker, 0)),
      );
    }
    expect(controller.encodeFormalDraft(), isNot(orderedEquals(moved)));
  });

  testWidgets('cut conflict preserves source and pending intent', (
    tester,
  ) async {
    await openEditor(tester);
    final before = controller.encodeFormalDraft();
    final sourceGeometry = controller.markers
        .where((marker) => marker.row <= 14)
        .map((marker) => markerGeometry(marker, 0))
        .toList();

    await activateRow(tester, 'NPC', 0, longPress: true);
    await tapControl(tester, 'selection-cut');
    await submitPasteAtRow(tester, 32, previewValid: false);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 0);
    expect(controller.cutPending, isTrue);
    expect(controller.pastePending, isTrue);
    expect(
      controller.markers
          .where((marker) => marker.row <= 14)
          .map((marker) => markerGeometry(marker, 0)),
      sourceGeometry,
    );
    expect(find.text('粘贴范围内存在其他标记'), findsOneWidget);

    await tapControl(tester, 'selection-exit');
    expect(controller.chartInteractionMode, EditorChartInteractionMode.normal);
    expect(controller.hasClipboard, isFalse);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
  });

  testWidgets(
    'cross-column selection resolves Showtime and ignores blank rows',
    (tester) async {
      await openEditor(tester, withSecondMove: false, withShowtime: true);
      final before = controller.encodeFormalDraft();
      final revision = controller.projectRevision;
      final canUndo = controller.canUndo;
      final canRedo = controller.canRedo;
      final showD = controller.showtimeMoves.single;
      await activateRow(tester, '玩家', showD.row, longPress: true);
      expect(controller.selections.single.firstRow, 0);
      expect(controller.selections.single.lastRow, 14);
      for (final column in ['NPC', 'Showtime', '玩家']) {
        await activateRow(tester, column, 16);
        expect(controller.selections.single.firstRow, 0);
        expect(find.text('该标记不属于可选择的舞步'), findsOneWidget);
        await activateRow(tester, column, 5);
        expect(controller.selections.single.firstRow, 0);
        await activateRow(tester, column, showD.row);
        expect(controller.selections, isEmpty);
        await activateRow(tester, column, showD.row);
        expect(controller.selections.single.firstRow, 0);
      }
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, revision);
      expect(controller.canUndo, canUndo);
      expect(controller.canRedo, canRedo);
    },
  );

  testWidgets('first cut placement invalidates Showtime in one history', (
    tester,
  ) async {
    await openEditor(tester, withSecondMove: false, withShowtime: true);
    final before = controller.encodeFormalDraft();
    expect(controller.showtimeMoves, hasLength(1));

    await activateRow(tester, 'NPC', 0, longPress: true);
    await tapControl(tester, 'selection-cut');
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.showtimeMoves, hasLength(1));
    expect(controller.cutPending, isTrue);

    await submitPasteAtRow(tester, 32, previewValid: true);
    expect(controller.showtimeMoves, isEmpty);
    expect(
      controller.markers.where(
        (marker) => marker.column == OTGY_CHART_COLUMN_SHOWTIME,
      ),
      isEmpty,
    );
    expect(controller.projectRevision, 1);
    expect(controller.cutPending, isFalse);
    expect(controller.pastePending, isTrue);

    await tester.tap(find.byTooltip('撤销'));
    await tester.pumpAndSettle();
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.showtimeMoves, hasLength(1));
    expect(controller.cutPending, isFalse);
    expect(controller.pastePending, isTrue);
  });
}
