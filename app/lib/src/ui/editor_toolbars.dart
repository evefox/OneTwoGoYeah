import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../application/editor_controller.dart';
import 'editor_theme.dart';

class EditorToolbars extends StatelessWidget {
  const EditorToolbars({
    super.key,
    required this.controller,
    required this.onChooseAudio,
    required this.onAnalyze,
    required this.onExportAudio,
    required this.onImportSlk,
    required this.onOpenDrafts,
    required this.onSave,
    required this.onSaveAs,
    required this.onClear,
    required this.onExportSlk,
  });

  final EditorController controller;
  final VoidCallback onChooseAudio;
  final VoidCallback onAnalyze;
  final VoidCallback onExportAudio;
  final VoidCallback onImportSlk;
  final VoidCallback onOpenDrafts;
  final VoidCallback onSave;
  final VoidCallback onSaveAs;
  final VoidCallback onClear;
  final VoidCallback onExportSlk;

  @override
  Widget build(BuildContext context) {
    return DecoratedBox(
      decoration: const BoxDecoration(
        color: EditorColors.surface,
        border: Border(bottom: BorderSide(color: EditorColors.border)),
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final compact = constraints.maxWidth < 680;
          final mobile =
              compact &&
              (defaultTargetPlatform == TargetPlatform.android ||
                  defaultTargetPlatform == TargetPlatform.iOS);
          return Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              _AudioToolbar(
                controller: controller,
                compact: compact,
                mobile: mobile,
                onChooseAudio: onChooseAudio,
                onAnalyze: onAnalyze,
                onExportAudio: onExportAudio,
              ),
              const Divider(height: 1),
              _ChartToolbar(
                controller: controller,
                onImportSlk: onImportSlk,
                onOpenDrafts: onOpenDrafts,
                onSave: onSave,
                onSaveAs: onSaveAs,
                onClear: onClear,
                onExportSlk: onExportSlk,
              ),
            ],
          );
        },
      ),
    );
  }
}

class _AudioToolbar extends StatefulWidget {
  const _AudioToolbar({
    required this.controller,
    required this.compact,
    required this.mobile,
    required this.onChooseAudio,
    required this.onAnalyze,
    required this.onExportAudio,
  });

  final EditorController controller;
  final bool compact;
  final bool mobile;
  final VoidCallback onChooseAudio;
  final VoidCallback onAnalyze;
  final VoidCallback onExportAudio;

  @override
  State<_AudioToolbar> createState() => _AudioToolbarState();
}

