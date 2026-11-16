library;

import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'otgy_native_bindings_generated.dart' as native;

export 'otgy_native_bindings_generated.dart'
    show
        OTGY_APPLICATION_MODE_CHART_INTERACTION,
        OTGY_APPLICATION_MODE_EXPORTING_AUDIO,
        OTGY_APPLICATION_MODE_IDLE,
        OTGY_APPLICATION_MODE_PLAYING,
        OTGY_APPLICATION_OPERATION_ALLOWED,
        OTGY_APPLICATION_OPERATION_CHART_INTERACTION_ACTIVE,
        OTGY_APPLICATION_OPERATION_GENERAL,
        OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION,
        OTGY_APPLICATION_OPERATION_NAVIGATION_OR_VIEW,
        OTGY_APPLICATION_OPERATION_PAUSE_PLAYBACK,
        OTGY_APPLICATION_OPERATION_SELECTION_COMMAND,
        OTGY_APPLICATION_OPERATION_TOGGLE_WAVEFORM,
        OTGY_CHART_COLUMN_NPC,
        OTGY_CHART_COLUMN_PLAYER,
        OTGY_CHART_COLUMN_SHOWTIME,
        OTGY_CHART_EDIT_EMPTY_DANCE_MOVE_END,
        OTGY_CHART_EDIT_NO_CHANGE,
        OTGY_CHART_EDIT_NOTE_LIMIT_REACHED,
        OTGY_CHART_EDIT_POSITION_OCCUPIED,
        OTGY_CHART_EDIT_SHOWTIME_PARTIALLY_COMPLETED,
        OTGY_CHART_MARKER_NPC_BLOCK_START,
        OTGY_CHART_MARKER_NPC_MOVE_END,
        OTGY_CHART_MARKER_NPC_NOTE,
        OTGY_CHART_MARKER_PLAYER_BLOCK_START,
        OTGY_CHART_MARKER_PLAYER_MOVE_END,
        OTGY_CHART_MARKER_PLAYER_NOTE,
        OTGY_CHART_MARKER_SHOWTIME_MOVE,
        OTGY_CHART_MARKER_SHOWTIME_START;

final class OtgyException implements Exception {
  const OtgyException({
    required this.status,
    required this.domain,
    required this.code,
    required this.message,
  });

  final int status;
  final int domain;
  final int code;
  final String message;

  @override
  String toString() => 'OtgyException($domain:$code): $message';
}

final class ChartMarker {
  const ChartMarker({
    required this.id,
    required this.row,
    required this.column,
    required this.type,
    required this.showNCount,
    this.ordinal,
  });

  final int id;
  final int row;
  final int column;
  final int type;
  final int showNCount;
  final int? ordinal;
}

final class ShowtimeMove {
  const ShowtimeMove({
    required this.eventId,
    required this.row,
    required this.showNCount,
    required this.rangeStart,
    required this.rangeEnd,
    required this.isFirst,
    required this.isLast,
  });

  final int eventId;
  final int row;
  final int showNCount;
  final int rangeStart;
  final int rangeEnd;
  final bool isFirst;
  final bool isLast;
}

final class EditResult {
  const EditResult({
    required this.code,
    required this.changed,
    required this.primaryEventId,
    this.playerActionsCleared = false,
  });

  final int code;
  final bool changed;
  final int primaryEventId;
  final bool playerActionsCleared;
}

/// The three NPC marker types the desktop keyboard path places explicitly.
/// Values match `ChartRoleEventType` for the NPC roles.
enum ChartNpcMarker {
  blockStart(0),
  note(1),
  moveEnd(2);

  const ChartNpcMarker(this.value);

  final int value;
}

/// How a range selection combines with the existing selection set. Values match
/// the `OTGY_CHART_SELECTION_*` constants.
enum ChartSelectionMode {
  replace(0),
  add(1),
  subtract(2);

  const ChartSelectionMode(this.value);

  final int value;
}

/// The role a selected unit belongs to. Values match the
/// `OTGY_CHART_ROLE_*` constants.
enum ChartSelectionRole {
  npc(native.OTGY_CHART_ROLE_NPC),
  player(native.OTGY_CHART_ROLE_PLAYER);

  const ChartSelectionRole(this.value);

  final int value;

  static ChartSelectionRole fromValue(int value) =>
      value == player.value ? player : npc;
}

/// Which part of a drill group a selected unit covers. Values match the
/// `OTGY_CHART_SELECTION_PART_*` constants.
enum ChartSelectionPart {
  start(native.OTGY_CHART_SELECTION_PART_START),
  actions(native.OTGY_CHART_SELECTION_PART_ACTIONS);

  const ChartSelectionPart(this.value);

  final int value;

  static ChartSelectionPart fromValue(int value) =>
      value == actions.value ? actions : start;
}

final class ChartSelection {
  const ChartSelection({
    required this.active,
    required this.danceMoveDrillId,
    required this.drillGroupId,
    required this.role,
    required this.part,
    required this.firstRow,
    required this.lastRow,
    required this.eventCount,
  });

  final bool active;
  final int danceMoveDrillId;
  final int drillGroupId;
  final ChartSelectionRole role;
  final ChartSelectionPart part;
  final int firstRow;
  final int lastRow;
  final int eventCount;

  /// Synthetic row band for painting a drag preview or paste envelope that has
  /// no unit identity yet.
  const ChartSelection.band({required this.firstRow, required this.lastRow})
    : active = true,
      danceMoveDrillId = 0,
      drillGroupId = 0,
      role = ChartSelectionRole.npc,
      part = ChartSelectionPart.start,
      eventCount = 0;

  factory ChartSelection._fromNative(native.otgy_chart_selection value) =>
      ChartSelection(
        active: value.active != 0,
        danceMoveDrillId: value.dance_move_drill_id,
        drillGroupId: value.drill_group_id,
        role: ChartSelectionRole.fromValue(value.role),
        part: ChartSelectionPart.fromValue(value.part),
        firstRow: value.first_row,
        lastRow: value.last_row,
        eventCount: value.event_count,
      );
}

final class SessionResult {
  const SessionResult({required this.code, required this.changed});

  final int code;
  final bool changed;
}

final class ChartPastePreview {
  ChartPastePreview({
    required this.code,
    required this.firstRow,
    required this.lastRow,
    required List<ChartSelection> selections,
    required List<ChartMarker> markers,
  }) : selections = List.unmodifiable(selections),
       markers = List.unmodifiable(markers);

  final int code;
  final int firstRow;
  final int lastRow;
  final List<ChartSelection> selections;
  final List<ChartMarker> markers;
  bool get valid => code == native.OTGY_CHART_EDIT_SUCCESS;
}

final class TimelineRange {
  const TimelineRange({
    required this.beginSeconds,
    required this.endSeconds,
    required this.endRow,
    required this.usesZeroBpmProjection,
  });

  final double beginSeconds;
  final double endSeconds;
  final double endRow;
  final bool usesZeroBpmProjection;
}

final class TimelineNavigation {
  const TimelineNavigation({
    required this.toBeginRow,
    required this.pageUpRow,
    required this.pageDownRow,
    required this.toEndRow,
    required this.canToBegin,
    required this.canPageUp,
    required this.canPageDown,
    required this.canToEnd,
  });

  final double toBeginRow;
  final double pageUpRow;
  final double pageDownRow;
  final double toEndRow;
  final bool canToBegin;
  final bool canPageUp;
  final bool canPageDown;
  final bool canToEnd;
}

final class WaveformBlock {
  const WaveformBlock({
    required this.blockIndex,
    required this.pixelsPerSecond,
    required this.channelWidth,
    required this.channelSpacing,
    required this.leftAlpha,
    required this.rightAlpha,
  });

  static const height = 128;

  final int blockIndex;
  final int pixelsPerSecond;
  final int channelWidth;
  final int channelSpacing;
  final Uint8List leftAlpha;
  final Uint8List rightAlpha;
}

/// Display shape used by the waveform renderer.
enum WaveformShape {
  signed(0),
  rectified(1);

  const WaveformShape(this.value);
  final int value;
}

/// Alpha/luminance policy used by the waveform renderer.
enum WaveformLuminance {
  uniform(0),
  amplitude(1);

  const WaveformLuminance(this.value);
  final int value;
}

/// Supersampling factor used before the final tile is reduced to display size.
enum WaveformSampling {
  oneX(0),
  twoX(1),
  threeX(2),
  fourX(3);

  const WaveformSampling(this.value);
  final int value;
}

/// Optional ArrowVortex-compatible display filter.
enum WaveformFilter {
  none(0),
  highPass(1),
  lowPass(2);

  const WaveformFilter(this.value);
  final int value;
}

final class AudioSourceInfo {
  const AudioSourceInfo({
    required this.codec,
    required this.measuredBitrateBps,
    required this.nominalBitrateBps,
  });

  final int codec;
  final int measuredBitrateBps;
  final int nominalBitrateBps;
}

final class AudioExportQualityPolicy {
  const AudioExportQualityPolicy({
    required this.nominalBitratesBps,
    required this.sourceBitrateBps,
    required this.qualityCeiling,
    required this.defaultQuality,
  });

  final List<int> nominalBitratesBps;
  final int sourceBitrateBps;
  final int qualityCeiling;
  final int defaultQuality;
}

final class AudioAnalysisCandidate {
  const AudioAnalysisCandidate({
    required this.bpm,
    required this.detectorOffsetSeconds,
    required this.appOffsetSeconds,
    required this.fitness,
  });

  final double bpm;
  final double detectorOffsetSeconds;
  final double appOffsetSeconds;
  final double fitness;
}

final class AudioAnalysis {
  const AudioAnalysis({
    required this.analyzedFrameCount,
    required this.candidates,
  });

