import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  late Directory directory;
  late LocalProjectStore store;
  late EditorController controller;
  late NativeChartEditor fixtureEditor;

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-cursor-keys-');
    store = await LocalProjectStore.openAt(directory);
    fixtureEditor = NativeChartEditor.createEmpty();
    controller = EditorController(editor: fixtureEditor);
  });

  tearDown(() async {
    controller.dispose();
    if (await directory.exists()) {
      await directory.delete(recursive: true);
    }
  });

  Future<void> openEditor(WidgetTester tester) async {
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pumpAndSettle();
  }

  Future<void> moveTo(WidgetTester tester, double row) async {
    controller.moveToRow(row);
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

  testWidgets('arrow keys snap one row in each direction', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 16);
      await press(tester, LogicalKeyboardKey.arrowUp);
      expect(controller.currentRow, 15);
      await press(tester, LogicalKeyboardKey.arrowDown);
      expect(controller.currentRow, 16);

      // A fractional cursor snaps to the next grid line, not a fixed delta.
      await moveTo(tester, 16.4);
      await press(tester, LogicalKeyboardKey.arrowUp);
      expect(controller.currentRow, 16);
      await moveTo(tester, 16.4);
      await press(tester, LogicalKeyboardKey.arrowDown);
      expect(controller.currentRow, 17);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('page keys snap to beat and measure boundaries', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 21);

      await press(tester, LogicalKeyboardKey.pageUp);
      expect(controller.currentRow, 16);
      await moveTo(tester, 21);
      await press(tester, LogicalKeyboardKey.pageUp, shift: true);
      expect(controller.currentRow, 20);

      await moveTo(tester, 21);
      await press(tester, LogicalKeyboardKey.pageDown);
      expect(controller.currentRow, 32);
      await moveTo(tester, 21);
      await press(tester, LogicalKeyboardKey.pageDown, shift: true);
      expect(controller.currentRow, 24);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('home and end jump to the chart edges', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      final endRow = controller.timelineRange.endRow;

      await moveTo(tester, 12);
      await press(tester, LogicalKeyboardKey.home);
      expect(controller.currentRow, 0);

      await press(tester, LogicalKeyboardKey.end);
      expect(controller.currentRow, endRow);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('primary arrow jumps between markers', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    controller.dispose();
    final editor = NativeChartEditor.createEmpty();
    addDanceMove(editor, 16);
    addDanceMove(editor, 64);
    controller = EditorController(editor: editor);
    try {
      await openEditor(tester);
      await moveTo(tester, 40);

      await press(tester, LogicalKeyboardKey.arrowUp, meta: true);
      expect(controller.currentRow, 30);
      await press(tester, LogicalKeyboardKey.arrowUp, meta: true);
      expect(controller.currentRow, 28);
      await press(tester, LogicalKeyboardKey.arrowDown, meta: true);
      expect(controller.currentRow, 30);

      // No marker remains before the first row: the cursor stays put.
      await moveTo(tester, 16);
      await press(tester, LogicalKeyboardKey.arrowUp, meta: true);
      expect(controller.currentRow, 16);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('paste shortcut commits at the judgement line', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    controller.dispose();
    final editor = NativeChartEditor.createEmpty();
    addDanceMove(editor, 0);
    controller = EditorController(editor: editor);
    try {
      await openEditor(tester);
      controller.enterSelectionMode();
      controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
      await tester.pumpAndSettle();
      // Use the toolbar button so the paste chord has to work after the
      // pointer has been on a control, not only after a canvas gesture.
      await tester.tap(find.byKey(const ValueKey('selection-copy')));
      await tester.pumpAndSettle();
      expect(controller.pastePending, isTrue);
      final revision = controller.projectRevision;

      await moveTo(tester, 32);
      await press(tester, LogicalKeyboardKey.keyV, meta: true);
      expect(controller.projectRevision, greaterThan(revision));
      expect(
        controller.markers.where((marker) => marker.row >= 32).length,
        greaterThanOrEqualTo(7),
      );
      // A successful paste stays in paste-pending mode and keeps the cursor.
      expect(controller.pastePending, isTrue);
      expect(controller.currentRow, 32);

      // Without a prepared clipboard the same chord does nothing.
      controller.exitChartInteraction();
      await tester.pumpAndSettle();
      final afterExit = controller.projectRevision;
      await press(tester, LogicalKeyboardKey.keyV, meta: true);
      expect(controller.projectRevision, afterExit);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('navigation stays available inside a selection session', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 20);
      controller.enterSelectionMode();
      await tester.pumpAndSettle();

      await press(tester, LogicalKeyboardKey.arrowDown);
      expect(controller.currentRow, 21);
      await press(tester, LogicalKeyboardKey.end);
      expect(controller.currentRow, controller.timelineRange.endRow);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('playback blocks cursor navigation', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 20);
      controller.setPlaybackModeForTest(EditorPlaybackMode.simulation);
      await tester.pump();
      await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
      await tester.pump();
      expect(controller.currentRow, 20);
      await tester.sendKeyEvent(LogicalKeyboardKey.end);
      await tester.pump();
      expect(controller.currentRow, 20);
      controller.setPlaybackModeForTest(EditorPlaybackMode.idle);
      await tester.pump();
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('mobile does not register navigation or paste keys', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    try {
      await openEditor(tester);
      await moveTo(tester, 20);
      await press(tester, LogicalKeyboardKey.arrowDown);
      expect(controller.currentRow, 20);
      await press(tester, LogicalKeyboardKey.end);
      expect(controller.currentRow, 20);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });
}
