import 'dart:async';
import 'dart:isolate';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:otgy_native/otgy_native.dart';
import 'package:wakelock_plus/wakelock_plus.dart';

import '../infrastructure/local_diagnostics.dart';
import '../infrastructure/playback_output.dart';

enum EditorNoticeKind { info, error }

enum WorkspaceSourceKind { untitled, importedSlk, savedDraft }

enum EditorPlaybackMode { idle, chartPreview, rhythmTest }

/// Selects which packaged WAV set feeds chart playback. The trimmed variant
/// only differs for the Chinese clips listed in
/// [_trimmedPlaybackSoundFiles]; all other clips keep their shipped asset.
enum PlaybackSoundVariant { original, trimmed }

enum EditorChartInteractionMode { normal, selection, pastePending }

const double targetEditorRowSpacing = 46;

/// Playback view density. UI Zoom 6.73 makes the shared core return
/// 100 px/s; the playback projection is capped at that same value and uses
/// this density for its waveform cache.
const double _playbackVisualZoom = 6.73;

typedef AudioExportSaveCallback = Future<void> Function(
  AudioExportResult result,
  bool Function() isCancelled,
  void Function(int completedUnits, int totalUnits) onProgress,
);

/// Applies the display keep-awake policy used while playback is running.
typedef KeepScreenOnCallback = Future<void> Function(bool enabled);

Future<void> _platformKeepScreenOn(bool enabled) =>
    WakelockPlus.toggle(enable: enabled);

final class AudioExportCancelled implements Exception {
  const AudioExportCancelled();
}

const _playbackSoundFiles = <(PlaybackSoundId, String)>[
  (PlaybackSoundId.f1_1, 'f1-1'),
  (PlaybackSoundId.f1_2, 'f1-2'),
  (PlaybackSoundId.f1_3, 'f1-3'),
  (PlaybackSoundId.f1_4, 'f1-4'),
  (PlaybackSoundId.f1_6, 'f1-6'),
  (PlaybackSoundId.f1_7, 'f1-7'),
  (PlaybackSoundId.f1_8, 'f1-8'),
  (PlaybackSoundId.f1_9, 'f1-9'),
  (PlaybackSoundId.f1Go, 'f1-go'),
  (PlaybackSoundId.f1Yeah, 'f1_yeah'),
  (PlaybackSoundId.m1_1, 'm1-1'),
  (PlaybackSoundId.m1_2, 'm1-2'),
  (PlaybackSoundId.m1_3, 'm1-3'),
  (PlaybackSoundId.m1_4, 'm1-4'),
  (PlaybackSoundId.m1_6, 'm1-6'),
  (PlaybackSoundId.m1_7, 'm1-7'),
  (PlaybackSoundId.m1_8, 'm1-8'),
  (PlaybackSoundId.m1_9, 'm1-9'),
  (PlaybackSoundId.m1Go, 'm1-go'),
  (PlaybackSoundId.m1Yeah, 'm1_yeah'),
  (PlaybackSoundId.spaceBar, 'space_bar'),
  (PlaybackSoundId.turn1, 'turn_1'),
  (PlaybackSoundId.turn2, 'turn_2'),
];

/// Chinese clips whose shipped WAV starts with more than 1 ms of digital
/// silence. The trimmed variant loads these from
/// `assets/sounds_trimmed/zh`; every other clip keeps its shipped asset.
/// Keep in sync with `scripts/trim_playback_sounds.py`.
const _trimmedPlaybackSoundFiles = <String>{
  'f1-6',
  'f1-8',
  'f1-9',
  'm1-4',
  'm1-6',
  'm1-7',
  'm1-9',
};

/// Clips that are byte-identical in both languages and ship once from
/// `assets/sounds/common`.
const _sharedPlaybackSoundFiles = <String>{'space_bar', 'turn_1', 'turn_2'};

final class _BackgroundAudioAnalysis {
  const _BackgroundAudioAnalysis({
    required this.source,
    required this.extension,
    required this.stagePort,
  });

  final Uint8List source;
  final String extension;
  final SendPort stagePort;

  List<AudioAnalysisCandidate> call() {
    final audio = OtgyNative.decodeAudio(source, extension);
    try {
      return audio
          .analyzeSync(onStage: (stage) => stagePort.send(stage.value))
          .candidates;
    } finally {
      audio.dispose();
    }
  }
}

Future<AudioExportResult> _runAudioExportInIsolate({
  required Uint8List source,
  required String extension,
  required double offset,
  required int quality,
  required String baseName,
  required int cancelAddress,
  required SendPort progressSendPort,
}) {
  // Keep this closure in a top-level helper. Closures created inside
  // EditorController.exportAudio share that method's context with UI listeners
  // and can accidentally capture unsendable controller state (for example the
  // pending playback-sound Future).
  return Isolate.run(() {
    final borrowedCancellation = AudioExportCancellation.borrowAddress(
      cancelAddress,
    );
    final audio = OtgyNative.decodeAudio(source, extension);
    try {
      return audio.exportOgg(
        offset: offset,
        quality: quality,
        baseName: baseName,
        cancellation: borrowedCancellation,
        onProgress: (progress) {
          progressSendPort.send([
            progress.stage.value,
            progress.completedUnits,
            progress.totalUnits,
          ]);
        },
      );
    } finally {
      audio.dispose();
      borrowedCancellation.dispose();
    }
  });
}

final class EditorNotice {
  const EditorNotice(this.message, {this.kind = EditorNoticeKind.info});

  final String message;
  final EditorNoticeKind kind;
}

class EditorController extends ChangeNotifier {
  EditorController({
    NativeChartEditor? editor,
    PlaybackOutput? playbackOutput,
    DiagnosticRecorder? diagnosticRecorder,
    KeepScreenOnCallback? keepScreenOn,
  }) : _editor = editor ?? NativeChartEditor.createEmpty(),
       _playbackOutput = playbackOutput ?? const MethodChannelPlaybackOutput(),
       _keepScreenOn = keepScreenOn ?? _platformKeepScreenOn,
       _diagnosticRecorder =
           diagnosticRecorder ??
           (LocalDiagnostics.detailedEnabled
               ? LocalDiagnostics.instance?.record
               : null) {
    _zoom = _automaticEditZoomForBpm(_bpm);
    _refreshProjection();
    _refreshViewMetrics();
    _savedPayload = _editor.encodeFormal(bpm: _bpm, offset: _offset);
  }