  final int analyzedFrameCount;
  final List<AudioAnalysisCandidate> candidates;
}

enum SyncAnalysisStage {
  detectingOnsets(0),
  scanningIntervals(1),
  refiningCandidates(2),
  selectingBpm(3),
  calculatingOffsets(4),
  finalizingResults(5);

  const SyncAnalysisStage(this.value);

  final int value;

  static SyncAnalysisStage? fromValue(int value) => switch (value) {
    0 => detectingOnsets,
    1 => scanningIntervals,
    2 => refiningCandidates,
    3 => selectingBpm,
    4 => calculatingOffsets,
    5 => finalizingResults,
    _ => null,
  };
}

final class AudioExportResult {
  const AudioExportResult({
    required this.bytes,
    required this.signedShiftFrames,
    required this.frameCount,
  });

  final Uint8List bytes;
  final int signedShiftFrames;
  final int frameCount;
}

enum AudioExportStage {
  preparingAudio(1),
  encoding(2),
  saving(3);

  const AudioExportStage(this.value);

  final int value;

  static AudioExportStage? fromValue(int value) => switch (value) {
    1 => preparingAudio,
    2 => encoding,
    3 => saving,
    _ => null,
  };
}

/// Selects the exported audio end.
///
/// [sourceEnd] keeps the decoded source length after the Offset head shift.
/// [beat] ends at a chart beat row (a multiple of four 1/16 rows).
enum AudioEndMode { sourceEnd, beat }

final class AudioExportProgress {
  const AudioExportProgress({
    required this.stage,
    required this.completedUnits,
    required this.totalUnits,
  });

  final AudioExportStage stage;
  final int completedUnits;
  final int totalUnits;
}

final class AudioExportCancellation {
  AudioExportCancellation._(this._handle, this._ownsHandle);

  factory AudioExportCancellation.create() {
    return using((arena) {
      final outFlag = arena<Pointer<native.otgy_audio_export_cancel_flag>>();
      final outError = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_audio_export_cancel_flag_create(outFlag, outError),
          outError,
        );
        return AudioExportCancellation._(outFlag.value, true);
      } finally {
        _destroyError(outError);
      }
    });
  }

  factory AudioExportCancellation.borrowAddress(int address) =>
      AudioExportCancellation._(
        Pointer<native.otgy_audio_export_cancel_flag>.fromAddress(address),
        false,
      );

  Pointer<native.otgy_audio_export_cancel_flag> _handle;
  final bool _ownsHandle;

  int get address => _requireHandle().address;
  bool get isRequested =>
      native.otgy_audio_export_cancel_flag_is_requested(_requireHandle()) != 0;

  void request() {
    native.otgy_audio_export_cancel_flag_request(_requireHandle());
  }

  Pointer<native.otgy_audio_export_cancel_flag> _requireHandle() {
    if (_handle == nullptr) {
      throw StateError('AudioExportCancellation has been disposed.');
    }
    return _handle;
  }

  void dispose() {
    if (_handle == nullptr) {
      return;
    }
    if (_ownsHandle) {
      native.otgy_audio_export_cancel_flag_destroy(_handle);
    }
    _handle = nullptr;
  }
}

final class DecodedFormalDraft {
  const DecodedFormalDraft({required this.editor, this.repaired = false});

  final NativeChartEditor editor;
  final bool repaired;
}

final class DecodedRecovery {
  const DecodedRecovery({required this.editor, this.repaired = false});

  final NativeChartEditor editor;
  final bool repaired;
}

final class ChartDiagnostic {
  const ChartDiagnostic({
    required this.code,
    required this.objectId,
    required this.relatedId,
    required this.row,
  });

  final int code;
  final int objectId;
  final int relatedId;
  final int row;
}

final class SlkExport {
  const SlkExport({required this.diagnostics, this.bytes});

  final List<ChartDiagnostic> diagnostics;
  final Uint8List? bytes;
  bool get isFinished => bytes != null;
}

final class PlaybackPcm {
  const PlaybackPcm({
    required this.sampleRate,
    required this.frameCount,
    required this.interleavedPcm16le,
  });

  final int sampleRate;
  final int frameCount;
  final Uint8List interleavedPcm16le;
}

enum PlaybackJudgementBasis { sixteenthNote, halfBeat }

enum PlaybackJudgementGrade { perfect, great, cool, bad, miss }

final class PlaybackJudgementConfig {
  const PlaybackJudgementConfig({
    this.basis = PlaybackJudgementBasis.halfBeat,
    this.perfectFraction = 0.125,
    this.greatFraction = 0.25,
    this.coolFraction = 0.375,
    this.badFraction = 0.5,
    this.offsetMs = 0.0,
  });

  final PlaybackJudgementBasis basis;
  final double perfectFraction;
  final double greatFraction;
  final double coolFraction;
  final double badFraction;
  final double offsetMs;

  PlaybackJudgementConfig copyWith({
    PlaybackJudgementBasis? basis,
    double? perfectFraction,
    double? greatFraction,
    double? coolFraction,
    double? badFraction,
    double? offsetMs,
  }) {
    return PlaybackJudgementConfig(
      basis: basis ?? this.basis,
      perfectFraction: perfectFraction ?? this.perfectFraction,
      greatFraction: greatFraction ?? this.greatFraction,
      coolFraction: coolFraction ?? this.coolFraction,
      badFraction: badFraction ?? this.badFraction,
      offsetMs: offsetMs ?? this.offsetMs,
    );
  }
}

final class PlaybackJudgement {
  const PlaybackJudgement({
    required this.grade,
    required this.deltaMs,
    required this.noteId,
    required this.expectedSound,
    required this.pressedSound,
    this.noteSeconds = 0,
    this.pressSeconds = 0,
    this.queueDelayMs = 0,
  });

  final PlaybackJudgementGrade grade;
  final double deltaMs;
  final int noteId;
  final PlaybackSoundId expectedSound;
  final PlaybackSoundId pressedSound;

  /// Song timeline of the matched note.
  final double noteSeconds;

  /// Song timeline at which the press was judged.
  final double pressSeconds;

  /// Time from queueing the key to the audio thread consuming it.
  final double queueDelayMs;
}

final class PlaybackEngineState {
  const PlaybackEngineState({
    required this.renderedFrames,
    required this.positionSeconds,
    required this.finished,
  });

  final int renderedFrames;
  final double positionSeconds;
  final bool finished;
}

/// The four symmetric judgement windows in milliseconds for one BPM and
/// judgement configuration.
final class PlaybackJudgementWindows {
  const PlaybackJudgementWindows({
    required this.unitMs,
    required this.perfectMs,
    required this.greatMs,
    required this.coolMs,
    required this.badMs,
  });

  final double unitMs;
  final double perfectMs;
  final double greatMs;
  final double coolMs;
  final double badMs;
}

/// One matched press of a simulation run, in song timeline seconds.
final class PlaybackJudgementSample {
  const PlaybackJudgementSample({
    required this.noteSeconds,
    required this.pressSeconds,
    required this.correctKey,
  });

  final double noteSeconds;
  final double pressSeconds;
  final bool correctKey;
}

/// One-run judgement summary produced by the shared core. Grade counts use
/// the current offset; prediction counts use [bestPerfectOffsetMs].
final class PlaybackJudgementAnalysis {
  const PlaybackJudgementAnalysis({
    required this.sampleCount,
    required this.correctKeyCount,
    required this.wrongKeyCount,
    required this.autoMissCount,
    required this.perfectCount,
    required this.greatCount,
    required this.coolCount,
    required this.badCount,
    required this.missCount,
    required this.predictedPerfectCount,
    required this.predictedGreatCount,
    required this.predictedCoolCount,
    required this.predictedBadCount,
    required this.predictedMissCount,
    required this.meanMs,
    required this.medianMs,
    required this.stddevMs,
    required this.perfectMs,
    required this.greatMs,
    required this.coolMs,
    required this.badMs,
    required this.currentOffsetMs,
    required this.bestPerfectOffsetMs,
    required this.bestPerfectCount,
    required this.bestPerfectLowMs,
    required this.bestPerfectHighMs,
    required this.histogramMinMs,
    required this.histogramStepMs,
    required this.histogram,
  });

  final int sampleCount;
  final int correctKeyCount;
  final int wrongKeyCount;
  final int autoMissCount;
  final int perfectCount;
  final int greatCount;
  final int coolCount;
  final int badCount;
  final int missCount;
  final int predictedPerfectCount;
  final int predictedGreatCount;
  final int predictedCoolCount;
  final int predictedBadCount;
  final int predictedMissCount;
  final double meanMs;
  final double medianMs;
  final double stddevMs;
  final double perfectMs;
  final double greatMs;
  final double coolMs;
  final double badMs;
  final double currentOffsetMs;
  final double bestPerfectOffsetMs;
  final int bestPerfectCount;
  final double bestPerfectLowMs;
  final double bestPerfectHighMs;
  final double histogramMinMs;
  final double histogramStepMs;

  /// Signed raw delta counts, one millisecond per bucket, starting at
  /// [histogramMinMs]. The core owns the width; treat the list as read-only.
  final List<int> histogram;
}

enum PlaybackSoundLanguage { chinese, english }

enum PlaybackDanceMode { fourKey, eightKey }

enum PlaybackSoundId {
  f1_1,
  f1_2,
  f1_3,
  f1_4,
  f1_6,
  f1_7,
  f1_8,
  f1_9,
  f1Go,
  f1Yeah,
  m1_1,
  m1_2,
  m1_3,
  m1_4,
  m1_6,
  m1_7,
  m1_8,
  m1_9,
  m1Go,
  m1Yeah,
  spaceBar,
  turn1,
  turn2,
  miss,
}

