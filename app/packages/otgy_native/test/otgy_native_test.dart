import 'dart:io';
import 'dart:typed_data';

import 'package:otgy_native/otgy_native.dart';
import 'package:test/test.dart';

void main() {
  test('stable ABI creates and edits an empty chart', () {
    expect(OtgyNative.apiVersion, 1);
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

  test('formal and recovery payloads preserve incomplete native drafts', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    editor.click(OTGY_CHART_COLUMN_NPC, 3);
    const bpm = 129.24970691676435;
    const offset = -0.090521541950113374;

    final formal = editor.encodeFormal(bpm: bpm, offset: offset);
    final decoded = NativeChartEditor.decodeFormal(formal);
    addTearDown(decoded.editor.dispose);
    expect(decoded.bpm, bpm);
    expect(decoded.offset, offset);
    expect(decoded.editor.markers().single.row, 3);

    final recovery = editor.encodeRecovery();
    final restored = NativeChartEditor.decodeRecovery(recovery);
    addTearDown(restored.dispose);
    expect(restored.markers().single.type, OTGY_CHART_MARKER_NPC_BLOCK_START);
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
