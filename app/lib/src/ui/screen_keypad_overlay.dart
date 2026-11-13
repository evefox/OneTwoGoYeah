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
  const ScreenKeypadOverlay({super.key, required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: controller,
      builder: (context, _) {
        final arrows =
            controller.screenKeypadLayout == ScreenKeypadLayout.arrows &&
            controller.playbackDanceMode == PlaybackDanceMode.fourKey;
        return Material(
          color: Colors.transparent,
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: () {},
            child: ColoredBox(
              color: const Color(0xD910151A),
              child: arrows
                  ? _ArrowLayout(controller: controller)
                  : _NumericLayout(controller: controller),
            ),
          ),
        );
      },
    );
  }
}

class _NumericLayout extends StatelessWidget {
  const _NumericLayout({required this.controller});

  final EditorController controller;

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
          child: Center(child: _PlayButton(controller: controller)),
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
            child: IgnorePointer(
              child: _JudgementText(judgement: judgement),
            ),
          ),
        Positioned(
          left: 0,
          right: 0,
          bottom: bottomMargin,
          child: Center(child: _NumericKeypad(controller: controller)),
        ),
      ],
    );
  }
}

class _ArrowLayout extends StatelessWidget {
  const _ArrowLayout({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    final judgement = controller.judgementEnabled
        ? controller.latestJudgement
        : null;
    return Center(
      child: RotatedBox(
        quarterTurns: 1,
        child: SizedBox(
          width: 720,
          height: 320,
          child: Stack(
            children: [
              Positioned(
                top: 12,
                left: 14,
                child: _PlayButton(controller: controller),
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
              Center(child: _ArrowKeys(controller: controller)),
            ],
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
  const _NumericKeypad({required this.controller});

  static const _keyWidth = 66.0;
  static const _keyHeight = 58.0;
  static const _gap = 10.0;

  final EditorController controller;

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
      fontSize: 22,
      onTap: enabled
          ? () => controller.pushSimulationTrigger(sound)
          : null,
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
  const _ArrowKeys({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 462,
      height: 138,
      child: Stack(
        children: [
          Positioned(
            left: 198,
            top: 0,
            child: _KeySurface(
              label: '↑',
              width: 66,
              height: 64,
              enabled: true,
              fontSize: 26,
              onTap: () => controller.pushSimulationTrigger(
                PlaybackSoundId.f1_8,
              ),
            ),
          ),
          Positioned(
            left: 0,
            top: 74,
            child: Row(
              children: [
                _KeySurface(
                  label: 'Ctrl',
                  width: 112,
                  height: 64,
                  enabled: true,
                  fontSize: 15,
                  onTap: () => controller.pushSimulationTrigger(
                    PlaybackSoundId.f1Go,
                  ),
                ),
                const SizedBox(width: 10),
                _KeySurface(
                  label: '←',
                  width: 66,
                  height: 64,
                  enabled: true,
                  fontSize: 26,
                  onTap: () => controller.pushSimulationTrigger(
                    PlaybackSoundId.f1_4,
                  ),
                ),
                const SizedBox(width: 10),
                _KeySurface(
                  label: '↓',
                  width: 66,
                  height: 64,
                  enabled: true,
                  fontSize: 26,
                  onTap: () => controller.pushSimulationTrigger(
                    PlaybackSoundId.f1_2,
                  ),
                ),
                const SizedBox(width: 10),
                _KeySurface(
                  label: '→',
                  width: 66,
                  height: 64,
                  enabled: true,
                  fontSize: 26,
                  onTap: () => controller.pushSimulationTrigger(
                    PlaybackSoundId.f1_6,
                  ),
                ),
                const SizedBox(width: 10),
                _KeySurface(
                  label: '0',
                  width: 112,
                  height: 64,
                  enabled: true,
                  fontSize: 24,
                  onTap: () => controller.pushSimulationTrigger(
                    PlaybackSoundId.f1Yeah,
                  ),
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
        width: width,
        height: height,
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
            onTap: onTap,
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
    );
  }
}