final class PlaybackSoundAsset {
  const PlaybackSoundAsset({
    required this.language,
    required this.id,
    required this.wavBytes,
  });

  final PlaybackSoundLanguage language;
  final PlaybackSoundId id;
  final Uint8List wavBytes;
}

final class NativePlaybackSoundBank {
  NativePlaybackSoundBank._(this._handle);

  factory NativePlaybackSoundBank.decode(List<PlaybackSoundAsset> assets) {
    return using((arena) {
      final nativeAssets = arena<native.otgy_playback_sound_asset>(
        assets.length,
      );
      final outBank = arena<Pointer<native.otgy_playback_sound_bank>>();
      final error = arena<Pointer<native.otgy_error>>();
      for (var index = 0; index < assets.length; index++) {
        final asset = assets[index];
        nativeAssets[index]
          ..language = asset.language.index
          ..id = asset.id.index
          ..wav_bytes = _copyBytes(asset.wavBytes, arena);
      }
      try {
        _checkStatus(
          native.otgy_playback_sound_bank_create(
            nativeAssets,
            assets.length,
            outBank,
            error,
          ),
          error,
        );
        return NativePlaybackSoundBank._(outBank.value);
      } finally {
        _destroyError(error);
      }
    });
  }

  Pointer<native.otgy_playback_sound_bank> _handle;

  Pointer<native.otgy_playback_sound_bank> _requireHandle() {
    if (_handle == nullptr) {
      throw StateError('NativePlaybackSoundBank has been disposed.');
    }
    return _handle;
  }

  void dispose() {
    if (_handle != nullptr) {
      native.otgy_playback_sound_bank_destroy(_handle);
      _handle = nullptr;
    }
  }
}

/// Owns a real-time simulation engine. The source [NativeAudio] PCM and the
/// [NativePlaybackSoundBank] must stay alive until this engine is disposed.
final class NativePlaybackEngine {
  NativePlaybackEngine._(this._handle);

  Pointer<native.otgy_playback_engine> _handle;

  /// Computes the judgement windows the engine would use at [bpm]. The tuning
  /// menu uses this to preview window widths without duplicating the timing
  /// rules in Dart.
  static PlaybackJudgementWindows windowsFor({
    required double bpm,
    required PlaybackJudgementBasis basis,
    required double perfectFraction,
    required double greatFraction,
    required double coolFraction,
    required double badFraction,
  }) {
    return using((arena) {
      final output = arena<native.otgy_playback_judgement_windows>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_playback_judgement_windows_for(
            bpm,
            basis.index,
            perfectFraction,
            greatFraction,
            coolFraction,
            badFraction,
            output,
            error,
          ),
          error,
        );
        return PlaybackJudgementWindows(
          unitMs: output.ref.unit_ms,
          perfectMs: output.ref.perfect_ms,
          greatMs: output.ref.great_ms,
          coolMs: output.ref.cool_ms,
          badMs: output.ref.bad_ms,
        );
      } finally {
        _destroyError(error);
      }
    });
  }

  /// Analyses one simulation run. The core derives the timings from the
  /// note/press timelines, so the samples are independent of the offset that
  /// graded them.
  static PlaybackJudgementAnalysis analyse({
    required List<PlaybackJudgementSample> samples,
    required int autoMissCount,
    required double bpm,
    required PlaybackJudgementConfig config,
  }) {
    return using((arena) {
      final output = arena<native.otgy_judgement_analysis>();
      final error = arena<Pointer<native.otgy_error>>();
      Pointer<native.otgy_judgement_sample> nativeSamples = nullptr;
      if (samples.isNotEmpty) {
        nativeSamples = arena<native.otgy_judgement_sample>(samples.length);
        for (var index = 0; index < samples.length; index++) {
          final sample = samples[index];
          nativeSamples[index]
            ..note_seconds = sample.noteSeconds
            ..press_seconds = sample.pressSeconds
            ..correct_key = sample.correctKey ? 1 : 0;
        }
      }
      try {
        _checkStatus(
          native.otgy_analyse_judgements(
            nativeSamples,
            samples.length,
            autoMissCount,
            bpm,
            config.basis.index,
            config.perfectFraction,
            config.greatFraction,
            config.coolFraction,
            config.badFraction,
            config.offsetMs,
            output,
            error,
          ),
          error,
        );
        final ref = output.ref;
        return PlaybackJudgementAnalysis(
          sampleCount: ref.sample_count,
          correctKeyCount: ref.correct_key_count,
          wrongKeyCount: ref.wrong_key_count,
          autoMissCount: ref.auto_miss_count,
          perfectCount: ref.perfect_count,
          greatCount: ref.great_count,
          coolCount: ref.cool_count,
          badCount: ref.bad_count,
          missCount: ref.miss_count,
          predictedPerfectCount: ref.predicted_perfect_count,
          predictedGreatCount: ref.predicted_great_count,
          predictedCoolCount: ref.predicted_cool_count,
          predictedBadCount: ref.predicted_bad_count,
          predictedMissCount: ref.predicted_miss_count,
          meanMs: ref.mean_ms,
          medianMs: ref.median_ms,
          stddevMs: ref.stddev_ms,
          perfectMs: ref.perfect_ms,
          greatMs: ref.great_ms,
          coolMs: ref.cool_ms,
          badMs: ref.bad_ms,
          currentOffsetMs: ref.current_offset_ms,
          bestPerfectOffsetMs: ref.best_perfect_offset_ms,
          bestPerfectCount: ref.best_perfect_count,
          bestPerfectLowMs: ref.best_perfect_low_ms,
          bestPerfectHighMs: ref.best_perfect_high_ms,
          histogramMinMs: ref.histogram_min_ms,
          histogramStepMs: ref.histogram_step_ms,
          histogram: List<int>.generate(
            ref.histogram_count,
            (index) => ref.histogram[index],
            growable: false,
          ),
        );
      } finally {
        _destroyError(error);
      }
    });
  }

  Pointer<native.otgy_playback_engine> _requireHandle() {
    if (_handle == nullptr) {
      throw StateError('NativePlaybackEngine has been disposed.');
    }
    return _handle;
  }

  /// Native address used by the platform streaming audio adapter.
  int get nativeHandle => _requireHandle().address;

  int get sampleRate =>
      native.otgy_playback_engine_get_sample_rate(_requireHandle());

  void pushTrigger(PlaybackSoundId sound) {
    native.otgy_playback_engine_push_trigger(_requireHandle(), sound.index);
  }

  List<PlaybackJudgement> drainJudgements({int capacity = 32}) {
    if (capacity <= 0) {
      return const <PlaybackJudgement>[];
    }
    return using((arena) {
      final output = arena<native.otgy_playback_judgement>(capacity);
      final count = arena<Uint32>();
      native.otgy_playback_engine_drain_judgements(
        _requireHandle(),
        output,
        capacity,
        count,
      );
      return <PlaybackJudgement>[
        for (var index = 0; index < count.value; index++)
          PlaybackJudgement(
            grade: PlaybackJudgementGrade.values[output[index].grade],
            deltaMs: output[index].delta_ms,
            noteId: output[index].note_id,
            expectedSound: PlaybackSoundId.values[output[index].expected_sound],
            pressedSound: PlaybackSoundId.values[output[index].pressed_sound],
            noteSeconds: output[index].note_seconds,
            pressSeconds: output[index].press_seconds,
            queueDelayMs: output[index].queue_delay_ms,
          ),
      ];
    });
  }

  PlaybackEngineState state() {
    return using((arena) {
      final output = arena<native.otgy_playback_engine_state>();
      native.otgy_playback_engine_get_state(_requireHandle(), output);
      return PlaybackEngineState(
        renderedFrames: output.ref.rendered_frames,
        positionSeconds: output.ref.position_seconds,
        finished: output.ref.finished != 0,
      );
    });
  }

  void reset() {
    native.otgy_playback_engine_reset(_requireHandle());
  }

  void dispose() {
    if (_handle != nullptr) {
      native.otgy_playback_engine_destroy(_handle);
      _handle = nullptr;
    }
  }
}

final class PlaybackPosition {
  const PlaybackPosition({
    required this.isPlaying,
    required this.timelineSeconds,
    required this.audioDurationSeconds,
  });

  final bool isPlaying;
  final double timelineSeconds;
  final double audioDurationSeconds;
}

final class NativeAudio {
  NativeAudio._(this._handle);

