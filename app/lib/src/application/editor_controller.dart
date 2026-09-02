import 'dart:async';
import 'dart:isolate';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:otgy_native/otgy_native.dart';

import '../infrastructure/playback_output.dart';

enum EditorNoticeKind { info, error }

enum WorkspaceSourceKind { untitled, importedSlk, savedDraft }

bool mobileChartEditingEnabled(double rowSpacing) => rowSpacing >= 40;

const double targetEditorRowSpacing = 46;
const double playbackVisualZoom = 8;

typedef AudioExportSaveCallback = Future<void> Function(
  AudioExportResult result,
  bool Function() isCancelled,
  void Function(int completedUnits, int totalUnits) onProgress,
);

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
  EditorController({NativeChartEditor? editor, PlaybackOutput? playbackOutput})
    : _editor = editor ?? NativeChartEditor.createEmpty(),
      _playbackOutput = playbackOutput ?? const MethodChannelPlaybackOutput() {
    _zoom = _automaticEditZoomForBpm(_bpm);
    _refreshProjection();
    _refreshViewMetrics();
    _savedPayload = _editor.encodeFormal(bpm: _bpm, offset: _offset);
  }

  NativeChartEditor _editor;
  final PlaybackOutput _playbackOutput;
  NativeAudio? _audio;
  NativePlaybackSoundBank? _playbackSoundBank;
  Future<NativePlaybackSoundBank>? _playbackSoundBankFuture;
  List<ChartMarker> _markers = const [];
  List<ShowtimeMove> _showtimeMoves = const [];
  List<WaveformBlock> _waveformBlocks = const [];
  List<WaveformBlock> _playbackWaveformBlocks = const [];
  EditorNotice? _notice;
  bool _isBusy = false;
  bool _isExportingAudio = false;
  bool _audioExportCancellationRequested = false;
  bool _isPlaying = false;
  bool _isDirty = false;
  bool _waveformVisible = true;
  PlaybackSoundLanguage _playbackSoundLanguage = PlaybackSoundLanguage.chinese;
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

  List<ChartMarker> get markers => _markers;
  List<ShowtimeMove> get showtimeMoves => _showtimeMoves;
  List<WaveformBlock> get waveformBlocks => _waveformBlocks;
  List<WaveformBlock> get canvasWaveformBlocks {
    if (_isPlaying &&
        playbackVisualPixelsPerSecond < _pixelsPerSecond &&
        _playbackWaveformBlocks.isNotEmpty) {
      return _playbackWaveformBlocks;
    }
    return _waveformBlocks;
  }

  EditorNotice? get notice => _notice;
  bool get isBusy => _isBusy;
  bool get isExportingAudio => _isExportingAudio;
  bool get audioExportCancellationRequested =>
      _audioExportCancellationRequested;
  AudioExportProgress? get audioExportProgress => _audioExportProgress;
  bool get isPlaying => _isPlaying;
  bool get generalActionsEnabled =>
      OtgyNative.coordinateApplicationOperation(
        mode: _isPlaying
            ? 1
            : _isExportingAudio
            ? 2
            : 0,
        operation: 0,
      ) ==
      0;
  bool get isDirty => _isDirty;
  bool get waveformVisible => _waveformVisible;
  PlaybackSoundLanguage get playbackSoundLanguage => _playbackSoundLanguage;
  PlaybackDanceMode get playbackDanceMode => _playbackDanceMode;
  bool get transitionSoundsEnabled => _transitionSoundsEnabled;
  bool get hasAudio => _audio != null;
  bool get canUndo => _editor.canUndo;
  bool get canRedo => _editor.canRedo;
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
        _pixelsPerSecondForZoom(playbackVisualZoom).toDouble(),
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
      return;
    }
    _setBusy(true);
    NativeAudio? pending;
    try {
      final pendingZoom = _automaticEditZoomForBpm(100);
      final dot = fileName.lastIndexOf('.');
      final extension = dot < 0 ? '' : fileName.substring(dot);
      pending = OtgyNative.decodeAudio(bytes, extension);
      final editWaveform = _generateWaveformProjection(pending, pendingZoom);
      final playbackWaveform = pendingZoom == playbackVisualZoom
          ? editWaveform
          : _generateWaveformProjection(pending, playbackVisualZoom);

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
      _pixelsPerSecond = editWaveform.pixelsPerSecond;
      _zoom = pendingZoom;
      _editZoomAutomatic = true;
      _bpm = 100;
      _offset = 0;
      _currentRow = 0;
      _invalidatePlaybackSoundSession();
      _notice = const EditorNotice('音频已加载');
      _updateDirty();
    } on Object {
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
    _updateDirty();
  }

  void nudgeOffsetByBeat(double beats) {
    if (_bpm == 0) {
      return;
    }
    setOffset((_offset + 60 / _bpm * beats).clamp(-30, 30).toDouble());
  }

  void clickCell(int column, int row, {required bool mobile}) {
    if (_isBusy || !generalActionsEnabled) {
      return;
    }
    if (mobile && !mobileChartEditingEnabled(rowSpacing)) {
      _notice = const EditorNotice('请放大后编辑');
      notifyListeners();
      return;
    }
    final result = _editor.click(column, math.max(0, row));
    if (result.changed) {
      _refreshProjection();
      _commitChartChange();
    } else {
      _notice = EditorNotice(_editMessage(result.code));
      notifyListeners();
    }
  }

  void undo() {
    if (!canUndo || _isBusy || !generalActionsEnabled) {
      return;
    }
    _editor.undo();
    _refreshProjection();
    _commitChartChange();
  }

  void redo() {
    if (!canRedo || _isBusy || !generalActionsEnabled) {
      return;
    }
    _editor.redo();
    _refreshProjection();
    _commitChartChange();
  }

  void clearChart() {
    if (_isBusy || !generalActionsEnabled) {
      return;
    }
    final result = _editor.clear();
    if (result.changed) {
      _refreshProjection();
      _commitChartChange();
    }
  }

  Future<List<AudioAnalysisCandidate>> analyzeAudio() {
    final cached = _analysisCandidates;
    if (cached != null) {
      return Future.value(cached);
    }
    final existing = _analysisFuture;
    if (existing != null) {
      return existing;
    }
    final bytes = _audioSourceBytes;
    final extension = _audioExtension;
    if (bytes == null || extension == null) {
      return Future.error(StateError('No audio is loaded.'));
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
      }
    });
    _analysisStage = SyncAnalysisStage.detectingOnsets;
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
                return _analysisCandidates!;
              }
              throw StateError('Audio changed while analysis was running.');
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
    } on OtgyException catch (error) {
      if (error.domain == 9 && error.code == 1) {
        throw const AudioExportCancelled();
      }
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
      return;
    }
    final pending = NativeChartEditor.importSlk(bytes);
    _replaceEditor(pending);
    _sourceKind = WorkspaceSourceKind.importedSlk;
    _sourceName = fileName;
    _draftId = null;
    _savedPayload = null;
    _isDirty = true;
    _projectRevision++;
    _currentRow = 0;
    notifyListeners();
  }

  void loadFormalDraft({
    required Uint8List payload,
    required String draftId,
    required String draftName,
  }) {
    if (!generalActionsEnabled) {
      return;
    }
    final decoded = NativeChartEditor.decodeFormal(payload);
    _replaceEditor(decoded.editor);
    _bpm = decoded.bpm;
    _offset = decoded.offset;
    _sourceKind = WorkspaceSourceKind.savedDraft;
    _sourceName = draftName;
    _draftId = draftId;
    _savedPayload = Uint8List.fromList(payload);
    _isDirty = false;
    _projectRevision++;
    _currentRow = 0;
    _synchronizeAutomaticEditZoom();
    notifyListeners();
  }

  void restoreRecovery(Uint8List payload) {
    if (!generalActionsEnabled) {
      return;
    }
    final pending = NativeChartEditor.decodeRecovery(payload);
    _replaceEditor(pending);
    _sourceKind = WorkspaceSourceKind.untitled;
    _sourceName = null;
    _draftId = null;
    _savedPayload = null;
    _isDirty = true;
    _projectRevision++;
    _currentRow = 0;
    notifyListeners();
  }

  Uint8List encodeFormalDraft() =>
      _editor.encodeFormal(bpm: _bpm, offset: _offset);

  Uint8List encodeRecoverySnapshot() => _editor.encodeRecovery();

  SlkExport exportSlk() => _editor.exportSlk();

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
    if (_isPlaying) {
      await pausePlayback();
      return;
    }
    final audio = _audio;
    if (audio == null || !generalActionsEnabled) {
      showNotice('请先加载音频');
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
      final usesChartSounds = _markers.isNotEmpty;
      PlaybackPcm playback;
      if (usesChartSounds) {
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
      _isPlaying = true;
      final generation = ++_playbackGeneration;
      _playbackTimer?.cancel();
      _playbackTimer = Timer.periodic(
        const Duration(milliseconds: 33),
        (_) => unawaited(_pollPlayback(generation)),
      );
      notifyListeners();
    } on Object {
      _playbackGeneration++;
      _isPlaying = false;
      _playbackStart = null;
      unawaited(_playbackOutput.dispose());
      showNotice(_markers.isNotEmpty ? '无法加载或混合谱面音效' : '无法启动音频播放', error: true);
    } finally {
      _setBusy(false);
    }
  }

  Future<void> pausePlayback() async {
    if (!_isPlaying) {
      return;
    }
    final generation = _playbackGeneration;
    try {
      final state = await _playbackOutput.pause();
      if (generation != _playbackGeneration) {
        return;
      }
      _finishPlaybackAt(state.playedFrames, forcePause: true);
    } on Object {
      if (generation != _playbackGeneration) {
        return;
      }
      _finishPlaybackAt(_playbackPlayedFrames, forcePause: true);
    }
  }

  Future<void> _pollPlayback(int generation) async {
    if (!_isPlaying ||
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
    } on Object {
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
      _playbackTimer?.cancel();
      _playbackTimer = null;
      _playbackStart = null;
      _isPlaying = false;
      _playbackGeneration++;
      if (_playbackSoundSeed != null &&
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

  void setPlaybackSoundLanguage(PlaybackSoundLanguage value) {
    if (_isPlaying || _isBusy || value == _playbackSoundLanguage) {
      return;
    }
    _playbackSoundLanguage = value;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void setPlaybackDanceMode(PlaybackDanceMode value) {
    if (_isPlaying || _isBusy || value == _playbackDanceMode) {
      return;
    }
    _playbackDanceMode = value;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void setTransitionSoundsEnabled(bool value) {
    if (_isPlaying || _isBusy || value == _transitionSoundsEnabled) {
      return;
    }
    _transitionSoundsEnabled = value;
    _invalidatePlaybackSoundSession();
    notifyListeners();
  }

  void moveToRow(double value) {
    if (!generalActionsEnabled) {
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

  void moveByPixels(double pixels) {
    if (rowSpacing <= 0) {
      return;
    }
    moveToRow(_currentRow + pixels / rowSpacing);
  }

  Future<void> setZoom(double value) async {
    final next = (value * 4).round() / 4;
    final clamped = next.clamp(1, 19).toDouble();
    if (clamped == _zoom || _isBusy || !generalActionsEnabled) {
      return;
    }
    _editZoomAutomatic = false;
    _zoom = clamped;
    await _regenerateWaveform();
  }

  Future<void> setScale(double value) async {
    final next = (value * 4).round() / 4;
    final clamped = next.clamp(1, 10).toDouble();
    if (clamped == _scale || _isBusy || !generalActionsEnabled) {
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
        final playbackWaveform = includePlayback
            ? _zoom == playbackVisualZoom
                  ? editWaveform
                  : _generateWaveformProjection(audio, playbackVisualZoom)
            : null;
        _waveformBlocks = editWaveform.blocks;
        _pixelsPerSecond = editWaveform.pixelsPerSecond;
        if (playbackWaveform != null) {
          _playbackWaveformBlocks = playbackWaveform.blocks;
        }
      }
      notifyListeners();
    } finally {
      _setBusy(false);
    }
  }

  void _refreshViewMetrics() {
    _pixelsPerSecond = _pixelsPerSecondForZoom(_zoom);
  }

  ({List<WaveformBlock> blocks, int pixelsPerSecond})
  _generateWaveformProjection(NativeAudio audio, double zoom) {
    final first = audio.generateWaveform(
      zoom: zoom,
      scale: _effectiveScale,
      blockIndex: 0,
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
        final data = await rootBundle.load(
          'assets/sounds/$directory/$fileName.wav',
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
    _ => '没有可应用的修改',
  };

  @override
  void dispose() {
    _playbackTimer?.cancel();
    _audioExportCancellation?.request();
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