  NativeChartEditor _editor;
  final PlaybackOutput _playbackOutput;
  final KeepScreenOnCallback _keepScreenOn;
  final DiagnosticRecorder? _diagnosticRecorder;
  NativeAudio? _audio;
  NativePlaybackSoundBank? _playbackSoundBank;
  Future<NativePlaybackSoundBank>? _playbackSoundBankFuture;
  List<ChartMarker> _markers = const [];
  List<ShowtimeMove> _showtimeMoves = const [];
  List<WaveformBlock> _waveformBlocks = const [];
  List<WaveformBlock> _playbackWaveformBlocks = const [];
  List<WaveformBlock> _filteredWaveformBlocks = const [];
  List<WaveformBlock> _playbackFilteredWaveformBlocks = const [];
  EditorNotice? _notice;
  bool _isBusy = false;
  bool _isExportingAudio = false;
  bool _audioExportCancellationRequested = false;
  EditorPlaybackMode _playbackMode = EditorPlaybackMode.idle;
  bool _isDirty = false;
  EditorChartInteractionMode _chartInteractionMode =
      EditorChartInteractionMode.normal;
  ChartPastePreview? _pastePreview;
  int? _pastePreviewAnchorRow;
  bool _waveformVisible = true;
  WaveformFilter _waveformFilter = WaveformFilter.none;
  double _waveformFilterStrength = 0.75;
  bool _waveformOverlayOriginal = true;
  PlaybackSoundLanguage _playbackSoundLanguage = PlaybackSoundLanguage.chinese;
  PlaybackSoundVariant _playbackSoundVariant = PlaybackSoundVariant.trimmed;
  PlaybackDanceMode _playbackDanceMode = PlaybackDanceMode.fourKey;
  bool _transitionSoundsEnabled = false;
  String? _audioFileName;
  Uint8List? _audioSourceBytes;
  String? _audioExtension;
  AudioExportQualityPolicy? _audioExportQuality;
  List<AudioAnalysisCandidate>? _analysisCandidates;
  Future<List<AudioAnalysisCandidate>>? _analysisFuture;
  SyncAnalysisStage? _analysisStage;
  int _audioVersion = 0;
  final Map<double, int> _zoomPixelsPerSecond = {};
  WorkspaceSourceKind _sourceKind = WorkspaceSourceKind.untitled;
  String? _sourceName;
  String? _draftId;
  Uint8List? _savedPayload;
  int _projectRevision = 0;
  double _bpm = 100;
  double _offset = 0;
  double _offsetBeatBaseline = 0;
  double _offsetBeatAdjustment = 0;
  double _zoom = 10;
  double _scale = 4;
  double _currentRow = 0;
  int _pixelsPerSecond = 0;
  bool _editZoomAutomatic = true;
  Timer? _playbackTimer;
  PlaybackPosition? _playbackStart;
  int _playbackSampleRate = 0;
  int _playbackFrameCount = 0;
  int _playbackPlayedFrames = 0;
  bool _playbackPollActive = false;
  int _playbackGeneration = 0;
  final math.Random _playbackSoundRandom = math.Random();
  int? _playbackSoundSeed;
  double? _playbackSoundResumeRow;
  AudioExportCancellation? _audioExportCancellation;
  AudioExportProgress? _audioExportProgress;

  Map<String, Object?> get diagnosticState =>
      Map<String, Object?>.unmodifiable({
        'revision': _projectRevision,
        'dirty': _isDirty,
        'sourceKind': _sourceKind.name,
        'hasAudio': hasAudio,
        'bpm': _bpm,
        'offset': _offset,
        'currentRow': _currentRow,
        'markerCount': _markers.length,
        'showtimeMoveCount': _showtimeMoves.length,
        'canUndo': canUndo,
        'canRedo': canRedo,
        'interactionMode': _chartInteractionMode.name,
        'selectionCount': selectionCount,
        'hasSelection': hasSelection,
        'hasClipboard': hasClipboard,
        'clipboardCount': clipboardCount,
        'cutPending': cutPending,
        'pastePreviewAnchorRow': _pastePreviewAnchorRow,
        'pastePreviewCode': _pastePreview?.code,
      });

  /// Callers use static event identifiers and diagnostic metadata only.
  void recordDiagnostic(
    String event, [
    Map<String, Object?> fields = const {},
  ]) {
    final recorder = _diagnosticRecorder;
    if (recorder == null) return;
    try {
      const numericFields = {
        'column',
        'row',
        'anchorRow',
        'code',
        'domain',
        'status',
        'candidateCount',
        'stage',
        'audioVersion',
        'completedUnits',
        'totalUnits',
      };
      const booleanFields = {
        'changed',
        'enabled',
        'blocked',
        'cached',
        'reused',
      };
      final metadata = <String, Object?>{};
      for (final entry in fields.entries) {
        final value = entry.value;
        if ((numericFields.contains(entry.key) &&
                value is num &&
                value.isFinite) ||
            (booleanFields.contains(entry.key) && value is bool)) {
          metadata[entry.key] = value;
        }
      }
      recorder(
        event,
        Map<String, Object?>.unmodifiable({
          ...metadata,
          'state': diagnosticState,
        }),
      );
    } catch (_) {
      // Diagnostics must never change an editor operation's outcome.
    }
  }

  void recordDiagnosticException(
    String operation,
    Object error,
    StackTrace stack,
  ) {
    final recorder = _diagnosticRecorder;
    if (recorder == null) return;
    try {
      recorder(
        'exception',
        Map<String, Object?>.unmodifiable({
          'operation': operation,
          'type': error.runtimeType.toString(),
          'stack': LocalDiagnostics.sanitizeText(stack.toString()),
          if (error is OtgyException) ...{
            'domain': error.domain,
            'code': error.code,
            'status': error.status,
          },
          'state': diagnosticState,
        }),
      );
    } catch (_) {
      // Diagnostics must never change an editor operation's outcome.
    }
  }

  T _diagnosticCall<T>(String operation, T Function() action) {
    try {
      return action();
    } catch (error, stack) {
      recordDiagnosticException(operation, error, stack);
      rethrow;
    }
  }

  void _recordEdit(
    String event,
    EditResult result, [
    Map<String, Object?> parameters = const {},
  ]) {
    recordDiagnostic(event, {
      ...parameters,
      'code': result.code,
      'changed': result.changed,
    });
  }

  List<ChartMarker> get markers => _markers;
  List<ShowtimeMove> get showtimeMoves => _showtimeMoves;
  List<WaveformBlock> get waveformBlocks => _waveformBlocks;
  List<WaveformBlock> get canvasWaveformBlocks {
    if (isPlaying &&
        playbackVisualPixelsPerSecond < _pixelsPerSecond &&
        _playbackWaveformBlocks.isNotEmpty) {
      return _playbackWaveformBlocks;
    }
    return _waveformBlocks;
  }

  List<WaveformBlock> get canvasFilteredWaveformBlocks {
    if (_waveformFilter == WaveformFilter.none) {
      return const [];
    }
    if (isPlaying &&
        playbackVisualPixelsPerSecond < _pixelsPerSecond &&
        _playbackFilteredWaveformBlocks.isNotEmpty) {
      return _playbackFilteredWaveformBlocks;
    }
    return _filteredWaveformBlocks;
  }

  EditorNotice? get notice => _notice;
  bool get isBusy => _isBusy;
  bool get isExportingAudio => _isExportingAudio;
  bool get audioExportCancellationRequested =>
      _audioExportCancellationRequested;
  AudioExportProgress? get audioExportProgress => _audioExportProgress;
  bool get isPlaying => _playbackMode != EditorPlaybackMode.idle;
  bool get isChartPlaying => _playbackMode == EditorPlaybackMode.chartPreview;
  bool get isRhythmTesting => _playbackMode == EditorPlaybackMode.rhythmTest;

  /// The single global application-operation mode. Playback and audio export
  /// outrank the chart interaction session, so an active session can never
  /// widen what a running playback or export allows.
  int get _applicationOperationMode {
    if (isPlaying) {
      return OTGY_APPLICATION_MODE_PLAYING;
    }
    if (_isExportingAudio) {
      return OTGY_APPLICATION_MODE_EXPORTING_AUDIO;
    }
    if (_chartInteractionMode != EditorChartInteractionMode.normal) {
      return OTGY_APPLICATION_MODE_CHART_INTERACTION;
    }
    return OTGY_APPLICATION_MODE_IDLE;
  }

  bool _operationAllowed(int operation) =>
      OtgyNative.coordinateApplicationOperation(
        mode: _applicationOperationMode,
        operation: operation,
      ) ==
      OTGY_APPLICATION_OPERATION_ALLOWED;

  /// Audio, chart data, persistence, export and playback control. This is the
  /// safe default, and every selection or paste session rejects it.
  bool get generalActionsEnabled =>
      _operationAllowed(OTGY_APPLICATION_OPERATION_GENERAL);

  /// Selection switching, clipboard preparation, paste submission and exit.
  bool get selectionCommandEnabled =>
      _operationAllowed(OTGY_APPLICATION_OPERATION_SELECTION_COMMAND);

  /// Timeline movement, Zoom, Scale, page navigation, time jump and the
  /// waveform settings panel. Kept available during a session so the target row
  /// stays reachable.
  bool get navigationEnabled =>
      _operationAllowed(OTGY_APPLICATION_OPERATION_NAVIGATION_OR_VIEW);