  static NativeAudio empty({int sampleRate = 44100}) {
    return using((arena) {
      final outPcm = arena<Pointer<native.otgy_pcm>>();
      final outError = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_pcm_copy_from_channels(
            sampleRate,
            nullptr,
            0,
            nullptr,
            0,
            outPcm,
            outError,
          ),
          outError,
        );
        return NativeAudio._(outPcm.value);
      } finally {
        _destroyError(outError);
      }
    });
  }

  Pointer<native.otgy_pcm> _handle;

  int get sampleRate => native.otgy_pcm_get_sample_rate(_requireHandle());
  int get frameCount => native.otgy_pcm_get_frame_count(_requireHandle());
  double get durationSeconds => frameCount / sampleRate;

  /// Returns a resampled copy for simulation playback at the device output
  /// rate, or null when resampling fails. The receiver stays untouched.
  NativeAudio? resampleTo(int targetSampleRate) {
    if (targetSampleRate <= 0 || targetSampleRate == sampleRate) {
      return null;
    }
    return using((arena) {
      final outPcm = arena<Pointer<native.otgy_pcm>>();
      final outError = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_pcm_resample(
            _requireHandle(),
            targetSampleRate,
            outPcm,
            outError,
          ),
          outError,
        );
        return NativeAudio._(outPcm.value);
      } on Object {
        return null;
      } finally {
        _destroyError(outError);
      }
    });
  }

  AudioSourceInfo get sourceInfo {
    return using((arena) {
      final info = arena<native.otgy_audio_source_info>();
      if (native.otgy_pcm_get_source_info(_requireHandle(), info) == 0) {
        return const AudioSourceInfo(
          codec: 0,
          measuredBitrateBps: 0,
          nominalBitrateBps: 0,
        );
      }
      return AudioSourceInfo(
        codec: info.ref.codec,
        measuredBitrateBps: info.ref.measured_bitrate_bps,
        nominalBitrateBps: info.ref.nominal_bitrate_bps,
      );
    });
  }

  Pointer<native.otgy_pcm> _requireHandle() {
    if (_handle == nullptr) {
      throw StateError('NativeAudio has been disposed.');
    }
    return _handle;
  }

  WaveformBlock generateWaveform({
    required double zoom,
    required double scale,
    required int blockIndex,
    WaveformShape shape = WaveformShape.rectified,
    WaveformLuminance luminance = WaveformLuminance.uniform,
    WaveformSampling sampling = WaveformSampling.twoX,
    WaveformFilter filter = WaveformFilter.none,
    double filterStrength = 0.75,
  }) {
    final outTile = calloc<Pointer<native.otgy_waveform_tile>>();
    final outError = calloc<Pointer<native.otgy_error>>();
    try {
      final status = native.otgy_generate_waveform_tile(
        _requireHandle(),
        zoom,
        scale,
        blockIndex,
        shape.value,
        luminance.value,
        sampling.value,
        filter.value,
        filterStrength,
        outTile,
        outError,
      );
      _checkStatus(status, outError);
      final tile = outTile.value;
      try {
        final left = native.otgy_waveform_tile_get_left(tile);
        final right = native.otgy_waveform_tile_get_right(tile);
        return WaveformBlock(
          blockIndex: native.otgy_waveform_tile_get_block_index(tile),
          pixelsPerSecond: native.otgy_waveform_tile_get_pixels_per_second(
            tile,
          ),
          channelWidth: native.otgy_waveform_tile_get_channel_width(tile),
          channelSpacing: native.otgy_waveform_tile_get_channel_spacing(tile),
          leftAlpha: Uint8List.fromList(
            left.data.cast<Uint8>().asTypedList(left.length),
          ),
          rightAlpha: Uint8List.fromList(
            right.data.cast<Uint8>().asTypedList(right.length),
          ),
        );
      } finally {
        native.otgy_waveform_tile_destroy(tile);
      }
    } finally {
      calloc.free(outTile);
      _destroyError(outError);
      calloc.free(outError);
    }
  }

  AudioExportQualityPolicy exportQualityPolicy() {
    return using((arena) {
      final policy = arena<native.otgy_audio_export_quality_policy>();
      final error = arena<Pointer<native.otgy_error>>();
      final info = _nativeSourceInfo(sourceInfo, arena);
      try {
        _checkStatus(
          native.otgy_calculate_audio_export_quality(
            sampleRate,
            info.ref,
            policy,
            error,
          ),
          error,
        );
        return AudioExportQualityPolicy(
          nominalBitratesBps: List.unmodifiable([
            for (var index = 0; index < 5; index++)
              policy.ref.nominal_bitrates_bps[index],
          ]),
          sourceBitrateBps: policy.ref.source_bitrate_bps,
          qualityCeiling: policy.ref.quality_ceiling,
          defaultQuality: policy.ref.default_quality,
        );
      } finally {
        _destroyError(error);
      }
    });
  }

  AudioAnalysis analyzeSync({void Function(SyncAnalysisStage stage)? onStage}) {
    final stageCallback = onStage == null
        ? null
        : NativeCallable<native.otgy_sync_stage_callbackFunction>.isolateLocal((
            int value,
            Pointer<Void> _,
          ) {
            final stage = SyncAnalysisStage.fromValue(value);
            if (stage != null) {
              onStage(stage);
            }
          });
    try {
      return using((arena) {
        final analysis = arena<Pointer<native.otgy_sync_analysis>>();
        final error = arena<Pointer<native.otgy_error>>();
        try {
          _checkStatus(
            native.otgy_analyze_sync(
              _requireHandle(),
              stageCallback?.nativeFunction ?? nullptr,
              nullptr,
              analysis,
              error,
            ),
            error,
          );
          final handle = analysis.value;
          final data = native.otgy_sync_analysis_get_candidates(handle);
          final count = native.otgy_sync_analysis_get_candidate_count(handle);
          return AudioAnalysis(
            analyzedFrameCount: native
                .otgy_sync_analysis_get_analyzed_frame_count(handle),
            candidates: List.unmodifiable([
              for (var index = 0; index < count; index++)
                AudioAnalysisCandidate(
                  bpm: data[index].bpm,
                  detectorOffsetSeconds: data[index].detector_offset_seconds,
                  appOffsetSeconds: data[index].app_offset_seconds,
                  fitness: data[index].fitness,
                ),
            ]),
          );
        } finally {
          if (analysis.value != nullptr) {
            native.otgy_sync_analysis_destroy(analysis.value);
          }
          _destroyError(error);
        }
      });
    } finally {
      stageCallback?.close();
    }
  }

  AudioExportResult exportOgg({
    required double offset,
    required int quality,
    required String baseName,
    double bpm = 100,
    AudioEndMode endMode = AudioEndMode.sourceEnd,
    int endRow = 0,
    String title = '',
    String artist = '',
    String album = '',
    AudioExportCancellation? cancellation,
    void Function(AudioExportProgress progress)? onProgress,
  }) {
    final progressCallback = onProgress == null
        ? null
        : NativeCallable<
            native.otgy_audio_export_progress_callbackFunction
          >.isolateLocal((
            int stage,
            int completedUnits,
            int totalUnits,
            Pointer<Void> _,
          ) {
            final decodedStage = AudioExportStage.fromValue(stage);
            if (decodedStage != null) {
              onProgress(
                AudioExportProgress(
                  stage: decodedStage,
                  completedUnits: completedUnits,
                  totalUnits: totalUnits,
                ),
              );
            }
          });
    final cancelCallback = cancellation == null
        ? null
        : NativeCallable<native.otgy_cancel_callbackFunction>.isolateLocal(
            (Pointer<Void> _) => cancellation.isRequested ? 1 : 0,
            exceptionalReturn: 1,
          );
    try {
      return using((arena) {
        final snapshot = arena<Pointer<native.otgy_audio_export_snapshot>>();
        final output = arena<Pointer<native.otgy_audio_export_output>>();
        final error = arena<Pointer<native.otgy_error>>();
        final requestedName = _copyString(baseName, arena);
        final metadata = arena<native.otgy_audio_export_metadata>()
          ..ref.title = _copyString(title, arena)
          ..ref.artist = _copyString(artist, arena)
          ..ref.album = _copyString(album, arena);
        final info = _nativeSourceInfo(sourceInfo, arena);
        try {
          if (endMode == AudioEndMode.beat) {
            _checkStatus(
              native.otgy_audio_export_snapshot_create_with_end(
                _requireHandle(),
                1,
                offset,
                bpm,
                native.OTGY_AUDIO_END_MODE_BEAT,
                endRow,
                quality,
                info.ref,
                requestedName,
                metadata.ref,
                snapshot,
                error,
              ),
              error,
            );
          } else {
            _checkStatus(
              native.otgy_audio_export_snapshot_create(
                _requireHandle(),
                1,
                offset,
                quality,
                info.ref,
                requestedName,
                metadata.ref,
                snapshot,
                error,
              ),
              error,
            );
          }
          _checkStatus(
            native.otgy_audio_export_snapshot_encode(
              snapshot.value,
              progressCallback?.nativeFunction ?? nullptr,
              cancelCallback?.nativeFunction ?? nullptr,
              nullptr,
              output,
              error,
            ),
            error,
          );
          final handle = output.value;
          final bytes = native.otgy_audio_export_output_get_bytes(handle);
          return AudioExportResult(
            bytes: Uint8List.fromList(
              bytes.data.cast<Uint8>().asTypedList(bytes.length),
            ),
            signedShiftFrames: native
                .otgy_audio_export_output_get_signed_shift_frames(handle),
            frameCount: native.otgy_audio_export_output_get_frame_count(handle),
          );
        } finally {
          if (output.value != nullptr) {
            native.otgy_audio_export_output_destroy(output.value);
          }
          if (snapshot.value != nullptr) {
            native.otgy_audio_export_snapshot_destroy(snapshot.value);
          }
          _destroyError(error);
        }
      });
    } finally {
      progressCallback?.close();
      cancelCallback?.close();
    }
  }

  PlaybackPcm renderPlayback(double timelineStartSeconds) {
    return using((arena) {
      final output = arena<Pointer<native.otgy_playback_pcm>>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_render_playback_pcm(
            _requireHandle(),
            timelineStartSeconds,
            output,
            error,
          ),
          error,
        );
        final handle = output.value;
        final bytes = native.otgy_playback_pcm_get_bytes(handle);
        return PlaybackPcm(
          sampleRate: native.otgy_playback_pcm_get_sample_rate(handle),
          frameCount: native.otgy_playback_pcm_get_frame_count(handle),
          interleavedPcm16le: bytes.length == 0
              ? Uint8List(0)
              : Uint8List.fromList(
                  bytes.data.cast<Uint8>().asTypedList(bytes.length),
                ),
        );
      } finally {
        if (output.value != nullptr) {
          native.otgy_playback_pcm_destroy(output.value);
        }
        _destroyError(error);
      }
    });
  }

  PlaybackPcm renderChartPlayback({
    required NativeChartEditor editor,
    required NativePlaybackSoundBank soundBank,
    required PlaybackSoundLanguage language,
    required PlaybackDanceMode danceMode,
    required bool transitionSoundsEnabled,
    required int randomSeed,
    required double bpm,
    required double offset,
    required double timelineStartSeconds,
  }) {
    return using((arena) {
      final output = arena<Pointer<native.otgy_playback_pcm>>();
      final error = arena<Pointer<native.otgy_error>>();
      final config = arena<native.otgy_playback_sound_config>()
        ..ref.language = language.index
        ..ref.dance_mode = danceMode.index
        ..ref.transition_sounds_enabled = transitionSoundsEnabled ? 1 : 0
        ..ref.random_seed = randomSeed
        ..ref.bpm = bpm
        ..ref.offset_seconds = offset;
      Pointer<native.otgy_chart> chart = nullptr;
      try {
        chart = editor._copyChart(error);
        _checkStatus(
          native.otgy_render_chart_playback_pcm(
            _requireHandle(),
            chart,
            soundBank._requireHandle(),
            config.ref,
            timelineStartSeconds,
            output,
            error,
          ),
          error,
        );
        final handle = output.value;
        final bytes = native.otgy_playback_pcm_get_bytes(handle);
        return PlaybackPcm(
          sampleRate: native.otgy_playback_pcm_get_sample_rate(handle),
          frameCount: native.otgy_playback_pcm_get_frame_count(handle),
          interleavedPcm16le: bytes.length == 0
              ? Uint8List(0)
              : Uint8List.fromList(
                  bytes.data.cast<Uint8>().asTypedList(bytes.length),
                ),
        );
      } finally {
        if (output.value != nullptr) {
          native.otgy_playback_pcm_destroy(output.value);
        }
        if (chart != nullptr) {
          native.otgy_chart_destroy(chart);
        }
        _destroyError(error);
      }
    });
  }

  PlaybackPcm renderRhythmTestPlayback({
    required NativePlaybackSoundBank soundBank,
    required PlaybackSoundLanguage language,
    required double bpm,
    required double offset,
    required double timelineStartSeconds,
  }) {
    return using((arena) {
      final output = arena<Pointer<native.otgy_playback_pcm>>();
      final error = arena<Pointer<native.otgy_error>>();
      final config = arena<native.otgy_rhythm_test_sound_config>()
        ..ref.language = language.index
        ..ref.bpm = bpm
        ..ref.offset_seconds = offset;
      try {
        _checkStatus(
          native.otgy_render_rhythm_test_playback_pcm(
            _requireHandle(),
            soundBank._requireHandle(),
            config.ref,
            timelineStartSeconds,
            output,
            error,
          ),
          error,
        );
        final handle = output.value;
        final bytes = native.otgy_playback_pcm_get_bytes(handle);
        return PlaybackPcm(
          sampleRate: native.otgy_playback_pcm_get_sample_rate(handle),
          frameCount: native.otgy_playback_pcm_get_frame_count(handle),
          interleavedPcm16le: bytes.length == 0
              ? Uint8List(0)
              : Uint8List.fromList(
                  bytes.data.cast<Uint8>().asTypedList(bytes.length),
                ),
        );
      } finally {
        if (output.value != nullptr) {
          native.otgy_playback_pcm_destroy(output.value);
        }
        _destroyError(error);
      }
    });
  }

  NativePlaybackEngine createPlaybackEngine({
    required NativeChartEditor editor,
    required NativePlaybackSoundBank soundBank,
    required PlaybackSoundLanguage language,
    required PlaybackDanceMode danceMode,
    required bool transitionSoundsEnabled,
    required int randomSeed,
    required double bpm,
    required double offset,
    required double startSeconds,
    required double endSeconds,
    PlaybackJudgementConfig judgement = const PlaybackJudgementConfig(),
    bool judgementEnabled = true,
  }) {
    return using((arena) {
      final output = arena<Pointer<native.otgy_playback_engine>>();
      final error = arena<Pointer<native.otgy_error>>();
      final config = arena<native.otgy_playback_engine_config>()
        ..ref.language = language.index
        ..ref.dance_mode = danceMode.index
        ..ref.transition_sounds_enabled = transitionSoundsEnabled ? 1 : 0
        ..ref.random_seed = randomSeed
        ..ref.bpm = bpm
        ..ref.offset_seconds = offset
        ..ref.start_seconds = startSeconds
        ..ref.end_seconds = endSeconds
        ..ref.judgement_basis = judgement.basis.index
        ..ref.judgement_perfect_fraction = judgement.perfectFraction
        ..ref.judgement_great_fraction = judgement.greatFraction
        ..ref.judgement_cool_fraction = judgement.coolFraction
        ..ref.judgement_bad_fraction = judgement.badFraction
        ..ref.judgement_offset_ms = judgement.offsetMs
        ..ref.judgement_enabled = judgementEnabled ? 1 : 0;
      Pointer<native.otgy_chart> chart = nullptr;
      try {
        chart = editor._copyChart(error);
        _checkStatus(
          native.otgy_playback_engine_create(
            _requireHandle(),
            chart,
            soundBank._requireHandle(),
            config.ref,
            output,
            error,
          ),
          error,
        );
        return NativePlaybackEngine._(output.value);
      } finally {
        if (chart != nullptr) {
          native.otgy_chart_destroy(chart);
        }
        _destroyError(error);
      }
    });
  }

  void dispose() {
    if (_handle != nullptr) {
      native.otgy_pcm_destroy(_handle);
      _handle = nullptr;
    }
  }
}

