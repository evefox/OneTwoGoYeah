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

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-explicit-keys-');
    store = await LocalProjectStore.openAt(directory);
    controller = EditorController();
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

  Future<void> moveTo(WidgetTester tester, int row) async {
    controller.moveToRow(row.toDouble());
    await tester.pumpAndSettle();
  }

  Future<void> press(WidgetTester tester, LogicalKeyboardKey key) async {
    await tester.sendKeyEvent(key);
    await tester.pumpAndSettle();
  }

  ChartMarker? markerAt(int row) {
    for (final marker in controller.markers) {
      if (marker.row == row) {
        return marker;
      }
    }
    return null;
  }

  testWidgets('number keys place each marker type and advance the cursor', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 16);

      await press(tester, LogicalKeyboardKey.digit1);
      expect(markerAt(16)?.type, OTGY_CHART_MARKER_NPC_BLOCK_START);
      expect(controller.currentRow, 17);

      await press(tester, LogicalKeyboardKey.digit2);
      expect(markerAt(17)?.type, OTGY_CHART_MARKER_NPC_NOTE);
      expect(controller.currentRow, 18);

      await press(tester, LogicalKeyboardKey.digit3);
      expect(markerAt(18)?.type, OTGY_CHART_MARKER_NPC_MOVE_END);
      expect(controller.currentRow, 19);

      // Showtime needs a complete player block, so this press is rejected and
      // leaves both the draft and the cursor alone.
      final beforeRejection = controller.markers.length;
      await press(tester, LogicalKeyboardKey.digit4);
      expect(controller.markers.length, beforeRejection);
      expect(controller.currentRow, 19);

      // The player key is the ordinary contextual click at the cursor.
      await press(tester, LogicalKeyboardKey.digit5);
      expect(markerAt(19)?.type, OTGY_CHART_MARKER_PLAYER_BLOCK_START);
      expect(controller.currentRow, 20);

      await press(tester, LogicalKeyboardKey.digit5);
      expect(markerAt(20)?.type, OTGY_CHART_MARKER_PLAYER_NOTE);
      expect(markerAt(21)?.type, OTGY_CHART_MARKER_PLAYER_MOVE_END);
      expect(controller.currentRow, 21);

      final history = controller.projectRevision;
      await press(tester, LogicalKeyboardKey.digit1);
      expect(controller.projectRevision, history);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('pressing the placed type again removes only that marker', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 16);
      await press(tester, LogicalKeyboardKey.digit1);
      await press(tester, LogicalKeyboardKey.digit2);
      await press(tester, LogicalKeyboardKey.digit3);
      expect(controller.currentRow, 19);

      await moveTo(tester, 17);
      await press(tester, LogicalKeyboardKey.digit2);
      expect(markerAt(17), isNull);
      expect(markerAt(16)?.type, OTGY_CHART_MARKER_NPC_BLOCK_START);
      expect(markerAt(18)?.type, OTGY_CHART_MARKER_NPC_MOVE_END);
      // A removal never advances the cursor.
      expect(controller.currentRow, 17);

      // A different NPC type on an occupied row is rejected without history.
      final historyBeforeConflict = controller.projectRevision;
      await moveTo(tester, 16);
      await press(tester, LogicalKeyboardKey.digit2);
      expect(controller.projectRevision, historyBeforeConflict);
      expect(markerAt(16)?.type, OTGY_CHART_MARKER_NPC_BLOCK_START);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('the showtime key uses the contextual click at the cursor', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 0);
      await press(tester, LogicalKeyboardKey.digit1);
      controller.moveToRow(10);
      await press(tester, LogicalKeyboardKey.digit2);
      controller.moveToRow(20);
      await press(tester, LogicalKeyboardKey.digit3);
      controller.moveToRow(30);
      await press(tester, LogicalKeyboardKey.digit5);
      controller.moveToRow(40);
      await press(tester, LogicalKeyboardKey.digit5);
      expect(markerAt(40)?.type, OTGY_CHART_MARKER_PLAYER_NOTE);
      expect(markerAt(50)?.type, OTGY_CHART_MARKER_PLAYER_MOVE_END);

      controller.moveToRow(60);
      await tester.pumpAndSettle();
      await press(tester, LogicalKeyboardKey.digit4);
      expect(markerAt(60)?.type, OTGY_CHART_MARKER_SHOWTIME_START);
      expect(
        controller.markers.any(
          (marker) => marker.type == OTGY_CHART_MARKER_SHOWTIME_MOVE,
        ),
        isTrue,
      );
      // The cursor advances one sixteenth even when completion placed a
      // Showtime move right after it.
      expect(controller.currentRow, 61);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('mobile does not register placement keys', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    try {
      await openEditor(tester);
      await moveTo(tester, 16);
      await press(tester, LogicalKeyboardKey.digit1);
      expect(controller.markers, isEmpty);
      expect(controller.currentRow, 16);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('holding a marker key does not repeat placement', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 16);

      await tester.sendKeyDownEvent(LogicalKeyboardKey.digit1);
      await tester.pumpAndSettle();
      expect(markerAt(16)?.type, OTGY_CHART_MARKER_NPC_BLOCK_START);
      expect(controller.currentRow, 17);

      await tester.sendKeyRepeatEvent(LogicalKeyboardKey.digit1);
      await tester.pumpAndSettle();
      expect(controller.markers.length, 1);
      expect(controller.currentRow, 17);

      await tester.sendKeyUpEvent(LogicalKeyboardKey.digit1);
      await tester.pumpAndSettle();
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('selection and playback block placement keys', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await moveTo(tester, 16);

      controller.enterSelectionMode();
      await tester.pumpAndSettle();
      await press(tester, LogicalKeyboardKey.digit1);
      expect(controller.markers, isEmpty);
      expect(controller.currentRow, 16);
      controller.exitChartInteraction();
      await tester.pumpAndSettle();

      controller.setPlaybackModeForTest(EditorPlaybackMode.simulation);
      await tester.pump();
      await tester.sendKeyEvent(LogicalKeyboardKey.digit1);
      await tester.pump();
      expect(controller.markers, isEmpty);
      expect(controller.currentRow, 16);
      controller.setPlaybackModeForTest(EditorPlaybackMode.idle);
      await tester.pump();
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });
}