  /// Markers stay focusable and activatable whenever their own mode's command
  /// is allowed: plain cell activation in normal mode, and the selection toggle
  /// inside a chart interaction session.
  bool get markerInteractionEnabled =>
      generalActionsEnabled || selectionCommandEnabled;

  /// Undo and redo.
  bool get historyNavigationEnabled =>
      _operationAllowed(OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION);
  bool get isDirty => _isDirty;
  bool get waveformVisible => _waveformVisible;
  WaveformFilter get waveformFilter => _waveformFilter;
  double get waveformFilterStrength => _waveformFilterStrength;
  bool get waveformOverlayOriginal => _waveformOverlayOriginal;
  bool get waveformFilterEnabled => _waveformFilter != WaveformFilter.none;
  PlaybackSoundLanguage get playbackSoundLanguage => _playbackSoundLanguage;
  PlaybackSoundVariant get playbackSoundVariant => _playbackSoundVariant;
  PlaybackDanceMode get playbackDanceMode => _playbackDanceMode;
  bool get transitionSoundsEnabled => _transitionSoundsEnabled;
  bool get hasAudio => _audio != null;
  bool get canUndo => !cutPending && _editor.canUndo;
  bool get canRedo => !cutPending && _editor.canRedo;
  EditorChartInteractionMode get chartInteractionMode => _chartInteractionMode;
  bool get selectionMode =>
      _chartInteractionMode == EditorChartInteractionMode.selection;
  bool get pastePending =>
      _chartInteractionMode == EditorChartInteractionMode.pastePending;
  List<ChartSelection> get selections => _editor.selections;
  int get selectionCount => _editor.selectionCount;
  bool get hasSelection => _editor.hasSelection;
  bool get hasClipboard => _editor.hasClipboard;
  int get clipboardCount => _editor.clipboardCount;
  bool get cutPending => _editor.cutPending;
  ChartPastePreview? get pastePreview => _pastePreview;
  int? get pastePreviewAnchorRow => _pastePreviewAnchorRow;
  bool get canCopySelection => selectionMode && hasSelection;
  bool get canCutSelection => selectionMode && hasSelection;
  bool get canPaste => pastePending && hasClipboard;
  String? get audioFileName => _audioFileName;
  String get chartName => _sourceName ?? '未命名草稿';
  WorkspaceSourceKind get sourceKind => _sourceKind;
  String? get draftId => _draftId;
  int get projectRevision => _projectRevision;
  AudioExportQualityPolicy? get audioExportQuality => _audioExportQuality;
  List<AudioAnalysisCandidate>? get analysisCandidates => _analysisCandidates;
  SyncAnalysisStage? get analysisStage => _analysisStage;
  double get bpm => _bpm;
  double get offset => _offset;
  double get zoom => _zoom;
  double get scale => _scale;
  double get currentRow => _currentRow;
  double get currentSongSeconds => OtgyNative.sixteenthRowToSeconds(
    bpm: projectedBpm,
    offset: _offset,
    row: _currentRow,
  );
  int get pixelsPerSecond => _pixelsPerSecond;
  double? get audioDurationSeconds => _audio?.durationSeconds;
  double get playbackVisualPixelsPerSecond => math
      .min(
        _pixelsPerSecond.toDouble(),
        _pixelsPerSecondForZoom(_playbackVisualZoom).toDouble(),
      )
      .toDouble();

  double get projectedBpm => _bpm == 0 ? 100 : _bpm;
  double get rowSpacing => _rowSpacingForPixelsPerSecond(_pixelsPerSecond);
  double get playbackRowSpacing =>
      _rowSpacingForPixelsPerSecond(playbackVisualPixelsPerSecond);

  double _rowSpacingForPixelsPerSecond(num pixelsPerSecond) {
    final first = OtgyNative.sixteenthRowToSeconds(
      bpm: projectedBpm,
      offset: _offset,
      row: 0,
    );
    final second = OtgyNative.sixteenthRowToSeconds(
      bpm: projectedBpm,
      offset: _offset,
      row: 1,
    );
    return pixelsPerSecond * (second - first);
  }

  TimelineRange get timelineRange => _editor.timelineRange(
    bpm: _bpm,
    offset: _offset,
    audioEndSeconds: _audio?.durationSeconds,
  );

  TimelineNavigation navigation(double visibleBeats) {
    final range = timelineRange;
    final clampedRow = _currentRow.clamp(0, range.endRow).toDouble();
    return OtgyNative.calculateNavigation(
      range: range,
      currentRow: clampedRow,
      visibleBeats: math.max(1, visibleBeats),
    );
  }

  Future<void> loadAudioBytes(Uint8List bytes, String fileName) async {
    if (_isBusy || !generalActionsEnabled) {
      recordDiagnostic('audio.load', {'blocked': true});
      return;
    }
    recordDiagnostic('audio.load.start');
    _setBusy(true);
    NativeAudio? pending;
    try {
      final pendingZoom = _automaticEditZoomForBpm(100);
      final dot = fileName.lastIndexOf('.');
      final extension = dot < 0 ? '' : fileName.substring(dot);
      pending = OtgyNative.decodeAudio(bytes, extension);
      final editWaveform = _generateWaveformProjection(pending, pendingZoom);
      final editFilteredWaveform = _waveformFilter == WaveformFilter.none
          ? (
              blocks: const <WaveformBlock>[],
              pixelsPerSecond: editWaveform.pixelsPerSecond,
            )
          : _generateWaveformProjection(
              pending,
              pendingZoom,
              filter: _waveformFilter,
            );
      final playbackWaveform = pendingZoom == _playbackVisualZoom
          ? editWaveform
          : _generateWaveformProjection(pending, _playbackVisualZoom);
      final playbackFilteredWaveform = _waveformFilter == WaveformFilter.none
          ? (
              blocks: const <WaveformBlock>[],
              pixelsPerSecond: playbackWaveform.pixelsPerSecond,
            )
          : pendingZoom == _playbackVisualZoom
          ? editFilteredWaveform
          : _generateWaveformProjection(
              pending,
              _playbackVisualZoom,
              filter: _waveformFilter,
            );

      _audio?.dispose();
      _audio = pending;
      pending = null;
      _audioFileName = fileName;
      _audioSourceBytes = Uint8List.fromList(bytes);
      _audioExtension = extension;
      _audioExportQuality = _audio!.exportQualityPolicy();
      _analysisCandidates = null;
      _analysisFuture = null;
      _analysisStage = null;
      _audioVersion++;
      _waveformBlocks = editWaveform.blocks;
      _playbackWaveformBlocks = playbackWaveform.blocks;
      _filteredWaveformBlocks = editFilteredWaveform.blocks;
      _playbackFilteredWaveformBlocks = playbackFilteredWaveform.blocks;
      _pixelsPerSecond = editWaveform.pixelsPerSecond;
      _zoom = pendingZoom;
      _editZoomAutomatic = true;
      _bpm = 100;
      _offset = 0;
      _resetOffsetBeatAdjustment();
      _currentRow = 0;
      _invalidatePlaybackSoundSession();
      _notice = const EditorNotice('音频已加载');
      _updateDirty();
      recordDiagnostic('audio.load', {
        'changed': true,
        'audioVersion': _audioVersion,
      });
    } catch (error, stack) {
      recordDiagnosticException('audio.load', error, stack);
      pending?.dispose();
      _notice = const EditorNotice(
        '音频加载失败：无法解码或生成波形，所选音频未加载。',
        kind: EditorNoticeKind.error,
      );
      notifyListeners();
    } finally {
      _setBusy(false);
    }
  }

  void setBpm(double value) {
    if (!generalActionsEnabled ||
        !value.isFinite ||
        value < 0 ||
        value > 100000 ||
        value == _bpm) {
      return;
    }
    _bpm = value;
    _resetOffsetBeatAdjustment();
    _synchronizeAutomaticEditZoom();
    _updateDirty();
  }