class _AudioToolbarState extends State<_AudioToolbar> {
  bool _timingExpanded = true;

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final compact = widget.compact;
    final mobile = widget.mobile;
    final onChooseAudio = widget.onChooseAudio;
    final onAnalyze = widget.onAnalyze;
    final onExportAudio = widget.onExportAudio;
    final enabled = controller.generalActionsEnabled && !controller.isBusy;
    final file = Semantics(
      button: true,
      label: controller.audioFileName == null
          ? '导入音频'
          : '更换音频，当前为 ${controller.audioFileName}',
      child: Tooltip(
        message: controller.audioFileName ?? '导入 MP3 或 Ogg 音频',
        child: OutlinedButton.icon(
          key: const ValueKey('audio-picker'),
          onPressed: enabled ? onChooseAudio : null,
          icon: const Icon(Icons.audio_file_outlined, size: 18),
          label: Align(
            alignment: Alignment.centerLeft,
            child: _AudioFileName(fileName: controller.audioFileName),
          ),
        ),
      ),
    );
    final analyze = IconButton(
      key: const ValueKey('analyze-audio'),
      onPressed: enabled ? onAnalyze : null,
      tooltip: '分析 BPM 与 Offset',
      icon: const Icon(Icons.auto_graph_outlined, size: 20),
    );
    final bpm = _ValueMenu(
      key: const ValueKey('bpm-menu'),
      label: 'BPM',
      value: controller.bpm,
      min: 0,
      max: 100000,
      allowNegative: false,
      enabled: enabled,
      compact: mobile,
      commitTextOnEditingComplete: true,
      onChanged: controller.setBpm,
      stepButtons: [
        _StepAction(Icons.remove, 'BPM 减少 0.001', () {
          controller.nudgeBpm(-0.001);
        }, repeatDelta: -0.001),
        _StepAction(Icons.add, 'BPM 增加 0.001', () {
          controller.nudgeBpm(0.001);
        }, repeatDelta: 0.001),
      ],
    );
    final offset = _ValueMenu(
      key: const ValueKey('offset-menu'),
      label: 'Offset',
      value: controller.offset,
      min: -30,
      max: 30,
      allowNegative: true,
      enabled: enabled,
      compact: mobile,
      commitTextOnEditingComplete: true,
      onChanged: controller.setOffset,
      stepButtons: [
        _StepAction(Icons.keyboard_double_arrow_left, '往前一拍', () {
          controller.nudgeOffsetByBeat(1);
        }),
        _StepAction(Icons.chevron_left, '往前半拍', () {
          controller.nudgeOffsetByBeat(0.5);
        }),
        _StepAction(Icons.chevron_right, '往后半拍', () {
          controller.nudgeOffsetByBeat(-0.5);
        }),
        _StepAction(Icons.keyboard_double_arrow_right, '往后一拍', () {
          controller.nudgeOffsetByBeat(-1);
        }),
      ],
    );
    final export = IconButton(
      key: const ValueKey('export-audio'),
      onPressed: enabled ? onExportAudio : null,
      tooltip: '导出 Ogg 音频',
      icon: const Icon(Icons.ios_share_outlined, size: 20),
    );
    final timingToggle = IconButton(
      key: const ValueKey('toggle-timing-controls'),
      onPressed: enabled
          ? () => setState(() => _timingExpanded = !_timingExpanded)
          : null,
      tooltip: _timingExpanded ? '收起 BPM 与 Offset' : '展开 BPM 与 Offset',
      icon: Icon(
        _timingExpanded ? Icons.expand_less : Icons.expand_more,
        size: 20,
      ),
    );
    return Padding(
      padding: EdgeInsets.fromLTRB(8, compact ? 6 : 7, 8, compact ? 4 : 7),
      child: mobile
          ? Column(
              children: [
                // Keep the audio source and its analysis entry point together.
                // The value controls then get the full width on a phone, which
                // makes the current timing values easier to scan and tap.
                Row(
                  children: [
                    Expanded(child: file),
                    const SizedBox(width: 4),
                    analyze,
                    timingToggle,
                    export,
                  ],
                ),
                if (_timingExpanded) ...[
                  const SizedBox(height: 4),
                  Row(
                    children: [
                      Expanded(child: bpm),
                      const SizedBox(width: 4),
                      Expanded(child: offset),
                    ],
                  ),
                ],
              ],
            )
          : compact
          ? Column(
              children: [
                Row(
                  children: [
                    Expanded(child: file),
                    analyze,
                    export,
                  ],
                ),
                const SizedBox(height: 4),
                Row(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: [bpm, offset],
                ),
              ],
            )
          : LayoutBuilder(
              builder: (context, constraints) {
                final targetFileWidth = (constraints.maxWidth * 0.38)
                    .clamp(240.0, 420.0)
                    .toDouble();
                final availableFileWidth = constraints.maxWidth - 454;
                final fileWidth = targetFileWidth > availableFileWidth
                    ? availableFileWidth
                    : targetFileWidth;
                return Row(
                  children: [
                    SizedBox(width: fileWidth, child: file),
                    const SizedBox(width: 6),
                    analyze,
                    const Spacer(),
                    SizedBox(width: 176, child: bpm),
                    SizedBox(width: 176, child: offset),
                    export,
                  ],
                );
              },
            ),
    );
  }
}

