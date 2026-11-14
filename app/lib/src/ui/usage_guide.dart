import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'editor_keymap.dart';
import 'editor_theme.dart';
import 'usage_guide_content.dart';

/// Full-screen, read-only help. The first page is a five-image swipe carousel;
/// swiping down (or the bottom pill) opens three vertical help entries. Detail
/// pages return to the help page and never touch the editor state.
///
/// The shortcut detail page is the one exception to "read-only" on desktop:
/// rows for [EditorShortcutAction] values open a capture dialog and report the
/// new keymap through [onKeymapChanged]. Rebinding is an input preference only;
/// no page here sends a domain command or touches the draft.
class UsageGuideScreen extends StatefulWidget {
  const UsageGuideScreen({
    super.key,
    required this.keymap,
    required this.onKeymapChanged,
  });

  final EditorKeymap keymap;
  final ValueChanged<EditorKeymap> onKeymapChanged;

  @override
  State<UsageGuideScreen> createState() => _UsageGuideScreenState();
}

class _UsageGuideScreenState extends State<UsageGuideScreen> {
  final PageController _imageController = PageController();
  final PageController _verticalController = PageController();
  int _imageIndex = 0;
  int _verticalIndex = 0;
  int? _detailIndex;
  late EditorKeymap _keymap = widget.keymap;

  bool get _mobile =>
      defaultTargetPlatform == TargetPlatform.iOS ||
      defaultTargetPlatform == TargetPlatform.android;

  TargetPlatform get _platform => defaultTargetPlatform;

  @override
  void dispose() {
    _imageController.dispose();
    _verticalController.dispose();
    super.dispose();
  }

  void _applyKeymap(EditorKeymap next) {
    setState(() => _keymap = next);
    widget.onKeymapChanged(next);
  }