  void nudgeBpm(double delta) {
    setBpm((_bpm + delta).clamp(0, 100000).toDouble());
  }

  void setOffset(double value) {
    if (!generalActionsEnabled ||
        !value.isFinite ||
        value < -30 ||
        value > 30 ||
        value == _offset) {
      return;
    }
    _offset = value;
    _resetOffsetBeatAdjustment();
    _updateDirty();
  }

  void nudgeOffsetByBeat(double beats) {
    if (!generalActionsEnabled || _bpm == 0 || !beats.isFinite) {
      return;
    }
    final cumulativeBeats = _offsetBeatAdjustment + beats;
    final value = OtgyNative.adjustOffsetByBeats(
      bpm: _bpm,
      baselineOffset: _offsetBeatBaseline,
      cumulativeBeats: cumulativeBeats,
    );
    if (value == _offset) {
      return;
    }
    _offset = value;
    if (value == -30 || value == 30) {
      _resetOffsetBeatAdjustment();
    } else {
      _offsetBeatAdjustment = cumulativeBeats;
    }
    _updateDirty();
  }

  void clickCell(int column, int row) {
    if (_isBusy || !generalActionsEnabled) {
      recordDiagnostic('click', {
        'column': column,
        'row': row,
        'blocked': true,
      });
      return;
    }
    final result = _diagnosticCall(
      'click',
      () => _editor.click(column, math.max(0, row)),
    );
    if (result.changed) {
      if (result.playerActionsCleared) {
        _notice = EditorNotice('演示已修改；对应练习无法自动修正，已清空练习 N/S，保留起始标记。');
      }
      _refreshProjection();
      _commitChartChange();
    } else {
      _notice = EditorNotice(_editMessage(result.code));
      notifyListeners();
    }
    _recordEdit('click', result, {'column': column, 'row': row});
  }

  void toggleSelectionMode() {
    if (_isBusy || !selectionCommandEnabled) {
      recordDiagnostic('selectionMode.toggle', {'blocked': true});
      return;
    }
    if (_chartInteractionMode == EditorChartInteractionMode.normal) {
      enterSelectionMode();
    } else {
      exitChartInteraction();
    }
  }

  void enterSelectionMode() {
    if (_isBusy ||
        !selectionCommandEnabled ||
        _chartInteractionMode != EditorChartInteractionMode.normal) {
      recordDiagnostic('selectionMode.enter', {'blocked': true});
      return;
    }
    _chartInteractionMode = EditorChartInteractionMode.selection;
    _pastePreview = null;
    _pastePreviewAnchorRow = null;
    notifyListeners();
    recordDiagnostic('selectionMode.enter', {'enabled': true, 'changed': true});
  }

  void exitSelectionMode() => exitChartInteraction();

  void exitChartInteraction() {
    if (_isBusy ||
        !selectionCommandEnabled ||
        _chartInteractionMode == EditorChartInteractionMode.normal) {
      recordDiagnostic('selectionMode.exit', {'blocked': true});
      return;
    }
    final clearedSelection = _diagnosticCall(
      'selection.clear',
      _editor.clearSelection,
    );
    final clearedClipboard = _diagnosticCall(
      'clipboard.clear',
      _editor.clearClipboard,
    );
    _chartInteractionMode = EditorChartInteractionMode.normal;
    _pastePreview = null;
    _pastePreviewAnchorRow = null;
    notifyListeners();
    recordDiagnostic('selection.clear', {
      'code': clearedSelection.code,
      'changed': clearedSelection.changed,
    });
    recordDiagnostic('clipboard.clear', {
      'code': clearedClipboard.code,
      'changed': clearedClipboard.changed,
    });
    recordDiagnostic('selectionMode.exit', {'enabled': false, 'changed': true});
  }

  void selectAt(int column, int row) {
    if (_isBusy || !selectionCommandEnabled || !selectionMode) {
      recordDiagnostic('select', {
        'column': column,
        'row': row,
        'blocked': true,
      });
      return;
    }
    final result = _diagnosticCall(
      'select',
      () => _editor.select(column, math.max(0, row)),
    );
    if (result.code != 0) {
      _notice = EditorNotice(_editMessage(result.code));
    }
    notifyListeners();
    recordDiagnostic('select', {
      'column': column,
      'row': row,
      'code': result.code,
      'changed': result.changed,
    });
  }

  void copySelection() {
    if (_isBusy || !selectionCommandEnabled || !selectionMode) {
      recordDiagnostic('copy', {'blocked': true});
      return;
    }
    final result = _diagnosticCall('copy', _editor.copySelection);
    if (result.code == 0) {
      _chartInteractionMode = EditorChartInteractionMode.pastePending;
      _pastePreview = null;
      _pastePreviewAnchorRow = null;
    } else {
      _notice = EditorNotice(_editMessage(result.code));
    }
    notifyListeners();
    recordDiagnostic('copy', {'code': result.code, 'changed': result.changed});
  }

  void cutSelection() {
    if (_isBusy || !selectionCommandEnabled || !selectionMode) {
      recordDiagnostic('cut', {'blocked': true});
      return;
    }
    final result = _diagnosticCall('cut', _editor.cutSelection);
    if (result.code == 0) {
      _chartInteractionMode = EditorChartInteractionMode.pastePending;
      _pastePreview = null;
      _pastePreviewAnchorRow = null;
    } else {
      _notice = EditorNotice(_editMessage(result.code));
    }
    notifyListeners();
    recordDiagnostic('cut', {'code': result.code, 'changed': result.changed});
  }

  void updatePastePreview(int anchorRow) {
    if (_isBusy || !selectionCommandEnabled || !pastePending) {
      return;
    }
    final row = math.max(0, anchorRow);
    final preview = _diagnosticCall(
      'paste.preview',
      () => _editor.pastePreview(row),
    );
    _pastePreview = preview;
    _pastePreviewAnchorRow = row;
    notifyListeners();
    recordDiagnostic('paste.preview', {'anchorRow': row, 'code': preview.code});
  }

  void clearPastePreview() {
    if (_isBusy || !selectionCommandEnabled || _pastePreview == null) {
      return;
    }
    _pastePreview = null;
    _pastePreviewAnchorRow = null;
    notifyListeners();
  }

  void pasteAtRow(int anchorRow) {
    if (_isBusy || !selectionCommandEnabled || !pastePending) {
      recordDiagnostic('paste', {'anchorRow': anchorRow, 'blocked': true});
      return;
    }
    final row = math.max(0, anchorRow);
    final result = _diagnosticCall('paste', () => _editor.paste(row));
    if (result.changed) {
      _refreshProjection();
      _commitChartChange();
    } else {
      _notice = EditorNotice(_editMessage(result.code));
      notifyListeners();
    }
    _pastePreview = _diagnosticCall(
      'paste.preview',
      () => _editor.pastePreview(row),
    );
    _pastePreviewAnchorRow = row;
    notifyListeners();
    _recordEdit('paste', result, {'anchorRow': row});
  }

  void undo() {
    if (!canUndo || _isBusy || !historyNavigationEnabled) {
      recordDiagnostic('undo', {'blocked': true});
      return;
    }
    final result = _diagnosticCall('undo', _editor.undo);
    _refreshProjection();
    _commitChartChange();
    _recordEdit('undo', result);
  }

  void redo() {
    if (!canRedo || _isBusy || !historyNavigationEnabled) {
      recordDiagnostic('redo', {'blocked': true});
      return;
    }
    final result = _diagnosticCall('redo', _editor.redo);
    _refreshProjection();
    _commitChartChange();
    _recordEdit('redo', result);
  }

  void clearChart() {
    if (_isBusy || !generalActionsEnabled) {
      recordDiagnostic('clear', {'blocked': true});
      return;
    }
    final result = _diagnosticCall('clear', _editor.clear);
    if (result.changed) {
      _refreshProjection();
      _commitChartChange();
    }
    _recordEdit('clear', result);
  }

