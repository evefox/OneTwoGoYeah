import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:otgy_native/otgy_native.dart';

import '../application/editor_controller.dart';
import 'editor_theme.dart';

const double editorCursorViewportFraction = 0.25;
const double mobileEditorContentRightInset = 24;
const Duration playbackVisualPredictionLimit = Duration(milliseconds: 50);

final BoxDecoration _canvasControlSurfaceDecoration = BoxDecoration(
  color: EditorColors.surface.withValues(alpha: 0.92),
  borderRadius: BorderRadius.circular(6),
);

double editorCursorY(Size size) => size.height * editorCursorViewportFraction;

int projectEditorRow({
  required double y,
  required double cursorY,
  required double currentRow,
  required double rowSpacing,
}) {
  final raw = currentRow + (y - cursorY) / rowSpacing;
  return math.max(0, (raw + 0.5).floor());
}

double editorMarkerSize({
  required double rowSpacing,
  required bool mobile,
  required bool diamond,
}) {
  if (diamond) {
    return math.min(mobile ? 32 : 26, rowSpacing * 0.80).toDouble();
  }
  return math.min(mobile ? 28 : 22, rowSpacing * 0.70).toDouble();
}

int projectEditorColumn({
  required double x,
  required Rect chart,
  required List<WaveformBlock> waveformBlocks,
  double rightInset = 0,
}) => _CanvasColumnLayout.from(
  chart,
  waveformBlocks,
  rightInset: rightInset,
).columnAt(x);

const _visualChartColumns = <int>[
  OTGY_CHART_COLUMN_NPC,
  OTGY_CHART_COLUMN_SHOWTIME,
  OTGY_CHART_COLUMN_PLAYER,
];
const _visualChartColumnLabels = <String>['NPC', 'Showtime', '玩家'];

final class _CanvasColumnLayout {
  const _CanvasColumnLayout._({
    required this.chart,
    required this.centers,
    required this.boundaries,
    required this.waveformFit,
    required this.leftWaveformOrigin,
    required this.rightWaveformOrigin,
  });

  factory _CanvasColumnLayout.from(
    Rect chart,
    List<WaveformBlock> waveformBlocks, {
    double rightInset = 0,
  }) {
    final projection = Rect.fromLTRB(
      chart.left,
      chart.top,
      (chart.right - rightInset).clamp(chart.left, chart.right).toDouble(),
      chart.bottom,
    );
    var waveformFit = 1.0;
    var leftWaveformOrigin = projection.left;
    var rightWaveformOrigin = projection.right;
    var npcCenter = projection.left + projection.width / 6;
    var showtimeCenter = projection.center.dx;
    var playerCenter = projection.left + projection.width * 5 / 6;

    if (waveformBlocks.isNotEmpty) {
      final first = waveformBlocks.first;
      final nativeTotal = first.channelWidth * 2 + first.channelSpacing;
      waveformFit = math.min(1.0, projection.width / math.max(1, nativeTotal));
      leftWaveformOrigin = projection.center.dx - nativeTotal * waveformFit / 2;
      rightWaveformOrigin =
          leftWaveformOrigin +
          (first.channelWidth + first.channelSpacing) * waveformFit;
      npcCenter = leftWaveformOrigin + first.channelWidth * waveformFit / 2;
      playerCenter = rightWaveformOrigin + first.channelWidth * waveformFit / 2;
    }

    final centers = <double>[npcCenter, showtimeCenter, playerCenter];
    return _CanvasColumnLayout._(
      chart: chart,
      centers: centers,
      boundaries: <double>[
        chart.left,
        (npcCenter + showtimeCenter) / 2,
        (showtimeCenter + playerCenter) / 2,
        chart.right,
      ],
      waveformFit: waveformFit,
      leftWaveformOrigin: leftWaveformOrigin,
      rightWaveformOrigin: rightWaveformOrigin,
    );
  }

  final Rect chart;
  final List<double> centers;
  final List<double> boundaries;
  final double waveformFit;
  final double leftWaveformOrigin;
  final double rightWaveformOrigin;

  int columnAt(double x) {
    final visualIndex = x < boundaries[1]
        ? 0
        : x < boundaries[2]
        ? 1
        : 2;
    return _visualChartColumns[visualIndex];
  }

  int visualIndexForColumn(int column) => switch (column) {
    OTGY_CHART_COLUMN_NPC => 0,
    OTGY_CHART_COLUMN_SHOWTIME => 1,
    OTGY_CHART_COLUMN_PLAYER => 2,
    _ => throw ArgumentError.value(column, 'column'),
  };

  double centerForColumn(int column) => centers[visualIndexForColumn(column)];

  Rect laneAtVisualIndex(int visualIndex) => Rect.fromLTRB(
    boundaries[visualIndex],
    chart.top,
    boundaries[visualIndex + 1],
    chart.bottom,
  );

  Rect laneForColumn(int column) =>
      laneAtVisualIndex(visualIndexForColumn(column));
}

class EditorCanvas extends StatefulWidget {
  const EditorCanvas({super.key, required this.controller});

  final EditorController controller;

  @override
  State<EditorCanvas> createState() => _EditorCanvasState();
}

enum _CanvasGestureMode { undecided, verticalPan, horizontalPan, zoom }