final class NativeChartEditor {
  NativeChartEditor._(this._editor);

  Pointer<native.otgy_chart_editor> _editor;

  static NativeChartEditor createEmpty() {
    final outChart = calloc<Pointer<native.otgy_chart>>();
    final outEditor = calloc<Pointer<native.otgy_chart_editor>>();
    final outError = calloc<Pointer<native.otgy_error>>();
    try {
      _checkStatus(
        native.otgy_chart_create_empty(outChart, outError),
        outError,
      );
      _checkStatus(
        native.otgy_chart_editor_create(outChart.value, outEditor, outError),
        outError,
      );
      return NativeChartEditor._(outEditor.value);
    } finally {
      if (outChart.value != nullptr) {
        native.otgy_chart_destroy(outChart.value);
      }
      calloc.free(outChart);
      calloc.free(outEditor);
      _destroyError(outError);
      calloc.free(outError);
    }
  }

  static DecodedFormalDraft decodeFormal(Uint8List bytes) =>
      _decodeFormalDraft(bytes, repair: false);

  static DecodedFormalDraft decodeFormalWithRepair(Uint8List bytes) =>
      _decodeFormalDraft(bytes, repair: true);

  static DecodedFormalDraft _decodeFormalDraft(
    Uint8List bytes, {
    required bool repair,
  }) {
    return using((arena) {
      final input = _copyBytes(bytes, arena);
      final outChart = arena<Pointer<native.otgy_chart>>();
      final outEditor = arena<Pointer<native.otgy_chart_editor>>();
      final outChanged = arena<Uint8>();
      final outError = arena<Pointer<native.otgy_error>>();
      var editorTransferred = false;
      try {
        _checkStatus(
          native.otgy_project_decode_formal_draft(input, outChart, outError),
          outError,
        );
        _checkStatus(
          native.otgy_chart_editor_create(outChart.value, outEditor, outError),
          outError,
        );
        var repaired = false;
        if (repair) {
          _checkStatus(
            native.otgy_chart_editor_repair_relations(
              outEditor.value,
              outChanged,
              outError,
            ),
            outError,
          );
          repaired = outChanged.value != 0;
        }
        final editor = NativeChartEditor._(outEditor.value);
        editorTransferred = true;
        return DecodedFormalDraft(editor: editor, repaired: repaired);
      } finally {
        if (!editorTransferred && outEditor.value != nullptr) {
          native.otgy_chart_editor_destroy(outEditor.value);
        }
        if (outChart.value != nullptr) {
          native.otgy_chart_destroy(outChart.value);
        }
        _destroyError(outError);
      }
    });
  }

  static NativeChartEditor decodeRecovery(Uint8List bytes) =>
      _decodeRecoveryDraft(bytes, repair: false).editor;

  static DecodedRecovery decodeRecoveryWithRepair(Uint8List bytes) =>
      _decodeRecoveryDraft(bytes, repair: true);

  static DecodedRecovery _decodeRecoveryDraft(
    Uint8List bytes, {
    required bool repair,
  }) {
    return using((arena) {
      final input = _copyBytes(bytes, arena);
      final outChart = arena<Pointer<native.otgy_chart>>();
      final outEditor = arena<Pointer<native.otgy_chart_editor>>();
      final outChanged = arena<Uint8>();
      final outError = arena<Pointer<native.otgy_error>>();
      var editorTransferred = false;
      try {
        _checkStatus(
          native.otgy_project_decode_recovery_snapshot(
            input,
            outChart,
            outError,
          ),
          outError,
        );
        _checkStatus(
          native.otgy_chart_editor_create(outChart.value, outEditor, outError),
          outError,
        );
        var repaired = false;
        if (repair) {
          _checkStatus(
            native.otgy_chart_editor_repair_relations(
              outEditor.value,
              outChanged,
              outError,
            ),
            outError,
          );
          repaired = outChanged.value != 0;
        }
        final editor = NativeChartEditor._(outEditor.value);
        editorTransferred = true;
        return DecodedRecovery(editor: editor, repaired: repaired);
      } finally {
        if (!editorTransferred && outEditor.value != nullptr) {
          native.otgy_chart_editor_destroy(outEditor.value);
        }
        if (outChart.value != nullptr) {
          native.otgy_chart_destroy(outChart.value);
        }
        _destroyError(outError);
      }
    });
  }

