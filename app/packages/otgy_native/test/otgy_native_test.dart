import 'dart:io';
import 'dart:typed_data';

import 'package:otgy_native/otgy_native.dart';
import 'package:test/test.dart';

void main() {
  test('stable ABI creates and edits an empty chart', () {
    expect(OtgyNative.apiVersion, 2);
    expect(OtgyNative.coreVersion, isNotEmpty);

    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);

    expect(editor.markers(), isEmpty);
    final first = editor.click(OTGY_CHART_COLUMN_NPC, 0);
    expect(first.changed, isTrue);
    expect(editor.markers().single.type, OTGY_CHART_MARKER_NPC_BLOCK_START);

    final second = editor.click(OTGY_CHART_COLUMN_NPC, 4);
    expect(second.changed, isTrue);
    final markers = editor.markers();
    expect(markers, hasLength(2));
    expect(markers.last.type, OTGY_CHART_MARKER_NPC_NOTE);
    expect(markers.last.ordinal, 1);

    final range = editor.timelineRange(bpm: 100, offset: 0);
    expect(range.endRow, 64);
    expect(range.endSeconds, 9.6);

    expect(editor.canUndo, isTrue);
    editor.undo();
    expect(editor.markers(), hasLength(1));
    expect(editor.canRedo, isTrue);
  });

  test('multi-selection and preview values outlive native handles', () {
    final editor = NativeChartEditor.createEmpty();
    for (final firstRow in [0, 32]) {
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
    editor.select(OTGY_CHART_COLUMN_NPC, 32);
    editor.select(OTGY_CHART_COLUMN_NPC, 0);
    final selected = editor.selections;
    expect(selected.map((item) => item.firstRow), [0, 32]);
    expect(editor.selectionCount, 2);
    editor.select(OTGY_CHART_COLUMN_SHOWTIME, 200);
    expect(editor.selectionCount, 2);
    final before = editor.encodeRecovery();
    expect(editor.copySelection().code, 0);
    expect(editor.clipboardCount, 2);
    final preview = editor.pastePreview(100);
    expect(preview.valid, isTrue);
    expect(preview.firstRow, 100);
    expect(preview.lastRow, 146);
    expect(preview.selections.map((item) => item.firstRow), [100, 132]);
    final ghostRows = preview.markers.map((item) => item.row).toList();
    expect(ghostRows, [
      100,
      102,
      104,
      106,
      108,
      110,
      112,
      114,
      132,
      134,
      136,
      138,
      140,
      142,
      144,
      146,
    ]);
    expect(editor.encodeRecovery(), before);
    expect(() => selected.clear(), throwsUnsupportedError);
    expect(() => preview.markers.clear(), throwsUnsupportedError);
    expect(() => preview.selections.clear(), throwsUnsupportedError);
    expect(editor.paste(100).changed, isTrue);
    editor.clearSelection();
    editor.clearClipboard();
    editor.dispose();
    expect(selected.map((item) => item.firstRow), [0, 32]);
    expect(preview.markers.map((item) => item.row), ghostRows);
  });

  test('cut preparation preserves source until one atomic placement', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    editor.click(OTGY_CHART_COLUMN_NPC, 0);
    editor.click(OTGY_CHART_COLUMN_NPC, 4);
    editor.select(OTGY_CHART_COLUMN_NPC, 0);
    final before = editor.encodeRecovery();
    final sourceIds = editor.markers().map((marker) => marker.id).toSet();
    expect(editor.cutSelection().code, 0);
    expect(editor.cutPending, isTrue);
    expect(editor.encodeRecovery(), before);
    expect(editor.pastePreview(2).valid, isTrue);
    expect(editor.paste(2).changed, isTrue);
    expect(editor.markers().map((marker) => marker.row), [2, 6]);
    expect(editor.markers().every((m) => !sourceIds.contains(m.id)), isTrue);
    expect(editor.cutPending, isFalse);
    editor.undo();
    expect(editor.encodeRecovery(), before);
    expect(editor.cutPending, isFalse);
    expect(editor.paste(20).changed, isTrue);
    expect(editor.markers().map((marker) => marker.row), [0, 4, 20, 24]);
    final after = editor.encodeRecovery();
    expect(editor.pastePreview(0).valid, isFalse);
    expect(editor.paste(0).changed, isFalse);
    expect(editor.encodeRecovery(), after);
  });

  test('formal and recovery payloads preserve incomplete native drafts', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    editor.click(OTGY_CHART_COLUMN_NPC, 3);
    final formal = editor.encodeFormal();
    final decoded = NativeChartEditor.decodeFormal(formal);
    addTearDown(decoded.editor.dispose);
    expect(decoded.editor.markers().single.row, 3);
    expect(decoded.editor.encodeFormal(), orderedEquals(formal));

    final recovery = editor.encodeRecovery();
    final restored = NativeChartEditor.decodeRecovery(recovery);
    addTearDown(restored.dispose);
    expect(restored.markers().single.type, OTGY_CHART_MARKER_NPC_BLOCK_START);
  });

  test('Offset beat adjustments use one unrounded baseline', () {
    const bpm = 127.8878685355244;
    const offset = -0.43331065759637188;
    final adjusted = OtgyNative.adjustOffsetByBeats(
      bpm: bpm,
      baselineOffset: offset,
      cumulativeBeats: -2,
    );

    expect(adjusted, offset + (60 / bpm * -2));
    expect(adjusted.toStringAsFixed(3), '-1.372');
  });

  test('nearest whole-beat Offset moves the baseline closest to zero', () {
    expect(
      OtgyNative.nearestWholeBeatOffset(bpm: 120, offsetSeconds: 0.433),
      -1,
    );
    expect(OtgyNative.nearestWholeBeatOffset(bpm: 120, offsetSeconds: 0.2), 0);
    expect(
      OtgyNative.nearestWholeBeatOffset(bpm: 120, offsetSeconds: -0.433),
      1,
    );
    expect(
      OtgyNative.nearestWholeBeatOffset(bpm: 120, offsetSeconds: 0.25),
      -1,
    );
    expect(
      OtgyNative.nearestWholeBeatOffset(bpm: 120, offsetSeconds: -0.25),
      1,
    );
  });

  test(
    'SLK import and export remain byte compatible with the canonical file',
    () {
      final source = File('../../../example/z3116_lv5.slk').readAsBytesSync();
      final editor = NativeChartEditor.importSlk(source);
      addTearDown(editor.dispose);

      final exported = editor.exportSlk();
      expect(exported.diagnostics, isEmpty);
      expect(exported.bytes, source);
    },
  );

  test('empty chart validation returns structured diagnostics', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);

    final exported = editor.exportSlk();
    expect(exported.isFinished, isFalse);
    expect(exported.diagnostics, hasLength(1));
    expect(exported.diagnostics.single.code, 0);
  });

  test('analysis and Ogg export call the shared audio core', () {
    final bytes = Uint8List.fromList(
      File(
        '../../../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync(),
    );
    final audio = OtgyNative.decodeAudio(bytes, '.mp3');
    addTearDown(audio.dispose);

    final stages = <SyncAnalysisStage>[];
    final analysis = audio.analyzeSync(onStage: stages.add);
    expect(analysis.candidates, isNotEmpty);
    expect(analysis.candidates.first.bpm, 120);
    expect(stages, SyncAnalysisStage.values);
    final policy = audio.exportQualityPolicy();
    expect(policy.defaultQuality, inInclusiveRange(1, policy.qualityCeiling));
    final progress = <AudioExportProgress>[];
    final output = audio.exportOgg(
      offset: 0,
      quality: 1,
      baseName: 'fixture',
      onProgress: progress.add,
    );
    expect(output.bytes.sublist(0, 4), [0x4f, 0x67, 0x67, 0x53]);
    expect(output.frameCount, audio.frameCount);
    expect(
      progress.map((item) => item.stage),
      containsAll([AudioExportStage.preparingAudio, AudioExportStage.encoding]),
    );

    final cancellation = AudioExportCancellation.create();
    addTearDown(cancellation.dispose);
    cancellation.request();
    expect(cancellation.isRequested, isTrue);
    expect(
      () => audio.exportOgg(
        offset: 0,
        quality: 1,
        baseName: 'fixture',
        cancellation: cancellation,
      ),
      throwsA(
        isA<OtgyException>()
            .having((error) => error.domain, 'domain', 9)
            .having((error) => error.code, 'code', 1),
      ),
    );

    final playback = audio.renderPlayback(0);
    expect(playback.sampleRate, audio.sampleRate);
    expect(playback.frameCount, audio.frameCount);
    expect(playback.interleavedPcm16le.length, audio.frameCount * 4);
    final soundBank = NativePlaybackSoundBank.decode([
      PlaybackSoundAsset(
        language: PlaybackSoundLanguage.chinese,
        id: PlaybackSoundId.spaceBar,
        wavBytes: File('../../assets/sounds/common/space_bar.wav')
            .readAsBytesSync(),
      ),
    ]);
    addTearDown(soundBank.dispose);
    final rhythmTest = audio.renderRhythmTestPlayback(
      soundBank: soundBank,
      language: PlaybackSoundLanguage.chinese,
      bpm: 120,
      offset: 0,
      timelineStartSeconds: 0,
    );
    expect(rhythmTest.sampleRate, playback.sampleRate);
    expect(rhythmTest.frameCount, playback.frameCount);
    expect(rhythmTest.interleavedPcm16le, isNot(playback.interleavedPcm16le));
    final start = OtgyNative.startPlayback(
      audioDurationSeconds: audio.durationSeconds,
      cursorTimelineSeconds: 0,
    );
    expect(start.isPlaying, isTrue);
    final advanced = OtgyNative.advancePlaybackFrames(
      start: start,
      playedFrames: audio.sampleRate ~/ 2,
      sampleRate: audio.sampleRate,
    );
    expect(
      advanced.timelineSeconds,
      (audio.sampleRate ~/ 2) / audio.sampleRate,
    );
    expect(OtgyNative.pausePlayback(advanced).isPlaying, isFalse);
    expect(OtgyNative.coordinateApplicationOperation(mode: 1, operation: 0), 1);
    expect(OtgyNative.coordinateApplicationOperation(mode: 1, operation: 2), 0);
    // A selection or paste-pending session keeps its own commands, navigation
    // and history navigation, and rejects everything else.
    expect(
      OtgyNative.coordinateApplicationOperation(
        mode: OTGY_APPLICATION_MODE_CHART_INTERACTION,
        operation: OTGY_APPLICATION_OPERATION_GENERAL,
      ),
      OTGY_APPLICATION_OPERATION_CHART_INTERACTION_ACTIVE,
    );
    for (final operation in [
      OTGY_APPLICATION_OPERATION_SELECTION_COMMAND,
      OTGY_APPLICATION_OPERATION_NAVIGATION_OR_VIEW,
      OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION,
      OTGY_APPLICATION_OPERATION_TOGGLE_WAVEFORM,
      OTGY_APPLICATION_OPERATION_PAUSE_PLAYBACK,
    ]) {
      expect(
        OtgyNative.coordinateApplicationOperation(
          mode: OTGY_APPLICATION_MODE_CHART_INTERACTION,
          operation: operation,
        ),
        OTGY_APPLICATION_OPERATION_ALLOWED,
      );
    }
    // Idle keeps allowing the new classes, and the constants stay stable.
    for (final operation in [
      OTGY_APPLICATION_OPERATION_SELECTION_COMMAND,
      OTGY_APPLICATION_OPERATION_NAVIGATION_OR_VIEW,
      OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION,
    ]) {
      expect(
        OtgyNative.coordinateApplicationOperation(
          mode: OTGY_APPLICATION_MODE_IDLE,
          operation: operation,
        ),
        OTGY_APPLICATION_OPERATION_ALLOWED,
      );
    }
    expect(OTGY_APPLICATION_MODE_CHART_INTERACTION, 3);
    expect(OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION, 6);
    expect(OTGY_APPLICATION_OPERATION_CHART_INTERACTION_ACTIVE, 5);

    final defaultWaveform = audio.generateWaveform(
      zoom: 11,
      scale: 4,
      blockIndex: 1,
    );
    final avDefaultWaveform = audio.generateWaveform(
      zoom: 11,
      scale: 4,
      blockIndex: 1,
      shape: WaveformShape.rectified,
      luminance: WaveformLuminance.uniform,
      sampling: WaveformSampling.twoX,
    );
    expect(defaultWaveform.leftAlpha, avDefaultWaveform.leftAlpha);
    expect(defaultWaveform.rightAlpha, avDefaultWaveform.rightAlpha);
  });

  test('audio export names use the shared normalization rules', () {
    expect(OtgyNative.defaultAudioExportBaseName('bad:name.mp3'), 'bad_name');
    expect(
      OtgyNative.makeAudioExportFileName(
        baseName: '曲目',
        localTime: DateTime(2026, 8, 31, 9, 5, 7),
      ),
      '曲目_20260831_090507.ogg',
    );
  });
}