class _EditorCanvasState extends State<EditorCanvas>
    with SingleTickerProviderStateMixin {
  final _focusNode = FocusNode(debugLabel: '谱面编辑画布');
  late final ValueNotifier<double> _paintCurrentRow;
  late final Ticker _playbackVisualTicker;
  double _gestureZoom = 10;
  double _gestureTargetZoom = 10;
  double _gesturePreviewScale = 1;
  double _confirmedPlaybackRow = 0;
  double _visualAnchorRow = 0;
  double _playbackRowsPerSecond = 0;
  double _playbackEndRow = 0;
  Duration _tickerElapsed = Duration.zero;
  Duration _confirmedAt = Duration.zero;
  Duration _visualAnchorAt = Duration.zero;
  PointerDeviceKind? _gestureKind;
  _CanvasGestureMode _gestureMode = _CanvasGestureMode.undecided;
  bool _gestureEnabled = false;

  bool get _mobile =>
      defaultTargetPlatform == TargetPlatform.iOS ||
      defaultTargetPlatform == TargetPlatform.android;

  bool get _macOS => defaultTargetPlatform == TargetPlatform.macOS;

  @override
  void initState() {
    super.initState();
    _paintCurrentRow = ValueNotifier(widget.controller.currentRow);
    _playbackVisualTicker = createTicker(_handlePlaybackVisualFrame);
    widget.controller.addListener(_synchronizePlaybackVisual);
    _synchronizePlaybackVisual();
  }

  @override
  void didUpdateWidget(covariant EditorCanvas oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller == widget.controller) {
      return;
    }
    oldWidget.controller.removeListener(_synchronizePlaybackVisual);
    widget.controller.addListener(_synchronizePlaybackVisual);
    _stopPlaybackVisual();
    _synchronizePlaybackVisual();
  }

  @override
  void dispose() {
    widget.controller.removeListener(_synchronizePlaybackVisual);
    _playbackVisualTicker.dispose();
    _paintCurrentRow.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  void _synchronizePlaybackVisual() {
    final controller = widget.controller;
    if (!controller.isPlaying) {
      _stopPlaybackVisual();
      return;
    }
    if (!_playbackVisualTicker.isActive) {
      _startPlaybackVisual();
      return;
    }

    final confirmedRow = controller.currentRow;
    if (confirmedRow == _confirmedPlaybackRow) {
      return;
    }
    if (confirmedRow < _confirmedPlaybackRow) {
      _confirmedPlaybackRow = confirmedRow;
      _confirmedAt = _tickerElapsed;
      _visualAnchorRow = confirmedRow;
      _visualAnchorAt = _tickerElapsed;
      _setPaintCurrentRow(confirmedRow);
      return;
    }

    final projected = _projectPlaybackVisualRow(_tickerElapsed);
    _confirmedPlaybackRow = confirmedRow;
    _confirmedAt = _tickerElapsed;
    _visualAnchorRow = math.max(projected, confirmedRow);
    _visualAnchorAt = _tickerElapsed;
    _setPaintCurrentRow(_visualAnchorRow);
  }

  void _startPlaybackVisual() {
    final controller = widget.controller;
    _tickerElapsed = Duration.zero;
    _confirmedAt = Duration.zero;
    _visualAnchorAt = Duration.zero;
    _confirmedPlaybackRow = controller.currentRow;
    _visualAnchorRow = controller.currentRow;
    _playbackRowsPerSecond =
        controller.playbackVisualPixelsPerSecond /
        math.max(0.001, controller.playbackRowSpacing);
    final audioEndSeconds = controller.audioDurationSeconds;
    _playbackEndRow = audioEndSeconds == null
        ? controller.currentRow
        : OtgyNative.secondsToSixteenthRow(
            bpm: controller.projectedBpm,
            offset: controller.offset,
            seconds: audioEndSeconds,
          ).clamp(0, controller.timelineRange.endRow).toDouble();
    _setPaintCurrentRow(controller.currentRow);
    _playbackVisualTicker.start();
  }

  void _stopPlaybackVisual() {
    if (_playbackVisualTicker.isActive) {
      _playbackVisualTicker.stop();
    }
    _tickerElapsed = Duration.zero;
    _confirmedAt = Duration.zero;
    _visualAnchorAt = Duration.zero;
    _confirmedPlaybackRow = widget.controller.currentRow;
    _visualAnchorRow = widget.controller.currentRow;
    _setPaintCurrentRow(widget.controller.currentRow);
  }

  void _handlePlaybackVisualFrame(Duration elapsed) {
    _tickerElapsed = elapsed;
    if (!widget.controller.isPlaying) {
      _stopPlaybackVisual();
      return;
    }
    _setPaintCurrentRow(_projectPlaybackVisualRow(elapsed));
  }

  double _projectPlaybackVisualRow(Duration elapsed) {
    final anchorMicros = math.max(
      0,
      (elapsed - _visualAnchorAt).inMicroseconds,
    );
    final confirmedMicros = math.min(
      playbackVisualPredictionLimit.inMicroseconds,
      math.max(0, (elapsed - _confirmedAt).inMicroseconds),
    );
    final candidate = _visualAnchorRow + _rowsForMicros(anchorMicros);
    final confirmedCeiling =
        _confirmedPlaybackRow + _rowsForMicros(confirmedMicros);
    return math
        .max(_paintCurrentRow.value, math.min(candidate, confirmedCeiling))
        .clamp(0, _playbackEndRow)
        .toDouble();
  }

  double _rowsForMicros(int microseconds) =>
      microseconds / Duration.microsecondsPerSecond * _playbackRowsPerSecond;

  void _setPaintCurrentRow(double value) {
    if (_paintCurrentRow.value != value) {
      _paintCurrentRow.value = value;
    }
  }

  Rect _chartRect(Size size) => Rect.fromLTRB(0, 28, size.width, size.height);

  double _cursorY(Size size) => editorCursorY(size);

  int? _columnAt(Offset point, Size size) {
    final rect = _chartRect(size);
    if (!rect.contains(point)) {
      return null;
    }
    return projectEditorColumn(
      x: point.dx,
      chart: rect,
      waveformBlocks: widget.controller.canvasWaveformBlocks,
      rightInset: _mobile ? mobileEditorContentRightInset : 0,
    );
  }

  int _rowAt(double y, Size size) {
    return projectEditorRow(
      y: y,
      cursorY: _cursorY(size),
      currentRow: widget.controller.currentRow,
      rowSpacing: widget.controller.rowSpacing,
    );
  }

  void _handleTap(TapUpDetails details, Size size) {
    _focusNode.requestFocus();
    final column = _columnAt(details.localPosition, size);
    if (column == null) {
      return;
    }
    widget.controller.clickCell(
      column,
      _rowAt(details.localPosition.dy, size),
      mobile: _mobile,
    );
  }

  void _handleScroll(PointerScrollEvent event) {
    if ((_macOS && event.scrollDelta.dx.abs() >= event.scrollDelta.dy.abs()) ||
        event.scrollDelta.dy == 0) {
      return;
    }
    final keyboard = HardwareKeyboard.instance;
    final control = keyboard.isControlPressed || keyboard.isMetaPressed;
    if (control && keyboard.isAltPressed && !_mobile) {
      unawaited(
        widget.controller.setScale(
          widget.controller.scale + (event.scrollDelta.dy > 0 ? -0.25 : 0.25),
        ),
      );
    } else if (control) {
      unawaited(
        widget.controller.setZoom(
          widget.controller.zoom + (event.scrollDelta.dy > 0 ? -0.25 : 0.25),
        ),
      );
    } else {
      widget.controller.moveByPixels(event.scrollDelta.dy);
    }
  }

  void _handleScaleStart(ScaleStartDetails details) {
    _gestureZoom = widget.controller.zoom;
    _gestureTargetZoom = _gestureZoom;
    _gestureKind = details.kind;
    _gestureMode = _CanvasGestureMode.undecided;
    _gestureEnabled =
        widget.controller.generalActionsEnabled && !widget.controller.isBusy;
    if (_gesturePreviewScale != 1) {
      setState(() => _gesturePreviewScale = 1);
    }
  }

  void _handleScaleUpdate(ScaleUpdateDetails details) {
    if (!_gestureEnabled) {
      return;
    }
    if (_mobile) {
      if (details.pointerCount >= 2) {
        unawaited(
          widget.controller.setZoom(
            _zoomForGestureScale(_gestureZoom, details.scale),
          ),
        );
      } else if (details.pointerCount == 1) {
        widget.controller.moveByPixels(-details.focalPointDelta.dy);
      }
      return;
    }
    if (!_macOS || _gestureKind != PointerDeviceKind.trackpad) {
      return;
    }

    if (_gestureMode == _CanvasGestureMode.undecided) {
      if ((details.scale - 1).abs() > 0.001) {
        _gestureMode = _CanvasGestureMode.zoom;
      } else if (details.focalPointDelta.distanceSquared > 0) {
        _gestureMode =
            details.focalPointDelta.dy.abs() > details.focalPointDelta.dx.abs()
            ? _CanvasGestureMode.verticalPan
            : _CanvasGestureMode.horizontalPan;
      } else {
        return;
      }
    }

    if (_gestureMode == _CanvasGestureMode.verticalPan) {
      widget.controller.moveByPixels(-details.focalPointDelta.dy);
      return;
    }
    if (_gestureMode == _CanvasGestureMode.horizontalPan) {
      return;
    }

    _gestureTargetZoom = _zoomForGestureScale(_gestureZoom, details.scale);
    final previewScale = math
        .pow(2, (_gestureTargetZoom - _gestureZoom) / 2)
        .toDouble();
    if (previewScale != _gesturePreviewScale) {
      setState(() => _gesturePreviewScale = previewScale);
    }
  }

  void _handleScaleEnd(ScaleEndDetails details) {
    final commitTrackpadZoom =
        _gestureEnabled &&
        _macOS &&
        _gestureKind == PointerDeviceKind.trackpad &&
        _gestureMode == _CanvasGestureMode.zoom;
    final targetZoom = _gestureTargetZoom;
    _gestureEnabled = false;
    _gestureKind = null;
    _gestureMode = _CanvasGestureMode.undecided;
    if (_gesturePreviewScale != 1) {
      setState(() => _gesturePreviewScale = 1);
    }
    if (commitTrackpadZoom) {
      unawaited(widget.controller.setZoom(targetZoom));
    }
  }

  double _zoomForGestureScale(double initialZoom, double gestureScale) {
    if (!gestureScale.isFinite || gestureScale <= 0) {
      return initialZoom;
    }
    return (initialZoom + math.log(gestureScale) / math.ln2 * 2)
        .clamp(1, 19)
        .toDouble();
  }

  void _zoomBy(double delta) {
    unawaited(widget.controller.setZoom(widget.controller.zoom + delta));
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = Size(constraints.maxWidth, constraints.maxHeight);
        final baseSpacing = widget.controller.isPlaying
            ? widget.controller.playbackRowSpacing
            : widget.controller.rowSpacing;
        final spacing = math.max(
          0.001,
          baseSpacing *
              (widget.controller.isPlaying ? 1 : _gesturePreviewScale),
        );
        final visibleBeats = math
            .max(1, constraints.maxHeight / spacing / 4)
            .toDouble();
        final navigation = widget.controller.navigation(visibleBeats);
        final markerSemantics = <Widget>[];
        final rect = _chartRect(size);
        final columnLayout = _CanvasColumnLayout.from(
          rect,
          widget.controller.canvasWaveformBlocks,
          rightInset: _mobile ? mobileEditorContentRightInset : 0,
        );
        for (final marker in widget.controller.markers) {
          final y =
              _cursorY(size) +
              (marker.row - widget.controller.currentRow) * spacing;
          if (y < rect.top - spacing || y > rect.bottom + spacing) {
            continue;
          }
          final lane = columnLayout.laneForColumn(marker.column);
          markerSemantics.add(
            Positioned(
              left: lane.left,
              top: y - spacing / 2,
              width: lane.width,
              height: spacing,
              child: Semantics(
                button: true,
                label: _markerSemanticLabel(marker),
                onTap: () => widget.controller.clickCell(
                  marker.column,
                  marker.row,
                  mobile: _mobile,
                ),
                child: const SizedBox.expand(),
              ),
            ),
          );
        }

        final shortcutBindings = <ShortcutActivator, VoidCallback>{
          const SingleActivator(LogicalKeyboardKey.keyZ, control: true):
              widget.controller.undo,
          const SingleActivator(
            LogicalKeyboardKey.keyZ,
            control: true,
            shift: true,
          ): widget.controller.redo,
          const SingleActivator(LogicalKeyboardKey.keyZ, meta: true):
              widget.controller.undo,
          const SingleActivator(
            LogicalKeyboardKey.keyZ,
            meta: true,
            shift: true,
          ): widget.controller.redo,
          const SingleActivator(LogicalKeyboardKey.space):
              widget.controller.togglePlayback,
        };
        if (_macOS) {
          shortcutBindings.addAll({
            const SingleActivator(LogicalKeyboardKey.equal, meta: true): () =>
                _zoomBy(0.25),
            const SingleActivator(
              LogicalKeyboardKey.equal,
              meta: true,
              shift: true,
            ): () =>
                _zoomBy(0.25),
            const SingleActivator(LogicalKeyboardKey.add, meta: true): () =>
                _zoomBy(0.25),
            const SingleActivator(LogicalKeyboardKey.minus, meta: true): () =>
                _zoomBy(-0.25),
            const SingleActivator(LogicalKeyboardKey.digit0, meta: true): () =>
                unawaited(widget.controller.setZoom(10)),
          });
        }

        return CallbackShortcuts(
          bindings: shortcutBindings,
          child: Focus(
            focusNode: _focusNode,
            autofocus: true,
            child: Stack(
              fit: StackFit.expand,
              children: [
                Listener(
                  onPointerSignal: (event) {
                    if (event is PointerScrollEvent) {
                      _handleScroll(event);
                    }
                  },
                  onPointerDown: (event) {
                    if (event.buttons == kMiddleMouseButton) {
                      widget.controller.moveToRow(
                        _rowAt(event.localPosition.dy, size).toDouble(),
                      );
                    }
                  },
                  child: GestureDetector(
                    key: const ValueKey('editor-canvas'),
                    behavior: HitTestBehavior.opaque,
                    onTapUp: (details) => _handleTap(details, size),
                    onScaleStart: _handleScaleStart,
                    onScaleUpdate: _handleScaleUpdate,
                    onScaleEnd: _handleScaleEnd,
                    child: Semantics(
                      label: '谱面编辑画布，NPC、Showtime、玩家三列',
                      child: Stack(
                        fit: StackFit.expand,
                        children: [
                          RepaintBoundary(
                            key: const ValueKey('editor-canvas-repaint'),
                            child: CustomPaint(
                              key: const ValueKey('editor-canvas-paint'),
                              painter: _EditorCanvasPainter(
                                controller: widget.controller,
                                mobile: _mobile,
                                rowSpacing: spacing,
                                currentRow: _paintCurrentRow,
                              ),
                            ),
                          ),
                          ...markerSemantics,
                          for (
                            var visualIndex = 0;
                            visualIndex < 3;
                            visualIndex++
                          )
                            Positioned(
                              key: ValueKey(
                                'column-header-${_visualChartColumnLabels[visualIndex]}',
                              ),
                              left: (columnLayout.centers[visualIndex] - 44)
                                  .clamp(rect.left, rect.right - 88)
                                  .toDouble(),
                              top: 0,
                              width: 88,
                              height: rect.top,
                              child: Semantics(
                                header: true,
                                label: _visualChartColumnLabels[visualIndex],
                                child: const SizedBox.expand(),
                              ),
                            ),
                        ],
                      ),
                    ),
                  ),
                ),
                Positioned(
                  right: 8,
                  top: 40,
                  child: _NavigationRail(
                    controller: widget.controller,
                    navigation: navigation,
                  ),
                ),
                Positioned(
                  left: 8,
                  bottom: 8,
                  child: _HistoryControls(controller: widget.controller),
                ),
                if (widget.controller.isBusy)
                  const Positioned.fill(
                    child: ColoredBox(
                      color: Color(0x6610151A),
                      child: Center(child: CircularProgressIndicator()),
                    ),
                  ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _NavigationRail extends StatefulWidget {
  const _NavigationRail({required this.controller, required this.navigation});

  final EditorController controller;
  final TimelineNavigation navigation;

  @override
  State<_NavigationRail> createState() => _NavigationRailState();
}

class _NavigationRailState extends State<_NavigationRail> {
  final _soundMenu = MenuController();

  @override
  void initState() {
    super.initState();
    widget.controller.addListener(_handleControllerChanged);
  }

  @override
  void didUpdateWidget(covariant _NavigationRail oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller != widget.controller) {
      oldWidget.controller.removeListener(_handleControllerChanged);
      widget.controller.addListener(_handleControllerChanged);
    }
  }

  @override
  void dispose() {
    widget.controller.removeListener(_handleControllerChanged);
    super.dispose();
  }

  void _handleControllerChanged() {
    if (widget.controller.isPlaying && _soundMenu.isOpen) {
      _soundMenu.close();
    }
  }

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final navigation = widget.navigation;
    return DecoratedBox(
      key: const ValueKey('navigation-control-surface'),
      decoration: _canvasControlSurfaceDecoration,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 2),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            IconButton(
              key: const ValueKey('waveform-toggle-button'),
              onPressed: controller.toggleWaveform,
              tooltip: controller.waveformVisible ? '隐藏波形' : '显示波形',
              icon: Icon(
                controller.waveformVisible
                    ? Icons.graphic_eq
                    : Icons.hide_source_outlined,
                size: 20,
                color: controller.waveformVisible ? EditorColors.accent : null,
              ),
            ),
            MenuAnchor(
              controller: _soundMenu,
              alignmentOffset: const Offset(-226, -36),
              menuChildren: [
                SizedBox(
                  key: const ValueKey('playback-sound-menu'),
                  width: 210,
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(14, 10, 14, 12),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Text(
                          '语言',
                          style: Theme.of(context).textTheme.labelMedium,
                        ),
                        const SizedBox(height: 6),
                        SegmentedButton<PlaybackSoundLanguage>(
                          key: const ValueKey('playback-sound-language'),
                          segments: const [
                            ButtonSegment(
                              value: PlaybackSoundLanguage.chinese,
                              label: Text('中文'),
                            ),
                            ButtonSegment(
                              value: PlaybackSoundLanguage.english,
                              label: Text('英文'),
                            ),
                          ],
                          selected: {controller.playbackSoundLanguage},
                          showSelectedIcon: false,
                          onSelectionChanged: (value) =>
                              controller.setPlaybackSoundLanguage(value.single),
                        ),
                        const SizedBox(height: 12),
                        Text(
                          '舞步模式',
                          style: Theme.of(context).textTheme.labelMedium,
                        ),
                        const SizedBox(height: 6),
                        SegmentedButton<PlaybackDanceMode>(
                          key: const ValueKey('playback-dance-mode'),
                          segments: const [
                            ButtonSegment(
                              value: PlaybackDanceMode.fourKey,
                              label: Text('4K'),
                            ),
                            ButtonSegment(
                              value: PlaybackDanceMode.eightKey,
                              label: Text('8K'),
                            ),
                          ],
                          selected: {controller.playbackDanceMode},
                          showSelectedIcon: false,
                          onSelectionChanged: (value) =>
                              controller.setPlaybackDanceMode(value.single),
                        ),
                        const SizedBox(height: 8),
                        SwitchListTile.adaptive(
                          key: const ValueKey('transition-sounds-switch'),
                          contentPadding: EdgeInsets.zero,
                          dense: true,
                          title: const Text('切换音效'),
                          value: controller.transitionSoundsEnabled,
                          onChanged: controller.setTransitionSoundsEnabled,
                        ),
                      ],
                    ),
                  ),
                ),
              ],
              builder: (context, menu, child) => IconButton(
                key: const ValueKey('playback-sound-button'),
                onPressed: controller.isPlaying
                    ? null
                    : () => menu.isOpen ? menu.close() : menu.open(),
                tooltip: '音效切换',
                icon: const Icon(Icons.spatial_audio_off_outlined, size: 20),
              ),
            ),
            const SizedBox(width: 24, child: Divider(height: 5)),
            IconButton(
              onPressed:
                  controller.generalActionsEnabled && navigation.canToBegin
                  ? () => controller.moveToRow(navigation.toBeginRow)
                  : null,
              tooltip: '跳到开头',
              icon: const Icon(Icons.vertical_align_top, size: 20),
            ),
            IconButton(
              onPressed:
                  controller.generalActionsEnabled && navigation.canPageUp
                  ? () => controller.moveToRow(navigation.pageUpRow)
                  : null,
              tooltip: '上翻一页',
              icon: const Icon(Icons.keyboard_arrow_up, size: 20),
            ),
            IconButton(
              onPressed:
                  controller.hasAudio &&
                      (!controller.isBusy || controller.isPlaying)
                  ? controller.togglePlayback
                  : null,
              tooltip: controller.isPlaying
                  ? '暂停'
                  : controller.hasAudio
                  ? '播放'
                  : '请先加载音频',
              icon: Icon(
                controller.isPlaying
                    ? Icons.pause_rounded
                    : Icons.play_arrow_rounded,
                size: 20,
                color: controller.isPlaying ? EditorColors.accent : null,
              ),
            ),
            IconButton(
              onPressed:
                  controller.generalActionsEnabled && navigation.canPageDown
                  ? () => controller.moveToRow(navigation.pageDownRow)
                  : null,
              tooltip: '下翻一页',
              icon: const Icon(Icons.keyboard_arrow_down, size: 20),
            ),
            IconButton(
              onPressed: controller.generalActionsEnabled && navigation.canToEnd
                  ? () => controller.moveToRow(navigation.toEndRow)
                  : null,
              tooltip: '跳到末尾',
              icon: const Icon(Icons.vertical_align_bottom, size: 20),
            ),
          ],
        ),
      ),
    );
  }
}