  Future<void> _editShortcut(EditorShortcutAction action) async {
    final result = await showDialog<List<ShortcutBinding>>(
      context: context,
      builder: (context) => _ShortcutCaptureDialog(
        action: action,
        keymap: _keymap,
        platform: _platform,
      ),
    );
    if (result == null || !mounted) {
      return;
    }
    _applyKeymap(_keymap.withBindings(action, result));
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: _detailIndex == null,
      onPopInvokedWithResult: (didPop, result) {
        if (didPop || _detailIndex == null) {
          return;
        }
        setState(() => _detailIndex = null);
      },
      child: Material(
        key: const ValueKey('usage-guide-screen'),
        color: Colors.black,
        child: IndexedStack(
          index: _detailIndex == null ? 0 : 1,
          children: [
            _buildHelp(context),
            _detailIndex == null
                ? const SizedBox.shrink()
                : _buildDetail(_detailIndex!),
          ],
        ),
      ),
    );
  }

  Widget _buildHelp(BuildContext context) {
    return Stack(
      fit: StackFit.expand,
      children: [
        PageView(
          key: const ValueKey('usage-guide-vertical'),
          controller: _verticalController,
          scrollDirection: Axis.vertical,
          onPageChanged: (index) => setState(() => _verticalIndex = index),
          children: [
            _buildImages(context),
            _buildCards(context),
          ],
        ),
        SafeArea(
          child: Align(
            alignment: Alignment.topRight,
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: _RoundOverlayButton(
                key: const ValueKey('usage-guide-close'),
                icon: Icons.close,
                tooltip: '关闭',
                onPressed: _closeHelp,
              ),
            ),
          ),
        ),
        SafeArea(
          child: Align(
            alignment: Alignment.bottomCenter,
            child: Padding(
              padding: const EdgeInsets.only(bottom: 16),
              child: _BottomPill(
                showingCards: _verticalIndex == 1,
                onPressed: () => _goToHelpPage(_verticalIndex == 1 ? 0 : 1),
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildImages(BuildContext context) {
    return Stack(
      fit: StackFit.expand,
      children: [
        PageView.builder(
          key: const ValueKey('usage-guide-carousel'),
          controller: _imageController,
          itemCount: usageGuideSlides.length,
          onPageChanged: (index) => setState(() => _imageIndex = index),
          itemBuilder: (context, index) {
            final slide = usageGuideSlides[index];
            return Image.asset(
              _mobile ? slide.mobileAsset : slide.desktopAsset,
              key: ValueKey('usage-guide-slide-$index'),
              semanticLabel: slide.semanticLabel,
              fit: BoxFit.contain,
              filterQuality: FilterQuality.medium,
            );
          },
        ),
        SafeArea(
          child: Align(
            alignment: Alignment.centerLeft,
            child: Padding(
              padding: const EdgeInsets.only(left: 12),
              child: _RoundOverlayButton(
                key: const ValueKey('usage-guide-prev'),
                icon: Icons.chevron_left,
                tooltip: '上一张',
                onPressed: _imageIndex == 0
                    ? null
                    : () => _goToImage(_imageIndex - 1),
              ),
            ),
          ),
        ),
        SafeArea(
          child: Align(
            alignment: Alignment.centerRight,
            child: Padding(
              padding: const EdgeInsets.only(right: 12),
              child: _RoundOverlayButton(
                key: const ValueKey('usage-guide-next'),
                icon: Icons.chevron_right,
                tooltip: '下一张',
                onPressed: _imageIndex == usageGuideSlides.length - 1
                    ? null
                    : () => _goToImage(_imageIndex + 1),
              ),
            ),
          ),
        ),
        const Positioned(
          left: 0,
          right: 0,
          bottom: 0,
          height: 190,
          child: IgnorePointer(
            child: DecoratedBox(
              decoration: BoxDecoration(
                gradient: LinearGradient(
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                  colors: [Colors.transparent, Color(0xD9000000)],
                ),
              ),
            ),
          ),
        ),
        SafeArea(
          child: Align(
            alignment: Alignment.bottomCenter,
            child: Padding(
              padding: const EdgeInsets.only(bottom: 78),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  _TipCaption(text: usageGuideSlides[_imageIndex].tip),
                  const SizedBox(height: 6),
                  _buildDots(),
                ],
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildDots() {
    return Row(
      mainAxisSize: MainAxisSize.min,
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        for (var index = 0; index < usageGuideSlides.length; index++)
          GestureDetector(
            key: ValueKey('usage-guide-dot-$index'),
            onTap: () => _goToImage(index),
            behavior: HitTestBehavior.opaque,
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 6),
              child: AnimatedContainer(
                duration: const Duration(milliseconds: 180),
                width: index == _imageIndex ? 20 : 8,
                height: 8,
                decoration: BoxDecoration(
                  color: Colors.white.withValues(
                    alpha: index == _imageIndex ? 0.9 : 0.35,
                  ),
                  borderRadius: BorderRadius.circular(4),
                ),
              ),
            ),
          ),
      ],
    );
  }

  Widget _buildCards(BuildContext context) {
    return ColoredBox(
      key: const ValueKey('usage-guide-cards'),
      color: EditorColors.canvas,
      child: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(24, 72, 24, 104),
          child: Center(
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 720),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  const Text(
                    '更多帮助',
                    style: TextStyle(
                      fontSize: 24,
                      fontWeight: FontWeight.w600,
                      color: EditorColors.text,
                    ),
                  ),
                  const SizedBox(height: 20),
                  for (var index = 0;
                      index < usageGuideTopics.length;
                      index++) ...[
                    _TopicCard(
                      index: index,
                      onTap: () => setState(() => _detailIndex = index),
                    ),
                    if (index != usageGuideTopics.length - 1)
                      const SizedBox(height: 12),
                  ],
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildDetail(int index) {
    final topic = usageGuideTopics[index];
    return ColoredBox(
      key: const ValueKey('usage-guide-detail'),
      color: EditorColors.canvas,
      child: SafeArea(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 8, 8, 8),
              child: Row(
                children: [
                  Expanded(
                    child: Semantics(
                      header: true,
                      child: Text(
                        topic.title,
                        key: const ValueKey('usage-guide-title'),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 21,
                          fontWeight: FontWeight.w600,
                          color: EditorColors.text,
                        ),
                      ),
                    ),
                  ),
                  _RoundOverlayButton(
                    key: const ValueKey('usage-guide-close'),
                    icon: Icons.close,
                    tooltip: '返回帮助',
                    onPressed: () => setState(() => _detailIndex = null),
                  ),
                ],
              ),
            ),
            const Divider(height: 1),
            Expanded(
              child: SingleChildScrollView(
                key: ValueKey('usage-guide-topic-body-$index'),
                padding: const EdgeInsets.fromLTRB(24, 20, 24, 32),
                child: Center(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 760),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        for (var blockIndex = 0;
                            blockIndex < topic.blocks.length;
                            blockIndex++)
                          Padding(
                            padding: EdgeInsets.only(
                              bottom: blockIndex == topic.blocks.length - 1
                                  ? 0
                                  : 18,
                            ),
                            child: _BlockView(
                              block: topic.blocks[blockIndex],
                              keymap: _keymap,
                              platform: _platform,
                              mobile: _mobile,
                              onEditAction: _editShortcut,
                              onResetKeymap: () => _applyKeymap(
                                EditorKeymap.defaults(_platform),
                              ),
                            ),
                          ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  void _goToImage(int index) {
    if (index < 0 || index >= usageGuideSlides.length) {
      return;
    }
    _imageController.animateToPage(
      index,
      duration: const Duration(milliseconds: 240),
      curve: Curves.easeOut,
    );
  }

  void _goToHelpPage(int index) {
    _verticalController.animateToPage(
      index,
      duration: const Duration(milliseconds: 260),
      curve: Curves.easeOut,
    );
  }

  void _closeHelp() {
    Navigator.of(context).maybePop();
  }
}

class _TipCaption extends StatelessWidget {
  const _TipCaption({required this.text});

  final String text;

  @override
  Widget build(BuildContext context) {
    return Container(
      constraints: const BoxConstraints(maxWidth: 560),
      margin: const EdgeInsets.symmetric(horizontal: 24),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 7),
      decoration: BoxDecoration(
        color: Colors.black.withValues(alpha: 0.42),
        borderRadius: BorderRadius.circular(14),
      ),
      child: Text(
        text,
        textAlign: TextAlign.center,
        style: TextStyle(
          fontSize: 13,
          height: 1.4,
          color: Colors.white.withValues(alpha: 0.88),
        ),
      ),
    );
  }
}

class _RoundOverlayButton extends StatelessWidget {
  const _RoundOverlayButton({
    super.key,
    required this.icon,
    required this.tooltip,
    required this.onPressed,
  });

  final IconData icon;
  final String tooltip;
  final VoidCallback? onPressed;

  @override
  Widget build(BuildContext context) {
    final enabled = onPressed != null;
    return Tooltip(
      message: tooltip,
      child: Material(
        color: Colors.white.withValues(alpha: enabled ? 0.14 : 0.05),
        shape: const CircleBorder(),
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          customBorder: const CircleBorder(),
          onTap: onPressed,
          child: SizedBox(
            width: 48,
            height: 48,
            child: Icon(
              icon,
              size: 28,
              color: Colors.white.withValues(alpha: enabled ? 0.92 : 0.3),
            ),
          ),
        ),
      ),
    );
  }
}

class _BottomPill extends StatelessWidget {
  const _BottomPill({required this.showingCards, required this.onPressed});

  final bool showingCards;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Material(
      key: const ValueKey('usage-guide-bottom-toggle'),
      color: Colors.white.withValues(alpha: 0.14),
      borderRadius: BorderRadius.circular(24),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onPressed,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 11),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                showingCards
                    ? Icons.keyboard_arrow_up
                    : Icons.keyboard_arrow_down,
                size: 20,
                color: Colors.white.withValues(alpha: 0.9),
              ),
              const SizedBox(width: 6),
              Text(
                showingCards ? '返回图片' : '更多帮助',
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w500,
                  color: Colors.white.withValues(alpha: 0.9),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _TopicCard extends StatelessWidget {
  const _TopicCard({required this.index, required this.onTap});

  final int index;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final topic = usageGuideTopics[index];
    return Material(
      color: EditorColors.surfaceRaised,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(10),
        side: const BorderSide(color: EditorColors.border),
      ),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        key: ValueKey('usage-guide-topic-$index'),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 18),
          child: Row(
            children: [
              Container(
                width: 42,
                height: 42,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: EditorColors.accent.withValues(alpha: 0.16),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Icon(topic.icon, size: 23, color: EditorColors.accent),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Text(
                  topic.title,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w600,
                    color: EditorColors.text,
                  ),
                ),
              ),
              const Icon(
                Icons.chevron_right,
                size: 22,
                color: EditorColors.textMuted,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _BlockView extends StatelessWidget {
  const _BlockView({
    required this.block,
    required this.keymap,
    required this.platform,
    required this.mobile,
    required this.onEditAction,
    required this.onResetKeymap,
  });

  final UsageGuideBlock block;
  final EditorKeymap keymap;
  final TargetPlatform platform;
  final bool mobile;
  final ValueChanged<EditorShortcutAction> onEditAction;
  final VoidCallback onResetKeymap;

  @override
  Widget build(BuildContext context) {
    return switch (block) {
      GuideParagraph(:final text) => Text(
        text,
        style: const TextStyle(
          fontSize: 15,
          height: 1.65,
          color: EditorColors.text,
        ),
      ),
      GuideBullets(:final items) => Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          for (final item in items)
            Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Padding(
                    padding: const EdgeInsets.only(top: 9, right: 9),
                    child: Container(
                      width: 5,
                      height: 5,
                      decoration: const BoxDecoration(
                        color: EditorColors.accent,
                        shape: BoxShape.circle,
                      ),
                    ),
                  ),
                  Expanded(
                    child: Text(
                      item,
                      style: const TextStyle(
                        fontSize: 15,
                        height: 1.65,
                        color: EditorColors.text,
                      ),
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
      GuideSteps(:final steps) => Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          for (var index = 0; index < steps.length; index++)
            Padding(
              padding: const EdgeInsets.only(bottom: 10),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Container(
                    width: 24,
                    height: 24,
                    alignment: Alignment.center,
                    decoration: BoxDecoration(
                      color: EditorColors.accent.withValues(alpha: 0.18),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Text(
                      '${index + 1}',
                      style: const TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                        color: EditorColors.accent,
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Text(
                      steps[index],
                      style: const TextStyle(
                        fontSize: 15,
                        height: 1.65,
                        color: EditorColors.text,
                      ),
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
      GuideShortcuts() => _ShortcutTable(
        keymap: keymap,
        platform: platform,
        mobile: mobile,
        onEditAction: onEditAction,
        onResetKeymap: onResetKeymap,
      ),
    };
  }
}

/// Keyboard and gesture table. Desktop players see the live keymap plus the
/// fixed mouse rows; mobile players see touch rows only, so neither platform
/// has to read instructions it cannot use.
class _ShortcutTable extends StatelessWidget {
  const _ShortcutTable({
    required this.keymap,
    required this.platform,
    required this.mobile,
    required this.onEditAction,
    required this.onResetKeymap,
  });

  final EditorKeymap keymap;
  final TargetPlatform platform;
  final bool mobile;
  final ValueChanged<EditorShortcutAction> onEditAction;
  final VoidCallback onResetKeymap;

  @override
  Widget build(BuildContext context) {
    final rows = <Widget>[
      if (!mobile)
        for (final action in EditorShortcutAction.values)
          _ShortcutRow(
            key: ValueKey('shortcut-row-${action.id}'),
            keys: formatShortcutBindings(keymap.bindingsFor(action), platform),
            description: action.label,
            editable: true,
            onTap: () => onEditAction(action),
          ),
      if (!mobile)
        for (final (keys, description) in desktopFixedShortcutRows(platform))
          _ShortcutRow(keys: keys, description: description),
      if (mobile)
        for (final (keys, description) in mobileShortcutGestureRows)
          _ShortcutRow(keys: keys, description: description),
    ];
    return Container(
      decoration: BoxDecoration(
        color: EditorColors.surface,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: EditorColors.border),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          for (var index = 0; index < rows.length; index++) ...[
            rows[index],
            if (index != rows.length - 1)
              Divider(
                height: 1,
                color: EditorColors.border.withValues(alpha: 0.5),
              ),
          ],
          if (!mobile) ...[
            const Divider(height: 20, color: EditorColors.border),
            Text(
              '点击键盘行可以改成自己的组合；Backspace 清除绑定，Esc、Enter、复制和剪切保持固定。',
              style: TextStyle(
                fontSize: 12.5,
                height: 1.5,
                color: EditorColors.textMuted,
              ),
            ),
            const SizedBox(height: 4),
            Align(
              alignment: Alignment.centerLeft,
              child: TextButton.icon(
                key: const ValueKey('shortcut-reset-all'),
                onPressed: keymap.isDefaultFor(platform) ? null : onResetKeymap,
                style: TextButton.styleFrom(
                  foregroundColor: EditorColors.accent,
                ),
                icon: const Icon(Icons.restart_alt, size: 18),
                label: const Text('恢复默认快捷键'),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _ShortcutRow extends StatelessWidget {
  const _ShortcutRow({
    super.key,
    required this.keys,
    required this.description,
    this.editable = false,
    this.onTap,
  });

  final String keys;
  final String description;
  final bool editable;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final content = Padding(
      padding: const EdgeInsets.symmetric(vertical: 10),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  keys,
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    height: 1.4,
                    color: EditorColors.text,
                    fontFeatures: [FontFeature.tabularFigures()],
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  description,
                  style: const TextStyle(
                    fontSize: 13.5,
                    height: 1.45,
                    color: EditorColors.textMuted,
                  ),
                ),
              ],
            ),
          ),
          if (editable)
            const Padding(
              padding: EdgeInsets.only(left: 12, top: 1),
              child: Icon(
                Icons.edit_outlined,
                size: 18,
                color: EditorColors.accent,
              ),
            ),
        ],
      ),
    );
    if (!editable) {
      return content;
    }
    return Semantics(
      button: true,
      label: '$description，当前 $keys，点击修改',
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(6),
        child: content,
      ),
    );
  }
}

/// Captures one new chord. The dialog owns no editor state: it returns the new
/// binding list, or null when the player cancels.
class _ShortcutCaptureDialog extends StatefulWidget {
  const _ShortcutCaptureDialog({
    required this.action,
    required this.keymap,
    required this.platform,
  });

  final EditorShortcutAction action;
  final EditorKeymap keymap;
  final TargetPlatform platform;

  @override
  State<_ShortcutCaptureDialog> createState() => _ShortcutCaptureDialogState();
}

class _ShortcutCaptureDialogState extends State<_ShortcutCaptureDialog> {
  final FocusNode _focusNode = FocusNode(debugLabel: '快捷键捕获');
  ShortcutBinding? _pending;
  bool _unbind = false;
  String? _error;

  List<ShortcutBinding> get _current =>
      widget.keymap.bindingsFor(widget.action);

  List<ShortcutBinding> get _defaults =>
      defaultEditorShortcutBindings(widget.platform)[widget.action] ??
      const <ShortcutBinding>[];

  bool get _matchesDefaults {
    final defaults = _defaults;
    if (defaults.length != _current.length) {
      return false;
    }
    for (var index = 0; index < defaults.length; index++) {
      if (!defaults[index].hasSameChord(_current[index])) {
        return false;
      }
    }
    return true;
  }

  bool get _canApply => _unbind || (_pending != null && _error == null);

  String get _previewText {
    if (_unbind) {
      return '未绑定';
    }
    final pending = _pending;
    if (pending != null) {
      return formatShortcutBinding(pending, widget.platform);
    }
    return formatShortcutBindings(_current, widget.platform);
  }

  @override
  void dispose() {
    _focusNode.dispose();
    super.dispose();
  }

  KeyEventResult _handleKey(FocusNode node, KeyEvent event) {
    if (event is! KeyDownEvent) {
      return KeyEventResult.ignored;
    }
    final key = event.logicalKey;
    if (key == LogicalKeyboardKey.escape) {
      Navigator.of(context).maybePop();
      return KeyEventResult.handled;
    }
    if (key == LogicalKeyboardKey.backspace ||
        key == LogicalKeyboardKey.delete) {
      setState(() {
        _pending = null;
        _unbind = true;
        _error = null;
      });
      return KeyEventResult.handled;
    }
    if (isModifierOnlyKey(key)) {
      // Modifier-only presses never become a binding; repaint so the preview
      // can show that the modifier is being held.
      setState(() {});
      return KeyEventResult.handled;
    }
    final keyboard = HardwareKeyboard.instance;
    final binding = ShortcutBinding(
      keyId: key.keyId,
      control: keyboard.isControlPressed,
      meta: keyboard.isMetaPressed,
      shift: keyboard.isShiftPressed,
      alt: keyboard.isAltPressed,
    );
    final conflict = widget.keymap.conflictingAction(widget.action, binding);
    final String? error;
    if (isReservedShortcut(binding)) {
      error = 'Esc、Enter、复制和剪切使用固定组合';
    } else if (conflict != null) {
      error = '该组合已用于「${conflict.label}」';
    } else {
      error = null;
    }
    setState(() {
      _pending = binding;
      _unbind = false;
      _error = error;
    });
    return KeyEventResult.handled;
  }

  void _apply() {
    if (_unbind) {
      Navigator.of(context).pop(<ShortcutBinding>[]);
      return;
    }
    final pending = _pending;
    if (pending == null || _error != null) {
      return;
    }
    Navigator.of(context).pop(<ShortcutBinding>[pending]);
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      key: const ValueKey('shortcut-capture-dialog'),
      backgroundColor: EditorColors.surfaceRaised,
      title: const Text(
        '修改快捷键',
        style: TextStyle(
          fontSize: 19,
          fontWeight: FontWeight.w600,
          color: EditorColors.text,
        ),
      ),
      content: Focus(
        focusNode: _focusNode,
        autofocus: true,
        onKeyEvent: _handleKey,
        child: SizedBox(
          width: 380,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                widget.action.label,
                style: const TextStyle(
                  fontSize: 14,
                  color: EditorColors.textMuted,
                ),
              ),
              const SizedBox(height: 12),
              Semantics(
                liveRegion: true,
                child: Container(
                  key: const ValueKey('shortcut-capture-preview'),
                  padding: const EdgeInsets.symmetric(
                    horizontal: 16,
                    vertical: 18,
                  ),
                  decoration: BoxDecoration(
                    color: EditorColors.canvas,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(
                      color: _error != null
                          ? EditorColors.danger
                          : EditorColors.accent.withValues(alpha: 0.6),
                    ),
                  ),
                  child: Text(
                    _previewText,
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      fontSize: 20,
                      fontWeight: FontWeight.w600,
                      color: _error != null
                          ? EditorColors.danger
                          : EditorColors.text,
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 10),
              Text(
                _error ?? (_unbind ? '应用后该操作没有快捷键' : '按下新的组合键'),
                key: const ValueKey('shortcut-capture-hint'),
                textAlign: TextAlign.center,
                style: TextStyle(
                  fontSize: 13,
                  height: 1.4,
                  color: _error != null
                      ? EditorColors.danger
                      : EditorColors.textMuted,
                ),
              ),
              const SizedBox(height: 4),
              const Text(
                'Esc 取消，Backspace 清除绑定。',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 12, color: EditorColors.textMuted),
              ),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          key: const ValueKey('shortcut-capture-default'),
          onPressed: _matchesDefaults
              ? null
              : () => Navigator.of(context).pop(_defaults),
          child: const Text('恢复默认'),
        ),
        TextButton(
          key: const ValueKey('shortcut-capture-cancel'),
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('取消'),
        ),
        FilledButton(
          key: const ValueKey('shortcut-capture-apply'),
          onPressed: _canApply ? _apply : null,
          child: const Text('应用'),
        ),
      ],
    );
  }
}