  static NativeChartEditor importSlk(Uint8List bytes) {
    return using((arena) {
      final input = _copyBytes(bytes, arena);
      final document = arena<Pointer<native.otgy_slk_document>>();
      final chart = arena<Pointer<native.otgy_chart>>();
      final editor = arena<Pointer<native.otgy_chart_editor>>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(native.otgy_slk_import(input, document, error), error);
        _checkStatus(
          native.otgy_slk_document_project_chart(document.value, chart, error),
          error,
        );
        _checkStatus(
          native.otgy_chart_editor_create(chart.value, editor, error),
          error,
        );
        return NativeChartEditor._(editor.value);
      } finally {
        if (chart.value != nullptr) {
          native.otgy_chart_destroy(chart.value);
        }
        if (document.value != nullptr) {
          native.otgy_slk_document_destroy(document.value);
        }
        _destroyError(error);
      }
    });
  }

  Pointer<native.otgy_chart_editor> _requireEditor() {
    if (_editor == nullptr) {
      throw StateError('NativeChartEditor has been disposed.');
    }
    return _editor;
  }

  bool get canUndo => native.otgy_chart_editor_can_undo(_requireEditor()) != 0;
  bool get canRedo => native.otgy_chart_editor_can_redo(_requireEditor()) != 0;

  List<ChartSelection> get selections => using((arena) {
    final result = arena<Pointer<native.otgy_chart_selections>>();
    final error = arena<Pointer<native.otgy_error>>();
    try {
      _checkStatus(
        native.otgy_chart_editor_copy_selections(
          _requireEditor(),
          result,
          error,
        ),
        error,
      );
      final handle = result.value;
      final data = native.otgy_chart_selections_get_data(handle);
      return List<ChartSelection>.unmodifiable([
        for (var i = 0; i < native.otgy_chart_selections_get_count(handle); i++)
          ChartSelection._fromNative(data[i]),
      ]);
    } finally {
      native.otgy_chart_selections_destroy(result.value);
      _destroyError(error);
    }
  });

  int get selectionCount =>
      native.otgy_chart_editor_selection_count(_requireEditor());
  bool get hasSelection => selectionCount != 0;
  bool get hasClipboard =>
      native.otgy_chart_editor_has_clipboard(_requireEditor()) != 0;
  int get clipboardCount =>
      native.otgy_chart_editor_clipboard_count(_requireEditor());
  int get clipboardMarkerCount =>
      native.otgy_chart_editor_clipboard_marker_count(_requireEditor());
  bool get cutPending =>
      native.otgy_chart_editor_cut_pending(_requireEditor()) != 0;

  ChartPastePreview pastePreview(int anchorRow) => using((arena) {
    final result = arena<Pointer<native.otgy_chart_paste_preview>>();
    final error = arena<Pointer<native.otgy_error>>();
    try {
      _checkStatus(
        native.otgy_chart_editor_preview_paste(
          _requireEditor(),
          anchorRow,
          result,
          error,
        ),
        error,
      );
      final handle = result.value;
      final selections = native.otgy_chart_paste_preview_get_selections(handle);
      final markers = native.otgy_chart_paste_preview_get_markers(handle);
      return ChartPastePreview(
        code: native.otgy_chart_paste_preview_get_code(handle),
        firstRow: native.otgy_chart_paste_preview_get_first_row(handle),
        lastRow: native.otgy_chart_paste_preview_get_last_row(handle),
        selections: [
          for (
            var i = 0;
            i < native.otgy_chart_paste_preview_get_selection_count(handle);
            i++
          )
            ChartSelection._fromNative(selections[i]),
        ],
        markers: [
          for (
            var i = 0;
            i < native.otgy_chart_paste_preview_get_marker_count(handle);
            i++
          )
            ChartMarker(
              id: markers[i].id,
              row: markers[i].row,
              column: markers[i].column,
              type: markers[i].type,
              showNCount: markers[i].show_n_count,
            ),
        ],
      );
    } finally {
      native.otgy_chart_paste_preview_destroy(result.value);
      _destroyError(error);
    }
  });

  SessionResult select(int column, int row) => _session(
    (result, error) => native.otgy_chart_editor_select(
      _requireEditor(),
      column,
      row,
      result,
      error,
    ),
  );

  SessionResult selectAll() => _session(
    (result, error) =>
        native.otgy_chart_editor_select_all(_requireEditor(), result, error),
  );

  SessionResult selectRange(
    int firstRow,
    int lastRow,
    ChartSelectionMode mode,
  ) => _session(
    (result, error) => native.otgy_chart_editor_select_range(
      _requireEditor(),
      firstRow,
      lastRow,
      mode.value,
      result,
      error,
    ),
  );

  EditResult deleteSelection() => _edit(
    (result, error) => native.otgy_chart_editor_delete_selection(
      _requireEditor(),
      result,
      error,
    ),
  );

  SessionResult clearSelection() => _session(
    (result, error) => native.otgy_chart_editor_clear_selection(
      _requireEditor(),
      result,
      error,
    ),
  );

  SessionResult copySelection() => _session(
    (result, error) => native.otgy_chart_editor_copy_selection(
      _requireEditor(),
      result,
      error,
    ),
  );

  SessionResult clearClipboard() => _session(
    (result, error) => native.otgy_chart_editor_clear_clipboard(
      _requireEditor(),
      result,
      error,
    ),
  );

  SessionResult _session(
    int Function(
      Pointer<native.otgy_chart_session_result>,
      Pointer<Pointer<native.otgy_error>>,
    )
    operation,
  ) {
    return using((arena) {
      final result = arena<native.otgy_chart_session_result>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(operation(result, error), error);
        return SessionResult(
          code: result.ref.code,
          changed: result.ref.changed != 0,
        );
      } finally {
        _destroyError(error);
      }
    });
  }

  SessionResult cutSelection() => _session(
    (result, error) =>
        native.otgy_chart_editor_cut_selection(_requireEditor(), result, error),
  );

  EditResult paste(int anchorRow) => _edit(
    (result, error) => native.otgy_chart_editor_paste(
      _requireEditor(),
      anchorRow,
      result,
      error,
    ),
  );

  EditResult click(int column, int row) => using((arena) {
    final result = arena<native.otgy_chart_edit_result>();
    final cleared = arena<Uint8>();
    final error = arena<Pointer<native.otgy_error>>();
    try {
      _checkStatus(
        native.otgy_chart_editor_click_with_effects(
          _requireEditor(),
          column,
          row,
          result,
          cleared,
          error,
        ),
        error,
      );
      return EditResult(
        code: result.ref.code,
        changed: result.ref.changed != 0,
        primaryEventId: result.ref.primary_event_id,
        playerActionsCleared: cleared.value != 0,
      );
    } finally {
      _destroyError(error);
    }
  });

  EditResult placeMarker(int column, ChartNpcMarker marker, int row) =>
      using((arena) {
        final result = arena<native.otgy_chart_edit_result>();
        final cleared = arena<Uint8>();
        final error = arena<Pointer<native.otgy_error>>();
        try {
          _checkStatus(
            native.otgy_chart_editor_place_marker(
              _requireEditor(),
              column,
              row,
              marker.value,
              result,
              cleared,
              error,
            ),
            error,
          );
          return EditResult(
            code: result.ref.code,
            changed: result.ref.changed != 0,
            primaryEventId: result.ref.primary_event_id,
            playerActionsCleared: cleared.value != 0,
          );
        } finally {
          _destroyError(error);
        }
      });

  EditResult removeMarker(int column, int row) => using((arena) {
    final result = arena<native.otgy_chart_edit_result>();
    final cleared = arena<Uint8>();
    final error = arena<Pointer<native.otgy_error>>();
    try {
      _checkStatus(
        native.otgy_chart_editor_remove_marker(
          _requireEditor(),
          column,
          row,
          result,
          cleared,
          error,
        ),
        error,
      );
      return EditResult(
        code: result.ref.code,
        changed: result.ref.changed != 0,
        primaryEventId: result.ref.primary_event_id,
        playerActionsCleared: cleared.value != 0,
      );
    } finally {
      _destroyError(error);
    }
  });

  EditResult clear() => _edit(
    (result, error) =>
        native.otgy_chart_editor_clear(_requireEditor(), result, error),
  );

  EditResult undo() => _edit(
    (result, error) =>
        native.otgy_chart_editor_undo(_requireEditor(), result, error),
  );

  EditResult redo() => _edit(
    (result, error) =>
        native.otgy_chart_editor_redo(_requireEditor(), result, error),
  );

  EditResult _edit(
    int Function(
      Pointer<native.otgy_chart_edit_result>,
      Pointer<Pointer<native.otgy_error>>,
    )
    operation,
  ) {
    return using((arena) {
      final result = arena<native.otgy_chart_edit_result>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(operation(result, error), error);
        return EditResult(
          code: result.ref.code,
          changed: result.ref.changed != 0,
          primaryEventId: result.ref.primary_event_id,
        );
      } finally {
        _destroyError(error);
      }
    });
  }

  Pointer<native.otgy_chart> _copyChart(
    Pointer<Pointer<native.otgy_error>> error,
  ) {
    final outChart = calloc<Pointer<native.otgy_chart>>();
    try {
      _checkStatus(
        native.otgy_chart_editor_copy_chart(_requireEditor(), outChart, error),
        error,
      );
      return outChart.value;
    } finally {
      calloc.free(outChart);
    }
  }

  List<ChartMarker> markers() {
    final outMarkers = calloc<Pointer<native.otgy_chart_markers>>();
    final error = calloc<Pointer<native.otgy_error>>();
    Pointer<native.otgy_chart> chart = nullptr;
    try {
      chart = _copyChart(error);
      _checkStatus(
        native.otgy_chart_project_markers(chart, outMarkers, error),
        error,
      );
      final handle = outMarkers.value;
      final count = native.otgy_chart_markers_get_count(handle);
      final data = native.otgy_chart_markers_get_data(handle);
      final result = <ChartMarker>[];
      for (var index = 0; index < count; index++) {
        final item = data[index];
        int? ordinal;
        if (item.type == native.OTGY_CHART_MARKER_NPC_NOTE ||
            item.type == native.OTGY_CHART_MARKER_PLAYER_NOTE) {
          final outOrdinal = calloc<Uint64>();
          try {
            _checkStatus(
              native.otgy_chart_note_ordinal(chart, item.id, outOrdinal, error),
              error,
            );
            ordinal = outOrdinal.value;
          } finally {
            calloc.free(outOrdinal);
          }
        }
        result.add(
          ChartMarker(
            id: item.id,
            row: item.row,
            column: item.column,
            type: item.type,
            showNCount: item.show_n_count,
            ordinal: ordinal,
          ),
        );
      }
      return result;
    } finally {
      if (outMarkers.value != nullptr) {
        native.otgy_chart_markers_destroy(outMarkers.value);
      }
      if (chart != nullptr) {
        native.otgy_chart_destroy(chart);
      }
      calloc.free(outMarkers);
      _destroyError(error);
      calloc.free(error);
    }
  }

  List<ShowtimeMove> showtimeMoves() {
    final outMoves = calloc<Pointer<native.otgy_showtime_moves>>();
    final error = calloc<Pointer<native.otgy_error>>();
    Pointer<native.otgy_chart> chart = nullptr;
    try {
      chart = _copyChart(error);
      _checkStatus(
        native.otgy_chart_project_showtime_moves(chart, outMoves, error),
        error,
      );
      final handle = outMoves.value;
      final count = native.otgy_showtime_moves_get_count(handle);
      final data = native.otgy_showtime_moves_get_data(handle);
      return [
        for (var index = 0; index < count; index++)
          ShowtimeMove(
            eventId: data[index].event_id,
            row: data[index].row,
            showNCount: data[index].show_n_count,
            rangeStart: data[index].range_start,
            rangeEnd: data[index].range_end,
            isFirst: data[index].is_first_in_sequence != 0,
            isLast: data[index].is_last_in_sequence != 0,
          ),
      ];
    } finally {
      if (outMoves.value != nullptr) {
        native.otgy_showtime_moves_destroy(outMoves.value);
      }
      if (chart != nullptr) {
        native.otgy_chart_destroy(chart);
      }
      calloc.free(outMoves);
      _destroyError(error);
      calloc.free(error);
    }
  }

  TimelineRange timelineRange({
    required double bpm,
    required double offset,
    double? audioEndSeconds,
    int? audioEndRow,
  }) {
    return using((arena) {
      final range = arena<native.otgy_timeline_range>();
      final error = arena<Pointer<native.otgy_error>>();
      final timing = arena<native.otgy_timing_parameters>()
        ..ref.bpm = bpm
        ..ref.offset_seconds = offset;
      Pointer<native.otgy_chart> chart = nullptr;
      try {
        chart = _copyChart(error);
        if (audioEndRow == null) {
          _checkStatus(
            native.otgy_chart_calculate_timeline_range(
              chart,
              timing.ref,
              audioEndSeconds == null ? 0 : 1,
              audioEndSeconds ?? 0,
              range,
              error,
            ),
            error,
          );
        } else {
          _checkStatus(
            native.otgy_chart_calculate_timeline_range_with_end(
              chart,
              timing.ref,
              audioEndSeconds == null ? 0 : 1,
              audioEndSeconds ?? 0,
              1,
              audioEndRow,
              range,
              error,
            ),
            error,
          );
        }
        return TimelineRange(
          beginSeconds: range.ref.begin_seconds,
          endSeconds: range.ref.end_seconds,
          endRow: range.ref.end_sixteenth_row,
          usesZeroBpmProjection: range.ref.uses_zero_bpm_projection != 0,
        );
      } finally {
        if (chart != nullptr) {
          native.otgy_chart_destroy(chart);
        }
        _destroyError(error);
      }
    });
  }

  Uint8List encodeFormal() {
    return using((arena) {
      final outBytes = arena<Pointer<native.otgy_bytes>>();
      final error = arena<Pointer<native.otgy_error>>();
      Pointer<native.otgy_chart> chart = nullptr;
      try {
        chart = _copyChart(error);
        _checkStatus(
          native.otgy_project_encode_formal_draft(chart, outBytes, error),
          error,
        );
        return _copyOwnedBytes(outBytes.value);
      } finally {
        if (outBytes.value != nullptr) {
          native.otgy_bytes_destroy(outBytes.value);
        }
        if (chart != nullptr) {
          native.otgy_chart_destroy(chart);
        }
        _destroyError(error);
      }
    });
  }

  Uint8List encodeRecovery() {
    return using((arena) {
      final outBytes = arena<Pointer<native.otgy_bytes>>();
      final error = arena<Pointer<native.otgy_error>>();
      Pointer<native.otgy_chart> chart = nullptr;
      try {
        chart = _copyChart(error);
        _checkStatus(
          native.otgy_project_encode_recovery_snapshot(chart, outBytes, error),
          error,
        );
        return _copyOwnedBytes(outBytes.value);
      } finally {
        if (outBytes.value != nullptr) {
          native.otgy_bytes_destroy(outBytes.value);
        }
        if (chart != nullptr) {
          native.otgy_chart_destroy(chart);
        }
        _destroyError(error);
      }
    });
  }

  SlkExport exportSlk() {
    return using((arena) {
      final validation = arena<Pointer<native.otgy_chart_validation>>();
      final finished = arena<Pointer<native.otgy_finished_chart>>();
      final outBytes = arena<Pointer<native.otgy_bytes>>();
      final error = arena<Pointer<native.otgy_error>>();
      Pointer<native.otgy_chart> chart = nullptr;
      try {
        chart = _copyChart(error);
        _checkStatus(
          native.otgy_chart_validate_finished(chart, validation, error),
          error,
        );
        final count = native.otgy_chart_validation_get_diagnostic_count(
          validation.value,
        );
        final data = native.otgy_chart_validation_get_diagnostics(
          validation.value,
        );
        final diagnostics = List<ChartDiagnostic>.unmodifiable([
          for (var index = 0; index < count; index++)
            ChartDiagnostic(
              code: data[index].code,
              objectId: data[index].object_id,
              relatedId: data[index].related_id,
              row: data[index].row,
            ),
        ]);
        if (native.otgy_chart_validation_is_finished(validation.value) == 0) {
          return SlkExport(diagnostics: diagnostics);
        }
        _checkStatus(
          native.otgy_chart_validation_copy_finished(
            validation.value,
            finished,
            error,
          ),
          error,
        );
        _checkStatus(
          native.otgy_finished_chart_export_slk(
            finished.value,
            outBytes,
            error,
          ),
          error,
        );
        return SlkExport(
          diagnostics: diagnostics,
          bytes: _copyOwnedBytes(outBytes.value),
        );
      } finally {
        if (outBytes.value != nullptr) {
          native.otgy_bytes_destroy(outBytes.value);
        }
        if (finished.value != nullptr) {
          native.otgy_finished_chart_destroy(finished.value);
        }
        if (validation.value != nullptr) {
          native.otgy_chart_validation_destroy(validation.value);
        }
        if (chart != nullptr) {
          native.otgy_chart_destroy(chart);
        }
        _destroyError(error);
      }
    });
  }

  void dispose() {
    if (_editor != nullptr) {
      native.otgy_chart_editor_destroy(_editor);
      _editor = nullptr;
    }
  }
}