class _HistoryControls extends StatelessWidget {
  const _HistoryControls({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    return DecoratedBox(
      key: const ValueKey('history-control-surface'),
      decoration: _canvasControlSurfaceDecoration,
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          IconButton(
            onPressed: controller.generalActionsEnabled && controller.canUndo
                ? controller.undo
                : null,
            tooltip: '撤销',
            icon: const Icon(Icons.undo, size: 20),
          ),
          IconButton(
            onPressed: controller.generalActionsEnabled && controller.canRedo
                ? controller.redo
                : null,
            tooltip: '重做',
            icon: const Icon(Icons.redo, size: 20),
          ),
        ],
      ),
    );
  }
}

class _EditorCanvasPainter extends CustomPainter {
  _EditorCanvasPainter({
    required this.controller,
    required this.mobile,
    required this.rowSpacing,
    required this.currentRow,
  }) : super(repaint: currentRow);

  final EditorController controller;
  final bool mobile;
  final double rowSpacing;
  final ValueListenable<double> currentRow;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = EditorColors.canvas);
    final chart = Rect.fromLTRB(0, 28, size.width, size.height);
    final cursorY = editorCursorY(size);
    final spacing = rowSpacing;
    final paintCurrentRow = currentRow.value;
    final columnLayout = _CanvasColumnLayout.from(
      chart,
      controller.canvasWaveformBlocks,
      rightInset: mobile ? mobileEditorContentRightInset : 0,
    );

    _drawColumnBackgrounds(canvas, columnLayout);
    if (controller.waveformVisible && controller.waveformBlocks.isNotEmpty) {
      _drawWaveform(
        canvas,
        chart,
        columnLayout,
        cursorY,
        spacing,
        paintCurrentRow,
      );
    }
    _drawShowtimeRanges(canvas, chart, cursorY, spacing, paintCurrentRow);
    _drawGrid(canvas, chart, cursorY, spacing, paintCurrentRow);
    _drawMarkers(
      canvas,
      chart,
      columnLayout,
      cursorY,
      spacing,
      paintCurrentRow,
    );
    _drawCursor(canvas, chart, cursorY);
    _drawHeaders(canvas, chart, columnLayout);
  }

  void _drawColumnBackgrounds(Canvas canvas, _CanvasColumnLayout columnLayout) {
    canvas.drawRect(
      columnLayout.laneAtVisualIndex(1),
      Paint()..color = EditorColors.text.withValues(alpha: 0.018),
    );
  }

  void _drawWaveform(
    Canvas canvas,
    Rect chart,
    _CanvasColumnLayout columnLayout,
    double cursorY,
    double spacing,
    double paintCurrentRow,
  ) {
    final blocks = controller.canvasWaveformBlocks;
    final first = blocks.first;
    final waveformPaint = Paint()..color = EditorColors.waveform;

    for (
      var screenY = chart.top.floor();
      screenY < chart.bottom.ceil();
      screenY++
    ) {
      final row = paintCurrentRow + (screenY - cursorY) / spacing;
      final seconds = OtgyNative.sixteenthRowToSeconds(
        bpm: controller.projectedBpm,
        offset: controller.offset,
        row: row,
      );
      final pixel = (seconds * first.pixelsPerSecond).floor();
      if (pixel < 0) {
        continue;
      }
      final blockIndex = pixel ~/ WaveformBlock.height;
      if (blockIndex < 0 || blockIndex >= blocks.length) {
        continue;
      }
      final block = blocks[blockIndex];
      final sourceY = pixel % WaveformBlock.height;
      _drawAlphaRow(
        canvas,
        block.leftAlpha,
        sourceY,
        block.channelWidth,
        columnLayout.leftWaveformOrigin,
        screenY.toDouble(),
        columnLayout.waveformFit,
        waveformPaint,
      );
      _drawAlphaRow(
        canvas,
        block.rightAlpha,
        sourceY,
        block.channelWidth,
        columnLayout.rightWaveformOrigin,
        screenY.toDouble(),
        columnLayout.waveformFit,
        waveformPaint,
      );
    }
  }

  void _drawAlphaRow(
    Canvas canvas,
    List<int> alpha,
    int row,
    int width,
    double xOrigin,
    double y,
    double fit,
    Paint paint,
  ) {
    final offset = row * width;
    var start = -1;
    var end = -1;
    var maximum = 0;
    for (var x = 0; x < width; x++) {
      final value = alpha[offset + x];
      if (value > 0) {
        start = start < 0 ? x : start;
        end = x;
        maximum = math.max(maximum, value);
      }
    }
    if (start < 0) {
      return;
    }
    paint.color = EditorColors.waveform.withValues(
      alpha: 0.16 + maximum / 255 * 0.44,
    );
    canvas.drawRect(
      Rect.fromLTRB(xOrigin + start * fit, y, xOrigin + (end + 1) * fit, y + 1),
      paint,
    );
  }

  void _drawShowtimeRanges(
    Canvas canvas,
    Rect chart,
    double cursorY,
    double spacing,
    double paintCurrentRow,
  ) {
    for (final move in controller.showtimeMoves) {
      final top = cursorY + (move.rangeStart - paintCurrentRow) * spacing;
      final bottom = cursorY + (move.rangeEnd - paintCurrentRow) * spacing;
      final visible = Rect.fromLTRB(
        chart.left,
        top,
        chart.right,
        bottom,
      ).intersect(chart);
      if (visible.isEmpty) {
        continue;
      }
      if (move.isLast) {
        final openTop =
            cursorY + (move.rangeEnd - 8 - paintCurrentRow) * spacing;
        final regularRect = Rect.fromLTRB(
          chart.left,
          top,
          chart.right,
          openTop,
        ).intersect(chart);
        if (!regularRect.isEmpty) {
          canvas.drawRect(
            regularRect,
            Paint()..color = EditorColors.markerD.withValues(alpha: 0.10),
          );
        }
        final openRect = Rect.fromLTRB(
          chart.left,
          openTop,
          chart.right,
          bottom,
        ).intersect(chart);
        canvas.drawRect(
          openRect,
          Paint()..color = EditorColors.markerD.withValues(alpha: 0.055),
        );
        final dash = Paint()
          ..color = EditorColors.markerD.withValues(alpha: 0.55)
          ..strokeWidth = 1;
        for (var x = chart.left; x < chart.right; x += 8) {
          canvas.drawLine(
            Offset(x, openTop),
            Offset(math.min(x + 4, chart.right), openTop),
            dash,
          );
        }
      } else {
        canvas.drawRect(
          visible,
          Paint()..color = EditorColors.markerD.withValues(alpha: 0.10),
        );
      }
    }
  }

  void _drawGrid(
    Canvas canvas,
    Rect chart,
    double cursorY,
    double spacing,
    double paintCurrentRow,
  ) {
    if (controller.bpm == 0) {
      return;
    }
    final first = math.max(
      0,
      (paintCurrentRow - cursorY / spacing).floor() - 1,
    );
    final last = (paintCurrentRow + (chart.bottom - cursorY) / spacing).ceil();
    for (var row = first; row <= last; row++) {
      final y = cursorY + (row - paintCurrentRow) * spacing;
      if (y < chart.top || y > chart.bottom) {
        continue;
      }
      final measure = row % 16 == 0;
      if (controller.isPlaying && !measure) {
        continue;
      }
      final beat = !measure && row % 4 == 0;
      final color = measure
          ? EditorColors.text.withValues(alpha: 0.52)
          : beat
          ? EditorColors.text.withValues(alpha: 0.30)
          : EditorColors.text.withValues(alpha: 0.075);
      canvas.drawLine(
        Offset(chart.left, y),
        Offset(chart.right, y),
        Paint()
          ..color = color
          ..strokeWidth = measure
              ? 2
              : beat
              ? 1.25
              : 1,
      );
      if (measure) {
        _paintText(
          canvas,
          '${row ~/ 16 + 1}',
          Offset(chart.left + 7, y - 10),
          const TextStyle(color: EditorColors.textMuted, fontSize: 10),
        );
      }
    }
  }

  void _drawMarkers(
    Canvas canvas,
    Rect chart,
    _CanvasColumnLayout columnLayout,
    double cursorY,
    double spacing,
    double paintCurrentRow,
  ) {
    final circleSize = editorMarkerSize(
      rowSpacing: spacing,
      mobile: mobile,
      diamond: false,
    );
    final diamondSize = editorMarkerSize(
      rowSpacing: spacing,
      mobile: mobile,
      diamond: true,
    );
    for (final marker in controller.markers) {
      final center = Offset(
        columnLayout.centerForColumn(marker.column),
        cursorY + (marker.row - paintCurrentRow) * spacing,
      );
      if (center.dy < chart.top - diamondSize ||
          center.dy > chart.bottom + diamondSize) {
        continue;
      }
      final isDiamond = _isDiamondMarker(marker.type);
      if (isDiamond) {
        final half = diamondSize / 2;
        final path = Path()
          ..moveTo(center.dx, center.dy - half)
          ..lineTo(center.dx + half, center.dy)
          ..lineTo(center.dx, center.dy + half)
          ..lineTo(center.dx - half, center.dy)
          ..close();
        canvas.drawPath(path, Paint()..color = EditorColors.markerC);
        canvas.drawPath(
          path,
          Paint()
            ..color = const Color(0xFF7D682C)
            ..style = PaintingStyle.stroke
            ..strokeWidth = 1.6,
        );
        continue;
      }

      final color = switch (marker.type) {
        OTGY_CHART_MARKER_NPC_NOTE ||
        OTGY_CHART_MARKER_PLAYER_NOTE => EditorColors.markerN,
        OTGY_CHART_MARKER_NPC_MOVE_END ||
        OTGY_CHART_MARKER_PLAYER_MOVE_END => EditorColors.markerS,
        _ => EditorColors.markerD,
      };
      final radius = circleSize / 2;
      canvas.drawCircle(center, radius, Paint()..color = color);
      canvas.drawCircle(
        center,
        radius,
        Paint()
          ..color = EditorColors.canvas.withValues(alpha: 0.72)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.7,
      );

      if (marker.type == OTGY_CHART_MARKER_NPC_NOTE ||
          marker.type == OTGY_CHART_MARKER_PLAYER_NOTE) {
        canvas.drawCircle(
          center,
          math.max(1, radius - 3.5),
          Paint()
            ..color = EditorColors.canvas.withValues(alpha: 0.58)
            ..style = PaintingStyle.stroke
            ..strokeWidth = 1,
        );
      } else if (marker.type == OTGY_CHART_MARKER_NPC_MOVE_END ||
          marker.type == OTGY_CHART_MARKER_PLAYER_MOVE_END) {
        canvas.drawLine(
          Offset(center.dx - radius * 0.55, center.dy + radius * 0.45),
          Offset(center.dx + radius * 0.55, center.dy + radius * 0.45),
          Paint()
            ..color = EditorColors.canvas.withValues(alpha: 0.68)
            ..strokeWidth = 1.5,
        );
      } else {
        final cue = Paint()
          ..color = EditorColors.text.withValues(alpha: 0.72)
          ..strokeWidth = 1.2;
        canvas.drawLine(
          Offset(center.dx, center.dy - radius - 2),
          Offset(center.dx, center.dy - radius + 1),
          cue,
        );
        canvas.drawLine(
          Offset(center.dx, center.dy + radius - 1),
          Offset(center.dx, center.dy + radius + 2),
          cue,
        );
      }

      final number = marker.type == OTGY_CHART_MARKER_SHOWTIME_MOVE
          ? marker.showNCount
          : marker.ordinal;
      if (number != null && circleSize >= 18) {
        final text = '$number';
        final fontSize = math
            .max(
              7,
              math.min(13, circleSize * 0.45 * math.min(1, 2.2 / text.length)),
            )
            .toDouble();
        _paintText(
          canvas,
          text,
          center,
          TextStyle(
            color: EditorColors.canvas,
            fontSize: fontSize,
            fontWeight: FontWeight.w700,
          ),
          centered: true,
        );
      }
    }
  }

  void _drawCursor(Canvas canvas, Rect chart, double y) {
    canvas.drawLine(
      Offset(chart.left, y),
      Offset(chart.right, y),
      Paint()
        ..color = EditorColors.judgement
        ..strokeWidth = 2,
    );
    canvas.drawCircle(
      Offset(chart.left + 4, y),
      3,
      Paint()..color = EditorColors.judgement,
    );
  }

  void _drawHeaders(
    Canvas canvas,
    Rect chart,
    _CanvasColumnLayout columnLayout,
  ) {
    canvas.drawRect(
      Rect.fromLTRB(chart.left, 0, chart.right, chart.top),
      Paint()..color = EditorColors.surfaceRaised,
    );
    for (var index = 0; index < _visualChartColumnLabels.length; index++) {
      _paintText(
        canvas,
        _visualChartColumnLabels[index],
        Offset(columnLayout.centers[index], 14),
        const TextStyle(
          color: EditorColors.textMuted,
          fontSize: 11,
          fontWeight: FontWeight.w600,
        ),
        centered: true,
      );
    }
  }

  void _paintText(
    Canvas canvas,
    String text,
    Offset position,
    TextStyle style, {
    bool centered = false,
  }) {
    final painter = TextPainter(
      text: TextSpan(text: text, style: style),
      textDirection: TextDirection.ltr,
      maxLines: 1,
    )..layout();
    painter.paint(
      canvas,
      centered
          ? position - Offset(painter.width / 2, painter.height / 2)
          : position,
    );
  }

  @override
  bool shouldRepaint(covariant _EditorCanvasPainter oldDelegate) => true;
}

String _markerSemanticLabel(ChartMarker marker) {
  final role = switch (marker.column) {
    OTGY_CHART_COLUMN_NPC => 'NPC',
    OTGY_CHART_COLUMN_PLAYER => '玩家',
    _ => 'Showtime',
  };
  final type = switch (marker.type) {
    OTGY_CHART_MARKER_NPC_BLOCK_START ||
    OTGY_CHART_MARKER_PLAYER_BLOCK_START ||
    OTGY_CHART_MARKER_SHOWTIME_START => '起点',
    OTGY_CHART_MARKER_NPC_NOTE ||
    OTGY_CHART_MARKER_PLAYER_NOTE => '动作 ${marker.ordinal}',
    OTGY_CHART_MARKER_NPC_MOVE_END || OTGY_CHART_MARKER_PLAYER_MOVE_END => '结束',
    _ => '舞步 ${marker.showNCount}',
  };
  return '$role $type，第 ${marker.row} 个十六分行';
}

bool _isDiamondMarker(int type) =>
    type == OTGY_CHART_MARKER_NPC_BLOCK_START ||
    type == OTGY_CHART_MARKER_PLAYER_BLOCK_START ||
    type == OTGY_CHART_MARKER_SHOWTIME_START;
