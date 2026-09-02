import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/infrastructure/playback_output.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';
import 'package:one_two_go_yeah/src/ui/editor_screen.dart';
import 'package:one_two_go_yeah/src/ui/editor_theme.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  late Directory temporaryDirectory;
  late LocalProjectStore store;
  late EditorController controller;

  setUp(() async {
    temporaryDirectory = await Directory.systemTemp.createTemp('otgy-widget-');
    store = await LocalProjectStore.openAt(temporaryDirectory);
    controller = EditorController();
  });

  tearDown(() async {
    controller.dispose();
    if (await temporaryDirectory.exists()) {
      await temporaryDirectory.delete(recursive: true);
    }
  });

  Future<void> pumpEditor(
    WidgetTester tester, {
    Size size = const Size(1000, 700),
  }) async {
    await tester.binding.setSurfaceSize(size);
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pump();
  }

  test('projects fallback columns as NPC, Showtime, and player', () {
    const chart = Rect.fromLTWH(0, 28, 900, 600);
    int columnAt(double x) =>
        projectEditorColumn(x: x, chart: chart, waveformBlocks: const []);

    expect(columnAt(0), OTGY_CHART_COLUMN_NPC);
    expect(columnAt(299.99), OTGY_CHART_COLUMN_NPC);
    expect(columnAt(300), OTGY_CHART_COLUMN_SHOWTIME);
    expect(columnAt(599.99), OTGY_CHART_COLUMN_SHOWTIME);
    expect(columnAt(600), OTGY_CHART_COLUMN_PLAYER);
    expect(columnAt(900), OTGY_CHART_COLUMN_PLAYER);
  });

  test('recognizes SLK extensions without case sensitivity', () {
    expect(hasSlkFileExtension('chart.slk'), isTrue);
    expect(hasSlkFileExtension('chart.SLK'), isTrue);
    expect(hasSlkFileExtension('chart.txt'), isFalse);
  });

  test('recognizes supported audio extensions without case sensitivity', () {
    expect(hasSupportedAudioFileExtension('track.mp3'), isTrue);
    expect(hasSupportedAudioFileExtension('track.MP3'), isTrue);
    expect(hasSupportedAudioFileExtension('track.ogg'), isTrue);
    expect(hasSupportedAudioFileExtension('track.OGG'), isTrue);
    expect(hasSupportedAudioFileExtension('track.wav'), isFalse);
  });

  test('keeps filtered waveform layers subordinate to chart markers', () {
    expect(
      editorWaveformRowOpacity(255, EditorWaveformLayer.original),
      closeTo(0.52, 1e-12),
    );
    expect(
      editorWaveformRowOpacity(255, EditorWaveformLayer.overlayOriginal),
      closeTo(0.24, 1e-12),
    );
    expect(
      editorWaveformRowOpacity(255, EditorWaveformLayer.filtered),
      closeTo(0.36, 1e-12),
    );
    expect(EditorColors.waveformFilter, const Color(0xFFA7BDC2));
    expect(EditorColors.waveformFilter, isNot(EditorColors.judgement));
    expect(EditorColors.waveformFilter, isNot(EditorColors.markerNpc));
    expect(EditorColors.waveformFilter, isNot(EditorColors.markerPlayer));
    expect(EditorColors.waveformFilter, isNot(EditorColors.markerS));
    expect(EditorColors.waveformFilter, isNot(EditorColors.markerD));
  });

  testWidgets('shows the three editor regions and precise timing summaries', (
    tester,
  ) async {
    await pumpEditor(tester);

    expect(find.byKey(const ValueKey('audio-picker')), findsOneWidget);
    expect(find.byKey(const ValueKey('chart-menu')), findsOneWidget);
    expect(find.byKey(const ValueKey('editor-canvas')), findsOneWidget);
    expect(find.text('100.000'), findsOneWidget);
    expect(find.text('0.000'), findsOneWidget);
    expect(find.text('00:00.000'), findsOneWidget);
    expect(find.text('行 0.00'), findsNothing);
    expect(find.bySemanticsLabel('NPC'), findsOneWidget);
    expect(find.bySemanticsLabel('玩家'), findsOneWidget);
    expect(find.bySemanticsLabel('Showtime'), findsOneWidget);

    controller.moveToRow(4);
    await tester.pump();
    expect(find.text('00:00.600'), findsOneWidget);

    controller.setOffset(0.75);
    await tester.pump();
    expect(find.text('-00:00.150'), findsOneWidget);

    controller.setOffset(0);
    controller.setBpm(0.1);
    controller.moveToRow(64);
    await tester.pump();
    expect(find.text('160:00.000'), findsOneWidget);
  });

  testWidgets('jumps to a song time from the status bar', (tester) async {
    await pumpEditor(tester);

    await tester.tap(find.byKey(const ValueKey('song-time')));
    await tester.pumpAndSettle();
    expect(find.text('跳转到歌曲时间'), findsOneWidget);
    expect(find.byKey(const ValueKey('jump-time-minutes')), findsOneWidget);
    expect(find.byKey(const ValueKey('jump-time-seconds')), findsOneWidget);

    await tester.enterText(
      find.byKey(const ValueKey('jump-time-minutes')),
      '0',
    );
    await tester.enterText(
      find.byKey(const ValueKey('jump-time-seconds')),
      '5.250',
    );
    await tester.tap(find.widgetWithText(FilledButton, '跳转'));
    await tester.pumpAndSettle();

    final expectedRow = OtgyNative.secondsToSixteenthRow(
      bpm: controller.projectedBpm,
      offset: controller.offset,
      seconds: 5.25,
    );
    expect(controller.currentRow, closeTo(expectedRow, 1e-12));
    expect(find.text('00:05.250'), findsOneWidget);

    final unchanged = controller.currentRow;
    await tester.tap(find.byKey(const ValueKey('song-time')));
    await tester.pumpAndSettle();
    await tester.enterText(
      find.byKey(const ValueKey('jump-time-seconds')),
      '60',
    );
    await tester.tap(find.widgetWithText(FilledButton, '跳转'));
    await tester.pump();
    expect(find.text('请输入 0 到 60 秒以内'), findsOneWidget);
    expect(controller.currentRow, unchanged);
    await tester.tap(find.widgetWithText(TextButton, '取消'));
    await tester.pumpAndSettle();
    expect(controller.currentRow, unchanged);
  });

  testWidgets('keeps bottom info notices legible on their backgrounds', (
    tester,
  ) async {
    await pumpEditor(tester);

    controller.showNotice('普通提示');
    await tester.pump();
    await tester.pump();

    expect(
      tester.widget<SnackBar>(find.byType(SnackBar)).backgroundColor,
      EditorColors.surfaceRaised,
    );
    expect(
      tester.widget<Text>(find.text('普通提示')).style?.color,
      EditorColors.text,
    );
  });

  testWidgets('keeps bottom error notices legible on their backgrounds', (
    tester,
  ) async {
    await pumpEditor(tester);

    controller.showNotice('错误提示', error: true);
    await tester.pump();
    await tester.pump();

    expect(
      tester.widget<SnackBar>(find.byType(SnackBar)).backgroundColor,
      EditorColors.danger,
    );
    expect(
      tester.widget<Text>(find.text('错误提示')).style?.color,
      EditorColors.canvas,
    );
  });

  testWidgets(
    'aligns chart columns to waveform channels without stretching on wide windows',
    (tester) async {
      final previousPlatform = debugDefaultTargetPlatformOverride;
      debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');
      await pumpEditor(tester);

      Rect headerRect(String label) =>
          tester.getRect(find.byKey(ValueKey('column-header-$label')));

      final block = controller.canvasWaveformBlocks.first;
      final nativeWaveformWidth = block.channelWidth * 2 + block.channelSpacing;
      final canvas = tester.getRect(
        find.byKey(const ValueKey('editor-canvas')),
      );
      expect(nativeWaveformWidth, lessThan(canvas.width));

      final npc = headerRect('NPC');
      final showtime = headerRect('Showtime');
      final player = headerRect('玩家');
      final expectedNpcCenter =
          canvas.center.dx - nativeWaveformWidth / 2 + block.channelWidth / 2;
      final expectedPlayerCenter =
          canvas.center.dx + nativeWaveformWidth / 2 - block.channelWidth / 2;

      expect(npc.center.dx, closeTo(expectedNpcCenter, 0.01));
      expect(showtime.center.dx, closeTo(canvas.center.dx, 0.01));
      expect(player.center.dx, closeTo(expectedPlayerCenter, 0.01));
      expect(npc.center.dx, lessThan(showtime.center.dx));
      expect(showtime.center.dx, lessThan(player.center.dx));

      final localChart = Rect.fromLTRB(0, 28, canvas.width, canvas.height);
      final localNpcCenter = expectedNpcCenter - canvas.left;
      final localShowtimeCenter = localChart.center.dx;
      final localPlayerCenter = expectedPlayerCenter - canvas.left;
      final npcShowtimeBoundary = (localNpcCenter + localShowtimeCenter) / 2;
      final showtimePlayerBoundary =
          (localShowtimeCenter + localPlayerCenter) / 2;
      int columnAt(double x) => projectEditorColumn(
        x: x,
        chart: localChart,
        waveformBlocks: controller.canvasWaveformBlocks,
      );

      expect(columnAt(localChart.left), OTGY_CHART_COLUMN_NPC);
      expect(columnAt(npcShowtimeBoundary - 0.01), OTGY_CHART_COLUMN_NPC);
      expect(columnAt(npcShowtimeBoundary), OTGY_CHART_COLUMN_SHOWTIME);
      expect(
        columnAt(showtimePlayerBoundary - 0.01),
        OTGY_CHART_COLUMN_SHOWTIME,
      );
      expect(columnAt(showtimePlayerBoundary), OTGY_CHART_COLUMN_PLAYER);
      expect(columnAt(localChart.right), OTGY_CHART_COLUMN_PLAYER);

      final npcOffset = npc.center.dx - canvas.center.dx;
      final playerOffset = player.center.dx - canvas.center.dx;
      final roleAxisDistance = player.center.dx - npc.center.dx;

      controller.toggleWaveform();
      await tester.pump();
      expect(headerRect('NPC').center.dx, closeTo(npc.center.dx, 0.01));
      expect(headerRect('玩家').center.dx, closeTo(player.center.dx, 0.01));

      await tester.binding.setSurfaceSize(const Size(1400, 700));
      await tester.pumpAndSettle();
      final wideCanvas = tester.getRect(
        find.byKey(const ValueKey('editor-canvas')),
      );
      final wideNpc = headerRect('NPC');
      final wideShowtime = headerRect('Showtime');
      final widePlayer = headerRect('玩家');

      expect(
        wideNpc.center.dx - wideCanvas.center.dx,
        closeTo(npcOffset, 0.01),
      );
      expect(
        widePlayer.center.dx - wideCanvas.center.dx,
        closeTo(playerOffset, 0.01),
      );
      expect(wideShowtime.center.dx, closeTo(wideCanvas.center.dx, 0.01));
      expect(
        widePlayer.center.dx - wideNpc.center.dx,
        closeTo(roleAxisDistance, 0.01),
      );
      debugDefaultTargetPlatformOverride = previousPlatform;
    },
  );

  testWidgets('reserves mobile clearance beside the right control rail', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    await pumpEditor(tester, size: const Size(390, 700));

    Rect headerRect(String label) =>
        tester.getRect(find.byKey(ValueKey('column-header-$label')));

    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final chart = Rect.fromLTRB(0, 28, canvas.width, canvas.height);
    final block = controller.canvasWaveformBlocks.first;
    final nativeWidth = block.channelWidth * 2 + block.channelSpacing;
    final projectionWidth = canvas.width - mobileEditorContentRightInset;
    final fit = math.min(1.0, projectionWidth / nativeWidth);
    final leftOrigin = (projectionWidth - nativeWidth * fit) / 2;
    final expectedNpc = leftOrigin + block.channelWidth * fit / 2;
    final expectedShowtime = projectionWidth / 2;
    final expectedPlayer =
        leftOrigin +
        (block.channelWidth + block.channelSpacing) * fit +
        block.channelWidth * fit / 2;

    expect(
      headerRect('NPC').center.dx,
      closeTo(canvas.left + expectedNpc, 0.01),
    );
    expect(
      headerRect('Showtime').center.dx,
      closeTo(canvas.left + expectedShowtime, 0.01),
    );
    expect(
      headerRect('玩家').center.dx,
      closeTo(canvas.left + expectedPlayer, 0.01),
    );
    expect(
      headerRect('Showtime').center.dx,
      closeTo(canvas.center.dx - mobileEditorContentRightInset / 2, 0.01),
    );

    final unshiftedFit = math.min(1.0, canvas.width / nativeWidth);
    final unshiftedLeftOrigin = (canvas.width - nativeWidth * unshiftedFit) / 2;
    final unshiftedPlayer =
        unshiftedLeftOrigin +
        (block.channelWidth + block.channelSpacing) * unshiftedFit +
        block.channelWidth * unshiftedFit / 2;
    final unshiftedBoundary = (canvas.width / 2 + unshiftedPlayer) / 2;
    final shiftedBoundary = (expectedShowtime + expectedPlayer) / 2;
    final reclaimedPlayerX = (shiftedBoundary + unshiftedBoundary) / 2;
    expect(
      projectEditorColumn(
        x: reclaimedPlayerX,
        chart: chart,
        waveformBlocks: controller.canvasWaveformBlocks,
      ),
      OTGY_CHART_COLUMN_SHOWTIME,
    );
    expect(
      projectEditorColumn(
        x: reclaimedPlayerX,
        chart: chart,
        waveformBlocks: controller.canvasWaveformBlocks,
        rightInset: mobileEditorContentRightInset,
      ),
      OTGY_CHART_COLUMN_PLAYER,
    );
    debugDefaultTargetPlatformOverride = previousPlatform;
  });

  testWidgets('edits through the native chart command and supports history', (
    tester,
  ) async {
    await pumpEditor(tester);
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final chartWidth = canvas.width;
    final cursorY = canvas.top + editorCursorY(canvas.size);
    final npcX = canvas.left + chartWidth / 6;

    await tester.tapAt(Offset(npcX, cursorY));
    await tester.pump();
    expect(controller.markers, hasLength(1));
    expect(controller.markers.single.type, OTGY_CHART_MARKER_NPC_BLOCK_START);
    expect(find.bySemanticsLabel(RegExp('NPC 起点')), findsOneWidget);

    await tester.tap(find.byTooltip('撤销'));
    await tester.pump();
    expect(controller.markers, isEmpty);
    await tester.tap(find.byTooltip('重做'));
    await tester.pump();
    expect(controller.markers, hasLength(1));
  });

  testWidgets('uses responsive toolbar layout without losing core controls', (
    tester,
  ) async {
    await pumpEditor(tester, size: const Size(390, 844));

    expect(find.byKey(const ValueKey('audio-picker')), findsOneWidget);
    expect(find.byKey(const ValueKey('analyze-audio')), findsOneWidget);
    expect(find.byIcon(Icons.troubleshoot_outlined), findsOneWidget);
    expect(find.byKey(const ValueKey('export-audio')), findsOneWidget);
    expect(find.byKey(const ValueKey('save-chart')), findsOneWidget);
    expect(find.byKey(const ValueKey('export-slk')), findsOneWidget);
    expect(find.bySemanticsLabel('谱面编辑画布，NPC、Showtime、玩家三列'), findsOneWidget);
  });

  testWidgets('uses flat file controls without shrinking their control size', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
    try {
      await pumpEditor(tester, size: const Size(390, 844));

      for (final key in const ['audio-picker', 'chart-menu']) {
        final button = tester.widget<TextButton>(find.byKey(ValueKey(key)));
        expect(
          button.style?.side?.resolve(const <WidgetState>{}),
          BorderSide.none,
        );
        expect(
          button.style?.side?.resolve(const <WidgetState>{WidgetState.focused}),
          BorderSide.none,
        );
        expect(
          button.style?.overlayColor?.resolve(const <WidgetState>{
            WidgetState.focused,
          }),
          isNot(Colors.transparent),
        );
        expect(
          button.style?.minimumSize?.resolve(const <WidgetState>{}),
          const Size(48, 48),
        );
        expect(tester.getSize(find.byKey(ValueKey(key))).height, 40);
      }
      for (final key in const ['bpm-menu', 'offset-menu']) {
        final control = find.descendant(
          of: find.byKey(ValueKey(key)),
          matching: find.byType(TextButton),
        );
        final button = tester.widget<TextButton>(control);
        expect(button.style?.side?.resolve(const <WidgetState>{}), isNull);
        expect(
          button.style?.backgroundColor?.resolve(const <WidgetState>{}),
          isNull,
        );
        expect(tester.getSize(find.byKey(ValueKey(key))).height, 40);
      }
      expect(
        find.descendant(
          of: find.byKey(const ValueKey('chart-menu')),
          matching: find.byIcon(Icons.arrow_drop_down),
        ),
        findsNothing,
      );
      expect(
        tester.getRect(find.byKey(const ValueKey('editor-canvas'))).top,
        closeTo(131, 1),
      );
    } finally {
      debugDefaultTargetPlatformOverride = previousPlatform;
    }
  });

  testWidgets('gives mobile timing controls a clear, tappable second row', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    try {
      await pumpEditor(tester, size: const Size(390, 844));

      final audio = tester.getRect(find.byKey(const ValueKey('audio-picker')));
      final analyze = tester.getRect(
        find.byKey(const ValueKey('analyze-audio')),
      );
      final bpm = tester.getRect(find.byKey(const ValueKey('bpm-menu')));
      final offset = tester.getRect(find.byKey(const ValueKey('offset-menu')));
      final export = tester.getRect(find.byKey(const ValueKey('export-audio')));
      final toggle = tester.getRect(
        find.byKey(const ValueKey('toggle-timing-controls')),
      );
      final chart = tester.getRect(find.byKey(const ValueKey('chart-menu')));
      final save = tester.getRect(find.byKey(const ValueKey('save-chart')));
      final clear = tester.getRect(find.byKey(const ValueKey('clear-chart')));
      final exportSlk = tester.getRect(
        find.byKey(const ValueKey('export-slk')),
      );
      final bpmLabel = tester.getRect(find.text('BPM'));
      final bpmValue = tester.getRect(find.text('100.000'));
      final offsetLabel = tester.getRect(find.text('Offset'));
      final offsetValue = tester.getRect(find.text('0.000'));

      expect(audio.width, greaterThan(180));
      expect(bpm.top, greaterThan(audio.bottom));
      expect(offset.center.dy, closeTo(bpm.center.dy, 1));
      expect(export.center.dy, closeTo(toggle.center.dy, 1));
      expect(export.left, greaterThan(toggle.right - 1));
      expect(bpm.width, greaterThan(120));
      expect(offset.width, greaterThan(120));
      expect(bpmLabel.center.dy, closeTo(bpmValue.center.dy, 1));
      expect(offsetLabel.center.dy, closeTo(offsetValue.center.dy, 1));
      expect(audio.right, closeTo(chart.right, 0.01));
      expect(analyze.center.dx, closeTo(save.center.dx, 0.01));
      expect(toggle.center.dx, closeTo(clear.center.dx, 0.01));
      expect(export.center.dx, closeTo(exportSlk.center.dx, 0.01));
      expect(
        tester
            .widget<IconButton>(find.byKey(const ValueKey('save-chart')))
            .tooltip,
        '保存',
      );
      expect(
        find.descendant(
          of: find.byKey(const ValueKey('save-chart')),
          matching: find.text('保存'),
        ),
        findsNothing,
      );

      await tester.tap(find.byKey(const ValueKey('toggle-timing-controls')));
      await tester.pump();
      expect(find.byKey(const ValueKey('bpm-menu')), findsNothing);
      expect(find.byTooltip('展开 BPM 与 Offset'), findsOneWidget);

      await tester.tap(find.byKey(const ValueKey('toggle-timing-controls')));
      await tester.pump();
      expect(find.byKey(const ValueKey('bpm-menu')), findsOneWidget);
      expect(find.byTooltip('收起 BPM 与 Offset'), findsOneWidget);
    } finally {
      debugDefaultTargetPlatformOverride = previousPlatform;
    }
  });

  testWidgets('keeps macOS compact windows on the desktop toolbar layout', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await pumpEditor(tester, size: const Size(390, 844));

      final audio = tester.getRect(find.byKey(const ValueKey('audio-picker')));
      final bpm = tester.getRect(find.byKey(const ValueKey('bpm-menu')));
      final offset = tester.getRect(find.byKey(const ValueKey('offset-menu')));
      final bpmLabel = tester.getRect(find.text('BPM'));
      final bpmValue = tester.getRect(find.text('100.000'));
      expect(bpm.top, greaterThan(audio.bottom));
      expect(offset.center.dy, closeTo(bpm.center.dy, 1));
      expect(bpmLabel.center.dy, closeTo(bpmValue.center.dy, 1));
    } finally {
      debugDefaultTargetPlatformOverride = previousPlatform;
    }
  });

  testWidgets('keeps the desktop audio toolbar on one row', (tester) async {
    await pumpEditor(tester, size: const Size(1000, 700));

    final audio = tester.getRect(find.byKey(const ValueKey('audio-picker')));
    final analyze = tester.getRect(find.byKey(const ValueKey('analyze-audio')));
    final bpm = tester.getRect(find.byKey(const ValueKey('bpm-menu')));
    final offset = tester.getRect(find.byKey(const ValueKey('offset-menu')));
    final bpmLabel = tester.getRect(find.text('BPM'));
    final bpmValue = tester.getRect(find.text('100.000'));
    final offsetLabel = tester.getRect(find.text('Offset'));
    final offsetValue = tester.getRect(find.text('0.000'));

    expect(audio.width, lessThan(400));
    expect(bpm.top, closeTo(audio.top, 1));
    expect(offset.top, closeTo(audio.top, 1));
    expect(bpm.width, 176);
    expect(offset.width, 176);
    expect(bpm.left, greaterThan(analyze.right));
    expect(bpmLabel.center.dy, closeTo(bpmValue.center.dy, 1));
    expect(offsetLabel.center.dy, closeTo(offsetValue.center.dy, 1));
  });

  testWidgets('uses flat icon save controls on desktop', (tester) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    addTearDown(() => debugDefaultTargetPlatformOverride = previousPlatform);
    await pumpEditor(tester, size: const Size(1000, 700));

    final initialSave = tester.widget<IconButton>(
      find.byKey(const ValueKey('save-chart')),
    );
    final initialClearRect = tester.getRect(
      find.byKey(const ValueKey('clear-chart')),
    );
    final initialExportRect = tester.getRect(
      find.byKey(const ValueKey('export-slk')),
    );
    expect(initialSave.tooltip, '保存');
    expect(tester.getSize(find.byKey(const ValueKey('save-chart'))).width, 48);

    final payload = controller.encodeFormalDraft();
    controller.markSaved(
      draftId: 'desktop-style',
      draftName: 'desktop-style',
      payload: payload,
    );
    await tester.pump();

    expect(
      tester
          .widget<IconButton>(find.byKey(const ValueKey('save-chart')))
          .tooltip,
      '保存',
    );
    expect(
      tester
          .widget<IconButton>(find.byKey(const ValueKey('save-options')))
          .tooltip,
      '保存选项',
    );
    expect(tester.getSize(find.byKey(const ValueKey('save-chart'))).width, 48);
    expect(
      tester.getSize(find.byKey(const ValueKey('save-options'))).width,
      48,
    );
    expect(
      tester.getRect(find.byKey(const ValueKey('clear-chart'))).left,
      closeTo(initialClearRect.left, 0.01),
    );
    expect(
      tester.getRect(find.byKey(const ValueKey('export-slk'))).left,
      closeTo(initialExportRect.left, 0.01),
    );
    debugDefaultTargetPlatformOverride = previousPlatform;
  });

  testWidgets('keeps the audio extension visible in a compact toolbar', (
    tester,
  ) async {
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    const name = 'a_very_long_audio_file_name_that_cannot_fit.mp3';
    await controller.loadAudioBytes(audio, name);

    await pumpEditor(tester, size: const Size(390, 844));

    expect(find.text('.mp3'), findsOneWidget);
    expect(find.byTooltip(name), findsOneWidget);
  });

  testWidgets(
    'gates empty chart actions and rejects blank draft names safely',
    (tester) async {
      final previousPlatform = debugDefaultTargetPlatformOverride;
      debugDefaultTargetPlatformOverride = TargetPlatform.android;
      try {
        await pumpEditor(tester, size: const Size(390, 844));
        final initialSaveRect = tester.getRect(
          find.byKey(const ValueKey('save-chart')),
        );
        final initialClearRect = tester.getRect(
          find.byKey(const ValueKey('clear-chart')),
        );
        final initialExportRect = tester.getRect(
          find.byKey(const ValueKey('export-slk')),
        );
        expect(initialSaveRect.width, 40);
        expect(find.text('点击导入音频'), findsOneWidget);
        expect(find.text('点击导入slk'), findsOneWidget);
        expect(
          tester
              .widget<IconButton>(find.byKey(const ValueKey('save-chart')))
              .onPressed,
          isNull,
        );
        expect(
          tester
              .widget<IconButton>(find.byKey(const ValueKey('clear-chart')))
              .onPressed,
          isNull,
        );
        expect(
          tester
              .widget<TextButton>(find.byKey(const ValueKey('chart-menu')))
              .onPressed,
          isNotNull,
        );

        final canvas = tester.getRect(
          find.byKey(const ValueKey('editor-canvas')),
        );
        await tester.tapAt(
          Offset(
            canvas.left + canvas.width / 6,
            canvas.top + editorCursorY(canvas.size),
          ),
        );
        await tester.pump();
        expect(controller.markers, hasLength(1));
        expect(find.textContaining('未命名草稿'), findsOneWidget);

        for (var attempt = 0; attempt < 10; attempt++) {
          final save = tester.widget<IconButton>(
            find.byKey(const ValueKey('save-chart')),
          );
          expect(save.onPressed, isNotNull);
          save.onPressed!();
          await tester.pumpAndSettle();
          if (find.text('保存草稿').evaluate().isNotEmpty) {
            break;
          }
          await tester.runAsync(
            () => Future<void>.delayed(const Duration(milliseconds: 10)),
          );
          await tester.pump();
        }
        expect(find.text('保存草稿'), findsOneWidget);

        await tester.tap(find.widgetWithText(FilledButton, '保存'));
        await tester.pumpAndSettle();

        expect(tester.takeException(), isNull);
        expect(find.text('草稿名不能为空'), findsOneWidget);

        tester
            .widget<IconButton>(find.byKey(const ValueKey('save-chart')))
            .onPressed!();
        await tester.pumpAndSettle();
        await tester.enterText(find.byType(TextFormField), 'chart-with-marker');
        await tester.tap(find.widgetWithText(FilledButton, '保存'));
        await tester.pump();
        for (var attempt = 0; attempt < 50; attempt++) {
          if (controller.sourceKind == WorkspaceSourceKind.savedDraft) {
            break;
          }
          await tester.runAsync(
            () => Future<void>.delayed(const Duration(milliseconds: 10)),
          );
          await tester.pump();
        }
        expect(controller.hasAudio, isFalse);
        expect(controller.chartName, 'chart-with-marker');
        expect(controller.sourceKind, WorkspaceSourceKind.savedDraft);
        await tester.pumpAndSettle();
        final savedSaveRect = tester.getRect(
          find.byKey(const ValueKey('save-chart')),
        );
        final saveOptionsRect = tester.getRect(find.byTooltip('保存选项'));
        final savedClearRect = tester.getRect(
          find.byKey(const ValueKey('clear-chart')),
        );
        final savedExportRect = tester.getRect(
          find.byKey(const ValueKey('export-slk')),
        );
        expect(savedSaveRect.width, 40);
        expect(saveOptionsRect.width, 40);
        expect(saveOptionsRect.left, closeTo(savedSaveRect.right, 0.01));
        expect(savedClearRect.left, closeTo(initialClearRect.left, 0.01));
        expect(savedExportRect.left, closeTo(initialExportRect.left, 0.01));

        await tester.tap(find.byKey(const ValueKey('clear-chart')));
        await tester.pumpAndSettle();
        await tester.tap(find.widgetWithText(FilledButton, '清空'));
        await tester.pumpAndSettle();
        expect(controller.markers, isEmpty);
        expect(find.textContaining('chart-with-marker'), findsOneWidget);
        expect(
          tester
              .widget<IconButton>(find.byKey(const ValueKey('save-chart')))
              .onPressed,
          isNull,
        );
        expect(
          tester
              .widget<IconButton>(find.byKey(const ValueKey('clear-chart')))
              .onPressed,
          isNull,
        );
        expect(
          tester
              .widget<IconButton>(find.byKey(const ValueKey('save-options')))
              .onPressed,
          isNull,
        );
        expect(tester.takeException(), isNull);
      } finally {
        debugDefaultTargetPlatformOverride = previousPlatform;
      }
    },
  );

  testWidgets('successful audio replacement restores automatic edit zoom', (
    tester,
  ) async {
    await controller.setZoom(8.25);
    controller.setBpm(180);
    expect(controller.zoom, 8.25);
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();

    await controller.loadAudioBytes(audio, 'fixture.mp3');

    expect(controller.bpm, 100);
    expect(controller.zoom, 9.5);
    expect(controller.rowSpacing, closeTo(targetEditorRowSpacing, 2));
  });

  testWidgets('reports audio picker platform failures', (tester) async {
    const channel = MethodChannel('plugins.flutter.io/file_selector');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          expect(call.method, 'openFile');
          throw PlatformException(code: 'unavailable');
        });
    addTearDown(
      () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null),
    );
    await pumpEditor(tester);

    await tester.tap(find.byKey(const ValueKey('audio-picker')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 250));

    expect(find.text('无法打开音频文件选择器。'), findsOneWidget);
  });

  testWidgets(
    'iOS audio picker keeps Ogg selectable and validates extensions',
    (tester) async {
      final previousPlatform = debugDefaultTargetPlatformOverride;
      debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
      const channel = MethodChannel('plugins.flutter.io/file_selector');
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, (call) async {
            expect(call.method, 'openFile');
            final arguments = call.arguments as Map<Object?, Object?>;
            final groups = arguments['acceptedTypeGroups'] as List<Object?>;
            final audioGroup = groups.single as Map<Object?, Object?>;
            expect(audioGroup['extensions'], ['mp3', 'ogg']);
            expect(audioGroup['uniformTypeIdentifiers'], ['public.data']);
            return ['/does/not/need/to/exist.wav'];
          });
      addTearDown(
        () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
            .setMockMethodCallHandler(channel, null),
      );
      try {
        await pumpEditor(tester);

        await tester.tap(find.byKey(const ValueKey('audio-picker')));
        await tester.pumpAndSettle();

        expect(find.text('音频加载失败：请选择 .mp3 或 .ogg 文件。'), findsOneWidget);
        expect(controller.hasAudio, isFalse);
      } finally {
        debugDefaultTargetPlatformOverride = previousPlatform;
      }
    },
  );

  testWidgets('reports SLK picker failures with copyable diagnostics', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
    const channel = MethodChannel('plugins.flutter.io/file_selector');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          expect(call.method, 'openFile');
          final arguments = call.arguments as Map<Object?, Object?>;
          final groups = arguments['acceptedTypeGroups'] as List<Object?>;
          final slkGroup = groups.single as Map<Object?, Object?>;
          expect(slkGroup['extensions'], ['slk']);
          expect(slkGroup['uniformTypeIdentifiers'], ['public.data']);
          throw PlatformException(
            code: 'unavailable',
            message: 'Document picker unavailable',
          );
        });
    addTearDown(
      () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null),
    );
    await pumpEditor(tester);

    await tester.tap(find.byKey(const ValueKey('chart-menu')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('导入 SLK'));
    await tester.pumpAndSettle();

    expect(find.text('SLK 导入失败'), findsOneWidget);
    final report = tester.widget<SelectableText>(
      find.byKey(const ValueKey('slk-import-file-picker-error-diagnostic')),
    );
    expect(report.data, contains('operation: slk-import-file-picker'));
    expect(report.data, contains('platformCode: unavailable'));
    expect(
      find.byKey(const ValueKey('copy-slk-import-file-picker-error')),
      findsOneWidget,
    );
    debugDefaultTargetPlatformOverride = previousPlatform;
  });

  testWidgets('rejects a non-SLK file returned by the iOS data picker', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
    const channel = MethodChannel('plugins.flutter.io/file_selector');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          expect(call.method, 'openFile');
          return ['/does/not/need/to/exist.txt'];
        });
    addTearDown(
      () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null),
    );
    await pumpEditor(tester);

    await tester.tap(find.byKey(const ValueKey('chart-menu')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('导入 SLK'));
    await tester.pumpAndSettle();

    expect(find.text('SLK 导入失败：请选择 .slk 文件。'), findsOneWidget);
    expect(controller.sourceKind, WorkspaceSourceKind.untitled);
    debugDefaultTargetPlatformOverride = previousPlatform;
  });

  testWidgets('audio export failure diagnostics can be copied', (tester) async {
    String? copiedText;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(SystemChannels.platform, (call) async {
          if (call.method == 'Clipboard.setData') {
            copiedText =
                (call.arguments as Map<Object?, Object?>)['text'] as String;
          }
          return null;
        });
    addTearDown(
      () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(SystemChannels.platform, null),
    );
    const diagnostic =
        'operation: audio-export\n'
        'exceptionType: MissingPluginException\n'
        'exception: File publisher is not registered.\n'
        'stackTrace:\n#0 test';

    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(body: AudioExportErrorDialog(diagnostic: diagnostic)),
      ),
    );
    expect(find.text('音频导出失败'), findsOneWidget);
    final report = tester.widget<SelectableText>(
      find.byKey(const ValueKey('audio-export-error-diagnostic')),
    );
    expect(report.data, contains('MissingPluginException'));

    await tester.tap(find.byKey(const ValueKey('copy-audio-export-error')));
    await tester.pump();
    expect(copiedText, diagnostic);
    expect(find.text('已复制'), findsOneWidget);
  });

  testWidgets('audio export settings close without a framework error', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    addTearDown(() => debugDefaultTargetPlatformOverride = previousPlatform);
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    await pumpEditor(tester, size: const Size(390, 844));

    final export = tester.widget<IconButton>(
      find.byKey(const ValueKey('export-audio')),
    );
    expect(export.onPressed, isNotNull);
    export.onPressed!();
    await tester.pumpAndSettle();
    expect(find.text('导出 Ogg 音频'), findsOneWidget);

    await tester.tap(find.widgetWithText(TextButton, '取消'));
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    debugDefaultTargetPlatformOverride = previousPlatform;
  });

  testWidgets('BPM long press previews and commits once on release', (
    tester,
  ) async {
    await pumpEditor(tester);
    await tester.tap(find.byKey(const ValueKey('bpm-menu')));
    await tester.pumpAndSettle();

    final increase = find.byTooltip('BPM 增加 0.001');
    final gesture = await tester.startGesture(tester.getCenter(increase));
    await tester.pump(const Duration(milliseconds: 700));
    expect(controller.bpm, 100);

    await gesture.up();
    await tester.pump();
    expect(controller.bpm, greaterThan(100));
  });

  testWidgets('BPM keeps complete local input and commits after editing', (
    tester,
  ) async {
    await pumpEditor(tester);
    await tester.tap(find.byKey(const ValueKey('bpm-menu')));
    await tester.pumpAndSettle();

    final input = find.byKey(const ValueKey('bpm-input'));
    expect(input, findsOneWidget);
    var controllerNotifications = 0;
    void countNotification() => controllerNotifications++;
    controller.addListener(countNotification);
    addTearDown(() => controller.removeListener(countNotification));

    for (final text in ['1', '12', '123']) {
      tester.testTextInput.updateEditingValue(
        TextEditingValue(
          text: text,
          selection: TextSelection.collapsed(offset: text.length),
        ),
      );
      await tester.pump();
      expect(tester.widget<TextField>(input).controller!.text, text);
      expect(controller.bpm, 100);
      expect(controllerNotifications, 0);
    }

    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pump();

    expect(controller.bpm, 123);
    expect(controllerNotifications, 1);
    expect(tester.widget<TextField>(input).controller!.text, '123.000');
    expect(find.text('123.000'), findsNWidgets(2));

    await tester.tap(input);
    tester.testTextInput.updateEditingValue(
      const TextEditingValue(
        text: '124',
        selection: TextSelection.collapsed(offset: 3),
      ),
    );
    await tester.pump();
    tester.binding.focusManager.primaryFocus?.unfocus();
    await tester.pump();

    expect(controller.bpm, 124);
    expect(controllerNotifications, 2);
    expect(tester.widget<TextField>(input).controller!.text, '124.000');
  });

  testWidgets('BPM restores the authoritative value after invalid input', (
    tester,
  ) async {
    await pumpEditor(tester);
    await tester.tap(find.byKey(const ValueKey('bpm-menu')));
    await tester.pumpAndSettle();

    final input = find.byKey(const ValueKey('bpm-input'));
    for (final text in ['100001', '']) {
      await tester.tap(input);
      tester.testTextInput.updateEditingValue(
        TextEditingValue(
          text: text,
          selection: TextSelection.collapsed(offset: text.length),
        ),
      );
      await tester.pump();
      expect(controller.bpm, 100);

      await tester.testTextInput.receiveAction(TextInputAction.done);
      await tester.pump();
      expect(controller.bpm, 100);
      expect(tester.widget<TextField>(input).controller!.text, '100.000');
    }
  });

  testWidgets('Offset keeps complete local input and commits after editing', (
    tester,
  ) async {
    await pumpEditor(tester);
    await tester.tap(find.byKey(const ValueKey('offset-menu')));
    await tester.pumpAndSettle();

    final input = find.byKey(const ValueKey('offset-input'));
    expect(input, findsOneWidget);
    var controllerNotifications = 0;
    void countNotification() => controllerNotifications++;
    controller.addListener(countNotification);
    addTearDown(() => controller.removeListener(countNotification));

    for (final text in ['1', '12', '12.', '12.3', '12.34', '12.345']) {
      tester.testTextInput.updateEditingValue(
        TextEditingValue(
          text: text,
          selection: TextSelection.collapsed(offset: text.length),
        ),
      );
      await tester.pump();
      expect(tester.widget<TextField>(input).controller!.text, text);
      expect(controller.offset, 0);
      expect(controllerNotifications, 0);
    }

    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pump();

    expect(controller.offset, 12.345);
    expect(controllerNotifications, 1);
    expect(tester.widget<TextField>(input).controller!.text, '12.345');

    await tester.tap(input);
    tester.testTextInput.updateEditingValue(
      const TextEditingValue(
        text: '31',
        selection: TextSelection.collapsed(offset: 2),
      ),
    );
    await tester.pump();
    expect(controller.offset, 12.345);

    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pump();
    expect(controller.offset, 12.345);
    expect(controllerNotifications, 1);
    expect(tester.widget<TextField>(input).controller!.text, '12.345');
  });

  testWidgets('dragging the canvas does not send a tap edit command', (
    tester,
  ) async {
    await pumpEditor(tester);
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final start = Offset(canvas.center.dx, canvas.center.dy);

    await tester.dragFrom(start, const Offset(0, 80));
    await tester.pump();

    expect(controller.markers, isEmpty);
  });

  testWidgets('canvas omits internal column divider strokes', (tester) async {
    await pumpEditor(tester, size: const Size(999, 700));
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(const ValueKey('editor-canvas-repaint')),
    );
    final image = await tester.runAsync(() => boundary.toImage(pixelRatio: 1));
    expect(image, isNotNull);
    final captured = image!;
    addTearDown(captured.dispose);
    final bytes = await tester.runAsync(
      () => captured.toByteData(format: ui.ImageByteFormat.rawRgba),
    );
    expect(bytes, isNotNull);

    List<int> pixel(int x, int y) {
      final offset = (y * captured.width + x) * 4;
      return List.generate(4, (channel) => bytes!.getUint8(offset + channel));
    }

    final chartWidth = captured.width;
    final columnWidth = chartWidth / 3;
    for (final divider in [columnWidth.round(), (columnWidth * 2).round()]) {
      expect(pixel(divider, 40), pixel(divider + 3, 40));
    }
  });

  testWidgets('timing grid spans the full canvas width', (tester) async {
    await pumpEditor(tester);
    await controller.setZoom(5);
    controller.moveToRow(11);
    await tester.pump();

    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(const ValueKey('editor-canvas-repaint')),
    );
    final image = await tester.runAsync(() => boundary.toImage(pixelRatio: 1));
    expect(image, isNotNull);
    final captured = image!;
    addTearDown(captured.dispose);
    final bytes = await tester.runAsync(
      () => captured.toByteData(format: ui.ImageByteFormat.rawRgba),
    );
    expect(bytes, isNotNull);

    int redAt(int x, int y) {
      final offset = (y * captured.width + x) * 4;
      return bytes!.getUint8(offset);
    }

    final cursorY = editorCursorY(
      Size(captured.width.toDouble(), captured.height.toDouble()),
    );
    final y = (cursorY + (16 - controller.currentRow) * controller.rowSpacing)
        .round();
    expect(redAt(1, y), greaterThan(redAt(1, y + 4)));
    expect(
      redAt(captured.width - 2, y),
      greaterThan(redAt(captured.width - 2, y + 4)),
    );
  });

  testWidgets('measure labels sit above their grid lines', (tester) async {
    await pumpEditor(tester);
    await controller.setZoom(10);
    controller.moveToRow(16);
    await tester.pump();

    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(const ValueKey('editor-canvas-repaint')),
    );
    final image = await tester.runAsync(() => boundary.toImage(pixelRatio: 1));
    expect(image, isNotNull);
    final captured = image!;
    addTearDown(captured.dispose);
    final bytes = await tester.runAsync(
      () => captured.toByteData(format: ui.ImageByteFormat.rawRgba),
    );
    expect(bytes, isNotNull);

    List<int> pixel(int x, int y) {
      final offset = (y * captured.width + x) * 4;
      return List.generate(4, (channel) => bytes!.getUint8(offset + channel));
    }

    final lineY = editorCursorY(
      Size(captured.width.toDouble(), captured.height.toDouble()),
    ).round();
    final background = pixel(24, lineY - 20);
    var inkAbove = 0;
    var inkBelow = 0;
    for (var x = 4; x < 24; x++) {
      for (var y = lineY - 10; y <= lineY - 3; y++) {
        if (!listEquals(pixel(x, y), background)) {
          inkAbove++;
        }
      }
      for (var y = lineY + 3; y <= lineY + 10; y++) {
        if (!listEquals(pixel(x, y), background)) {
          inkBelow++;
        }
      }
    }
    expect(inkAbove, greaterThan(0));
    expect(inkBelow, 0);
  });

  testWidgets('audio and SLK export buttons share the export icon', (
    tester,
  ) async {
    await pumpEditor(tester);
    final audio = tester.widget<IconButton>(
      find.byKey(const ValueKey('export-audio')),
    );
    final slk = tester.widget<IconButton>(
      find.byKey(const ValueKey('export-slk')),
    );
    expect((audio.icon as Icon).icon, (slk.icon as Icon).icon);
  });

  testWidgets('measure and beat lines keep a strong visual hierarchy', (
    tester,
  ) async {
    await pumpEditor(tester);
    await controller.setZoom(5);
    controller.moveToRow(11);
    await tester.pump();
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(const ValueKey('editor-canvas-repaint')),
    );
    final image = await tester.runAsync(() => boundary.toImage(pixelRatio: 1));
    expect(image, isNotNull);
    final captured = image!;
    addTearDown(captured.dispose);
    final bytes = await tester.runAsync(
      () => captured.toByteData(format: ui.ImageByteFormat.rawRgba),
    );
    expect(bytes, isNotNull);

    final cursorY = editorCursorY(
      Size(captured.width.toDouble(), captured.height.toDouble()),
    );
    final spacing = controller.rowSpacing;
    int strongestRedNear(int row) {
      final center = cursorY + (row - controller.currentRow) * spacing;
      var red = 0;
      for (var y = center.floor() - 2; y <= center.ceil() + 2; y++) {
        final offset = (y * captured.width + 90) * 4;
        red = math.max(red, bytes!.getUint8(offset));
      }
      return red;
    }

    final measure = strongestRedNear(16);
    final beat = strongestRedNear(12);
    final subdivision = strongestRedNear(13);
    expect(measure, greaterThan(beat));
    expect(beat, greaterThan(subdivision));
  });

  testWidgets(
    'playback keeps measure lines and restores the full grid when paused',
    (tester) async {
      controller.dispose();
      controller = EditorController(playbackOutput: _FakePlaybackOutput());
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');
      await pumpEditor(tester);

      controller.toggleWaveform();
      await controller.setZoom(5);
      controller.moveToRow(0);
      await tester.pump();

      Future<List<int>> sampleGrid() async {
        final boundary = tester.renderObject<RenderRepaintBoundary>(
          find.byKey(const ValueKey('editor-canvas-repaint')),
        );
        final image = await tester.runAsync(
          () => boundary.toImage(pixelRatio: 1),
        );
        expect(image, isNotNull);
        final captured = image!;
        final bytes = await tester.runAsync(
          () => captured.toByteData(format: ui.ImageByteFormat.rawRgba),
        );
        expect(bytes, isNotNull);
        final cursorY = editorCursorY(
          Size(captured.width.toDouble(), captured.height.toDouble()),
        );
        final spacing = controller.rowSpacing;
        expect(spacing, greaterThan(4));

        int strongestRedNear(int row) {
          final center = cursorY + (row - controller.currentRow) * spacing;
          var red = 0;
          for (var y = center.floor() - 2; y <= center.ceil() + 2; y++) {
            final offset = (y * captured.width + 90) * 4;
            red = math.max(red, bytes!.getUint8(offset));
          }
          return red;
        }

        final samples = [
          strongestRedNear(16),
          strongestRedNear(4),
          strongestRedNear(5),
        ];
        captured.dispose();
        return samples;
      }

      final pausedGrid = await sampleGrid();
      expect(pausedGrid[0], greaterThan(pausedGrid[1]));
      expect(pausedGrid[1], greaterThan(pausedGrid[2]));

      await controller.togglePlayback();
      await tester.pump();
      expect(controller.isPlaying, isTrue);
      final playingGrid = await sampleGrid();
      expect(playingGrid[0], greaterThan(playingGrid[1]));
      expect(playingGrid[1], playingGrid[2]);
      expect(playingGrid[1], lessThan(pausedGrid[1]));
      expect(playingGrid[2], lessThan(pausedGrid[2]));

      await controller.pausePlayback();
      await tester.pump();
      expect(controller.isPlaying, isFalse);
      final resumedGrid = await sampleGrid();
      expect(resumedGrid, pausedGrid);
    },
  );

  testWidgets('last SHOW-D half-measure is lighter than its regular range', (
    tester,
  ) async {
    await pumpEditor(tester);
    controller.clickCell(0, 0, mobile: false);
    controller.clickCell(0, 1, mobile: false);
    controller.clickCell(0, 2, mobile: false);
    controller.clickCell(0, 2, mobile: false);
    controller.clickCell(1, 3, mobile: false);
    controller.clickCell(1, 4, mobile: false);
    controller.clickCell(2, 6, mobile: false);
    expect(controller.showtimeMoves, hasLength(1));
    final move = controller.showtimeMoves.single;
    expect(move.isLast, isTrue);

    await controller.setZoom(5);
    controller.moveToRow(20);
    await tester.pump();
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(const ValueKey('editor-canvas-repaint')),
    );
    final image = await tester.runAsync(() => boundary.toImage(pixelRatio: 1));
    expect(image, isNotNull);
    final captured = image!;
    addTearDown(captured.dispose);
    final bytes = await tester.runAsync(
      () => captured.toByteData(format: ui.ImageByteFormat.rawRgba),
    );
    expect(bytes, isNotNull);

    final cursorY = editorCursorY(
      Size(captured.width.toDouble(), captured.height.toDouble()),
    );
    final spacing = controller.rowSpacing;
    int redAtRow(double row) {
      final y = (cursorY + (row - controller.currentRow) * spacing).round();
      final offset = (y * captured.width + 90) * 4;
      return bytes!.getUint8(offset);
    }

    final regularRed = redAtRow(move.rangeEnd - 11.5);
    final finalHalfRed = redAtRow(move.rangeEnd - 3.5);
    expect(regularRed, greaterThan(finalHalfRed));
  });

  testWidgets(
    'macOS trackpad pans the vertical timeline and ignores horizontal pan',
    (tester) async {
      debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
      try {
        await pumpEditor(tester);
        final canvas = tester.getRect(
          find.byKey(const ValueKey('editor-canvas')),
        );

        final vertical = await tester.createGesture(
          kind: PointerDeviceKind.trackpad,
        );
        await vertical.panZoomStart(canvas.center);
        await vertical.panZoomUpdate(canvas.center, pan: const Offset(0, -80));
        await tester.pump();
        expect(controller.currentRow, greaterThan(0));
        await vertical.panZoomEnd();

        final afterVerticalPan = controller.currentRow;
        final horizontal = await tester.createGesture(
          kind: PointerDeviceKind.trackpad,
        );
        await horizontal.panZoomStart(canvas.center);
        await horizontal.panZoomUpdate(
          canvas.center,
          pan: const Offset(120, -4),
        );
        await tester.pump();
        expect(controller.currentRow, afterVerticalPan);
        await horizontal.panZoomEnd();

        final horizontalScroll = TestPointer(999, PointerDeviceKind.trackpad);
        await tester.sendEventToBinding(horizontalScroll.hover(canvas.center));
        await tester.sendEventToBinding(
          horizontalScroll.scroll(const Offset(120, -4)),
        );
        await tester.pump();
        expect(controller.currentRow, afterVerticalPan);

        final zoomBeforeRotation = controller.zoom;
        final scaleBeforeRotation = controller.scale;
        final rotation = await tester.createGesture(
          kind: PointerDeviceKind.trackpad,
        );
        await rotation.panZoomStart(canvas.center);
        await rotation.panZoomUpdate(canvas.center, rotation: 0.5);
        await tester.pump();
        expect(controller.currentRow, afterVerticalPan);
        expect(controller.zoom, zoomBeforeRotation);
        expect(controller.scale, scaleBeforeRotation);
        await rotation.panZoomEnd();
      } finally {
        debugDefaultTargetPlatformOverride = null;
      }
    },
  );

  testWidgets(
    'macOS trackpad pinch previews continuously and commits quarter-step Zoom',
    (tester) async {
      debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
      try {
        await pumpEditor(tester);
        final canvasFinder = find.byKey(const ValueKey('editor-canvas'));
        final canvas = tester.getRect(canvasFinder);
        final paintFinder = find.byKey(const ValueKey('editor-canvas-paint'));
        final beforePainter = tester.widget<CustomPaint>(paintFinder).painter!;
        final beforeSpacing = (beforePainter as dynamic).rowSpacing as double;
        final zoomBefore = controller.zoom;
        final scaleBefore = controller.scale;

        final pinch = await tester.createGesture(
          kind: PointerDeviceKind.trackpad,
        );
        await pinch.panZoomStart(canvas.center);
        await pinch.panZoomUpdate(canvas.center, scale: 1.5);
        await tester.pump();

        expect(controller.zoom, zoomBefore);
        expect(controller.scale, scaleBefore);
        final previewPainter = tester.widget<CustomPaint>(paintFinder).painter!;
        final previewSpacing = (previewPainter as dynamic).rowSpacing as double;
        expect(previewSpacing, greaterThan(beforeSpacing));

        await pinch.panZoomEnd();
        await tester.pump();
        final expectedZoom =
            ((zoomBefore + math.log(1.5) / math.ln2 * 2) * 4).round() / 4;
        expect(controller.zoom, expectedZoom);
        expect(controller.zoom * 4, (controller.zoom * 4).roundToDouble());
        expect(controller.scale, scaleBefore);
        expect(controller.currentRow, 0);
      } finally {
        debugDefaultTargetPlatformOverride = null;
      }
    },
  );

  testWidgets('macOS Command shortcuts adjust and reset Zoom', (tester) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await pumpEditor(tester);
      final initialZoom = controller.zoom;

      await tester.sendKeyDownEvent(LogicalKeyboardKey.metaLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.equal);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.metaLeft);
      await tester.pump();
      expect(controller.zoom, initialZoom + 0.25);

      await tester.sendKeyDownEvent(LogicalKeyboardKey.metaLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.minus);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.metaLeft);
      await tester.pump();
      expect(controller.zoom, initialZoom);

      await controller.setZoom(12);
      await tester.pump();
      await tester.sendKeyDownEvent(LogicalKeyboardKey.metaLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.digit0);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.metaLeft);
      await tester.pump();
      expect(controller.zoom, 10);
    } finally {
      debugDefaultTargetPlatformOverride = null;
    }
  });

  testWidgets('disables backward navigation at the timeline beginning', (
    tester,
  ) async {
    await pumpEditor(tester);

    final toBeginning = tester.widget<IconButton>(
      find.widgetWithIcon(IconButton, Icons.vertical_align_top),
    );
    final pageUp = tester.widget<IconButton>(
      find.widgetWithIcon(IconButton, Icons.keyboard_arrow_up),
    );
    expect(toBeginning.onPressed, isNull);
    expect(pageUp.onPressed, isNull);
  });

  testWidgets('floating controls stay clickable through canvas rebuilds', (
    tester,
  ) async {
    await pumpEditor(tester);
    final waveform = find.byKey(const ValueKey('waveform-toggle-button'));
    final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
    addTearDown(mouse.removePointer);
    await mouse.addPointer(location: tester.getCenter(waveform));
    await tester.pump();

    for (var row = 1; row <= 5; row++) {
      controller.moveToRow(row.toDouble());
      await tester.pump();
      expect(waveform, findsOneWidget);
    }

    expect(controller.waveformVisible, isTrue);
    await mouse.down(tester.getCenter(waveform));
    await mouse.up();
    await tester.pumpAndSettle();

    expect(controller.waveformVisible, isTrue);
    expect(
      find.byKey(const ValueKey('waveform-settings-menu')),
      findsOneWidget,
    );
    await tester.tap(find.widgetWithIcon(IconButton, Icons.undo));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('waveform-settings-menu')), findsNothing);
  });

  testWidgets(
    'configures supported waveform options in an outside-dismiss menu',
    (tester) async {
      await pumpEditor(tester);

      expect(controller.waveformFilter, WaveformFilter.none);
      expect(controller.waveformOverlayOriginal, isTrue);

      await tester.tap(find.byKey(const ValueKey('waveform-toggle-button')));
      await tester.pumpAndSettle();
      expect(
        find.byKey(const ValueKey('waveform-settings-menu')),
        findsOneWidget,
      );
      expect(find.byKey(const ValueKey('waveform-shape')), findsNothing);
      expect(find.byKey(const ValueKey('waveform-luminance')), findsNothing);
      expect(find.byKey(const ValueKey('waveform-antialiasing')), findsNothing);

      await tester.tap(find.text('高通'));
      await tester.pumpAndSettle();

      expect(controller.waveformFilter, WaveformFilter.highPass);
      expect(controller.waveformFilterEnabled, isTrue);

      final slider = tester.widget<Slider>(
        find.byKey(const ValueKey('waveform-filter-strength')),
      );
      expect(slider.onChanged, isNotNull);
      await tester.tap(find.byKey(const ValueKey('waveform-overlay-original')));
      await tester.pump();
      expect(controller.waveformOverlayOriginal, isFalse);

      await tester.tap(find.text('关闭'));
      await tester.pumpAndSettle();
      expect(controller.waveformFilter, WaveformFilter.none);
      final disabledSlider = tester.widget<Slider>(
        find.byKey(const ValueKey('waveform-filter-strength')),
      );
      expect(disabledSlider.onChanged, isNull);

      await tester.tapAt(const Offset(20, 400));
      await tester.pumpAndSettle();
      expect(
        find.byKey(const ValueKey('waveform-settings-menu')),
        findsNothing,
      );
    },
  );

  test('generates a filtered waveform projection through the generic FFI entry point', () async {
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    expect(controller.waveformFilterEnabled, isFalse);
    final original = controller.waveformBlocks.first.leftAlpha;
    await controller.setWaveformFilter(WaveformFilter.highPass);
    expect(controller.waveformFilterEnabled, isTrue);
    expect(controller.canvasFilteredWaveformBlocks, isNotEmpty);
    expect(
      controller.canvasFilteredWaveformBlocks.first.leftAlpha,
      isNot(equals(original)),
    );
  });

  testWidgets('centers desktop navigation rail in the editor canvas', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await pumpEditor(tester);

      final canvas = tester.getRect(
        find.byKey(const ValueKey('editor-canvas')),
      );
      final rail = tester.getRect(
        find.byKey(const ValueKey('navigation-control-surface')),
      );

      expect(rail.center.dy, closeTo(canvas.center.dy, 0.01));
      expect(rail.right, closeTo(canvas.right - 8, 0.01));
    } finally {
      debugDefaultTargetPlatformOverride = previousPlatform;
    }
  });

  testWidgets('uses one flat surface style for canvas controls', (
    tester,
  ) async {
    await pumpEditor(tester);

    for (final key in const [
      'navigation-control-surface',
      'history-control-surface',
    ]) {
      final surface = tester.widget<DecoratedBox>(find.byKey(ValueKey(key)));
      final decoration = surface.decoration as BoxDecoration;
      expect(decoration.color, isNotNull);
      expect(decoration.border, isNull);
      expect(decoration.boxShadow, isNull);
      expect(decoration.borderRadius, BorderRadius.circular(6));
    }

    final waveform = tester.widget<IconButton>(
      find.byKey(const ValueKey('waveform-toggle-button')),
    );
    final undo = tester.widget<IconButton>(
      find.widgetWithIcon(IconButton, Icons.undo),
    );
    expect((waveform.icon as Icon).size, 20);
    expect((undo.icon as Icon).size, 20);
  });

  testWidgets('configures chart playback sounds in an outside-dismiss menu', (
    tester,
  ) async {
    await pumpEditor(tester);

    expect(controller.playbackSoundLanguage, PlaybackSoundLanguage.chinese);
    expect(controller.playbackDanceMode, PlaybackDanceMode.fourKey);
    expect(controller.transitionSoundsEnabled, isFalse);

    await tester.tap(find.byKey(const ValueKey('playback-sound-button')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('playback-sound-menu')), findsOneWidget);

    await tester.tap(find.text('英文'));
    await tester.pump();
    await tester.tap(find.text('8K'));
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('transition-sounds-switch')));
    await tester.pump();
    expect(controller.playbackSoundLanguage, PlaybackSoundLanguage.english);
    expect(controller.playbackDanceMode, PlaybackDanceMode.eightKey);
    expect(controller.transitionSoundsEnabled, isTrue);

    await tester.tapAt(const Offset(20, 400));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('playback-sound-menu')), findsNothing);
  });

  testWidgets('device playback uses shared PCM and enforces the core gate', (
    tester,
  ) async {
    controller.dispose();
    final output = _FakePlaybackOutput();
    controller = EditorController(playbackOutput: output);
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    await pumpEditor(tester);

    await controller.togglePlayback();
    await tester.pump();
    expect(controller.isPlaying, isTrue);
    expect(
      tester
          .widget<TextButton>(find.byKey(const ValueKey('song-time')))
          .onPressed,
      isNull,
    );
    expect(
      tester
          .widget<IconButton>(
            find.byKey(const ValueKey('playback-sound-button')),
          )
          .onPressed,
      isNull,
    );
    expect(output.sampleRate, 11025);
    expect(output.bytes, isNotEmpty);
    expect(output.bytes.length % 4, 0);

    controller.setBpm(130);
    expect(controller.bpm, 100);
    final waveformBefore = controller.waveformVisible;
    await tester.tap(find.byKey(const ValueKey('waveform-toggle-button')));
    await tester.pump();
    expect(controller.waveformVisible, isNot(waveformBefore));

    output.playedFrames = output.sampleRate ~/ 2;
    await controller.pausePlayback();
    await tester.pump();
    expect(controller.isPlaying, isFalse);
    expect(
      tester
          .widget<IconButton>(
            find.byKey(const ValueKey('playback-sound-button')),
          )
          .onPressed,
      isNotNull,
    );
    expect(
      controller.currentRow,
      OtgyNative.secondsToSixteenthRow(
        bpm: 100,
        offset: 0,
        seconds: output.playedFrames / output.sampleRate,
      ),
    );
  });

  testWidgets(
    'playback painting interpolates by display frame without drifting past the device',
    (tester) async {
      controller.dispose();
      final output = _FakePlaybackOutput();
      controller = EditorController(playbackOutput: output);
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');
      await pumpEditor(tester);

      double paintedRow() {
        final customPaint = tester.widget<CustomPaint>(
          find.byKey(const ValueKey('editor-canvas-paint')),
        );
        final painter = customPaint.painter!;
        final row = (painter as dynamic).currentRow as ValueListenable<double>;
        return row.value;
      }

      await controller.togglePlayback();
      await tester.pump();
      final confirmedStart = controller.currentRow;
      expect(paintedRow(), confirmedStart);
      expect(
        controller.canvasWaveformBlocks.first.pixelsPerSecond,
        controller.playbackVisualPixelsPerSecond,
      );
      expect(
        controller.canvasWaveformBlocks.first.pixelsPerSecond,
        lessThan(controller.waveformBlocks.first.pixelsPerSecond),
      );
      final playingPainter = tester
          .widget<CustomPaint>(
            find.byKey(const ValueKey('editor-canvas-paint')),
          )
          .painter!;
      expect(
        (playingPainter as dynamic).rowSpacing as double,
        controller.playbackRowSpacing,
      );
      expect(find.text('播放视图 170 px/s'), findsOneWidget);

      await tester.pump(const Duration(milliseconds: 16));
      expect(controller.currentRow, confirmedStart);
      expect(paintedRow(), greaterThan(confirmedStart));

      final maximumPrediction =
          controller.pixelsPerSecond /
          controller.rowSpacing *
          playbackVisualPredictionLimit.inMicroseconds /
          Duration.microsecondsPerSecond;
      await tester.pump(const Duration(milliseconds: 100));
      expect(controller.currentRow, confirmedStart);
      expect(paintedRow(), closeTo(confirmedStart + maximumPrediction, 1e-9));

      await tester.pump(const Duration(milliseconds: 100));
      expect(controller.currentRow, confirmedStart);
      expect(paintedRow(), closeTo(confirmedStart + maximumPrediction, 1e-9));

      output.playedFrames = output.sampleRate ~/ 2;
      await controller.pausePlayback();
      await tester.pump();
      expect(controller.isPlaying, isFalse);
      expect(paintedRow(), controller.currentRow);
      expect(
        controller.canvasWaveformBlocks.first.pixelsPerSecond,
        controller.waveformBlocks.first.pixelsPerSecond,
      );
      final pausedPainter = tester
          .widget<CustomPaint>(
            find.byKey(const ValueKey('editor-canvas-paint')),
          )
          .painter!;
      expect(
        (pausedPainter as dynamic).rowSpacing as double,
        controller.rowSpacing,
      );
      expect(
        find.text('Zoom ${controller.zoom.toStringAsFixed(2)}'),
        findsOneWidget,
      );

      final pausedRow = paintedRow();
      await tester.pump(const Duration(milliseconds: 100));
      expect(paintedRow(), pausedRow);
    },
  );

  testWidgets('loads packaged WAV sounds and renders chart playback PCM', (
    tester,
  ) async {
    controller.dispose();
    final output = _FakePlaybackOutput();
    controller = EditorController(playbackOutput: output);
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    controller.clickCell(0, 0, mobile: false);
    controller.setTransitionSoundsEnabled(true);
    await pumpEditor(tester);

    await controller.togglePlayback();
    await tester.pump();

    expect(controller.isPlaying, isTrue);
    expect(output.sampleRate, 11025);
    expect(output.bytes, isNotEmpty);
    await controller.pausePlayback();
  });

  test(
    'audio analysis runs through the controller background isolate',
    () async {
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');

      final candidates = await controller.analyzeAudio();

      expect(candidates, isNotEmpty);
      expect(candidates.first.bpm, 120);
      expect(controller.analysisCandidates, same(candidates));
      expect(controller.analysisStage, SyncAnalysisStage.finalizingResults);
    },
  );

  testWidgets('late device query cannot stop replacement playback', (
    tester,
  ) async {
    controller.dispose();
    final output = _FakePlaybackOutput();
    controller = EditorController(playbackOutput: output);
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    await controller.togglePlayback();

    final staleQuery = Completer<PlaybackDeviceState>();
    output.queryCompleter = staleQuery;
    await tester.pump(const Duration(milliseconds: 40));
    await controller.pausePlayback();
    await controller.togglePlayback();

    staleQuery.complete(
      PlaybackDeviceState(playedFrames: output.sampleRate ~/ 2, playing: false),
    );
    await tester.pump();

    expect(controller.isPlaying, isTrue);
    await controller.pausePlayback();
  });

  testWidgets('device interruption preserves its exact playback frame', (
    tester,
  ) async {
    controller.dispose();
    final output = _FakePlaybackOutput();
    controller = EditorController(playbackOutput: output);
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    await pumpEditor(tester);

    await controller.togglePlayback();
    output.playedFrames = output.sampleRate ~/ 2;
    output.playing = false;
    await tester.pump(const Duration(milliseconds: 40));

    expect(controller.isPlaying, isFalse);
    expect(
      controller.currentRow,
      OtgyNative.secondsToSixteenthRow(
        bpm: 100,
        offset: 0,
        seconds: output.playedFrames / output.sampleRate,
      ),
    );
  });

  testWidgets('playback query failure keeps the last confirmed frame', (
    tester,
  ) async {
    controller.dispose();
    final output = _FakePlaybackOutput();
    controller = EditorController(playbackOutput: output);
    final audio = File(
      '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
    ).readAsBytesSync();
    await controller.loadAudioBytes(audio, 'fixture.mp3');
    await pumpEditor(tester);

    await controller.togglePlayback();
    output.playedFrames = output.sampleRate ~/ 2;
    await tester.pump(const Duration(milliseconds: 40));
    output.failQuery = true;
    await tester.pump(const Duration(milliseconds: 40));

    expect(controller.isPlaying, isFalse);
    expect(
      controller.currentRow,
      OtgyNative.secondsToSixteenthRow(
        bpm: 100,
        offset: 0,
        seconds: output.playedFrames / output.sampleRate,
      ),
    );
  });

  testWidgets('back navigation protects unsaved work', (tester) async {
    await pumpEditor(tester);
    controller.setBpm(101);
    await tester.pump();

    await tester.binding.handlePopRoute();
    await tester.pumpAndSettle();
    expect(find.text('当前修改尚未保存'), findsOneWidget);

    await tester.tap(find.text('取消'));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('editor-canvas')), findsOneWidget);
    expect(controller.isDirty, isTrue);
  });

  test(
    'audio export stays gated through saving and supports cancellation',
    () async {
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');
      final saveEntered = Completer<void>();
      final releaseSave = Completer<void>();

      final export = controller.exportAudio(
        quality: 1,
        baseName: 'fixture',
        save: (result, isCancelled, onProgress) async {
          onProgress(0, result.bytes.length);
          saveEntered.complete();
          await releaseSave.future;
          if (isCancelled()) {
            throw StateError('cancelled');
          }
        },
      );
      await saveEntered.future;
      expect(controller.isExportingAudio, isTrue);
      expect(controller.generalActionsEnabled, isFalse);
      expect(controller.audioExportProgress?.stage, AudioExportStage.saving);

      controller.cancelAudioExport();
      expect(controller.audioExportCancellationRequested, isTrue);
      releaseSave.complete();
      await expectLater(export, throwsA(isA<AudioExportCancelled>()));
      expect(controller.isExportingAudio, isFalse);
      expect(controller.generalActionsEnabled, isTrue);
    },
  );
}

final class _FakePlaybackOutput implements PlaybackOutput {
  int sampleRate = 0;
  Uint8List bytes = Uint8List(0);
  int playedFrames = 0;
  bool playing = false;
  bool failQuery = false;
  Completer<PlaybackDeviceState>? queryCompleter;

  @override
  Future<void> start({
    required int sampleRate,
    required Uint8List interleavedPcm16le,
  }) async {
    this.sampleRate = sampleRate;
    bytes = Uint8List.fromList(interleavedPcm16le);
    playedFrames = 0;
    playing = true;
  }

  @override
  Future<PlaybackDeviceState> pause() async {
    playing = false;
    return PlaybackDeviceState(playedFrames: playedFrames, playing: playing);
  }

  @override
  Future<PlaybackDeviceState> query() async {
    final pending = queryCompleter;
    if (pending != null) {
      queryCompleter = null;
      return pending.future;
    }
    if (failQuery) {
      throw StateError('device interrupted');
    }
    return PlaybackDeviceState(playedFrames: playedFrames, playing: playing);
  }

  @override
  Future<void> dispose() async {
    playing = false;
  }
}