abstract final class OtgyNative {
  static int get apiVersion => native.otgy_c_api_version();

  static String get coreVersion {
    final view = native.otgy_core_version();
    return utf8.decode(view.data.cast<Uint8>().asTypedList(view.length));
  }

  static NativeAudio decodeAudio(Uint8List bytes, String extension) {
    return using((arena) {
      final input = _copyBytes(bytes, arena);
      final outPcm = arena<Pointer<native.otgy_pcm>>();
      final outError = arena<Pointer<native.otgy_error>>();
      try {
        final status = switch (extension.toLowerCase()) {
          '.mp3' => native.otgy_decode_mp3(input, outPcm, outError),
          '.ogg' => native.otgy_decode_ogg_vorbis(input, outPcm, outError),
          _ => throw const FormatException('Only MP3 and Ogg are supported.'),
        };
        _checkStatus(status, outError);
        return NativeAudio._(outPcm.value);
      } finally {
        _destroyError(outError);
      }
    });
  }

  static String defaultAudioExportBaseName(String sourceFileName) {
    return using((arena) {
      final output = arena<Pointer<native.otgy_bytes>>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_default_audio_export_base_name(
            _copyString(sourceFileName, arena),
            output,
            error,
          ),
          error,
        );
        return utf8.decode(_copyOwnedBytes(output.value));
      } finally {
        if (output.value != nullptr) {
          native.otgy_bytes_destroy(output.value);
        }
        _destroyError(error);
      }
    });
  }

  static String makeAudioExportFileName({
    required String baseName,
    required DateTime localTime,
    int collisionIndex = 1,
  }) {
    return using((arena) {
      final output = arena<Pointer<native.otgy_bytes>>();
      final error = arena<Pointer<native.otgy_error>>();
      final time = arena<native.otgy_audio_export_local_time>()
        ..ref.year = localTime.year
        ..ref.month = localTime.month
        ..ref.day = localTime.day
        ..ref.hour = localTime.hour
        ..ref.minute = localTime.minute
        ..ref.second = localTime.second;
      try {
        _checkStatus(
          native.otgy_make_audio_export_file_name(
            _copyString(baseName, arena),
            time.ref,
            collisionIndex,
            output,
            error,
          ),
          error,
        );
        return utf8.decode(_copyOwnedBytes(output.value));
      } finally {
        if (output.value != nullptr) {
          native.otgy_bytes_destroy(output.value);
        }
        _destroyError(error);
      }
    });
  }

  static int waveformPixelsPerSecond(double zoom, double scale) {
    final empty = NativeAudio.empty();
    try {
      return empty
          .generateWaveform(zoom: zoom, scale: scale, blockIndex: 0)
          .pixelsPerSecond;
    } finally {
      empty.dispose();
    }
  }

  static double adjustOffsetByBeats({
    required double bpm,
    required double baselineOffset,
    required double cumulativeBeats,
  }) {
    return using((arena) {
      final baseline = arena<native.otgy_timing_parameters>()
        ..ref.bpm = bpm
        ..ref.offset_seconds = baselineOffset;
      final result = arena<Double>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_adjust_offset_by_beats(
            baseline.ref,
            cumulativeBeats,
            result,
            error,
          ),
          error,
        );
        return result.value;
      } finally {
        _destroyError(error);
      }
    });
  }

  static int nearestWholeBeatOffset({
    required double bpm,
    required double offsetSeconds,
  }) {
    return using((arena) {
      final baseline = arena<native.otgy_timing_parameters>()
        ..ref.bpm = bpm
        ..ref.offset_seconds = offsetSeconds;
      final result = arena<Int64>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_nearest_whole_beat_offset(baseline.ref, result, error),
          error,
        );
        return result.value;
      } finally {
        _destroyError(error);
      }
    });
  }

  static double sixteenthRowToSeconds({
    required double bpm,
    required double offset,
    required double row,
  }) {
    return using((arena) {
      final timing = arena<native.otgy_timing_parameters>()
        ..ref.bpm = bpm
        ..ref.offset_seconds = offset;
      final result = arena<Double>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_sixteenth_row_to_seconds(timing.ref, row, result, error),
          error,
        );
        return result.value;
      } finally {
        _destroyError(error);
      }
    });
  }

  static double secondsToSixteenthRow({
    required double bpm,
    required double offset,
    required double seconds,
  }) {
    return using((arena) {
      final timing = arena<native.otgy_timing_parameters>()
        ..ref.bpm = bpm
        ..ref.offset_seconds = offset;
      final result = arena<Double>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_seconds_to_sixteenth_row(
            timing.ref,
            seconds,
            result,
            error,
          ),
          error,
        );
        return result.value;
      } finally {
        _destroyError(error);
      }
    });
  }

  static double audioEndExportSeconds({
    required AudioEndMode mode,
    required int endRow,
    required double bpm,
    required double offset,
    required double audioDurationSeconds,
  }) {
    return using((arena) {
      final result = arena<Double>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_calculate_audio_end_export_seconds(
            mode == AudioEndMode.beat
                ? native.OTGY_AUDIO_END_MODE_BEAT
                : native.OTGY_AUDIO_END_MODE_SOURCE_END,
            endRow,
            bpm,
            offset,
            audioDurationSeconds,
            result,
            error,
          ),
          error,
        );
        return result.value;
      } finally {
        _destroyError(error);
      }
    });
  }

  static int playbackFrameLimit({
    required int sampleRate,
    required double keepSeconds,
  }) {
    return using((arena) {
      final result = arena<Uint64>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_calculate_playback_frame_limit(
            sampleRate,
            keepSeconds,
            result,
            error,
          ),
          error,
        );
        return result.value;
      } finally {
        _destroyError(error);
      }
    });
  }

  static PlaybackPosition startPlayback({
    required double audioDurationSeconds,
    required double cursorTimelineSeconds,
  }) {
    return using((arena) {
      final result = arena<native.otgy_playback_position>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_start_playback(
            1,
            audioDurationSeconds,
            cursorTimelineSeconds,
            result,
            error,
          ),
          error,
        );
        return _playbackPosition(result.ref);
      } finally {
        _destroyError(error);
      }
    });
  }

  static PlaybackPosition advancePlaybackFrames({
    required PlaybackPosition start,
    required int playedFrames,
    required int sampleRate,
  }) {
    return using((arena) {
      final result = arena<native.otgy_playback_position>();
      final error = arena<Pointer<native.otgy_error>>();
      final nativeStart = arena<native.otgy_playback_position>()
        ..ref.phase = start.isPlaying ? 1 : 0
        ..ref.timeline_seconds = start.timelineSeconds
        ..ref.audio_duration_seconds = start.audioDurationSeconds;
      try {
        _checkStatus(
          native.otgy_advance_playback_frames(
            nativeStart.ref,
            playedFrames,
            sampleRate,
            result,
            error,
          ),
          error,
        );
        return _playbackPosition(result.ref);
      } finally {
        _destroyError(error);
      }
    });
  }

  static PlaybackPosition pausePlayback(PlaybackPosition position) {
    return using((arena) {
      final result = arena<native.otgy_playback_position>();
      final error = arena<Pointer<native.otgy_error>>();
      final input = arena<native.otgy_playback_position>()
        ..ref.phase = position.isPlaying ? 1 : 0
        ..ref.timeline_seconds = position.timelineSeconds
        ..ref.audio_duration_seconds = position.audioDurationSeconds;
      try {
        _checkStatus(
          native.otgy_pause_playback(input.ref, result, error),
          error,
        );
        return _playbackPosition(result.ref);
      } finally {
        _destroyError(error);
      }
    });
  }

  static int coordinateApplicationOperation({
    required int mode,
    required int operation,
  }) {
    return using((arena) {
      final result = arena<Uint32>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_coordinate_application_operation(
            mode,
            operation,
            result,
            error,
          ),
          error,
        );
        return result.value;
      } finally {
        _destroyError(error);
      }
    });
  }

  static TimelineNavigation calculateNavigation({
    required TimelineRange range,
    required double currentRow,
    required double visibleBeats,
  }) {
    return using((arena) {
      final nativeRange = arena<native.otgy_timeline_range>()
        ..ref.begin_seconds = range.beginSeconds
        ..ref.end_seconds = range.endSeconds
        ..ref.end_sixteenth_row = range.endRow
        ..ref.uses_zero_bpm_projection = range.usesZeroBpmProjection ? 1 : 0;
      final result = arena<native.otgy_timeline_navigation>();
      final error = arena<Pointer<native.otgy_error>>();
      try {
        _checkStatus(
          native.otgy_calculate_timeline_navigation(
            nativeRange.ref,
            currentRow,
            visibleBeats,
            result,
            error,
          ),
          error,
        );
        return TimelineNavigation(
          toBeginRow: result.ref.to_begin_row,
          pageUpRow: result.ref.page_up_row,
          pageDownRow: result.ref.page_down_row,
          toEndRow: result.ref.to_end_row,
          canToBegin: result.ref.can_to_begin != 0,
          canPageUp: result.ref.can_page_up != 0,
          canPageDown: result.ref.can_page_down != 0,
          canToEnd: result.ref.can_to_end != 0,
        );
      } finally {
        _destroyError(error);
      }
    });
  }
}

