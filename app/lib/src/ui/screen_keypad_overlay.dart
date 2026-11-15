import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:otgy_native/otgy_native.dart';

import '../application/editor_controller.dart';
import 'editor_theme.dart';
import 'judgement_display.dart';

class ScreenKeypadOverlay extends StatelessWidget {
  const ScreenKeypadOverlay({
    super.key,
    required this.controller,
    this.onTrigger,
  });

  final EditorController controller;

  /// Test seam: when provided it replaces the controller's native trigger.
  final ValueChanged<PlaybackSoundId>? onTrigger;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: controller,
      builder: (context, _) {
        final arrows =
            controller.screenKeypadLayout == ScreenKeypadLayout.arrows &&
            controller.playbackDanceMode == PlaybackDanceMode.fourKey;
        final trigger = onTrigger ?? controller.pushSimulationTrigger;
        return Material(
          color: Colors.transparent,
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: () {},
            child: ColoredBox(
              color: judgementScrimColor,
              child: arrows
                  ? _ArrowLayout(controller: controller, trigger: trigger)
                  : _NumericLayout(controller: controller, trigger: trigger),
            ),
          ),
        );
      },
    );
  }
}

class _NumericLayout extends StatelessWidget {
  const _NumericLayout({required this.controller, required this.trigger});

  final EditorController controller;
  final ValueChanged<PlaybackSoundId> trigger;

  @override
  Widget build(BuildContext context) {
    final viewPadding = MediaQuery.paddingOf(context);
    final height = MediaQuery.sizeOf(context).height;
    final bottomMargin = math.max(84.0, viewPadding.bottom + 48.0);
    final judgement = controller.judgementEnabled
        ? controller.latestJudgement
        : null;
    return Stack(
      children: [
        Positioned(
          top: viewPadding.top + 32,
          left: 0,
          right: 0,
          child: Center(
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                _PlayButton(controller: controller),
                if (controller.playbackDanceMode ==
                    PlaybackDanceMode.fourKey) ...[
                  const SizedBox(width: 10),
                  _LayoutSwitchButton(controller: controller),
                ],
              ],
            ),
          ),
        ),
        Positioned(
          top: viewPadding.top + 10,
          right: 12,
          child: _CloseButton(controller: controller),
        ),
        if (judgement != null)
          Positioned(
            top: height * 0.25 - 34,
            left: 0,
            right: 0,
            child: IgnorePointer(child: JudgementText(judgement: judgement)),
          ),
        Positioned(
          left: 12,
          right: 12,
          top: viewPadding.top + 96,
          bottom: bottomMargin,
          child: Align(
            alignment: Alignment.bottomCenter,
            // The keypad sizes itself to the available box: gaps compress
            // first, caps shrink only when the gaps are exhausted.
            child: _NumericKeypad(controller: controller, trigger: trigger),
          ),
        ),
      ],
    );
  }
}

class _ArrowLayout extends StatelessWidget {
  const _ArrowLayout({required this.controller, required this.trigger});

  final EditorController controller;
  final ValueChanged<PlaybackSoundId> trigger;

  @override
  Widget build(BuildContext context) {
    final judgement = controller.judgementEnabled
        ? controller.latestJudgement
        : null;
    return Center(
      child: RotatedBox(
        key: const ValueKey('arrow-keypad-rotation'),
        quarterTurns: controller.arrowKeypadFlipped ? 3 : 1,
        child: LayoutBuilder(
          builder: (context, constraints) {
            // The rotated frame may not exceed the space the device gives us;
            // the keypad itself squeezes its gaps before shrinking its caps.
            final width = math.min(720.0, constraints.maxWidth);
            final height = math.min(320.0, constraints.maxHeight);
            return SizedBox(
              width: width,
              height: height,
              child: Stack(
                children: [
                  Positioned(
                    top: 12,
                    left: 14,
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        _PlayButton(controller: controller),
                        if (controller.playbackDanceMode ==
                            PlaybackDanceMode.fourKey) ...[
                          const SizedBox(width: 10),
                          _LayoutSwitchButton(controller: controller),
                          const SizedBox(width: 10),
                          _FlipButton(controller: controller),
                        ],
                      ],
                    ),
                  ),
                  Positioned(
                    top: 14,
                    right: 14,
                    child: _CloseButton(controller: controller),
                  ),
                  if (judgement != null)
                    Positioned(
                      top: 12,
                      left: 70,
                      right: 70,
                      child: IgnorePointer(
                        child: JudgementText(judgement: judgement),
                      ),
                    ),
                  Center(
                    child: _ArrowKeys(controller: controller, trigger: trigger),
                  ),
                ],
              ),
            );
          },
        ),
      ),
    );
  }
}

