import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;

import 'package:file_selector/file_selector.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:otgy_native/otgy_native.dart';

import '../application/editor_controller.dart';
import '../infrastructure/audio_export_publisher.dart';
import '../infrastructure/local_diagnostics.dart';
import '../infrastructure/local_project_store.dart';
import '../infrastructure/local_ui_preferences.dart';
import 'draft_save_target.dart';
import 'editor_canvas.dart';
import 'editor_theme.dart';
import 'editor_toolbars.dart';
import 'screen_keypad_overlay.dart';

typedef ProjectStoreFactory = Future<LocalProjectStore> Function();

@visibleForTesting
bool hasSlkFileExtension(String fileName) =>
    fileName.toLowerCase().endsWith('.slk');

@visibleForTesting
String? normalizeAndroidSlkFileName(String fileName, String? mimeType) {
  if (hasSlkFileExtension(fileName)) {
    return fileName;
  }
  final normalizedMimeType = mimeType?.split(';').first.trim().toLowerCase();
  if (normalizedMimeType != 'application/octet-stream') {
    return null;
  }
  final slash = math.max(fileName.lastIndexOf('/'), fileName.lastIndexOf(r'\'));
  final dot = fileName.lastIndexOf('.');
  final baseName = dot > slash ? fileName.substring(0, dot) : fileName;
  return '${baseName.isEmpty ? 'chart' : baseName}.slk';
}

@visibleForTesting
bool hasSupportedAudioFileExtension(String fileName) {
  final lower = fileName.toLowerCase();
  return lower.endsWith('.mp3') || lower.endsWith('.ogg');
}

@visibleForTesting
String? normalizeAndroidAudioFileName(String fileName, String? mimeType) {
  if (hasSupportedAudioFileExtension(fileName)) {
    return fileName;
  }
  final normalizedMimeType = mimeType?.split(';').first.trim().toLowerCase();
  final extension = switch (normalizedMimeType) {
    'audio/mpeg' || 'audio/mp3' || 'audio/x-mp3' => '.mp3',
    'audio/ogg' ||
    'audio/x-ogg' ||
    'application/ogg' ||
    'application/x-ogg' => '.ogg',
    _ => null,
  };
  if (extension == null) {
    return null;
  }
  final slash = math.max(fileName.lastIndexOf('/'), fileName.lastIndexOf(r'\'));
  final dot = fileName.lastIndexOf('.');
  final baseName = dot > slash ? fileName.substring(0, dot) : fileName;
  return '${baseName.isEmpty ? 'audio' : baseName}$extension';
}

class EditorScreen extends StatefulWidget {
  const EditorScreen({
    super.key,
    this.controller,
    this.storeFactory,
    this.diagnostics,
    this.preferences,
  });

  final EditorController? controller;
  final ProjectStoreFactory? storeFactory;
  final LocalDiagnostics? diagnostics;
  final LocalUiPreferences? preferences;

  @override
  State<EditorScreen> createState() => _EditorScreenState();
}

class _EditorScreenState extends State<EditorScreen>
    with WidgetsBindingObserver {
  static const _recoveryDebounce = Duration(milliseconds: 500);
  static const _lifecycleChannel = MethodChannel('io.onetwogoyeah/lifecycle');

  late final EditorController _controller;
  late final bool _ownsController;
  LocalProjectStore? _store;
  Timer? _recoveryTimer;
  int _observedProjectRevision = 0;
  int _flushedProjectRevision = 0;
  bool _lastDirty = false;
  EditorNotice? _shownNotice;
  bool _exitConfirmationOpen = false;
  bool _allowExit = false;
  bool _exportingDiagnostics = false;
  bool _diagnosticPickerActive = false;
  bool _diagnosticExportCancelled = false;

  LocalDiagnostics? get _diagnostics =>
      widget.diagnostics ?? LocalDiagnostics.instance;

  void _record(String event, Map<String, Object?> fields) {
    _diagnostics?.record(event, fields);
  }

  void _recordException(String operation, Object error, StackTrace stack) {
    _diagnostics?.recordException(operation, error, stack);
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _ownsController = widget.controller == null;
    _controller = widget.controller ?? EditorController();
    _observedProjectRevision = _controller.projectRevision;
    _flushedProjectRevision = _controller.projectRevision;
    _lastDirty = _controller.isDirty;
    _controller.addListener(_handleControllerChange);
    _lifecycleChannel.setMethodCallHandler(_handlePlatformLifecycleCall);
    unawaited(_initializeStore());
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden ||
        state == AppLifecycleState.detached) {
      unawaited(_controller.pausePlayback());
      _controller.cancelAudioExport();
      if (!_diagnosticPickerActive || state == AppLifecycleState.detached) {
        _diagnosticExportCancelled = true;
        unawaited(_flushRecovery());
      }
      unawaited(_diagnostics?.flush());
    }
    if (state == AppLifecycleState.detached && !_controller.isDirty) {
      unawaited(_store?.endSessionNormally());
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _lifecycleChannel.setMethodCallHandler(null);
    _recoveryTimer?.cancel();
    _controller.removeListener(_handleControllerChange);
    if (_ownsController) {
      _controller.dispose();
    }
    super.dispose();
  }

  Future<void> _initializeStore() async {
    try {
      final store = await (widget.storeFactory ?? LocalProjectStore.open)();
      final session = await store.beginSession();
      _record('store.session.started', {
        'interrupted': session.wasInterrupted,
        'hasRecovery': session.recoveryPayload != null,
      });
      if (!mounted) {
        return;
      }
      _store = store;
      final payload = session.recoveryPayload;
      if (!session.wasInterrupted || payload == null) {
        return;
      }
      try {
        final probe = NativeChartEditor.decodeRecovery(payload);
        probe.dispose();
      } on Object catch (error, stack) {
        _recordException('recovery.validate', error, stack);
        await store.deleteRecovery();
        _controller.showNotice('无法恢复上次编辑：恢复数据已损坏。', error: true);
        return;
      }
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) {
          unawaited(_offerRecovery(payload));
        }
      });
    } on Object catch (error, stack) {
      _recordException('store.initialize', error, stack);
      if (mounted) {
        _controller.showNotice('无法访问本地草稿存储。', error: true);
      }
    }
  }

  void _handleControllerChange() {
    final revision = _controller.projectRevision;
    final dirty = _controller.isDirty;
    final becameClean = _lastDirty && !dirty;
    _lastDirty = dirty;
    if (becameClean) {
      // A clean workspace no longer needs a recovery snapshot. This covers
      // undo back to the baseline and replacement by a clean SLK or draft.
      _recoveryTimer?.cancel();
      _recoveryTimer = null;
      unawaited(_deleteCleanRecovery());
    }
    if (revision == _observedProjectRevision) {
      return;
    }
    _observedProjectRevision = revision;
    if (!dirty) {
      return;
    }
    _recoveryTimer?.cancel();
    _recoveryTimer = Timer(_recoveryDebounce, () {
      unawaited(_flushRecovery());
    });
  }

  Future<void> _deleteCleanRecovery() async {
    final store = _store;
    if (store == null) {
      return;
    }
    try {
      await store.deleteRecovery();
      _record('recovery.delete.completed', {});
    } on Object catch (error, stack) {
      _recordException('recovery.delete', error, stack);
    }
  }

  Future<void> _flushRecovery() async {
    _recoveryTimer?.cancel();
    _recoveryTimer = null;
    final store = _store;
    final revision = _controller.projectRevision;
    if (store == null ||
        !_controller.isDirty ||
        revision == _flushedProjectRevision) {
      return;
    }
    try {
      final payload = _controller.encodeRecoverySnapshot();
      await store.writeRecovery(payload);
      _flushedProjectRevision = revision;
      _record('recovery.write.completed', {
        'revision': revision,
        'bytes': payload.length,
      });
    } on Object catch (error, stack) {
      _recordException('recovery.write', error, stack);
      if (mounted) {
        _controller.showNotice('无法更新恢复快照。', error: true);
      }
    }
  }

  Future<void> _offerRecovery(Uint8List payload) async {
    final restore = await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        title: const Text('恢复上次编辑？'),
        content: const Text('检测到上次会话未正常结束，可以恢复最后保存的谱面内容。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('放弃恢复'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('恢复上次编辑'),
          ),
        ],
      ),
    );
    if (!mounted) {
      return;
    }
    if (restore ?? false) {
      final repaired = _controller.restoreRecovery(payload);
      if (repaired) {
        await _flushRecovery();
        _controller.showNotice('已恢复并修复上次谱面编辑');
      } else {
        _controller.showNotice('已恢复上次谱面编辑');
      }
    } else {
      await _store?.deleteRecovery();
      _record('recovery.discarded', {});
    }
  }

  Future<void> _chooseAudio() async {
    final type = XTypeGroup(
      label: 'MP3 或 Ogg 音频',
      extensions: const ['mp3', 'ogg'],
      uniformTypeIdentifiers: defaultTargetPlatform == TargetPlatform.iOS
          ? const ['public.data']
          : const ['public.mp3', 'org.xiph.ogg-audio'],
    );
    XFile? file;
    try {
      file = await openFile(acceptedTypeGroups: [type]);
    } on Object catch (error, stack) {
      _recordException('audio.file-picker', error, stack);
      _controller.showNotice('无法打开音频文件选择器。', error: true);
      return;
    }
    if (file == null) {
      return;
    }
    final normalizedFileName = defaultTargetPlatform == TargetPlatform.android
        ? normalizeAndroidAudioFileName(file.name, file.mimeType)
        : hasSupportedAudioFileExtension(file.name)
        ? file.name
        : null;
    if (normalizedFileName == null) {
      _controller.showNotice('音频加载失败：请选择 .mp3 或 .ogg 文件。', error: true);
      return;
    }
    var loaded = false;
    try {
      loaded = await _controller.loadAudioBytes(
        await file.readAsBytes(),
        normalizedFileName,
      );
    } on Object catch (error, stack) {
      _recordException('audio.read', error, stack);
      _controller.showNotice('无法读取所选音频文件。', error: true);
      return;
    }
    if (loaded && mounted) {
      // Audio import starts the shared BPM/Offset analysis immediately so the
      // candidate dialog is ready without a second toolbar action.
      await _analyzeAudio();
    }
  }

  Future<void> _jumpToSongTime() async {
    final current = _controller.currentSongSeconds;
    final initialSeconds = current.isFinite ? math.max(0, current) : 0.0;
    final initialMinutes = initialSeconds ~/ 60;
    var minuteText = '$initialMinutes';
    var secondText = (initialSeconds - initialMinutes * 60).toStringAsFixed(3);
    final formKey = GlobalKey<FormState>();
    final result = await showDialog<({int minutes, double seconds})>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('跳转到歌曲时间'),
        content: Form(
          key: formKey,
          child: Row(
            children: [
              Expanded(
                child: TextFormField(
                  key: const ValueKey('jump-time-minutes'),
                  initialValue: minuteText,
                  autofocus: true,
                  decoration: const InputDecoration(labelText: '分钟'),
                  keyboardType: TextInputType.number,
                  textInputAction: TextInputAction.next,
                  validator: _validateSongMinutes,
                  onChanged: (value) => minuteText = value,
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: TextFormField(
                  key: const ValueKey('jump-time-seconds'),
                  initialValue: secondText,
                  decoration: const InputDecoration(labelText: '秒'),
                  keyboardType: const TextInputType.numberWithOptions(
                    decimal: true,
                  ),
                  textInputAction: TextInputAction.done,
                  validator: _validateSongSeconds,
                  onChanged: (value) => secondText = value,
                ),
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () {
              if (!formKey.currentState!.validate()) {
                return;
              }
              Navigator.pop(dialogContext, (
                minutes: int.parse(minuteText.trim()),
                seconds: double.parse(secondText.trim()),
              ));
            },
            child: const Text('跳转'),
          ),
        ],
      ),
    );
    if (!mounted || result == null) {
      return;
    }
    _controller.moveToSongSeconds(result.minutes * 60 + result.seconds);
  }

  Future<void> _analyzeAudio() async {
    if (!_controller.generalActionsEnabled) {
      return;
    }
    if (!_controller.hasAudio) {
      _controller.showNotice('加载音频');
      return;
    }
    var loading = _controller.analysisCandidates == null;
    var failed = false;
    var candidates = _controller.analysisCandidates;
    if (loading) {
      unawaited(
        _controller
            .analyzeAudio()
            .then<void>((value) {
              candidates = value;
              loading = false;
            })
            .catchError((Object _) {
              failed = true;
              loading = false;
            }),
      );
    }
    if (!mounted) {
      return;
    }
    await showDialog<void>(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (context, setDialogState) {
          if (loading) {
            Future<void>.delayed(const Duration(milliseconds: 80), () {
              if (dialogContext.mounted) {
                setDialogState(() {});
              }
            });
          }
          return AlertDialog(
            title: const Text('BPM 与 Offset 候选'),
            content: SizedBox(
              width: 420,
              child: loading
                  ? _AnalysisProgress(stage: _controller.analysisStage)
                  : failed
                  ? const Text('分析失败，请重试')
                  : _AnalysisCandidates(
                      candidates: candidates ?? const [],
                      onSelect: (candidate) {
                        _controller.applyAnalysisCandidate(candidate);
                        Navigator.pop(dialogContext);
                      },
                    ),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(dialogContext),
                child: const Text('关闭'),
              ),
            ],
          );
        },
      ),
    );
  }

  Future<void> _exportAudio() async {
    final policy = _controller.audioExportQuality;
    if (!_controller.hasAudio || policy == null) {
      _controller.showNotice('请先加载音频');
      return;
    }
    var quality = policy.defaultQuality;
    var baseName = _controller.suggestedAudioBaseName;
    final settings = await showDialog<({String baseName, int quality})>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setState) => AlertDialog(
          title: const Text('导出 Ogg 音频'),
          content: SizedBox(
            width: 380,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                TextFormField(
                  initialValue: baseName,
                  decoration: const InputDecoration(labelText: '文件基础名称'),
                  onChanged: (value) => baseName = value,
                ),
                const SizedBox(height: 16),
                Text('品质 $quality'),
                Slider(
                  value: quality.toDouble(),
                  min: 1,
                  max: policy.qualityCeiling.toDouble(),
                  divisions: policy.qualityCeiling > 1
                      ? policy.qualityCeiling - 1
                      : null,
                  label: '$quality',
                  onChanged: policy.qualityCeiling > 1
                      ? (value) {
                          setState(() => quality = value.round());
                        }
                      : null,
                ),
                if (_controller.audioEndExportSeconds != null)
                  Text(
                    '导出长度 '
                    '${_formatSongTime(_controller.audioEndExportSeconds!)}',
                    style: const TextStyle(
                      color: EditorColors.textMuted,
                      fontSize: 12,
                    ),
                  ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(context, (
                baseName: baseName,
                quality: quality,
              )),
              child: const Text('选择保存位置'),
            ),
          ],
        ),
      ),
    );
    if (settings == null) {
      return;
    }
    final time = DateTime.now();
    final suggestedName = OtgyNative.makeAudioExportFileName(
      baseName: settings.baseName,
      localTime: time,
    );
    const type = XTypeGroup(
      label: 'Ogg Vorbis 音频',
      extensions: ['ogg'],
      mimeTypes: ['audio/ogg'],
      uniformTypeIdentifiers: ['org.xiph.ogg-audio'],
    );
    String? platformDocumentUri;
    String? selectedPath;
    var exportCompleted = false;
    try {
      final usesPlatformDocument =
          defaultTargetPlatform == TargetPlatform.android ||
          defaultTargetPlatform == TargetPlatform.iOS;
      if (usesPlatformDocument) {
        platformDocumentUri = await AudioExportPublisher.selectPlatformDocument(
          suggestedName: suggestedName,
          mimeType: 'audio/ogg',
        );
        if (platformDocumentUri == null) {
          return;
        }
      } else {
        final location = await getSaveLocation(
          suggestedName: suggestedName,
          acceptedTypeGroups: const [type],
        );
        if (location == null) {
          return;
        }
        selectedPath = location.path;
      }
      await _controller.exportAudio(
        quality: settings.quality,
        baseName: settings.baseName,
        save: (result, isCancelled, onProgress) async {
          if (platformDocumentUri case final documentUri?) {
            await AudioExportPublisher.publishPlatformDocument(
              bytes: result.bytes,
              documentUri: documentUri,
              isCancelled: isCancelled,
              onProgress: onProgress,
            );
          } else {
            await AudioExportPublisher.publish(
              bytes: result.bytes,
              selectedPath: selectedPath!,
              baseName: settings.baseName,
              localTime: time,
              isCancelled: isCancelled,
              onProgress: onProgress,
            );
          }
        },
      );
      if (platformDocumentUri case final documentUri?) {
        await AudioExportPublisher.commitPlatformDocument(
          documentUri: documentUri,
        );
      }
      exportCompleted = true;
      _controller.showNotice('音频已导出');
    } on AudioExportCancelled {
      _controller.showNotice('导出已取消');
    } on Object catch (error, stackTrace) {
      if (mounted) {
        await _showAudioExportError(error, stackTrace);
      }
    } finally {
      if (!exportCompleted && platformDocumentUri != null) {
        try {
          await AudioExportPublisher.discardPlatformDocument(
            documentUri: platformDocumentUri,
          );
        } on Object catch (error, stackTrace) {
          if (mounted) {
            await _showAudioExportError(error, stackTrace);
          }
        }
      }
    }
  }

  Future<void> _importSlk() async {
    if (!await _confirmWorkspaceReplacement()) {
      return;
    }
    final type = XTypeGroup(
      label: 'SLK 谱面',
      extensions: const ['slk'],
      mimeTypes: const ['application/octet-stream'],
      uniformTypeIdentifiers: defaultTargetPlatform == TargetPlatform.iOS
          ? const ['public.data']
          : null,
    );
    XFile? file;
    try {
      file = await openFile(acceptedTypeGroups: [type]);
    } on Object catch (error, stackTrace) {
      if (mounted) {
        await _showOperationError(
          title: 'SLK 导入失败',
          operation: 'slk-import-file-picker',
          error: error,
          stackTrace: stackTrace,
        );
      }
      return;
    }
    if (file == null) {
      return;
    }
    final normalizedFileName = defaultTargetPlatform == TargetPlatform.android
        ? normalizeAndroidSlkFileName(file.name, file.mimeType)
        : hasSlkFileExtension(file.name)
        ? file.name
        : null;
    if (normalizedFileName == null) {
      _controller.showNotice('SLK 导入失败：请选择 .slk 文件。', error: true);
      return;
    }
    try {
      _controller.importSlkBytes(await file.readAsBytes(), normalizedFileName);
      _controller.showNotice('SLK 已导入');
    } on Object catch (error, stack) {
      _recordException('slk.import', error, stack);
      _controller.showNotice('SLK 导入失败：文件不是完整有效的成品谱面。', error: true);
    }
  }

  Future<void> _openDrafts() async {
    final store = _store;
    if (store == null) {
      _controller.showNotice('本地草稿存储尚未就绪');
      return;
    }
    while (mounted) {
      List<DraftSummary> drafts;
      try {
        drafts = await store.listDrafts();
      } on Object catch (error, stack) {
        _recordException('draft.list', error, stack);
        if (mounted) _controller.showNotice('无法读取本地草稿。', error: true);
        return;
      }
      if (!mounted) {
        return;
      }
      final action = await showDialog<_DraftAction>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('本地草稿'),
          content: SizedBox(
            width: 520,
            child: drafts.isEmpty
                ? const Padding(
                    padding: EdgeInsets.symmetric(vertical: 24),
                    child: Text('当前没有已保存草稿'),
                  )
                : ListView.separated(
                    shrinkWrap: true,
                    itemCount: drafts.length,
                    separatorBuilder: (_, _) => const Divider(height: 1),
                    itemBuilder: (context, index) {
                      final draft = drafts[index];
                      final isCurrent = draft.id == _controller.draftId;
                      return ListTile(
                        title: Text(draft.name),
                        subtitle: Text(_formatTime(draft.updatedAt.toLocal())),
                        trailing: Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            TextButton(
                              onPressed: () => Navigator.pop(
                                context,
                                _DraftAction.load(draft),
                              ),
                              child: const Text('加载'),
                            ),
                            IconButton(
                              onPressed: isCurrent
                                  ? null
                                  : () => Navigator.pop(
                                      context,
                                      _DraftAction.delete(draft),
                                    ),
                              tooltip: isCurrent ? '当前草稿不能删除' : '删除草稿',
                              icon: const Icon(Icons.delete_outline),
                            ),
                          ],
                        ),
                      );
                    },
                  ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('关闭'),
            ),
          ],
        ),
      );
      if (action == null) {
        return;
      }
      if (action.delete) {
        final confirmed = await _confirmDeleteDraft(action.draft.name);
        if (confirmed) {
          try {
            await store.deleteDraft(action.draft.id);
            _record('draft.delete.completed', {});
          } on Object catch (error, stack) {
            _recordException('draft.delete', error, stack);
            if (mounted) _controller.showNotice('草稿删除失败。', error: true);
          }
        }
        continue;
      }
      if (!await _confirmWorkspaceReplacement()) {
        continue;
      }
      try {
        final repaired = _controller.loadFormalDraft(
          payload: action.draft.payload,
          draftId: action.draft.id,
          draftName: action.draft.name,
        );
        if (repaired) {
          final repairedPayload = _controller.encodeFormalDraft();
          try {
            await store.saveDraft(
              id: action.draft.id,
              name: action.draft.name,
              payload: repairedPayload,
            );
            _controller.markSaved(
              draftId: action.draft.id,
              draftName: action.draft.name,
              payload: repairedPayload,
            );
            _recoveryTimer?.cancel();
            _flushedProjectRevision = _controller.projectRevision;
            await store.deleteRecovery();
            _controller.showNotice('已修复旧草稿的舞步关系并保存');
            _record('draft.repair.persisted', {'draftId': action.draft.id});
          } on Object catch (error, stack) {
            _recordException('draft.repair.persist', error, stack);
            if (mounted) {
              _controller.showNotice('草稿已修复，但自动保存失败；请手动保存。', error: true);
            }
          }
        }
        return;
      } on Object catch (error, stack) {
        _recordException('draft.load', error, stack);
        _controller.showNotice('草稿加载失败：数据已损坏。', error: true);
      }
    }
  }

  Future<bool> _save({bool saveAs = false}) async {
    final store = _store;
    if (store == null) {
      _controller.showNotice('本地草稿存储尚未就绪');
      return false;
    }
    String? targetId;
    String targetName;
    if (_controller.sourceKind == WorkspaceSourceKind.savedDraft && !saveAs) {
      targetId = _controller.draftId;
      targetName = _controller.chartName;
    } else {
      final entered = await _requestDraftName();
      if (entered == null) {
        return false;
      }
      try {
        targetName = normalizeDraftName(entered);
      } on FormatException catch (error) {
        _controller.showNotice(error.message, error: true);
        return false;
      }
      DraftSummary? existing;
      try {
        existing = await store.findByName(targetName);
      } on Object catch (error, stack) {
        _recordException('draft.find', error, stack);
        _controller.showNotice('草稿保存失败。', error: true);
        return false;
      }
      final saveTarget = resolveDraftSaveTarget(existing);
      if (saveTarget.needsConfirmation &&
          !await _confirmOverwriteDraft(targetName)) {
        return false;
      }
      targetId = saveTarget.existingId;
    }
    try {
      final payload = _controller.encodeFormalDraft();
      final saved = await store.saveDraft(
        id: targetId,
        name: targetName,
        payload: payload,
      );
      _controller.markSaved(
        draftId: saved.id,
        draftName: saved.name,
        payload: saved.payload,
      );
      _recoveryTimer?.cancel();
      _flushedProjectRevision = _controller.projectRevision;
      await store.deleteRecovery();
      _record('draft.save.completed', {
        'saveAs': saveAs,
        'revision': _controller.projectRevision,
        'bytes': payload.length,
      });
      return true;
    } on Object catch (error, stack) {
      _recordException('draft.save', error, stack);
      _controller.showNotice('草稿保存失败。', error: true);
      return false;
    }
  }

  Future<String?> _requestDraftName() async {
    final initial = _controller.audioFileName != null
        ? _withoutExtension(_controller.audioFileName!)
        : _controller.sourceKind == WorkspaceSourceKind.importedSlk
        ? _withoutExtension(_controller.chartName)
        : '';
    var name = initial;
    final result = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('保存草稿'),
        content: TextFormField(
          initialValue: initial,
          autofocus: true,
          maxLength: draftNameMaxCharacters,
          decoration: const InputDecoration(labelText: '草稿名'),
          onChanged: (value) => name = value,
          onFieldSubmitted: (value) => Navigator.pop(context, value),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, name),
            child: const Text('保存'),
          ),
        ],
      ),
    );
    return result;
  }

  Future<void> _confirmClear() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('清空当前谱面？'),
        content: const Text('只清除谱面内容，草稿名会一并清除。已加载音频、BPM 和 Offset 会保留。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('清空'),
          ),
        ],
      ),
    );
    if (confirmed ?? false) {
      _controller.clearChart();
    }
  }

  Future<void> _exportDiagnostics() async {
    if (_exportingDiagnostics ||
        _controller.isBusy ||
        !_controller.generalActionsEnabled) {
      return;
    }
    _exportingDiagnostics = true;
    _diagnosticExportCancelled = false;
    String? documentUri;
    var completed = false;
    Future<void> discardUncommittedDocument() async {
      final uri = documentUri;
      if (completed || uri == null) return;
      documentUri = null;
      try {
        await AudioExportPublisher.discardPlatformDocument(documentUri: uri);
      } on Object catch (error, stack) {
        _recordException('diagnostics.export.cleanup', error, stack);
      }
    }

    try {
      final payload = _controller.encodeRecoverySnapshot();
      final state = _controller.diagnosticState;
      _record('diagnostics.export.requested', state);
      final logger = _diagnostics;
      final bundle = logger == null
          ? LocalDiagnostics.snapshotBundle(
              recoveryPayload: payload,
              state: state,
            )
          : await logger.exportBundle(recoveryPayload: payload, state: state);
      final bytes = Uint8List.fromList(utf8.encode(jsonEncode(bundle)));
      final time = DateTime.now().toUtc();
      final baseName = 'otgy-diagnostics-${time.microsecondsSinceEpoch}';
      final name = '$baseName.json';
      bool cancelled() => !mounted || _diagnosticExportCancelled;
      if (cancelled()) throw const AudioExportPublishCancelled();
      final mobile =
          defaultTargetPlatform == TargetPlatform.android ||
          defaultTargetPlatform == TargetPlatform.iOS;
      String? selectedPath;
      _diagnosticPickerActive = true;
      try {
        if (mobile) {
          documentUri = await AudioExportPublisher.selectPlatformDocument(
            suggestedName: name,
            mimeType: 'application/json',
          );
        } else {
          final location = await getSaveLocation(
            suggestedName: name,
            acceptedTypeGroups: const [
              XTypeGroup(
                label: '诊断包 JSON',
                extensions: ['json'],
                mimeTypes: ['application/json'],
                uniformTypeIdentifiers: ['public.json'],
              ),
            ],
          );
          selectedPath = location?.path;
        }
      } finally {
        _diagnosticPickerActive = false;
      }
      if (documentUri == null && selectedPath == null) {
        _record('diagnostics.export.cancelled', {});
        return;
      }
      if (cancelled()) throw const AudioExportPublishCancelled();
      if (documentUri case final uri?) {
        await AudioExportPublisher.publishPlatformDocument(
          bytes: bytes,
          documentUri: uri,
          isCancelled: cancelled,
        );
        if (cancelled()) throw const AudioExportPublishCancelled();
        await AudioExportPublisher.commitPlatformDocument(documentUri: uri);
      } else {
        await AudioExportPublisher.publish(
          bytes: bytes,
          selectedPath: selectedPath!,
          baseName: baseName,
          localTime: time,
          isCancelled: cancelled,
          collisionFileName: (index) => '${baseName}_$index.json',
        );
      }
      completed = true;
      _record('diagnostics.export.completed', {'bytes': bytes.length});
      if (mounted) _controller.showNotice('诊断包已导出');
    } on AudioExportPublishCancelled {
      _record('diagnostics.export.cancelled', {});
      if (mounted) _controller.showNotice('诊断导出已取消');
    } on Object catch (error, stack) {
      await discardUncommittedDocument();
      if (mounted) {
        await _showOperationError(
          title: '诊断包导出失败',
          operation: 'diagnostics-export',
          error: error,
          stackTrace: stack,
        );
      } else {
        _recordException('diagnostics-export', error, stack);
      }
    } finally {
      await discardUncommittedDocument();
      _diagnosticPickerActive = false;
      _exportingDiagnostics = false;
    }
  }

  Future<void> _exportSlk() async {
    if (!_controller.generalActionsEnabled) {
      return;
    }
    final result = _controller.exportSlk();
    if (!result.isFinished) {
      await showDialog<void>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('谱面尚未完成'),
          content: SizedBox(
            width: 460,
            child: ListView.builder(
              shrinkWrap: true,
              itemCount: result.diagnostics.length,
              itemBuilder: (context, index) {
                final item = result.diagnostics[index];
                return ListTile(
                  dense: true,
                  leading: const Icon(Icons.error_outline),
                  title: Text(_diagnosticLabel(item.code)),
                  subtitle: Text('行 ${item.row} · 对象 ${item.objectId}'),
                );
              },
            ),
          ),
          actions: [
            FilledButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('返回编辑'),
            ),
          ],
        ),
      );
      return;
    }
    const type = XTypeGroup(label: 'SLK 谱面', extensions: ['slk']);
    final name = '${_controller.suggestedProjectBaseName}.slk';
    final mobile =
        defaultTargetPlatform == TargetPlatform.android ||
        defaultTargetPlatform == TargetPlatform.iOS;
    String? documentUri;
    var committed = false;
    try {
      if (mobile) {
        documentUri = await AudioExportPublisher.selectPlatformDocument(
          suggestedName: name,
          mimeType: 'application/octet-stream',
        );
        if (documentUri == null) {
          return;
        }
        await AudioExportPublisher.publishPlatformDocument(
          bytes: result.bytes!,
          documentUri: documentUri,
          isCancelled: () => !mounted,
        );
        if (!mounted) {
          await AudioExportPublisher.discardPlatformDocument(
            documentUri: documentUri,
          );
          return;
        }
        await AudioExportPublisher.commitPlatformDocument(
          documentUri: documentUri,
        );
        committed = true;
      } else {
        final location = await getSaveLocation(
          suggestedName: name,
          acceptedTypeGroups: const [type],
        );
        if (location == null) {
          return;
        }
        await XFile.fromData(
          result.bytes!,
          name: name,
          mimeType: 'application/octet-stream',
        ).saveTo(location.path);
      }
      _controller.showNotice('SLK 已导出');
    } on Object catch (error, stack) {
      if (mobile && documentUri != null && !committed) {
        try {
          await AudioExportPublisher.discardPlatformDocument(
            documentUri: documentUri,
          );
        } on Object {
          // Best effort cleanup; the original failure remains authoritative.
        }
      }
      _recordException('slk.export', error, stack);
      if (mounted) {
        _controller.showNotice('SLK 导出失败。', error: true);
      }
    }
  }

  Future<bool> _confirmWorkspaceReplacement() async {
    if (!_controller.isDirty) {
      return true;
    }
    final choice = await _chooseUnsavedAction();
    return switch (choice) {
      _UnsavedChoice.discard => true,
      _UnsavedChoice.save => _save(),
      _ => false,
    };
  }

  Future<_UnsavedChoice?> _chooseUnsavedAction() => showDialog<_UnsavedChoice>(
    context: context,
    builder: (context) => AlertDialog(
      title: const Text('当前修改尚未保存'),
      content: const Text('继续操作前，可以先保存当前草稿。'),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context, _UnsavedChoice.cancel),
          child: const Text('取消'),
        ),
        TextButton(
          onPressed: () => Navigator.pop(context, _UnsavedChoice.discard),
          child: const Text('放弃修改'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context, _UnsavedChoice.save),
          child: const Text('保存并继续'),
        ),
      ],
    ),
  );

  Future<Object?> _handlePlatformLifecycleCall(MethodCall call) async {
    if (call.method == 'requestClose') {
      return _prepareExit();
    }
    throw MissingPluginException('Unknown lifecycle method ${call.method}.');
  }

  Future<void> _confirmExit() async {
    if (await _prepareExit() && mounted) {
      await Navigator.of(context).maybePop();
    }
  }

  Future<bool> _prepareExit() async {
    if (_exitConfirmationOpen) {
      return false;
    }
    _exitConfirmationOpen = true;
    try {
      final choice = _controller.isDirty
          ? await _chooseUnsavedAction()
          : _UnsavedChoice.discard;
      final proceed = switch (choice) {
        _UnsavedChoice.save => await _save(),
        _UnsavedChoice.discard => true,
        _ => false,
      };
      if (!proceed || !mounted) {
        return false;
      }
      if (choice == _UnsavedChoice.discard && _controller.isDirty) {
        await _store?.deleteRecovery();
      }
      if (_controller.chartInteractionMode !=
          EditorChartInteractionMode.normal) {
        _controller.exitChartInteraction();
      }
      await _store?.endSessionNormally();
      if (!mounted) {
        return false;
      }
      setState(() => _allowExit = true);
      return true;
    } finally {
      _exitConfirmationOpen = false;
    }
  }

  Future<void> _showAudioExportError(Object error, StackTrace stackTrace) {
    _recordException('audio.export', error, stackTrace);
    final diagnostic = _formatOperationDiagnostic(
      'audio-export',
      error,
      stackTrace,
    );
    return showDialog<void>(
      context: context,
      builder: (context) => AudioExportErrorDialog(diagnostic: diagnostic),
    );
  }

  Future<void> _showOperationError({
    required String title,
    required String operation,
    required Object error,
    required StackTrace stackTrace,
  }) {
    _recordException(operation, error, stackTrace);
    final diagnostic = _formatOperationDiagnostic(operation, error, stackTrace);
    return showDialog<void>(
      context: context,
      builder: (context) => OperationErrorDialog(
        title: title,
        diagnostic: diagnostic,
        diagnosticKey: '$operation-error-diagnostic',
        copyKey: 'copy-$operation-error',
      ),
    );
  }

  String _formatOperationDiagnostic(
    String operation,
    Object error,
    StackTrace stackTrace,
  ) {
    final lines = <String>[
      'operation: $operation',
      'exceptionType: ${error.runtimeType}',
      'exception: $error',
    ];
    if (error case PlatformException(
      :final code,
      :final message,
      :final details,
    )) {
      lines
        ..add('platformCode: $code')
        ..add('platformMessage: ${message ?? ''}')
        ..add('platformDetails: ${details ?? ''}');
    }
    if (error case FileSystemException(:final osError, :final path)) {
      lines
        ..add('fileSystemPath: ${path ?? ''}')
        ..add('fileSystemError: ${osError ?? ''}');
    }
    final trace = stackTrace.toString().trim();
    if (trace.isNotEmpty) {
      lines
        ..add('stackTrace:')
        ..add(trace);
    }
    return lines.join('\n');
  }

  Future<bool> _confirmDeleteDraft(String name) async =>
      await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('删除草稿？'),
          content: Text('“$name”删除后无法从草稿列表恢复。'),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('删除'),
            ),
          ],
        ),
      ) ??
      false;

  Future<bool> _confirmOverwriteDraft(String name) async =>
      await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('覆盖同名草稿？'),
          content: Text('已存在名为“$name”的草稿。'),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('返回'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('覆盖'),
            ),
          ],
        ),
      ) ??
      false;

  bool _showtimePartialPromptVisible = false;

  void _publishShowtimePartialPromptIfNeeded() {
    if (!_controller.showtimePartialPromptPending ||
        _showtimePartialPromptVisible) {
      return;
    }
    _showtimePartialPromptVisible = true;
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      if (!mounted) {
        _showtimePartialPromptVisible = false;
        return;
      }
      await showDialog<void>(
        context: context,
        builder: (dialogContext) => AlertDialog(
          title: const Text('Showtime 未完全生成'),
          content: const Text('时间轴上已有内容，部分舞步未能生成 Show-D。'),
          actions: [
            FilledButton(
              onPressed: () => Navigator.of(dialogContext).pop(),
              child: const Text('知道了'),
            ),
          ],
        ),
      );
      _showtimePartialPromptVisible = false;
      if (mounted) {
        _controller.acknowledgeShowtimePartialPrompt();
      }
    });
  }

  void _publishNoticeIfNeeded(EditorNotice? notice) {
    if (notice == null || identical(notice, _shownNotice)) {
      return;
    }
    _shownNotice = notice;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) {
        return;
      }
      final messenger = ScaffoldMessenger.of(context);
      messenger
        ..hideCurrentSnackBar()
        ..showSnackBar(
          SnackBar(
            content: Text(
              notice.message,
              style: TextStyle(
                color: notice.kind == EditorNoticeKind.error
                    ? EditorColors.canvas
                    : EditorColors.text,
              ),
            ),
            backgroundColor: notice.kind == EditorNoticeKind.error
                ? EditorColors.danger
                : EditorColors.surfaceRaised,
            behavior: SnackBarBehavior.floating,
            duration: const Duration(seconds: 2),
          ),
        );
      _controller.dismissNotice();
    });
  }

  KeyEventResult _handleSelectionShortcut(FocusNode node, KeyEvent event) {
    if (_controller.chartInteractionMode == EditorChartInteractionMode.normal) {
      return KeyEventResult.ignored;
    }
    final focusContext = FocusManager.instance.primaryFocus?.context;
    if (focusContext?.findAncestorWidgetOfExactType<EditableText>() != null) {
      return KeyEventResult.ignored;
    }
    final bindings = <ShortcutActivator, VoidCallback>{
      if (_controller.selectionMode) ...{
        const SingleActivator(LogicalKeyboardKey.keyC, control: true):
            _controller.copySelection,
        const SingleActivator(LogicalKeyboardKey.keyC, meta: true):
            _controller.copySelection,
        const SingleActivator(LogicalKeyboardKey.keyX, control: true):
            _controller.cutSelection,
        const SingleActivator(LogicalKeyboardKey.keyX, meta: true):
            _controller.cutSelection,
      },
      const SingleActivator(LogicalKeyboardKey.escape):
          _controller.exitChartInteraction,
    };
    for (final binding in bindings.entries) {
      if (binding.key.accepts(event, HardwareKeyboard.instance)) {
        binding.value();
        return KeyEventResult.handled;
      }
    }
    return KeyEventResult.ignored;
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _controller,
      builder: (context, child) {
        _publishNoticeIfNeeded(_controller.notice);
        _publishShowtimePartialPromptIfNeeded();
        return PopScope<Object?>(
          canPop: _allowExit || !_controller.isDirty,
          onPopInvokedWithResult: (didPop, result) {
            if (didPop) {
              unawaited(_store?.endSessionNormally());
            } else {
              unawaited(_confirmExit());
            }
          },
          child: Focus(
            canRequestFocus: false,
            skipTraversal: true,
            includeSemantics: false,
            onKeyEvent: _handleSelectionShortcut,
            child: Scaffold(
              body: Stack(
                children: [
                  SafeArea(
                    child: Column(
                      children: [
                        EditorToolbars(
                          controller: _controller,
                          onChooseAudio: _chooseAudio,
                          onAnalyze: _analyzeAudio,
                          onExportAudio: _exportAudio,
                          onImportSlk: _importSlk,
                          onOpenDrafts: _openDrafts,
                          onSave: _save,
                          onSaveAs: () => _save(saveAs: true),
                          onClear: _confirmClear,
                          onExportSlk: _exportSlk,
                          onExportDiagnostics: _exportDiagnostics,
                        ),
                        Expanded(
                          child: EditorCanvas(
                            controller: _controller,
                            preferences: widget.preferences,
                            onOpenDrafts: _openDrafts,
                            onSave: _save,
                            onSaveAs: () => _save(saveAs: true),
                            onExportSlk: _exportSlk,
                          ),
                        ),
                        _StatusBar(
                          controller: _controller,
                          onJumpToTime: _jumpToSongTime,
                        ),
                      ],
                    ),
                  ),
                  if (_controller.isExportingAudio)
                    _AudioExportProgressOverlay(controller: _controller),
                  if (_controller.screenKeypadOpen)
                    Positioned.fill(
                      child: ScreenKeypadOverlay(controller: _controller),
                    ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}

/// Displays a complete operation failure report and makes it easy to share the
/// exact diagnostic with a bug report.
class OperationErrorDialog extends StatefulWidget {
  const OperationErrorDialog({
    super.key,
    required this.title,
    required this.diagnostic,
    required this.diagnosticKey,
    required this.copyKey,
  });

  final String title;
  final String diagnostic;
  final String diagnosticKey;
  final String copyKey;

  @override
  State<OperationErrorDialog> createState() => _OperationErrorDialogState();
}

class _OperationErrorDialogState extends State<OperationErrorDialog> {
  bool _copied = false;

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(widget.title),
      content: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 680, maxHeight: 420),
        child: SingleChildScrollView(
          child: SelectableText(
            widget.diagnostic,
            key: ValueKey(widget.diagnosticKey),
          ),
        ),
      ),
      actions: [
        TextButton.icon(
          key: ValueKey(widget.copyKey),
          onPressed: () async {
            await Clipboard.setData(ClipboardData(text: widget.diagnostic));
            if (mounted) {
              setState(() => _copied = true);
            }
          },
          icon: Icon(_copied ? Icons.check : Icons.content_copy),
          label: Text(_copied ? '已复制' : '复制错误信息'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('关闭'),
        ),
      ],
    );
  }
}

class AudioExportErrorDialog extends OperationErrorDialog {
  const AudioExportErrorDialog({super.key, required super.diagnostic})
    : super(
        title: '音频导出失败',
        diagnosticKey: 'audio-export-error-diagnostic',
        copyKey: 'copy-audio-export-error',
      );
}

class _AudioExportProgressOverlay extends StatelessWidget {
  const _AudioExportProgressOverlay({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    final progress = controller.audioExportProgress;
    final total = progress?.totalUnits ?? 0;
    final completed = progress?.completedUnits ?? 0;
    final value = total > 0 ? (completed / total).clamp(0.0, 1.0) : null;
    final stage = switch (progress?.stage) {
      AudioExportStage.preparingAudio => '准备音频',
      AudioExportStage.encoding => '编码中',
      AudioExportStage.saving => '保存中',
      null => '准备音频',
    };
    return Stack(
      children: [
        const ModalBarrier(dismissible: false, color: Color(0xB310151A)),
        Center(
          child: Semantics(
            container: true,
            liveRegion: true,
            label: '音频导出进度，$stage',
            child: Material(
              color: EditorColors.surfaceRaised,
              elevation: 12,
              borderRadius: BorderRadius.circular(8),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 360),
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(22, 20, 22, 16),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        '正在导出 Ogg 音频',
                        style: Theme.of(context).textTheme.titleMedium,
                      ),
                      const SizedBox(height: 14),
                      LinearProgressIndicator(value: value),
                      const SizedBox(height: 10),
                      Text(
                        controller.audioExportCancellationRequested
                            ? '正在取消…'
                            : stage,
                      ),
                      const SizedBox(height: 12),
                      Align(
                        alignment: Alignment.centerRight,
                        child: TextButton(
                          onPressed: controller.audioExportCancellationRequested
                              ? null
                              : controller.cancelAudioExport,
                          child: const Text('取消导出'),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _AnalysisProgress extends StatelessWidget {
  const _AnalysisProgress({required this.stage});

  final SyncAnalysisStage? stage;

  @override
  Widget build(BuildContext context) {
    const stages = [
      '检测起音',
      '扫描节奏间隔',
      '精细计算候选',
      '筛选 BPM',
      '计算 Offset',
      '整理候选结果',
    ];
    final current = stage?.value ?? 0;
    return Semantics(
      liveRegion: true,
      label: '分析阶段 ${current + 1}，${stages[current]}',
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              for (var index = 0; index < stages.length; index++) ...[
                if (index > 0) const SizedBox(width: 4),
                Expanded(
                  child: Container(
                    height: 4,
                    decoration: BoxDecoration(
                      color: index <= current
                          ? EditorColors.accent
                          : EditorColors.border,
                      borderRadius: BorderRadius.circular(2),
                    ),
                  ),
                ),
              ],
            ],
          ),
          const SizedBox(height: 12),
          Text('正在使用共享核心分析音频：${stages[current]}'),
          const SizedBox(height: 8),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var index = 0; index < stages.length; index++)
                Chip(
                  label: Text(stages[index]),
                  backgroundColor: index == current
                      ? EditorColors.accent.withValues(alpha: 0.24)
                      : null,
                ),
            ],
          ),
        ],
      ),
    );
  }
}

class _AnalysisCandidates extends StatelessWidget {
  const _AnalysisCandidates({required this.candidates, required this.onSelect});

  final List<AudioAnalysisCandidate> candidates;
  final ValueChanged<AudioAnalysisCandidate> onSelect;

  @override
  Widget build(BuildContext context) {
    if (candidates.isEmpty) {
      return const Text('没有可用候选');
    }
    final best = candidates.fold<double>(0, (value, item) {
      return item.fitness > value ? item.fitness : value;
    });
    return ListView.separated(
      shrinkWrap: true,
      itemCount: candidates.length,
      separatorBuilder: (_, _) => const Divider(height: 1),
      itemBuilder: (context, index) {
        final item = candidates[index];
        final confidence = best <= 0 ? 0 : (item.fitness / best * 100).round();
        return ListTile(
          leading: CircleAvatar(child: Text('${index + 1}')),
          title: Text('${item.bpm.toStringAsFixed(3)} BPM'),
          subtitle: Text(
            'Offset ${item.appOffsetSeconds.toStringAsFixed(3)} · 置信度 $confidence%',
          ),
          onTap: () => onSelect(item),
        );
      },
    );
  }
}

class _StatusBar extends StatelessWidget {
  const _StatusBar({required this.controller, required this.onJumpToTime});

  final EditorController controller;
  final VoidCallback onJumpToTime;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 24,
      padding: const EdgeInsets.symmetric(horizontal: 9),
      decoration: const BoxDecoration(
        color: EditorColors.surface,
        border: Border(top: BorderSide(color: EditorColors.border)),
      ),
      child: Row(
        children: [
          Expanded(
            flex: 3,
            child: Align(
              alignment: Alignment.centerLeft,
              child: Tooltip(
                message: '点击跳转到歌曲时间',
                child: TextButton(
                  key: const ValueKey('song-time'),
                  onPressed: controller.navigationEnabled ? onJumpToTime : null,
                  style: TextButton.styleFrom(
                    foregroundColor: EditorColors.text,
                    padding: const EdgeInsets.symmetric(horizontal: 4),
                    minimumSize: Size.zero,
                    tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    textStyle: Theme.of(context).textTheme.bodySmall,
                  ),
                  child: Text(_formatSongTime(controller.currentSongSeconds)),
                ),
              ),
            ),
          ),
          Expanded(
            flex: 2,
            child: Text(
              controller.isPlaying
                  ? '播放视图 ${controller.playbackVisualPixelsPerSecond.toStringAsFixed(0)} px/s'
                  : 'Zoom ${controller.zoom.toStringAsFixed(2)}',
              overflow: TextOverflow.ellipsis,
              textAlign: TextAlign.right,
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            flex: 2,
            child: Text(
              'Scale ${controller.scale.toStringAsFixed(2)}',
              overflow: TextOverflow.ellipsis,
              textAlign: TextAlign.right,
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
        ],
      ),
    );
  }
}

String _formatSongTime(double seconds) {
  final totalMilliseconds = (seconds.abs() * 1000).truncate();
  final milliseconds = totalMilliseconds % 1000;
  final totalSeconds = totalMilliseconds ~/ 1000;
  final displaySeconds = totalSeconds % 60;
  final minutes = totalSeconds ~/ 60;
  final sign = seconds.isNegative && totalMilliseconds != 0 ? '-' : '';
  return '$sign${minutes.toString().padLeft(2, '0')}:'
      '${displaySeconds.toString().padLeft(2, '0')}.'
      '${milliseconds.toString().padLeft(3, '0')}';
}

String? _validateSongMinutes(String? value) {
  final minutes = int.tryParse(value?.trim() ?? '');
  if (minutes == null || minutes < 0) {
    return '请输入非负整数';
  }
  return null;
}

String? _validateSongSeconds(String? value) {
  final seconds = double.tryParse(value?.trim() ?? '');
  if (seconds == null || !seconds.isFinite || seconds < 0 || seconds >= 60) {
    return '请输入 0 到 60 秒以内';
  }
  return null;
}

enum _UnsavedChoice { save, discard, cancel }

final class _DraftAction {
  const _DraftAction.load(this.draft) : delete = false;
  const _DraftAction.delete(this.draft) : delete = true;

  final DraftSummary draft;
  final bool delete;
}

String _withoutExtension(String value) {
  final dot = value.lastIndexOf('.');
  return dot > 0 ? value.substring(0, dot) : value;
}

String _formatTime(DateTime value) {
  String two(int number) => number.toString().padLeft(2, '0');
  return '${value.year}-${two(value.month)}-${two(value.day)} '
      '${two(value.hour)}:${two(value.minute)}';
}

String _diagnosticLabel(int code) => switch (code) {
  0 => '谱面为空',
  1 => '事件标识重复',
  2 => '时间行重复',
  6 => '缺少引用对象',
  7 => '事件关系无效',
  8 => '演练块结构无效',
  9 => 'NPC 与玩家演练不匹配',
  10 => '舞步演练无效',
  11 => 'Showtime 无效',
  12 => '存在孤立事件',
  13 => '存在孤立演练组',
  14 => '舞步演练被多个 Showtime 引用',
  _ => '谱面结构不完整（代码 $code）',
};