  Future<List<AudioAnalysisCandidate>> analyzeAudio() {
    if (!generalActionsEnabled) {
      return Future.error(StateError('Audio analysis is not allowed now.'));
    }
    final cached = _analysisCandidates;
    if (cached != null) {
      recordDiagnostic('audio.analysis', {
        'cached': true,
        'candidateCount': cached.length,
      });
      return Future.value(cached);
    }
    final existing = _analysisFuture;
    if (existing != null) {
      recordDiagnostic('audio.analysis', {'reused': true});
      return existing;
    }
    final bytes = _audioSourceBytes;
    final extension = _audioExtension;
    if (bytes == null || extension == null) {
      final error = StateError('No audio is loaded.');
      recordDiagnosticException('audio.analysis', error, StackTrace.current);
      return Future.error(error);
    }
    final version = _audioVersion;
    final source = Uint8List.fromList(bytes);
    final stagePort = ReceivePort();
    final stageSendPort = stagePort.sendPort;
    final stageSubscription = stagePort.listen((message) {
      if (version != _audioVersion || message is! int) {
        return;
      }
      final stage = SyncAnalysisStage.fromValue(message);
      if (stage != null) {
        _analysisStage = stage;
        notifyListeners();
        recordDiagnostic('audio.analysis.stage', {'stage': stage.value});
      }
    });
    _analysisStage = SyncAnalysisStage.detectingOnsets;
    recordDiagnostic('audio.analysis.start', {'audioVersion': version});
    _setBusy(true);
    final future =
        Isolate.run(
              _BackgroundAudioAnalysis(
                source: source,
                extension: extension,
                stagePort: stageSendPort,
              ).call,
            )
            .then((candidates) {
              if (version == _audioVersion) {
                _analysisCandidates = List.unmodifiable(candidates);
                recordDiagnostic('audio.analysis', {
                  'candidateCount': candidates.length,
                });
                return _analysisCandidates!;
              }
              throw StateError('Audio changed while analysis was running.');
            })
            .onError((Object error, StackTrace stack) {
              recordDiagnosticException('audio.analysis', error, stack);
              Error.throwWithStackTrace(error, stack);
            })
            .whenComplete(() async {
              await stageSubscription.cancel();
              stagePort.close();
              if (version == _audioVersion) {
                _analysisFuture = null;
              }
              _setBusy(false);
            });
    _analysisFuture = future;
    return future;
  }

  void applyAnalysisCandidate(AudioAnalysisCandidate candidate) {
    if (!generalActionsEnabled ||
        !candidate.bpm.isFinite ||
        !candidate.appOffsetSeconds.isFinite) {
      return;
    }
    _bpm = candidate.bpm;
    _offset = candidate.appOffsetSeconds;
    _resetOffsetBeatAdjustment();
    _synchronizeAutomaticEditZoom();
    _updateDirty();
  }

  Future<void> exportAudio({
    required int quality,
    required String baseName,
    required AudioExportSaveCallback save,
  }) async {
    if (!generalActionsEnabled) {
      throw StateError('Another application operation is active.');
    }
    final bytes = _audioSourceBytes;
    final extension = _audioExtension;
    if (bytes == null || extension == null) {
      throw StateError('No audio is loaded.');
    }
    final source = Uint8List.fromList(bytes);
    final offset = _offset;
    final cancellation = AudioExportCancellation.create();
    final progressPort = ReceivePort();
    StreamSubscription<Object?>? progressSubscription;
    final cancelAddress = cancellation.address;
    final progressSendPort = progressPort.sendPort;
    _audioExportCancellation = cancellation;
    _isExportingAudio = true;
    _audioExportCancellationRequested = false;
    _audioExportProgress = const AudioExportProgress(
      stage: AudioExportStage.preparingAudio,
      completedUnits: 0,
      totalUnits: 0,
    );
    _setBusy(true);
    progressSubscription = progressPort.listen((message) {
      if (!identical(_audioExportCancellation, cancellation) ||
          message is! List<Object?> ||
          message.length != 3 ||
          message[0] is! int ||
          message[1] is! int ||
          message[2] is! int) {
        return;
      }
      final stage = AudioExportStage.fromValue(message[0]! as int);
      if (stage == null) {
        return;
      }
      _audioExportProgress = AudioExportProgress(
        stage: stage,
        completedUnits: message[1]! as int,
        totalUnits: message[2]! as int,
      );
      notifyListeners();
    });
    try {
      final result = await _runAudioExportInIsolate(
        source: source,
        extension: extension,
        offset: offset,
        quality: quality,
        baseName: baseName,
        cancelAddress: cancelAddress,
        progressSendPort: progressSendPort,
      );
      await progressSubscription.cancel();
      progressSubscription = null;
      progressPort.close();
      if (cancellation.isRequested) {
        throw const AudioExportCancelled();
      }
      _audioExportProgress = AudioExportProgress(
        stage: AudioExportStage.saving,
        completedUnits: 0,
        totalUnits: result.bytes.length,
      );
      notifyListeners();
      try {
        await save(result, () => cancellation.isRequested, (
          completedUnits,
          totalUnits,
        ) {
          if (!identical(_audioExportCancellation, cancellation)) {
            return;
          }
          _audioExportProgress = AudioExportProgress(
            stage: AudioExportStage.saving,
            completedUnits: completedUnits,
            totalUnits: totalUnits,
          );
          notifyListeners();
        });
      } on Object {
        if (cancellation.isRequested) {
          throw const AudioExportCancelled();
        }
        rethrow;
      }
      if (cancellation.isRequested) {
        throw const AudioExportCancelled();
      }
    } on OtgyException catch (error, stack) {
      recordDiagnosticException('audio.export', error, stack);
      if (error.domain == 9 && error.code == 1) {
        throw const AudioExportCancelled();
      }
      rethrow;
    } catch (error, stack) {
      recordDiagnosticException('audio.export', error, stack);
      rethrow;
    } finally {
      await progressSubscription?.cancel();
      progressPort.close();
      if (identical(_audioExportCancellation, cancellation)) {
        _audioExportCancellation = null;
        _audioExportProgress = null;
        _audioExportCancellationRequested = false;
      }
      cancellation.dispose();
      _isExportingAudio = false;
      _setBusy(false);
    }
  }

  void cancelAudioExport() {
    if (!_isExportingAudio ||
        OtgyNative.coordinateApplicationOperation(mode: 2, operation: 3) != 0) {
      return;
    }
    _audioExportCancellation?.request();
    _audioExportCancellationRequested = true;
    notifyListeners();
  }

  void importSlkBytes(Uint8List bytes, String fileName) {
    if (!generalActionsEnabled) {
      recordDiagnostic('project.importSlk', {'blocked': true});
      return;
    }
    final pending = _diagnosticCall(
      'project.importSlk',
      () => NativeChartEditor.importSlk(bytes),
    );
    _replaceEditor(pending);
    _sourceKind = WorkspaceSourceKind.importedSlk;
    _sourceName = fileName;
    _draftId = null;
    _savedPayload = null;
    _isDirty = true;
    _projectRevision++;
    _currentRow = 0;
    notifyListeners();
    recordDiagnostic('project.importSlk', {'changed': true});
  }

  bool loadFormalDraft({
    required Uint8List payload,
    required String draftId,
    required String draftName,
  }) {
    if (!generalActionsEnabled) {
      recordDiagnostic('project.load', {'blocked': true});
      return false;
    }
    final decoded = _diagnosticCall(
      'project.load',
      () => NativeChartEditor.decodeFormalWithRepair(payload),
    );
    _replaceEditor(decoded.editor);
    _bpm = decoded.bpm;
    _offset = decoded.offset;
    _resetOffsetBeatAdjustment();
    _sourceKind = WorkspaceSourceKind.savedDraft;
    _sourceName = draftName;
    _draftId = draftId;
    _savedPayload = decoded.repaired ? null : Uint8List.fromList(payload);
    _isDirty = decoded.repaired;
    _projectRevision++;
    _currentRow = 0;
    _synchronizeAutomaticEditZoom();
    notifyListeners();
    recordDiagnostic('project.load', {
      'changed': true,
      'repaired': decoded.repaired,
    });
    return decoded.repaired;
  }

