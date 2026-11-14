// SPDX-License-Identifier: GPL-3.0-or-later

import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:otgy_native/otgy_native.dart';

import '../application/editor_controller.dart';

const _gradePerfect = Color(0xFFC08BFF);
const _gradeGreat = Color(0xFF9BE37A);
const _gradeCool = Color(0xFF6FC8FF);
const _gradeBad = Color(0xFFFFA3A3);
const _gradeMiss = Color(0xFFFF5C5C);

/// Opens the one-run judgement analysis. The analysis is computed once here
/// and never persisted; the dialog only mutates the session tuning offset.
Future<void> showJudgementAnalysisDialog(
  BuildContext context, {
  required EditorController controller,
}) async {
  final analysis = controller.computeJudgementAnalysis();
  if (analysis == null) {
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('本次模拟播放没有可分析的按键')),
    );
    return;
  }
  await showDialog<void>(
    context: context,
    builder: (dialogContext) => _JudgementAnalysisDialog(
      controller: controller,
      analysis: analysis,
      windows: _windowsForRun(controller),
    ),
  );
}

PlaybackJudgementWindows? _windowsForRun(EditorController controller) {
  final config = controller.judgementRunConfig;
  try {
    return NativePlaybackEngine.windowsFor(
      bpm: controller.judgementRunBpm,
      basis: config.basis,
      perfectFraction: config.perfectFraction,
      greatFraction: config.greatFraction,
      coolFraction: config.coolFraction,
      badFraction: config.badFraction,
    );
  } on Object {
    return null;
  }
}

class _JudgementAnalysisDialog extends StatelessWidget {
  const _JudgementAnalysisDialog({
    required this.controller,
    required this.analysis,
    required this.windows,
  });

  final EditorController controller;
  final PlaybackJudgementAnalysis analysis;
  final PlaybackJudgementWindows? windows;

  static String _signed(double value, {int digits = 1}) =>
      '${value >= 0 ? '+' : ''}${value.toStringAsFixed(digits)}';

  @override
  Widget build(BuildContext context) {
    final config = controller.judgementRunConfig;
    final bpm = controller.judgementRunBpm;
    final best = analysis.bestPerfectOffsetMs.round();
    final low = analysis.bestPerfectLowMs.round();
    final high = analysis.bestPerfectHighMs.round();
    final enoughSamples = analysis.correctKeyCount >= 30;
    final windowsText = windows == null
        ? '判定窗口无效'
        : '窗口 P ${windows!.perfectMs.toStringAsFixed(1)} · '
              'G ${windows!.greatMs.toStringAsFixed(1)} · '
              'C ${windows!.coolMs.toStringAsFixed(1)} · '
              'B ${windows!.badMs.toStringAsFixed(1)} ms';
    return AlertDialog(
      key: const ValueKey('judgement-analysis-dialog'),
      title: const Text('判定分析'),
      content: SizedBox(
        width: 380,
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                '${bpm.toStringAsFixed(1)} BPM · 当前 Offset '
                '${config.offsetMs.round()} ms',
                style: Theme.of(context).textTheme.bodySmall,
              ),
              Text(windowsText, style: Theme.of(context).textTheme.bodySmall),
              const SizedBox(height: 12),
              Text('本次按键', style: Theme.of(context).textTheme.labelMedium),
              Text(
                '有效 ${analysis.sampleCount}（按对 ${analysis.correctKeyCount}'
                ' / 错键 ${analysis.wrongKeyCount}）· '
                '漏按 ${analysis.autoMissCount}',
              ),
              const SizedBox(height: 4),
              Wrap(
                spacing: 10,
                runSpacing: 2,
                children: [
                  _count(context, 'Perfect', analysis.perfectCount,
                      _gradePerfect),
                  _count(context, 'Great', analysis.greatCount, _gradeGreat),
                  _count(context, 'Cool', analysis.coolCount, _gradeCool),
                  _count(context, 'Bad', analysis.badCount, _gradeBad),
                  _count(context, 'Miss', analysis.missCount, _gradeMiss),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                '时间差（按下 − 音符）',
                style: Theme.of(context).textTheme.labelMedium,
              ),
              Text(
                '均值 ${_signed(analysis.meanMs)} ms · '
                '中位数 ${_signed(analysis.medianMs)} ms · '
                '标准差 ${analysis.stddevMs.toStringAsFixed(1)} ms',
                style: Theme.of(context).textTheme.bodySmall,
              ),
              const SizedBox(height: 6),
              SizedBox(
                height: 120,
                child: CustomPaint(
                  painter: _JudgementHistogramPainter(analysis),
                ),
              ),
              const SizedBox(height: 12),
              Text('推荐', style: Theme.of(context).textTheme.labelMedium),
              Text(
                'P 最多：${_signed(best.toDouble(), digits: 0)} ms'
                '（预测 P ${analysis.predictedPerfectCount} · '
                'G ${analysis.predictedGreatCount} · '
                'C ${analysis.predictedCoolCount} · '
                'B ${analysis.predictedBadCount} · '
                'M ${analysis.predictedMissCount}）',
              ),
              Text(
                'P 最多区间：${_signed(low.toDouble(), digits: 0)} ~ '
                '${_signed(high.toDouble(), digits: 0)} ms',
                style: Theme.of(context).textTheme.bodySmall,
              ),
              if (!enoughSamples)
                Padding(
                  padding: const EdgeInsets.only(top: 4),
                  child: Text(
                    '按对样本只有 ${analysis.correctKeyCount} 个，'
                    '结果仅供参考。',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                ),
              const SizedBox(height: 6),
              Text(
                '只分析本次播放；填入的 Offset 只属于当前会话，'
                '不写入谱面、草稿、SLK 或导出。',
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          key: const ValueKey('judgement-analysis-apply'),
          onPressed: controller.generalActionsEnabled
              ? () {
                  controller.setJudgementOffsetMs(
                    analysis.bestPerfectOffsetMs,
                  );
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(
                        '已填入 Offset ${_signed(best.toDouble(), digits: 0)} ms',
                      ),
                    ),
                  );
                }
              : null,
          child: const Text('填入 Offset'),
        ),
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('关闭'),
        ),
      ],
    );
  }

  Widget _count(
    BuildContext context,
    String label,
    int value,
    Color color,
  ) {
    return Text(
      '$label $value',
      style: Theme.of(
        context,
      ).textTheme.bodySmall?.copyWith(color: color),
    );
  }
}