class _PlayButton extends StatelessWidget {
  const _PlayButton({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      label: controller.isPlaying ? '暂停模拟播放' : '开始模拟播放',
      child: SizedBox(
        width: 52,
        height: 52,
        child: Material(
          color: EditorColors.accent,
          shape: const CircleBorder(),
          clipBehavior: Clip.antiAlias,
          child: InkWell(
            onTap: () => controller.togglePlayback(),
            child: Icon(
              controller.isPlaying
                  ? Icons.pause_rounded
                  : Icons.play_arrow_rounded,
              color: EditorColors.canvas,
              size: 28,
            ),
          ),
        ),
      ),
    );
  }
}

class _CloseButton extends StatelessWidget {
  const _CloseButton({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      label: '关闭屏幕键盘并暂停',
      child: SizedBox(
        width: 40,
        height: 40,
        child: Material(
          color: EditorColors.surfaceRaised,
          borderRadius: BorderRadius.circular(10),
          clipBehavior: Clip.antiAlias,
          child: InkWell(
            onTap: () => controller.closeScreenKeypad(),
            child: const Icon(Icons.close, size: 22),
          ),
        ),
      ),
    );
  }
}

class _FlipButton extends StatelessWidget {
  const _FlipButton({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      label: '翻转方向键布局',
      child: SizedBox(
        key: const ValueKey('screen-keypad-flip'),
        width: 40,
        height: 40,
        child: Material(
          color: EditorColors.surfaceRaised,
          borderRadius: BorderRadius.circular(10),
          clipBehavior: Clip.antiAlias,
          child: InkWell(
            onTap: controller.toggleArrowKeypadFlip,
            // The arrow frame is rotated for landscape; turn the glyph back
            // so its mirror axis matches the direction the layout flips.
            child: const RotatedBox(
              quarterTurns: 1,
              child: Icon(Icons.flip, size: 22),
            ),
          ),
        ),
      ),
    );
  }
}

class _LayoutSwitchButton extends StatelessWidget {
  const _LayoutSwitchButton({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    if (controller.playbackDanceMode != PlaybackDanceMode.fourKey) {
      return const SizedBox.shrink();
    }
    final arrows = controller.screenKeypadLayout == ScreenKeypadLayout.arrows;
    return Semantics(
      button: true,
      label: arrows ? '切换到数字键盘' : '切换到方向键键盘',
      child: SizedBox(
        key: const ValueKey('screen-keypad-layout-switch'),
        width: 40,
        height: 40,
        child: Material(
          color: EditorColors.surfaceRaised,
          borderRadius: BorderRadius.circular(10),
          clipBehavior: Clip.antiAlias,
          child: InkWell(
            onTap: controller.toggleScreenKeypadLayout,
            child: const Icon(Icons.sync, size: 24),
          ),
        ),
      ),
    );
  }
}

/// A standard full-size keyboard has a 19.05 mm key pitch with an 18 mm cap
/// and about 1 mm between caps. In Flutter's density-independent units that
/// is a 120 dp pitch: a 113 dp cap with a 7 dp gap.
///
/// The caps never grow. When the device cannot hold the keypad, the gaps
/// collapse first (down to [minGap]) and only then do the caps shrink, so a
/// small screen keeps thumb-sized keys for as long as possible.
class _KeyMetrics {
  const _KeyMetrics({required this.cap, required this.gap});

  static const capLimit = 113.0;
  static const maxGap = 7.0;
  static const minGap = 3.0;

  final double cap;
  final double gap;

  static _KeyMetrics fitGrid(
    BoxConstraints constraints, {
    required int columns,
    required int rows,
  }) {
    final gridWidth = capLimit * columns + maxGap * (columns - 1);
    final gridHeight = capLimit * rows + maxGap * (rows - 1);
    final maxWidth = constraints.maxWidth.isFinite
        ? constraints.maxWidth
        : gridWidth;
    final maxHeight = constraints.maxHeight.isFinite
        ? constraints.maxHeight
        : gridHeight;
    if (gridWidth <= maxWidth && gridHeight <= maxHeight) {
      return const _KeyMetrics(cap: capLimit, gap: maxGap);
    }
    // Squeeze the gaps before touching the caps.
    final gapByWidth = (maxWidth - capLimit * columns) / (columns - 1);
    final gapByHeight = (maxHeight - capLimit * rows) / (rows - 1);
    final gap = math
        .min(maxGap, math.min(gapByWidth, gapByHeight))
        .clamp(minGap, maxGap)
        .toDouble();
    if (capLimit * columns + gap * (columns - 1) <= maxWidth &&
        capLimit * rows + gap * (rows - 1) <= maxHeight) {
      return _KeyMetrics(cap: capLimit, gap: gap);
    }
    // The gaps are exhausted; scale everything down as a last resort.
    final scale = math
        .min(
          maxWidth / (capLimit * columns + minGap * (columns - 1)),
          maxHeight / (capLimit * rows + minGap * (rows - 1)),
        )
        .clamp(0.0, 1.0)
        .toDouble();
    return _KeyMetrics(cap: capLimit * scale, gap: minGap * scale);
  }
}

class _NumericKeypad extends StatelessWidget {
  const _NumericKeypad({required this.controller, required this.trigger});