  bool restoreRecovery(Uint8List payload) {
    if (!generalActionsEnabled) {
      recordDiagnostic('project.restore', {'blocked': true});
      return false;
    }
    final pending = _diagnosticCall(
      'project.restore',
      () => NativeChartEditor.decodeRecoveryWithRepair(payload),
    );
    _replaceEditor(pending.editor);
    _sourceKind = WorkspaceSourceKind.untitled;
    _sourceName = null;
    _draftId = null;
    _savedPayload = null;
    _isDirty = true;
    _projectRevision++;
    _currentRow = 0;
    notifyListeners();
    recordDiagnostic('project.restore', {
      'changed': true,
      'repaired': pending.repaired,
    });
    return pending.repaired;
  }

  Uint8List encodeFormalDraft() =>
      _editor.encodeFormal(bpm: _bpm, offset: _offset);

  Uint8List encodeRecoverySnapshot() => _editor.encodeRecovery();

  SlkExport exportSlk() {
    if (_isBusy || !generalActionsEnabled) {
      throw StateError('SLK export is not allowed now.');
    }
    return _editor.exportSlk();
  }

  void markSaved({
    required String draftId,
    required String draftName,
    required Uint8List payload,
  }) {
    _sourceKind = WorkspaceSourceKind.savedDraft;
    _sourceName = draftName;
    _draftId = draftId;
    _savedPayload = Uint8List.fromList(payload);
    _isDirty = false;
    _notice = const EditorNotice('草稿已保存');
    notifyListeners();
    recordDiagnostic('project.saved');
  }

  String get suggestedProjectBaseName {
    final name = _sourceName;
    if (name == null || name.isEmpty) {
      return 'chart';
    }
    final dot = name.lastIndexOf('.');
    return dot > 0 ? name.substring(0, dot) : name;
  }

  String get suggestedAudioBaseName =>
      OtgyNative.defaultAudioExportBaseName(_audioFileName ?? 'audio');

  Future<void> togglePlayback() async {
    if (isChartPlaying) {
      await pausePlayback();
      return;
    }
    if (isRhythmTesting) {
      return;
    }
    await _startPlayback(EditorPlaybackMode.chartPreview);
  }

  Future<void> toggleRhythmTest() async {
    if (isRhythmTesting) {
      await pausePlayback();
      return;
    }
    if (isChartPlaying) {
      return;
    }
    if (_bpm == 0) {
      showNotice('请先设置 BPM');
      return;
    }
    await _startPlayback(EditorPlaybackMode.rhythmTest);
  }

  Future<void> _startPlayback(EditorPlaybackMode mode) async {
    final audio = _audio;
    if (audio == null) {
      showNotice('请先加载音频');
      return;
    }
    if (!generalActionsEnabled) {
      showNotice('请先退出选择或粘贴模式');
      return;
    }
    if (_isBusy) {
      return;
    }
    _setBusy(true);
    try {
      final cursorSeconds = OtgyNative.sixteenthRowToSeconds(
        bpm: projectedBpm,
        offset: _offset,
        row: _currentRow,
      );
      final start = OtgyNative.startPlayback(
        audioDurationSeconds: audio.durationSeconds,
        cursorTimelineSeconds: cursorSeconds,
      );
      if (!start.isPlaying) {
        return;
      }
      PlaybackPcm playback;
      if (mode == EditorPlaybackMode.rhythmTest) {
        _invalidatePlaybackSoundSession();
        final soundBank = await _ensurePlaybackSoundBank();
        playback = audio.renderRhythmTestPlayback(
          soundBank: soundBank,
          language: _playbackSoundLanguage,
          bpm: _bpm,
          offset: _offset,
          timelineStartSeconds: start.timelineSeconds,
        );
      } else if (_markers.isNotEmpty) {
        final soundBank = await _ensurePlaybackSoundBank();
        final seed = _soundSeedForPlayback();
        playback = audio.renderChartPlayback(
          editor: _editor,
          soundBank: soundBank,
          language: _playbackSoundLanguage,
          danceMode: _playbackDanceMode,
          transitionSoundsEnabled: _transitionSoundsEnabled,
          randomSeed: seed,
          bpm: projectedBpm,
          offset: _offset,
          timelineStartSeconds: start.timelineSeconds,
        );
      } else {
        playback = audio.renderPlayback(start.timelineSeconds);
      }
      if (playback.frameCount == 0) {
        _applyPlaybackPosition(
          PlaybackPosition(
            isPlaying: false,
            timelineSeconds: audio.durationSeconds,
            audioDurationSeconds: audio.durationSeconds,
          ),
        );
        notifyListeners();
        return;
      }
      await _playbackOutput.start(
        sampleRate: playback.sampleRate,
        interleavedPcm16le: playback.interleavedPcm16le,
      );
      _playbackStart = start;
      _playbackSampleRate = playback.sampleRate;
      _playbackFrameCount = playback.frameCount;
      _playbackPlayedFrames = 0;
      _playbackMode = mode;
      _syncKeepScreenOn();
      final generation = ++_playbackGeneration;
      _playbackTimer?.cancel();
      _playbackTimer = Timer.periodic(
        const Duration(milliseconds: 33),
        (_) => unawaited(_pollPlayback(generation)),
      );
      notifyListeners();
    } catch (error, stack) {
      recordDiagnosticException('playback.start', error, stack);
      _playbackGeneration++;
      _playbackMode = EditorPlaybackMode.idle;
      _syncKeepScreenOn();
      _playbackStart = null;
      unawaited(_playbackOutput.dispose());
      showNotice(
        mode == EditorPlaybackMode.rhythmTest
            ? '无法启动节奏测试'
            : _markers.isNotEmpty
            ? '无法加载或混合谱面音效'
            : '无法启动音频播放',
        error: true,
      );
    } finally {
      _setBusy(false);
    }
  }

  Future<void> pausePlayback() async {
    if (!isPlaying) {
      return;
    }
    final generation = _playbackGeneration;
    try {
      final state = await _playbackOutput.pause();
      if (generation != _playbackGeneration) {
        return;
      }
      _finishPlaybackAt(state.playedFrames, forcePause: true);
    } catch (error, stack) {
      recordDiagnosticException('playback.pause', error, stack);
      if (generation != _playbackGeneration) {
        return;
      }
      _finishPlaybackAt(_playbackPlayedFrames, forcePause: true);
    }
  }

  Future<void> _pollPlayback(int generation) async {
    if (!isPlaying ||
        generation != _playbackGeneration ||
        _playbackPollActive) {
      return;
    }
    _playbackPollActive = true;
    try {
      final state = await _playbackOutput.query();
      if (generation != _playbackGeneration) {
        return;
      }
      _finishPlaybackAt(state.playedFrames, forcePause: !state.playing);
    } catch (error, stack) {
      recordDiagnosticException('playback.query', error, stack);
      if (generation != _playbackGeneration) {
        return;
      }
      _finishPlaybackAt(_playbackPlayedFrames, forcePause: true);
      showNotice('音频输出已中断', error: true);
    } finally {
      _playbackPollActive = false;
    }
  }

