import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:otgy_native/otgy_native.dart';

import '../application/editor_controller.dart';
import 'editor_theme.dart';

const _judgementPerfect = Color(0xFFC08BFF);
const _judgementGreat = Color(0xFF9BE37A);
const _judgementCool = Color(0xFF6FC8FF);
const _judgementBad = Color(0xFFFFA3A3);
const _judgementMiss = Color(0xFFFF5C5C);

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
              color: const Color(0xD910151A),
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
            child: IgnorePointer(child: _JudgementText(judgement: judgement)),
          ),
        Positioned(
          left: 12,
          right: 12,
          top: viewPadding.top + 96,
          bottom: bottomMargin,
          child: Align(
            alignment: Alignment.bottomCenter,
            child: FittedBox(
              fit: BoxFit.scaleDown,
              child: _NumericKeypad(controller: controller, trigger: trigger),
            ),
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
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: RotatedBox(
          quarterTurns: controller.arrowKeypadFlipped ? 3 : 1,
          child: SizedBox(
            width: 720,
            height: 320,
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
                      child: _JudgementText(judgement: judgement),
                    ),
                  ),
                Center(
                  child: _ArrowKeys(controller: controller, trigger: trigger),
                ),
              ],
            ),
          ),
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
            child: const Icon(Icons.flip, size: 22),
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

class _JudgementText extends StatelessWidget {
  const _JudgementText({required this.judgement});

  final PlaybackJudgement judgement;

  @override
  Widget build(BuildContext context) {
    final (label, color) = switch (judgement.grade) {
      PlaybackJudgementGrade.perfect => ('PERFECT', _judgementPerfect),
      PlaybackJudgementGrade.great => ('GREAT', _judgementGreat),
      PlaybackJudgementGrade.cool => ('COOL', _judgementCool),
      PlaybackJudgementGrade.bad => ('BAD', _judgementBad),
      PlaybackJudgementGrade.miss => ('MISS', _judgementMiss),
    };
    return Text(
      label,
      textAlign: TextAlign.center,
      style: TextStyle(
        color: color,
        fontSize: 22,
        fontWeight: FontWeight.w800,
        letterSpacing: 2.4,
        shadows: const [
          Shadow(color: Color(0xDD000000), blurRadius: 8, offset: Offset(0, 1)),
        ],
      ),
    );
  }
}

class _NumericKeypad extends StatelessWidget {
  const _NumericKeypad({required this.controller, required this.trigger});

  /// A standard keyboard key is a 19.05 mm pitch with an 18 mm cap and about
  /// 1 mm between caps. In Flutter's density-independent units that is
  /// roughly 108 dp per cap with a 7 dp gap, so the keypad matches a real
  /// keyboard instead of scaling with the device. `FittedBox.scaleDown` only
  /// shrinks it on screens that are physically too narrow to hold it.
  static const _keyWidth = 108.0;
  static const _keyHeight = 108.0;
  static const _gap = 7.0;

  final EditorController controller;
  final ValueChanged<PlaybackSoundId> trigger;

  bool _enabled(int number) {
    if (controller.playbackDanceMode == PlaybackDanceMode.eightKey) {
      return true;
    }
    return number == 2 || number == 4 || number == 6 || number == 8;
  }

  Widget _key(
    String label,
    PlaybackSoundId sound, {
    int? number,
    bool wide = false,
  }) {
    final enabled = number == null || _enabled(number);
    return _KeySurface(
      label: label,
      width: wide ? _keyWidth * 3 + _gap * 2 : _keyWidth,
      height: _keyHeight,
      enabled: enabled,
      fontSize: 34,
      onTap: enabled ? () => trigger(sound) : null,
    );
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _key('7', PlaybackSoundId.f1_7, number: 7),
            const SizedBox(width: _gap),
            _key('8', PlaybackSoundId.f1_8, number: 8),
            const SizedBox(width: _gap),
            _key('9', PlaybackSoundId.f1_9, number: 9),
          ],
        ),
        const SizedBox(height: _gap),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _key('4', PlaybackSoundId.f1_4, number: 4),
            const SizedBox(width: _gap),
            _key('5', PlaybackSoundId.f1Yeah),
            const SizedBox(width: _gap),
            _key('6', PlaybackSoundId.f1_6, number: 6),
          ],
        ),
        const SizedBox(height: _gap),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _key('1', PlaybackSoundId.f1_1, number: 1),
            const SizedBox(width: _gap),
            _key('2', PlaybackSoundId.f1_2, number: 2),
            const SizedBox(width: _gap),
            _key('3', PlaybackSoundId.f1_3, number: 3),
          ],
        ),
        const SizedBox(height: _gap),
        _key('0', PlaybackSoundId.f1Go, wide: true),
      ],
    );
  }
}

class _ArrowKeys extends StatelessWidget {
  const _ArrowKeys({required this.controller, required this.trigger});

  static const _key = 108.0;
  static const _wideKey = 140.0;
  static const _gap = 7.0;

  final EditorController controller;
  final ValueChanged<PlaybackSoundId> trigger;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 632,
      height: 223,
      child: Stack(
        children: [
          Positioned(
            left: 262,
            top: 0,
            child: _KeySurface(
              label: '↑',
              width: _key,
              height: _key,
              enabled: true,
              fontSize: 40,
              onTap: () => trigger(PlaybackSoundId.f1_8),
            ),
          ),
          Positioned(
            left: 0,
            top: _key + _gap,
            child: Row(
              children: [
                _KeySurface(
                  label: 'Ctrl',
                  width: _wideKey,
                  height: _key,
                  enabled: true,
                  fontSize: 20,
                  onTap: () => trigger(PlaybackSoundId.f1Go),
                ),
                const SizedBox(width: _gap),
                _KeySurface(
                  label: '←',
                  width: _key,
                  height: _key,
                  enabled: true,
                  fontSize: 40,
                  onTap: () => trigger(PlaybackSoundId.f1_4),
                ),
                const SizedBox(width: _gap),
                _KeySurface(
                  label: '↓',
                  width: _key,
                  height: _key,
                  enabled: true,
                  fontSize: 40,
                  onTap: () => trigger(PlaybackSoundId.f1_2),
                ),
                const SizedBox(width: _gap),
                _KeySurface(
                  label: '→',
                  width: _key,
                  height: _key,
                  enabled: true,
                  fontSize: 40,
                  onTap: () => trigger(PlaybackSoundId.f1_6),
                ),
                const SizedBox(width: _gap),
                _KeySurface(
                  label: '0',
                  width: _wideKey,
                  height: _key,
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
