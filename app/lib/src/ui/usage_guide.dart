import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'editor_theme.dart';
import 'usage_guide_content.dart';

/// Full-screen, read-only help. The first page is a five-image swipe carousel;
/// swiping down (or the bottom pill) opens three vertical help entries. Detail
/// pages return to the help page and never touch the editor state.
class UsageGuideScreen extends StatefulWidget {
  const UsageGuideScreen({super.key});

  @override
  State<UsageGuideScreen> createState() => _UsageGuideScreenState();
}

class _UsageGuideScreenState extends State<UsageGuideScreen> {
  final PageController _imageController = PageController();
  final PageController _verticalController = PageController();
  int _imageIndex = 0;
  int _verticalIndex = 0;
  int? _detailIndex;

  bool get _mobile =>
      defaultTargetPlatform == TargetPlatform.iOS ||
      defaultTargetPlatform == TargetPlatform.android;

  @override
  void dispose() {
    _imageController.dispose();
    _verticalController.dispose();
    super.dispose();
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
  const _BlockView({required this.block});

  final UsageGuideBlock block;

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
      GuideShortcuts(:final rows) => Container(
        decoration: BoxDecoration(
          color: EditorColors.surface,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(color: EditorColors.border),
        ),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            for (final (shortcut, description) in rows)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 8),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    SizedBox(
                      width: 250,
                      child: Text(
                        shortcut,
                        style: const TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w600,
                          color: EditorColors.text,
                          fontFeatures: [FontFeature.tabularFigures()],
                        ),
                      ),
                    ),
                    const SizedBox(width: 14),
                    Expanded(
                      child: Text(
                        description,
                        style: const TextStyle(
                          fontSize: 14,
                          height: 1.5,
                          color: EditorColors.textMuted,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
          ],
        ),
      ),
    };
  }
}