class _AudioFileName extends StatelessWidget {
  const _AudioFileName({required this.fileName});

  final String? fileName;

  @override
  Widget build(BuildContext context) {
    final value = fileName;
    if (value == null) {
      return const Text('导入音频');
    }
    final dot = value.lastIndexOf('.');
    if (dot <= 0 || dot == value.length - 1) {
      return Text(value, maxLines: 1, overflow: TextOverflow.ellipsis);
    }
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Flexible(
          child: Text(
            value.substring(0, dot),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
          ),
        ),
        Text(value.substring(dot)),
      ],
    );
  }
}

class _ChartToolbar extends StatelessWidget {
  const _ChartToolbar({
    required this.controller,
    required this.onImportSlk,
    required this.onOpenDrafts,
    required this.onSave,
    required this.onSaveAs,
    required this.onClear,
    required this.onExportSlk,
  });

  final EditorController controller;
  final VoidCallback onImportSlk;
  final VoidCallback onOpenDrafts;
  final VoidCallback onSave;
  final VoidCallback onSaveAs;
  final VoidCallback onClear;
  final VoidCallback onExportSlk;

  @override
  Widget build(BuildContext context) {
    final enabled = controller.generalActionsEnabled && !controller.isBusy;
    final savedDraft = controller.sourceKind == WorkspaceSourceKind.savedDraft;
    final saveControl = savedDraft
        ? Row(
            children: [
              SizedBox(
                width: 48,
                child: IconButton(
                  key: const ValueKey('save-chart'),
                  onPressed: enabled ? onSave : null,
                  tooltip: '保存',
                  icon: const Icon(Icons.save_outlined, size: 20),
                ),
              ),
              SizedBox(
                width: 48,
                child: MenuAnchor(
                  menuChildren: [
                    MenuItemButton(
                      onPressed: onSaveAs,
                      child: const Text('另存为'),
                    ),
                  ],
                  builder: (context, menu, child) => IconButton(
                    key: const ValueKey('save-options'),
                    onPressed: enabled
                        ? () => menu.isOpen ? menu.close() : menu.open()
                        : null,
                    tooltip: '保存选项',
                    icon: const Icon(Icons.arrow_drop_down, size: 20),
                  ),
                ),
              ),
            ],
          )
        : IconButton(
            key: const ValueKey('save-chart'),
            onPressed: enabled ? onSave : null,
            tooltip: '保存',
            icon: const Icon(Icons.save_outlined, size: 20),
          );
    return Padding(
      padding: const EdgeInsets.fromLTRB(8, 5, 8, 6),
      child: Row(
        children: [
          Expanded(
            child: MenuAnchor(
              menuChildren: [
                MenuItemButton(
                  leadingIcon: const Icon(Icons.file_open_outlined),
                  onPressed: onImportSlk,
                  child: const Text('导入 SLK'),
                ),
                MenuItemButton(
                  leadingIcon: const Icon(Icons.folder_open_outlined),
                  onPressed: onOpenDrafts,
                  child: const Text('打开草稿'),
                ),
              ],
              builder: (context, menu, child) => OutlinedButton.icon(
                key: const ValueKey('chart-menu'),
                onPressed: enabled
                    ? () => menu.isOpen ? menu.close() : menu.open()
                    : null,
                icon: const Icon(Icons.description_outlined, size: 18),
                label: Align(
                  alignment: Alignment.centerLeft,
                  child: Text(
                    '${controller.chartName}${controller.isDirty ? '  • 未保存' : ''}',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(width: 6),
          SizedBox(width: savedDraft ? 96 : 48, child: saveControl),
          IconButton(
            key: const ValueKey('clear-chart'),
            onPressed: enabled ? onClear : null,
            tooltip: '清空谱面',
            icon: const Icon(Icons.layers_clear_outlined, size: 20),
          ),
          const SizedBox(width: 2),
          IconButton(
            key: const ValueKey('export-slk'),
            onPressed: enabled ? onExportSlk : null,
            tooltip: '导出成品 SLK',
            icon: const Icon(Icons.ios_share_outlined, size: 20),
          ),
        ],
      ),
    );
  }
}

final class _StepAction {
  const _StepAction(this.icon, this.label, this.onPressed, {this.repeatDelta});

  final IconData icon;
  final String label;
  final VoidCallback onPressed;
  final double? repeatDelta;
}

class _ValueMenu extends StatefulWidget {
  const _ValueMenu({
    super.key,
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.allowNegative,
    required this.enabled,
    this.compact = false,
    this.commitTextOnEditingComplete = false,
    required this.onChanged,
    required this.stepButtons,
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final bool allowNegative;
  final bool enabled;
  final bool compact;
  final bool commitTextOnEditingComplete;
  final ValueChanged<double> onChanged;
  final List<_StepAction> stepButtons;

  @override
  State<_ValueMenu> createState() => _ValueMenuState();
}

class _ValueMenuState extends State<_ValueMenu> {
  final _controller = TextEditingController();
  final _focusNode = FocusNode();
  Timer? _repeatDelay;
  Timer? _repeatTimer;
  double? _repeatValue;
  bool _suppressNextTap = false;

  @override
  void initState() {
    super.initState();
    _focusNode.addListener(_handleFocusChanged);
  }

  @override
  void didUpdateWidget(covariant _ValueMenu oldWidget) {
    super.didUpdateWidget(oldWidget);
    if ((!widget.commitTextOnEditingComplete || !_focusNode.hasFocus) &&
        (!_controller.selection.isValid || oldWidget.value != widget.value) &&
        _controller.text != widget.value.toStringAsFixed(3)) {
      _controller.text = widget.value.toStringAsFixed(3);
    }
  }

  @override
  void dispose() {
    _repeatDelay?.cancel();
    _repeatTimer?.cancel();
    _focusNode.removeListener(_handleFocusChanged);
    _focusNode.dispose();
    _controller.dispose();
    super.dispose();
  }

  void _handleFocusChanged() {
    if (!_focusNode.hasFocus && widget.commitTextOnEditingComplete) {
      _commitText();
    }
  }

  void _beginRepeat(_StepAction action) {
    final delta = action.repeatDelta;
    if (delta == null) {
      return;
    }
    _cancelRepeat();
    _repeatDelay = Timer(const Duration(milliseconds: 500), () {
      _suppressNextTap = true;
      _repeatValue = widget.value;
      _advanceRepeat(delta);
      _repeatTimer = Timer.periodic(
        const Duration(milliseconds: 75),
        (_) => _advanceRepeat(delta),
      );
    });
  }

  void _advanceRepeat(double delta) {
    final current = _repeatValue;
    if (!mounted || current == null) {
      return;
    }
    final next = (current + delta).clamp(widget.min, widget.max).toDouble();
    _repeatValue = next;
    setState(() {
      _controller.text = next.toStringAsFixed(3);
      _controller.selection = TextSelection.collapsed(
        offset: _controller.text.length,
      );
    });
  }

  void _finishRepeat() {
    _repeatDelay?.cancel();
    _repeatDelay = null;
    _repeatTimer?.cancel();
    _repeatTimer = null;
    final value = _repeatValue;
    _repeatValue = null;
    if (value != null) {
      widget.onChanged(value);
    }
  }

  void _cancelRepeat() {
    final restoreValue = _repeatValue != null;
    _repeatDelay?.cancel();
    _repeatDelay = null;
    _repeatTimer?.cancel();
    _repeatTimer = null;
    _repeatValue = null;
    if (restoreValue && mounted) {
      setState(() {
        _controller.text = widget.value.toStringAsFixed(3);
        _controller.selection = TextSelection.collapsed(
          offset: _controller.text.length,
        );
      });
    }
  }

  void _activateStep(_StepAction action) {
    if (_suppressNextTap) {
      _suppressNextTap = false;
      return;
    }
    action.onPressed();
  }

  double? _parse(String text) {
    if (!RegExp(
      widget.allowNegative
          ? r'^-?\d{0,6}(?:\.\d{0,3})?$'
          : r'^\d{0,6}(?:\.\d{0,3})?$',
    ).hasMatch(text)) {
      return null;
    }
    final value = double.tryParse(text);
    if (value != null && value >= widget.min && value <= widget.max) {
      return value;
    }
    return null;
  }

  void _handleTextChanged(String text) {
    if (widget.commitTextOnEditingComplete) {
      return;
    }
    final value = _parse(text);
    if (value != null) {
      widget.onChanged(value);
    }
  }

  void _commitText() {
    final value = widget.enabled ? _parse(_controller.text) : null;
    if (value != null) {
      widget.onChanged(value);
    }
    final displayValue = value ?? widget.value;
    final displayText = displayValue.toStringAsFixed(3);
    _controller.value = TextEditingValue(
      text: displayText,
      selection: TextSelection.collapsed(offset: displayText.length),
    );
  }

  @override
  Widget build(BuildContext context) {
    return MenuAnchor(
      menuChildren: [
        SizedBox(
          width: widget.stepButtons.length > 2 ? 244 : 188,
          child: Padding(
            padding: const EdgeInsets.all(10),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  widget.label,
                  style: Theme.of(context).textTheme.bodySmall,
                ),
                const SizedBox(height: 6),
                TextField(
                  key: ValueKey('${widget.label.toLowerCase()}-input'),
                  controller: _controller,
                  focusNode: _focusNode,
                  enabled: widget.enabled,
                  autofocus: true,
                  keyboardType: const TextInputType.numberWithOptions(
                    decimal: true,
                    signed: true,
                  ),
                  inputFormatters: [
                    FilteringTextInputFormatter.allow(RegExp(r'[0-9.\-]')),
                  ],
                  onChanged: _handleTextChanged,
                  onSubmitted: widget.commitTextOnEditingComplete
                      ? (_) {
                          _commitText();
                          _focusNode.unfocus();
                        }
                      : null,
                  decoration: InputDecoration(suffixText: widget.label),
                ),
                const SizedBox(height: 8),
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    for (final action in widget.stepButtons)
                      Listener(
                        onPointerDown: widget.enabled
                            ? (_) => _beginRepeat(action)
                            : null,
                        onPointerUp: widget.enabled
                            ? (_) => _finishRepeat()
                            : null,
                        onPointerCancel: widget.enabled
                            ? (_) {
                                _cancelRepeat();
                                _suppressNextTap = false;
                              }
                            : null,
                        child: IconButton(
                          onPressed: widget.enabled
                              ? () => _activateStep(action)
                              : null,
                          tooltip: action.label,
                          icon: Icon(action.icon, size: 19),
                        ),
                      ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ],
      builder: (context, menu, child) => TextButton(
        onPressed: widget.enabled
            ? () {
                _controller.text = widget.value.toStringAsFixed(3);
                _controller.selection = TextSelection.collapsed(
                  offset: _controller.text.length,
                );
                menu.isOpen ? menu.close() : menu.open();
              }
            : null,
        style: TextButton.styleFrom(
          foregroundColor: EditorColors.text,
          backgroundColor: widget.compact ? EditorColors.canvas : null,
          side: widget.compact
              ? const BorderSide(color: EditorColors.border)
              : null,
          padding: const EdgeInsets.symmetric(horizontal: 8),
          minimumSize: const Size(84, 40),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(widget.label, style: Theme.of(context).textTheme.bodySmall),
            Text(
              widget.value.toStringAsFixed(3),
              style: const TextStyle(
                fontFeatures: [FontFeature.tabularFigures()],
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
