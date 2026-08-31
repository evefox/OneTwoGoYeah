import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/infrastructure/playback_output.dart';
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
    final cursorY = canvas.top + canvas.height * 0.52;
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
      controller.currentRow,
      OtgyNative.secondsToSixteenthRow(
        bpm: 100,
        offset: 0,
        seconds: output.playedFrames / output.sampleRate,
      ),
    );
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
