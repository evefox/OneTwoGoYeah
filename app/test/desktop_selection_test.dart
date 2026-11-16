import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  EditorController controller = EditorController(
    editor: NativeChartEditor.createEmpty(),
  );

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

  int selectedMoveCount(EditorController controller) =>
      controller.selections.map((unit) => unit.danceMoveDrillId).toSet().length;

  int selectedFirstRow(EditorController controller) => controller.selections
      .map((unit) => unit.firstRow)
      .reduce((left, right) => left < right ? left : right);

  void useTwoMoves() {
    final editor = NativeChartEditor.createEmpty();
    addDanceMove(editor, 0);
    addDanceMove(editor, 32);
    controller = EditorController(editor: editor);
  }

  void useEmptyChart() {
    controller = EditorController(editor: NativeChartEditor.createEmpty());
  }

  tearDown(() {
    controller.dispose();
  });

  Future<void> pumpCanvas(
    WidgetTester tester, {
    VoidCallback? onExportSlk,
    Size size = const Size(1200, 800),
  }) async {
    await tester.binding.setSurfaceSize(size);
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: EditorCanvas(controller: controller, onExportSlk: onExportSlk),
        ),
      ),
    );
    await tester.pumpAndSettle();
  }

  Offset rowPoint(WidgetTester tester, int row) {
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    return Offset(
      canvas.center.dx,
      canvas.top +
          editorCursorY(canvas.size) +
          (row - controller.currentRow) * controller.rowSpacing,
    );
  }

  Future<void> dragRows(WidgetTester tester, int from, int to) async {
    controller.moveToRow(0);
    await tester.pumpAndSettle();
    final gesture = await tester.startGesture(
      rowPoint(tester, from),
      kind: PointerDeviceKind.mouse,
    );
    await gesture.moveTo(rowPoint(tester, to));
    await tester.pump();
    await gesture.up();
    await tester.pumpAndSettle();
  }

  Future<void> press(
    WidgetTester tester,
    LogicalKeyboardKey key, {
    bool control = false,
    bool meta = false,
    bool shift = false,
  }) async {
    if (control) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
    }
    if (meta) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.metaLeft);
    }
    if (shift) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
    }
    await tester.sendKeyEvent(key);
    if (shift) {
      await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
    }
    if (meta) {
      await tester.sendKeyUpEvent(LogicalKeyboardKey.metaLeft);
    }
    if (control) {
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
    }
    await tester.pumpAndSettle();
  }

  testWidgets('desktop drag selects every move the range crosses', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    useTwoMoves();
    try {
      await pumpCanvas(tester);
      await dragRows(tester, 0, 36);

      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.selection,
      );
      expect(selectedMoveCount(controller), 2);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('a drag that stays inside one move selects only that move', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    useTwoMoves();
    try {
      await pumpCanvas(tester);
      await dragRows(tester, 0, 14);

      expect(selectedMoveCount(controller), 1);
      expect(selectedFirstRow(controller), 0);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('shift and alt clicks add and subtract moves', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    useTwoMoves();
    try {
      await pumpCanvas(tester);
      await dragRows(tester, 0, 14);
      expect(selectedMoveCount(controller), 1);

      controller.moveToRow(32);
      await tester.pumpAndSettle();
      await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
      await tester.tapAt(rowPoint(tester, 32), kind: PointerDeviceKind.mouse);
      await tester.pumpAndSettle();
      await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
      expect(selectedMoveCount(controller), 2);

      await tester.sendKeyDownEvent(LogicalKeyboardKey.altLeft);
      await tester.tapAt(rowPoint(tester, 32), kind: PointerDeviceKind.mouse);
      await tester.pumpAndSettle();
      await tester.sendKeyUpEvent(LogicalKeyboardKey.altLeft);
      expect(selectedMoveCount(controller), 1);
      expect(selectedFirstRow(controller), 0);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('Delete removes the selected moves and undo restores them', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    useTwoMoves();
    try {
      await pumpCanvas(tester);
      // Cover every unit of both moves so Delete removes the full content.
      await dragRows(tester, 0, 46);
      expect(selectedMoveCount(controller), 2);

      await press(tester, LogicalKeyboardKey.delete);
      expect(controller.markers, isEmpty);
      expect(controller.selectionCount, 0);

      await press(tester, LogicalKeyboardKey.keyZ, meta: true);
      expect(controller.markers, isNotEmpty);
      // Undo restores the content, not the selection set.
      expect(controller.selectionCount, 0);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('Delete is inert outside a selection session', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    useTwoMoves();
    try {
      await pumpCanvas(tester);
      final before = controller.markers.length;
      await press(tester, LogicalKeyboardKey.delete);
      expect(controller.markers.length, before);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('Ctrl+Shift+E exports only when the chart has content', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    try {
      var exports = 0;
      useTwoMoves();
      await pumpCanvas(tester, onExportSlk: () => exports++);
      await press(tester, LogicalKeyboardKey.keyE, control: true, shift: true);
      expect(exports, 1);

      useEmptyChart();
      await pumpCanvas(tester, onExportSlk: () => exports++);
      await press(tester, LogicalKeyboardKey.keyE, control: true, shift: true);
      expect(exports, 1);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('mobile drag does not start a selection', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    useTwoMoves();
    try {
      await pumpCanvas(tester, size: const Size(390, 844));
      await dragRows(tester, 0, 36);

      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );
      expect(controller.selectionCount, 0);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });
}