  void _finishPlaybackAt(int frames, {required bool forcePause}) {
    final start = _playbackStart;
    if (start == null) {
      return;
    }
    _playbackPlayedFrames = math.max(0, math.min(frames, _playbackFrameCount));
    var position = OtgyNative.advancePlaybackFrames(
      start: start,
      playedFrames: _playbackPlayedFrames,
      sampleRate: _playbackSampleRate,
    );
    if (forcePause && position.isPlaying) {
      position = OtgyNative.pausePlayback(position);
    }
    _applyPlaybackPosition(position);
    if (!position.isPlaying || forcePause) {
      final completedMode = _playbackMode;
      _playbackTimer?.cancel();
      _playbackTimer = null;
      _playbackStart = null;
      _playbackMode = EditorPlaybackMode.idle;
      _syncKeepScreenOn();
      _playbackGeneration++;
      if (completedMode == EditorPlaybackMode.chartPreview &&
          _playbackSoundSeed != null &&
          position.timelineSeconds < position.audioDurationSeconds) {
        _playbackSoundResumeRow = _currentRow;
      } else {
        _invalidatePlaybackSoundSession();
      }
    }
    notifyListeners();
  }

  void _applyPlaybackPosition(PlaybackPosition position) {
    final row = OtgyNative.secondsToSixteenthRow(
      bpm: projectedBpm,
      offset: _offset,
      seconds: position.timelineSeconds,
    );
    _currentRow = row.clamp(0, timelineRange.endRow).toDouble();
  }

  void toggleWaveform() {
    _waveformVisible = !_waveformVisible;
    notifyListeners();
  }

  Future<void> setWaveformFilter(WaveformFilter value) async {
    if (!navigationEnabled ||
        isPlaying ||
        _isBusy ||
        value == _waveformFilter) {
      return;
    }
    _waveformFilter = value;
    await _regenerateWaveform(includePlayback: true);
  }

  Future<void> setWaveformFilterStrength(double value) async {
    final next = value.clamp(0.0, 1.0).toDouble();
    if (!navigationEnabled ||
        isPlaying ||
        _isBusy ||
        next == _waveformFilterStrength) {
      return;
    }
    _waveformFilterStrength = next;
    if (_waveformFilter != WaveformFilter.none) {
      await _regenerateWaveform(includePlayback: true);
    } else {
      notifyListeners();
    }
  }

  void setWaveformOverlayOriginal(bool value) {
    if (!navigationEnabled ||
        isPlaying ||
        _isBusy ||
        value == _waveformOverlayOriginal) {
      return;
    }
    _waveformOverlayOriginal = value;
    notifyListeners();
  }

