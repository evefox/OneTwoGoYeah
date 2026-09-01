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

  testWidgets('shows the three editor regions and precise timing summaries', (
    tester,
  ) async {
    await pumpEditor(tester);

    expect(find.byKey(const ValueKey('audio-picker')), findsOneWidget);
    expect(find.byKey(const ValueKey('chart-menu')), findsOneWidget);
    expect(find.byKey(const ValueKey('editor-canvas')), findsOneWidget);
    expect(find.text('100.000'), findsOneWidget);
    expect(find.text('0.000'), findsOneWidget);
    expect(find.bySemanticsLabel('NPC'), findsOneWidget);
    expect(find.bySemanticsLabel('玩家'), findsOneWidget);
    expect(find.bySemanticsLabel('Showtime'), findsOneWidget);
  });

  testWidgets('edits through the native chart command and supports history', (
    tester,
  ) async {
    await pumpEditor(tester);
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final chartWidth = canvas.width - 94;
    final cursorY = canvas.top + editorCursorY(canvas.size);
    final npcX = canvas.left + 36 + chartWidth / 6;

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
    expect(find.byKey(const ValueKey('export-audio')), findsOneWidget);
    expect(find.byKey(const ValueKey('save-chart')), findsOneWidget);
    expect(find.byKey(const ValueKey('export-slk')), findsOneWidget);
    expect(find.bySemanticsLabel('谱面编辑画布，NPC、玩家、Showtime 三列'), findsOneWidget);
  });

  testWidgets('gives mobile timing controls a clear, tappable second row', (
    tester,
  ) async {
    final previousPlatform = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    try {
      await pumpEditor(tester, size: const Size(390, 844));

      final audio = tester.getRect(find.byKey(const ValueKey('audio-picker')));
      final bpm = tester.getRect(find.byKey(const ValueKey('bpm-menu')));
      final offset = tester.getRect(find.byKey(const ValueKey('offset-menu')));
      final export = tester.getRect(find.byKey(const ValueKey('export-audio')));
      final bpmLabel = tester.getRect(find.text('BPM'));
      final bpmValue = tester.getRect(find.text('100.000'));
      final offsetLabel = tester.getRect(find.text('Offset'));
      final offsetValue = tester.getRect(find.text('0.000'));

      expect(audio.width, greaterThan(300));
      expect(bpm.top, greaterThan(audio.bottom));
      expect(offset.center.dy, closeTo(bpm.center.dy, 1));
      expect(export.center.dy, closeTo(bpm.center.dy, 1));
      expect(bpm.width, greaterThan(120));
      expect(offset.width, greaterThan(120));
      expect(bpmLabel.center.dy, closeTo(bpmValue.center.dy, 1));
      expect(offsetLabel.center.dy, closeTo(offsetValue.center.dy, 1));
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

      final bpm = tester.getRect(find.byKey(const ValueKey('bpm-menu')));
      expect(bpm.width, lessThan(120));
    } finally {
      debugDefaultTargetPlatformOverride = previousPlatform;
    }
  });

  testWidgets('keeps the desktop audio toolbar on one row', (tester) async {
    await pumpEditor(tester, size: const Size(1000, 700));

    final audio = tester.getRect(find.byKey(const ValueKey('audio-picker')));
    final bpm = tester.getRect(find.byKey(const ValueKey('bpm-menu')));
    final offset = tester.getRect(find.byKey(const ValueKey('offset-menu')));

    expect(bpm.top, closeTo(audio.top, 1));
    expect(offset.top, closeTo(audio.top, 1));
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
    await pumpEditor(tester);
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

    final chartWidth = captured.width - 36 - 58;
    final columnWidth = chartWidth / 3;
    for (final divider in [
      (36 + columnWidth).round(),
      (36 + columnWidth * 2).round(),
    ]) {
      expect(pixel(divider, 40), pixel(divider + 3, 40));
    }
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
    await tester.pump();

    expect(controller.waveformVisible, isFalse);
    expect(controller.markers, isEmpty);
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
    controller.toggleWaveform();
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