PlaybackPosition _playbackPosition(native.otgy_playback_position value) {
  return PlaybackPosition(
    isPlaying: value.phase == 1,
    timelineSeconds: value.timeline_seconds,
    audioDurationSeconds: value.audio_duration_seconds,
  );
}

Pointer<native.otgy_audio_source_info> _nativeSourceInfo(
  AudioSourceInfo value,
  Allocator allocator,
) {
  return allocator<native.otgy_audio_source_info>()
    ..ref.codec = value.codec
    ..ref.measured_bitrate_bps = value.measuredBitrateBps
    ..ref.nominal_bitrate_bps = value.nominalBitrateBps;
}

native.otgy_string_view _copyString(String value, Allocator allocator) {
  final bytes = utf8.encode(value);
  final pointer = allocator<Uint8>(bytes.length);
  if (bytes.isNotEmpty) {
    pointer.asTypedList(bytes.length).setAll(0, bytes);
  }
  final view = allocator<native.otgy_string_view>();
  view.ref
    ..data = pointer.cast()
    ..length = bytes.length;
  return view.ref;
}

native.otgy_byte_view _copyBytes(Uint8List bytes, Allocator allocator) {
  final pointer = allocator<Uint8>(bytes.length);
  if (bytes.isNotEmpty) {
    pointer.asTypedList(bytes.length).setAll(0, bytes);
  }
  final view = allocator<native.otgy_byte_view>();
  view.ref
    ..data = pointer
    ..length = bytes.length;
  return view.ref;
}

Uint8List _copyOwnedBytes(Pointer<native.otgy_bytes> bytes) {
  final view = native.otgy_bytes_get_view(bytes);
  return Uint8List.fromList(view.data.cast<Uint8>().asTypedList(view.length));
}

void _checkStatus(int status, Pointer<Pointer<native.otgy_error>> outError) {
  if (status == native.OTGY_STATUS_OK) {
    return;
  }
  final error = outError.value;
  if (error == nullptr) {
    throw OtgyException(
      status: status,
      domain: 0,
      code: 0,
      message: 'Native call failed without a diagnostic.',
    );
  }
  final message = native.otgy_error_get_message(error);
  throw OtgyException(
    status: status,
    domain: native.otgy_error_get_domain(error),
    code: native.otgy_error_get_code(error),
    message: utf8.decode(
      message.data.cast<Uint8>().asTypedList(message.length),
      allowMalformed: true,
    ),
  );
}

void _destroyError(Pointer<Pointer<native.otgy_error>> outError) {
  if (outError.value != nullptr) {
    native.otgy_error_destroy(outError.value);
    outError.value = nullptr;
  }
}