  final EditorController controller;
  final ValueChanged<PlaybackSoundId> trigger;

  bool _enabled(int number) {
    if (controller.playbackDanceMode == PlaybackDanceMode.eightKey) {
      return true;
    }
    return number == 2 || number == 4 || number == 6 || number == 8;
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final metrics = _KeyMetrics.fitGrid(constraints, columns: 3, rows: 4);
        final keyWidth = metrics.cap;
        final keyHeight = metrics.cap;
        final gap = metrics.gap;

        Widget key(
          String label,
          PlaybackSoundId sound, {
          int? number,
          bool wide = false,
        }) {
          final enabled = number == null || _enabled(number);
          return _KeySurface(
            label: label,
            width: wide ? keyWidth * 3 + gap * 2 : keyWidth,
            height: keyHeight,
            enabled: enabled,
            fontSize: 34,
            onTap: enabled ? () => trigger(sound) : null,
          );
        }

        return SizedBox(
          width: keyWidth * 3 + gap * 2,
          height: keyHeight * 4 + gap * 3,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  key('7', PlaybackSoundId.f1_7, number: 7),
                  SizedBox(width: gap),
                  key('8', PlaybackSoundId.f1_8, number: 8),
                  SizedBox(width: gap),
                  key('9', PlaybackSoundId.f1_9, number: 9),
                ],
              ),
              SizedBox(height: gap),
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  key('4', PlaybackSoundId.f1_4, number: 4),
                  SizedBox(width: gap),
                  key('5', PlaybackSoundId.f1Yeah),
                  SizedBox(width: gap),
                  key('6', PlaybackSoundId.f1_6, number: 6),
                ],
              ),
              SizedBox(height: gap),
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  key('1', PlaybackSoundId.f1_1, number: 1),
                  SizedBox(width: gap),
                  key('2', PlaybackSoundId.f1_2, number: 2),
                  SizedBox(width: gap),
                  key('3', PlaybackSoundId.f1_3, number: 3),
                ],
              ),
              SizedBox(height: gap),
              key('0', PlaybackSoundId.f1Go, wide: true),
            ],
          ),
        );
      },
    );
  }
}

class _ArrowMetrics {
  const _ArrowMetrics({
    required this.cap,
    required this.wide,
    required this.gap,
    required this.clusterGap,
  });

  static const capLimit = 113.0;
  static const wideLimit = 150.0; // 1.25u, like a full-size Ctrl key.
  static const maxGap = 7.0;
  static const minGap = 3.0;

  /// Dead space that separates the arrow cluster from Ctrl and 0, the way a
  /// full-size keyboard keeps the inverted-T away from its neighbours.
  static const maxClusterGap = 18.0;
  static const minClusterGap = 6.0;

  final double cap;
  final double wide;
  final double gap;
  final double clusterGap;

  double get width => wide * 2 + cap * 3 + gap * 2 + clusterGap * 2;
  double get height => cap * 2 + gap;