class _JudgementHistogramPainter extends CustomPainter {
  _JudgementHistogramPainter(this.analysis);

  final PlaybackJudgementAnalysis analysis;

  @override
  void paint(Canvas canvas, Size size) {
    final histogram = analysis.histogram;
    if (histogram.isEmpty || size.width <= 0 || size.height <= 0) {
      return;
    }
    final histogramMin = analysis.histogramMinMs;
    final step = analysis.histogramStepMs;
    final histogramMax = histogramMin + step * histogram.length;
    final halfSpan = math.max(analysis.badMs, analysis.perfectMs) + 20.0;
    final displayMin = math.max(
      histogramMin,
      analysis.currentOffsetMs - halfSpan,
    );
    final displayMax = math.min(
      histogramMax,
      analysis.currentOffsetMs + halfSpan,
    );
    final minMs = math.min(displayMin, displayMax - 1.0);
    final maxMs = math.max(displayMax, minMs + 1.0);

    final background = Paint()..color = const Color(0xFF1B232B);
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Offset.zero & size,
        const Radius.circular(6),
      ),
      background,
    );

    var maxCount = 0;
    for (final count in histogram) {
      maxCount = math.max(maxCount, count);
    }
    if (maxCount > 0) {
      final barPaint = Paint()..color = const Color(0xFF8FA3B8);
      final columns = math.max(1, size.width.floor());
      final span = maxMs - minMs;
      for (var column = 0; column < columns; column++) {
        final startMs = minMs + span * column / columns;
        final endMs = minMs + span * (column + 1) / columns;
        final startIndex = ((startMs - histogramMin) / step).floor();
        final endIndex = ((endMs - histogramMin) / step).ceil();
        final start = math.max(0, math.min(startIndex, histogram.length - 1));
        final end = math.max(start + 1, math.min(endIndex, histogram.length));
        var count = 0;
        for (var index = start; index < end; index++) {
          count += histogram[index];
        }
        if (count == 0) {
          continue;
        }
        final height = size.height * count / maxCount;
        final left = size.width * column / columns;
        final width = size.width / columns;
        canvas.drawRect(
          Rect.fromLTWH(
            left,
            size.height - height,
            math.max(1.0, width - 0.5),
            height,
          ),
          barPaint,
        );
      }
    }

    void line(double valueMs, Color color, double width) {
      final fraction = (valueMs - minMs) / (maxMs - minMs);
      if (fraction < 0 || fraction > 1) {
        return;
      }
      final x = size.width * fraction;
      canvas.drawLine(
        Offset(x, 0),
        Offset(x, size.height),
        Paint()
          ..color = color
          ..strokeWidth = width,
      );
    }

    final offset = analysis.currentOffsetMs;
    line(offset - analysis.perfectMs, _gradePerfect, 1.0);
    line(offset + analysis.perfectMs, _gradePerfect, 1.0);
    line(offset - analysis.badMs, _gradeMiss.withValues(alpha: 0.6), 1.0);
    line(offset + analysis.badMs, _gradeMiss.withValues(alpha: 0.6), 1.0);
    line(analysis.bestPerfectOffsetMs, const Color(0xFFE6B75A), 1.5);

    _label(
      canvas,
      '${minMs.round()} ms',
      const Offset(4, 2),
      size,
    );
    _label(
      canvas,
      '${maxMs.round()} ms',
      Offset(size.width - 52, 2),
      size,
    );
  }

  void _label(Canvas canvas, String text, Offset offset, Size size) {
    final painter = TextPainter(
      text: TextSpan(
        text: text,
        style: const TextStyle(color: Colors.white54, fontSize: 9),
      ),
      textDirection: TextDirection.ltr,
    )..layout(maxWidth: size.width);
    painter.paint(canvas, offset);
  }

  @override
  bool shouldRepaint(covariant _JudgementHistogramPainter oldDelegate) =>
      oldDelegate.analysis != analysis;
}