  void setPlaybackSoundLanguage(PlaybackSoundLanguage value) {
    if (!generalActionsEnabled ||
        isPlaying ||
        _isBusy ||
        value == _playbackSoundLanguage) {
      return;
    }
    _playbackSoundLanguage = value;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void setPlaybackSoundVariant(PlaybackSoundVariant value) {
    if (!generalActionsEnabled ||
        isPlaying ||
        _isBusy ||
        value == _playbackSoundVariant) {
      return;
    }
    // The variant menu is only reachable while no playback start is busy, so
    // no sound bank load can be in flight here.
    _playbackSoundVariant = value;
    _playbackSoundBank?.dispose();
    _playbackSoundBank = null;
    _playbackSoundBankFuture = null;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void setPlaybackDanceMode(PlaybackDanceMode value) {
    if (!generalActionsEnabled ||
        isPlaying ||
        _isBusy ||
        value == _playbackDanceMode) {
      return;
    }
    _playbackDanceMode = value;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void setTransitionSoundsEnabled(bool value) {
    if (!generalActionsEnabled ||
        isPlaying ||
        _isBusy ||
        value == _transitionSoundsEnabled) {
      return;
    }
    _transitionSoundsEnabled = value;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void moveToRow(double value) {
    if (!navigationEnabled) {
      return;
    }
    final end = timelineRange.endRow;
    final next = value.clamp(0, end).toDouble();
    if (next == _currentRow) {
      return;
    }
    _currentRow = next;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void moveToSongSeconds(double seconds) {
    if (!seconds.isFinite || seconds < 0 || !navigationEnabled) {
      return;
    }
    final row = OtgyNative.secondsToSixteenthRow(
      bpm: projectedBpm,
      offset: _offset,
      seconds: seconds,
    );
    moveToRow(row);
  }

  void moveByPixels(double pixels) {
    if (rowSpacing <= 0) {
      return;
    }
    moveToRow(_currentRow + pixels / rowSpacing);
  }

  Future<void> setZoom(double value) async {
    final next = (value * 4).round() / 4;
    final clamped = next.clamp(1, 19).toDouble();
    if (clamped == _zoom || _isBusy || !navigationEnabled) {
      return;
    }
    _editZoomAutomatic = false;
    _zoom = clamped;
    await _regenerateWaveform();
  }

  Future<void> setScale(double value) async {
    final next = (value * 4).round() / 4;
    final clamped = next.clamp(1, 10).toDouble();
    if (clamped == _scale || _isBusy || !navigationEnabled) {
      return;
    }
    _scale = clamped;
    await _regenerateWaveform(includePlayback: true);
  }

  void dismissNotice() {
    if (_notice != null) {
      _notice = null;
      notifyListeners();
    }
  }

  void showNotice(String message, {bool error = false}) {
    _notice = EditorNotice(
      message,
      kind: error ? EditorNoticeKind.error : EditorNoticeKind.info,
    );
    notifyListeners();
  }

  double get _effectiveScale {
    final mobile =
        defaultTargetPlatform == TargetPlatform.iOS ||
        defaultTargetPlatform == TargetPlatform.android;
    return mobile ? 4 : _scale;
  }

  Future<void> _regenerateWaveform({bool includePlayback = false}) async {
    _setBusy(true);
    try {
      final audio = _audio;
      if (audio == null) {
        _refreshViewMetrics();
      } else {
        final editWaveform = _generateWaveformProjection(audio, _zoom);
        final editFilteredWaveform = _waveformFilter == WaveformFilter.none
            ? (
                blocks: const <WaveformBlock>[],
                pixelsPerSecond: editWaveform.pixelsPerSecond,
              )
            : _generateWaveformProjection(
                audio,
                _zoom,
                filter: _waveformFilter,
              );
        final playbackWaveform = includePlayback
            ? _zoom == _playbackVisualZoom
                  ? editWaveform
                  : _generateWaveformProjection(audio, _playbackVisualZoom)
            : null;
        final playbackFilteredWaveform =
            includePlayback && _waveformFilter != WaveformFilter.none
            ? _zoom == _playbackVisualZoom
                  ? editFilteredWaveform
                  : _generateWaveformProjection(
                      audio,
                      _playbackVisualZoom,
                      filter: _waveformFilter,
                    )
            : null;
        _waveformBlocks = editWaveform.blocks;
        _filteredWaveformBlocks = editFilteredWaveform.blocks;
        _pixelsPerSecond = editWaveform.pixelsPerSecond;
        if (playbackWaveform != null) {
          _playbackWaveformBlocks = playbackWaveform.blocks;
        }
        if (playbackFilteredWaveform != null) {
          _playbackFilteredWaveformBlocks = playbackFilteredWaveform.blocks;
        }
      }
      notifyListeners();
    } catch (error, stack) {
      recordDiagnosticException('waveform.generate', error, stack);
      rethrow;
    } finally {
      _setBusy(false);
    }
  }

  void _refreshViewMetrics() {
    _pixelsPerSecond = _pixelsPerSecondForZoom(_zoom);
  }

  ({List<WaveformBlock> blocks, int pixelsPerSecond})
  _generateWaveformProjection(
    NativeAudio audio,
    double zoom, {
    WaveformFilter filter = WaveformFilter.none,
  }) {
    final first = audio.generateWaveform(
      zoom: zoom,
      scale: _effectiveScale,
      blockIndex: 0,
      shape: WaveformShape.rectified,
      luminance: WaveformLuminance.uniform,
      sampling: WaveformSampling.twoX,
      filter: filter,
      filterStrength: _waveformFilterStrength,
    );
    final count = math.max(
      1,
      (audio.durationSeconds * first.pixelsPerSecond / 128).ceil(),
    );
    final blocks = <WaveformBlock>[first];
    for (var index = 1; index < count; index++) {
      blocks.add(
        audio.generateWaveform(
          zoom: zoom,
          scale: _effectiveScale,
          blockIndex: index,
          shape: WaveformShape.rectified,
          luminance: WaveformLuminance.uniform,
          sampling: WaveformSampling.twoX,
          filter: filter,
          filterStrength: _waveformFilterStrength,
        ),
      );
    }
    return (
      blocks: List.unmodifiable(blocks),
      pixelsPerSecond: first.pixelsPerSecond,
    );
  }

  int _pixelsPerSecondForZoom(double zoom) => _zoomPixelsPerSecond.putIfAbsent(
    zoom,
    () => OtgyNative.waveformPixelsPerSecond(zoom, _effectiveScale),
  );

  double _automaticEditZoomForBpm(double bpm) {
    if (!bpm.isFinite || bpm <= 0) {
      return _zoom;
    }
    var bestZoom = 1.0;
    var bestDistance = double.infinity;
    for (var quarterStep = 4; quarterStep <= 76; quarterStep++) {
      final zoom = quarterStep / 4;
      final pixelsPerSecond = _pixelsPerSecondForZoom(zoom);
      final rowSpacing = pixelsPerSecond * 15 / bpm;
      final distance = (rowSpacing - targetEditorRowSpacing).abs();
      if (distance < bestDistance) {
        bestZoom = zoom;
        bestDistance = distance;
      }
    }
    return bestZoom;
  }

  void _resetOffsetBeatAdjustment() {
    _offsetBeatBaseline = _offset;
    _offsetBeatAdjustment = 0;
  }

  void _synchronizeAutomaticEditZoom() {
    if (!_editZoomAutomatic || _bpm <= 0) {
      return;
    }
    final next = _automaticEditZoomForBpm(_bpm);
    if (next == _zoom) {
      return;
    }
    _zoom = next;
    if (_audio == null) {
      _refreshViewMetrics();
    } else {
      unawaited(_regenerateWaveform());
    }
  }

  void _refreshProjection() {
    _markers = List.unmodifiable(_editor.markers());
    _showtimeMoves = List.unmodifiable(_editor.showtimeMoves());
  }

  void _replaceEditor(NativeChartEditor pending) {
    final previous = _editor;
    _editor = pending;
    _chartInteractionMode = EditorChartInteractionMode.normal;
    _pastePreview = null;
    _pastePreviewAnchorRow = null;
    _refreshProjection();
    _invalidatePlaybackSoundSession();
    previous.dispose();
  }

  void _commitChartChange() {
    _projectRevision++;
    _invalidatePlaybackSoundSession();
    _updateDirty();
  }

  Future<NativePlaybackSoundBank> _ensurePlaybackSoundBank() async {
    final current = _playbackSoundBank;
    if (current != null) {
      return current;
    }
    final pending = _playbackSoundBankFuture ??= _loadPlaybackSoundBank();
    try {
      final bank = await pending;
      _playbackSoundBank = bank;
      if (identical(_playbackSoundBankFuture, pending)) {
        _playbackSoundBankFuture = null;
      }
      return bank;
    } on Object {
      if (identical(_playbackSoundBankFuture, pending)) {
        _playbackSoundBankFuture = null;
      }
      rethrow;
    }
  }

  Future<NativePlaybackSoundBank> _loadPlaybackSoundBank() async {
    final assets = <PlaybackSoundAsset>[];
    for (final language in PlaybackSoundLanguage.values) {
      final directory = language == PlaybackSoundLanguage.chinese ? 'zh' : 'en';
      for (final (id, fileName) in _playbackSoundFiles) {
        final replaced =
            language == PlaybackSoundLanguage.chinese &&
            _trimmedPlaybackSoundFiles.contains(fileName);
        final trimmed =
            replaced && _playbackSoundVariant == PlaybackSoundVariant.trimmed;
        // The replaced originals are kept out of the app bundle. Restore
        // `assets/sounds_original/zh/` in pubspec.yaml before enabling the
        // original sound-test variant.
        final original =
            replaced && _playbackSoundVariant == PlaybackSoundVariant.original;
        final shared = _sharedPlaybackSoundFiles.contains(fileName);
        final data = await rootBundle.load(
          shared
              ? 'assets/sounds/common/$fileName.wav'
              : trimmed
              ? 'assets/sounds_trimmed/zh/$fileName.wav'
              : original
              ? 'assets/sounds_original/zh/$fileName.wav'
              : 'assets/sounds/$directory/$fileName.wav',
        );
        assets.add(
          PlaybackSoundAsset(
            language: language,
            id: id,
            wavBytes: data.buffer.asUint8List(
              data.offsetInBytes,
              data.lengthInBytes,
            ),
          ),
        );
      }
    }
    return NativePlaybackSoundBank.decode(assets);
  }

  int _soundSeedForPlayback() {
    final existing = _playbackSoundSeed;
    if (existing != null && _playbackSoundResumeRow == _currentRow) {
      _playbackSoundResumeRow = null;
      return existing;
    }
    final seed =
        (_playbackSoundRandom.nextInt(1 << 30) << 30) |
        _playbackSoundRandom.nextInt(1 << 30);
    _playbackSoundSeed = seed;
    _playbackSoundResumeRow = null;
    return seed;
  }

  void _invalidatePlaybackSoundSession() {
    _playbackSoundSeed = null;
    _playbackSoundResumeRow = null;
  }

  /// Keeps the display awake only while playback is running. Failure is
  /// ignored on purpose: an unavailable platform implementation must never
  /// abort or interrupt playback.
  void _syncKeepScreenOn() {
    unawaited(_applyKeepScreenOn(_playbackMode != EditorPlaybackMode.idle));
  }

  Future<void> _applyKeepScreenOn(bool enabled) async {
    try {
      await _keepScreenOn(enabled);
    } catch (_) {
      // Best effort only.
    }
  }

  void _updateDirty() {
    final saved = _savedPayload;
    if (saved == null) {
      _isDirty = _projectRevision > 0 || _bpm != 100 || _offset != 0;
    } else {
      _isDirty = !_bytesEqual(
        saved,
        _editor.encodeFormal(bpm: _bpm, offset: _offset),
      );
    }
    notifyListeners();
  }

  void _setBusy(bool value) {
    if (_isBusy != value) {
      _isBusy = value;
      notifyListeners();
    }
  }

  static String _editMessage(int code) => switch (code) {
    2 => '当前位置已有标记',
    3 => '不能在结束符后追加',
    4 => '不可放置',
    5 => '没有可复制的 NPC 演示',
    6 => '生成范围内存在其他标记',
    7 => '自动生成标记无法编辑',
    8 => 'Showtime 位置不合法',
    9 => '没有剩余舞步可以生成',
    10 => 'Showtime 范围内存在其他标记',
    11 => '谱面草稿无法编辑',
    12 => '谱面行范围溢出',
    13 => '谱面标识已耗尽',
    14 => '内存不足，无法完成修改',
    17 => '该标记不属于可选择的舞步',
    18 => '剪贴板为空',
    19 => '粘贴范围内存在其他标记',
    20 => 'Showtime 内容不一致，请删除后重新生成',
    21 => '剪切来源已变化，原内容未删除',
    _ => '没有可应用的修改',
  };

  @override
  void dispose() {
    _playbackTimer?.cancel();
    _audioExportCancellation?.request();
    unawaited(_applyKeepScreenOn(false));
    unawaited(_playbackOutput.dispose());
    _audio?.dispose();
    _playbackSoundBank?.dispose();
    _editor.dispose();
    super.dispose();
  }
}

bool _bytesEqual(Uint8List left, Uint8List right) {
  if (left.length != right.length) {
    return false;
  }
  for (var index = 0; index < left.length; index++) {
    if (left[index] != right[index]) {
      return false;
    }
  }
  return true;
}