  static _ArrowMetrics fit(BoxConstraints constraints) {
    final fullWidth =
        wideLimit * 2 + capLimit * 3 + maxGap * 2 + maxClusterGap * 2;
    final fullHeight = capLimit * 2 + maxGap;
    final maxWidth = constraints.maxWidth.isFinite
        ? constraints.maxWidth
        : fullWidth;
    final maxHeight = constraints.maxHeight.isFinite
        ? constraints.maxHeight
        : fullHeight;
    final base = wideLimit * 2 + capLimit * 3;

    double widthWith(double g, double cluster) => base + g * 2 + cluster * 2;

    if (widthWith(maxGap, maxClusterGap) <= maxWidth &&
        fullHeight <= maxHeight) {
      return const _ArrowMetrics(
        cap: capLimit,
        wide: wideLimit,
        gap: maxGap,
        clusterGap: maxClusterGap,
      );
    }

    // Prefer squeezing the cluster dead space, then the key gaps, and only
    // then shrink the caps.
    var gap = maxGap;
    var clusterGap = maxClusterGap;
    if (widthWith(gap, clusterGap) > maxWidth) {
      clusterGap = ((maxWidth - base - gap * 2) / 2)
          .clamp(minClusterGap, maxClusterGap)
          .toDouble();
    }
    if (widthWith(gap, clusterGap) > maxWidth) {
      gap = ((maxWidth - base - clusterGap * 2) / 2)
          .clamp(minGap, maxGap)
          .toDouble();
    }
    if (capLimit * 2 + gap > maxHeight) {
      gap = (maxHeight - capLimit * 2).clamp(minGap, gap).toDouble();
    }

    final neededWidth = widthWith(gap, clusterGap);
    final neededHeight = capLimit * 2 + gap;
    var scale = 1.0;
    if (neededWidth > maxWidth) {
      scale = math.min(scale, maxWidth / neededWidth);
    }
    if (neededHeight > maxHeight) {
      scale = math.min(scale, maxHeight / neededHeight);
    }
    scale = scale.clamp(0.0, 1.0).toDouble();
    return _ArrowMetrics(
      cap: capLimit * scale,
      wide: wideLimit * scale,
      gap: gap * scale,
      clusterGap: clusterGap * scale,
    );
  }
}

class _ArrowKeys extends StatelessWidget {
  const _ArrowKeys({required this.controller, required this.trigger});

  final EditorController controller;
  final ValueChanged<PlaybackSoundId> trigger;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final m = _ArrowMetrics.fit(constraints);
        return SizedBox(
          width: m.width,
          height: m.height,
          child: Stack(
            children: [
              Positioned(
                left: m.wide + m.clusterGap + m.cap + m.gap,
                top: 0,
                child: _KeySurface(
                  label: '↑',
                  width: m.cap,
                  height: m.cap,
                  enabled: true,
                  fontSize: 40,
                  onTap: () => trigger(PlaybackSoundId.f1_8),
                ),
              ),
              Positioned(
                left: 0,
                top: m.cap + m.gap,
                child: Row(
                  children: [
                    _KeySurface(
                      label: 'Ctrl',
                      width: m.wide,
                      height: m.cap,
                      enabled: true,
                      fontSize: 20,
                      onTap: () => trigger(PlaybackSoundId.f1Go),
                    ),
                    SizedBox(width: m.clusterGap),
                    _KeySurface(
                      label: '←',
                      width: m.cap,
                      height: m.cap,
                      enabled: true,
                      fontSize: 40,
                      onTap: () => trigger(PlaybackSoundId.f1_4),
                    ),
                    SizedBox(width: m.gap),
                    _KeySurface(
                      label: '↓',
                      width: m.cap,
                      height: m.cap,
                      enabled: true,
                      fontSize: 40,
                      onTap: () => trigger(PlaybackSoundId.f1_2),
                    ),
                    SizedBox(width: m.gap),
                    _KeySurface(
                      label: '→',
                      width: m.cap,
                      height: m.cap,
                      enabled: true,
                      fontSize: 40,
                      onTap: () => trigger(PlaybackSoundId.f1_6),
                    ),
                    SizedBox(width: m.clusterGap),
                    _KeySurface(
                      label: '0',
                      width: m.wide,
                      height: m.cap,
                      enabled: true,
                      fontSize: 36,
                      onTap: () => trigger(PlaybackSoundId.f1Yeah),
                    ),
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _KeySurface extends StatelessWidget {
  const _KeySurface({
    required this.label,
    required this.width,
    required this.height,
    required this.enabled,
    required this.fontSize,
    required this.onTap,
  });

  final String label;
  final double width;
  final double height;
  final bool enabled;
  final double fontSize;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      enabled: enabled,
      label: label,
      child: SizedBox(
        key: ValueKey<String>('screen-keypad-key-$label'),
        width: width,
        height: height,
        child: Listener(
          // Rhythm input must fire on finger-down, not on tap-up, and a
          // Listener also reports every pointer so two thumbs can hit two
          // keys at once. InkWell only provides the pressed highlight.
          onPointerDown: enabled ? (_) => onTap?.call() : null,
          child: Material(
            color: enabled
                ? EditorColors.surfaceRaised
                : EditorColors.surface.withValues(alpha: 0.6),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(10),
              side: BorderSide(
                color: enabled
                    ? EditorColors.border
                    : EditorColors.border.withValues(alpha: 0.45),
              ),
            ),
            clipBehavior: Clip.antiAlias,
            child: InkWell(
              onTap: enabled ? () {} : null,
              child: Center(
                child: Text(
                  label,
                  style: TextStyle(
                    color: enabled
                        ? EditorColors.text
                        : EditorColors.textMuted.withValues(alpha: 0.45),
                    fontSize: fontSize,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
