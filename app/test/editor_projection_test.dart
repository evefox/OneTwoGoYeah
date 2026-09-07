import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';
import 'package:one_two_go_yeah/src/ui/editor_theme.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test(
    'automatic edit zoom keeps the initial sixteenth-row spacing usable',
    () {
      final controller = EditorController();
      addTearDown(controller.dispose);

      expect(controller.zoom, 9.5);
      controller.setBpm(127.882);

      expect(controller.zoom, 10);
      expect(controller.rowSpacing, closeTo(targetEditorRowSpacing, 0.2));
      expect(controller.zoom * 4, (controller.zoom * 4).roundToDouble());
    },
  );

  test('manual edit zoom wins over later BPM changes', () async {
    final controller = EditorController();
    addTearDown(controller.dispose);

    await controller.setZoom(8.25);
    controller.setBpm(180);

    expect(controller.zoom, 8.25);
  });

  test('song-time jumps use core timing and timeline bounds', () {
    final controller = EditorController();
    addTearDown(controller.dispose);

    controller.moveToSongSeconds(5.25);
    final expectedRow = OtgyNative.secondsToSixteenthRow(
      bpm: controller.projectedBpm,
      offset: controller.offset,
      seconds: 5.25,
    );
    expect(controller.currentRow, closeTo(expectedRow, 1e-12));

    final unchanged = controller.currentRow;
    controller.moveToSongSeconds(double.nan);
    controller.moveToSongSeconds(-1);
    expect(controller.currentRow, unchanged);

    controller.moveToSongSeconds(1000);
    expect(controller.currentRow, controller.timelineRange.endRow);
  });

  test(
    'playback visual density gives five seconds a stable maximum length',
    () async {
      final controller = EditorController();
      addTearDown(controller.dispose);
      controller.setBpm(127.882);

      final rowsInFiveSeconds = 127.882 / 3;
      expect(controller.playbackVisualPixelsPerSecond, 170);
      expect(
        controller.playbackRowSpacing * rowsInFiveSeconds,
        closeTo(850, 1e-9),
      );

      await controller.setZoom(7);
      expect(
        controller.playbackVisualPixelsPerSecond,
        controller.pixelsPerSecond,
      );
      expect(controller.playbackRowSpacing, controller.rowSpacing);
    },
  );

  test('row midpoint belongs to the later row', () {
    expect(
      projectEditorRow(
        y: 109.999,
        cursorY: 100,
        currentRow: 10,
        rowSpacing: 20,
      ),
      10,
    );
    expect(
      projectEditorRow(y: 110, cursorY: 100, currentRow: 10, rowSpacing: 20),
      11,
    );
  });

  test('chart editing remains available at dense row spacing', () {
    final controller = EditorController();
    addTearDown(controller.dispose);

    controller.clickCell(OTGY_CHART_COLUMN_NPC, 0);

    expect(controller.markers, isNotEmpty);
  });

  test('marker sizing stays fixed while Zoom spacing can become dense', () {
    expect(editorMarkerSize(mobile: false, diamond: false), 22);
    expect(editorMarkerSize(mobile: false, diamond: true), 28);
    expect(editorMarkerSize(mobile: true, diamond: false), 28);
    expect(editorMarkerSize(mobile: true, diamond: true), 36);
  });

  test('marker numbers keep fixed typography at every Zoom density', () {
    expect(
      editorMarkerFontSize(circleSize: 22, textLength: 1),
      closeTo(14.08, 1e-12),
    );
    expect(editorMarkerFontSize(circleSize: 28, textLength: 2), 16);
    expect(
      editorMarkerFontSize(circleSize: 22, textLength: 3),
      closeTo(10.325333333333334, 1e-9),
    );
  });

  test('marker styles use the confirmed flat role palette', () {
    final npcC = editorMarkerStyle(OTGY_CHART_MARKER_NPC_BLOCK_START);
    final npcN = editorMarkerStyle(OTGY_CHART_MARKER_NPC_NOTE);
    final npcS = editorMarkerStyle(OTGY_CHART_MARKER_NPC_MOVE_END);
    final playerC = editorMarkerStyle(OTGY_CHART_MARKER_PLAYER_BLOCK_START);
    final playerN = editorMarkerStyle(OTGY_CHART_MARKER_PLAYER_NOTE);
    final playerS = editorMarkerStyle(OTGY_CHART_MARKER_PLAYER_MOVE_END);
    final showC = editorMarkerStyle(OTGY_CHART_MARKER_SHOWTIME_START);
    final showD = editorMarkerStyle(OTGY_CHART_MARKER_SHOWTIME_MOVE);

    expect(npcC.fill, EditorColors.markerNpc);
    expect(npcN.fill, EditorColors.markerNpc);
    expect(npcS.fill, EditorColors.markerS);
    expect(playerC.fill, EditorColors.markerPlayer);
    expect(playerN.fill, EditorColors.markerPlayer);
    expect(playerS.fill, EditorColors.markerS);
    expect(showC.fill, EditorColors.markerD);
    expect(showD.fill, EditorColors.markerD);

    expect(npcC.isDiamond, isTrue);
    expect(playerC.isDiamond, isTrue);
    expect(showC.isDiamond, isTrue);
    expect(npcN.isDiamond, isFalse);
    expect(npcS.isDiamond, isFalse);
    expect(playerN.isDiamond, isFalse);
    expect(playerS.isDiamond, isFalse);
    expect(showD.isDiamond, isFalse);

    expect(npcC.showsNumber, isFalse);
    expect(npcN.showsNumber, isTrue);
    expect(npcS.showsNumber, isFalse);
    expect(playerC.showsNumber, isFalse);
    expect(playerN.showsNumber, isTrue);
    expect(playerS.showsNumber, isFalse);
    expect(showC.showsNumber, isFalse);
    expect(showD.showsNumber, isTrue);
    expect(EditorColors.markerText, const Color(0xFFF8FBFF));
  });
}
