import 'package:flutter/material.dart';
import 'package:otgy_native/otgy_native.dart';

/// The scrim shared by the screen keypad and the desktop playback layer, so
/// both presentations of a judgement dim the chart to the same degree.
const Color judgementScrimColor = Color(0xD910151A);

const _judgementPerfect = Color(0xFFC08BFF);
const _judgementGreat = Color(0xFF9BE37A);
const _judgementCool = Color(0xFF6FC8FF);
const _judgementBad = Color(0xFFFFA3A3);
const _judgementMiss = Color(0xFFFF5C5C);

/// The single presentation of a playback judgement.
///
/// The controller decides what is displayed and for how long; this widget only
/// maps the grade to its label and colour. The screen keypad and the desktop
/// playback layer both render it, so the two can never drift apart.
class JudgementText extends StatelessWidget {
  const JudgementText({super.key, required this.judgement});

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
