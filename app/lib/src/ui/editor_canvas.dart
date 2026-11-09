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
import 'usage_guide.dart';

const double editorCursorViewportFraction = 0.25;
const double mobileEditorContentRightInset = 24;
const Duration playbackVisualPredictionLimit = Duration(milliseconds: 50);

enum EditorWaveformLayer { original, overlayOriginal, filtered }

double editorWaveformRowOpacity(int alpha, EditorWaveformLayer layer) {
  final normalized = alpha.clamp(0, 255) / 255;
  return switch (layer) {
    EditorWaveformLayer.original => 0.12 + normalized * 0.40,
    EditorWaveformLayer.overlayOriginal => 0.06 + normalized * 0.18,
    EditorWaveformLayer.filtered => 0.08 + normalized * 0.28,
  };
}

final BoxDecoration _canvasControlSurfaceDecoration = BoxDecoration(
  color: EditorColors.surface.withValues(alpha: 0.92),
  borderRadius: BorderRadius.circular(6),
);

double editorCursorY(Size size) => size.height * editorCursorViewportFraction;

Rect? projectEditorSelectionBand({
  required ChartSelection selection,
  required Rect chart,
  required double cursorY,
  required double currentRow,
  required double rowSpacing,
}) {
  if (!selection.active) {
    return null;
  }
  final top = cursorY + (selection.firstRow - currentRow - 0.5) * rowSpacing;
  final bottom = cursorY + (selection.lastRow - currentRow + 0.5) * rowSpacing;
  if (bottom < chart.top || top > chart.bottom) {
    return null;
  }
  return Rect.fromLTRB(chart.left, top, chart.right, bottom);
}

int projectEditorRow({
  required double y,
  required double cursorY,
  required double currentRow,
  required double rowSpacing,
}) {
  final raw = currentRow + (y - cursorY) / rowSpacing;
  return math.max(0, (raw + 0.5).floor());
}

double editorMarkerSize({required bool mobile, required bool diamond}) {
  if (diamond) {
    return mobile ? 36 : 28;
  }
  return mobile ? 28 : 22;
}

double editorMarkerFontSize({
  required double circleSize,
  required int textLength,
}) {
  final length = math.max(1, textLength);
  return math
      .max(8, math.min(16, circleSize * 0.64 * math.min(1, 2.2 / length)))
      .toDouble();
}

final class EditorMarkerStyle {
  const EditorMarkerStyle({
    required this.fill,
    required this.isDiamond,
    required this.showsNumber,
  });

  final Color fill;
  final bool isDiamond;
  final bool showsNumber;
}

EditorMarkerStyle editorMarkerStyle(int type) => switch (type) {
  OTGY_CHART_MARKER_NPC_BLOCK_START => const EditorMarkerStyle(
    fill: EditorColors.markerNpc,
    isDiamond: true,
    showsNumber: false,
  ),
  OTGY_CHART_MARKER_NPC_NOTE => const EditorMarkerStyle(
    fill: EditorColors.markerNpc,
    isDiamond: false,
    showsNumber: true,
  ),
  OTGY_CHART_MARKER_NPC_MOVE_END => const EditorMarkerStyle(
    fill: EditorColors.markerS,
    isDiamond: false,
    showsNumber: false,
  ),
  OTGY_CHART_MARKER_PLAYER_BLOCK_START => const EditorMarkerStyle(
    fill: EditorColors.markerPlayer,
    isDiamond: true,
    showsNumber: false,
  ),
  OTGY_CHART_MARKER_PLAYER_NOTE => const EditorMarkerStyle(
    fill: EditorColors.markerPlayer,
    isDiamond: false,
    showsNumber: true,
  ),
  OTGY_CHART_MARKER_PLAYER_MOVE_END => const EditorMarkerStyle(
    fill: EditorColors.markerS,
    isDiamond: false,
    showsNumber: false,
  ),
  OTGY_CHART_MARKER_SHOWTIME_START => const EditorMarkerStyle(
    fill: EditorColors.markerD,
    isDiamond: true,
    showsNumber: false,
  ),
  OTGY_CHART_MARKER_SHOWTIME_MOVE => const EditorMarkerStyle(
    fill: EditorColors.markerD,
    isDiamond: false,
    showsNumber: true,
  ),
  _ => const EditorMarkerStyle(
    fill: EditorColors.markerD,
    isDiamond: false,
    showsNumber: false,
  ),
};

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

final class _MarkerLongPressIntent extends Intent {
  const _MarkerLongPressIntent();
}

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
  int? _focusHighlightedMarkerId;
  Size? _latestCanvasSize;
  Offset? _desktopHoverPosition;
  bool _pastePreviewSyncScheduled = false;
  int? _pastePreviewSyncedRevision;

  bool get _mobile =>
      defaultTargetPlatform == TargetPlatform.iOS ||
      defaultTargetPlatform == TargetPlatform.android;

  bool get _macOS => defaultTargetPlatform == TargetPlatform.macOS;

  @override
  void initState() {
    super.initState();
    _paintCurrentRow = ValueNotifier(widget.controller.currentRow);
    _playbackVisualTicker = createTicker(_handlePlaybackVisualFrame);
    widget.controller.addListener(_handleControllerChanged);
    _handleControllerChanged();
  }

  @override
  void didUpdateWidget(covariant EditorCanvas oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller == widget.controller) {
      return;
    }
    oldWidget.controller.removeListener(_handleControllerChanged);
    widget.controller.addListener(_handleControllerChanged);
    _pastePreviewSyncedRevision = null;
    _desktopHoverPosition = null;
    _stopPlaybackVisual();
    _handleControllerChanged();
  }

  @override
  void dispose() {
    widget.controller.removeListener(_handleControllerChanged);
    _playbackVisualTicker.dispose();
    _paintCurrentRow.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  void _handleControllerChanged() {
    _synchronizePlaybackVisual();
    _schedulePastePreviewSync();
  }

  void _schedulePastePreviewSync() {
    if (_pastePreviewSyncScheduled) {
      return;
    }
    _pastePreviewSyncScheduled = true;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _pastePreviewSyncScheduled = false;
      if (!mounted) {
        return;
      }
      _syncPastePreview();
    });
  }

  void _syncPastePreview() {
    final controller = widget.controller;
    final size = _latestCanvasSize;
    if (size == null ||
        !controller.pastePending ||
        !controller.hasClipboard ||
        !controller.selectionCommandEnabled ||
        controller.isBusy) {
      return;
    }
    final position = _mobile
        ? Offset(size.width / 2, _cursorY(size))
        : _desktopHoverPosition;
    if (position == null || (!_mobile && _columnAt(position, size) == null)) {
      controller.clearPastePreview();
      return;
    }
    final row = _rowAt(position.dy, size);
    if (controller.pastePreviewAnchorRow != row ||
        _pastePreviewSyncedRevision != controller.projectRevision) {
      _pastePreviewSyncedRevision = controller.projectRevision;
      controller.updatePastePreview(row);
    }
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
    if (_mobile && widget.controller.pastePending) {
      return;
    }
    _handleCellActivate(column, _rowAt(details.localPosition.dy, size));
  }

  void _handleCellActivate(int column, int row) {
    _focusNode.requestFocus();
    switch (widget.controller.chartInteractionMode) {
      case EditorChartInteractionMode.normal:
        widget.controller.clickCell(column, row);
      case EditorChartInteractionMode.selection:
        widget.controller.selectAt(column, row);
      case EditorChartInteractionMode.pastePending:
        if (!_mobile) {
          widget.controller.pasteAtRow(row);
          _pastePreviewSyncedRevision = widget.controller.projectRevision;
        }
    }
  }

  void _handleMarkerActivate(int column, int row) {
    if (widget.controller.pastePending) {
      return;
    }
    _handleCellActivate(column, row);
  }

  void _handleMarkerSelection(int column, int row) {
    if (widget.controller.chartInteractionMode ==
        EditorChartInteractionMode.normal) {
      widget.controller.enterSelectionMode();
    }
    if (widget.controller.selectionMode) {
      widget.controller.selectAt(column, row);
    }
  }

  void _handleLongPress(LongPressStartDetails details, Size size) {
    final column = _columnAt(details.localPosition, size);
    if (column == null) {
      return;
    }
    _focusNode.requestFocus();
    _handleMarkerSelection(column, _rowAt(details.localPosition.dy, size));
  }

  void _updateDesktopPastePreview(Offset position, Size size) {
    if (_mobile) {
      return;
    }
    _desktopHoverPosition = position;
    _syncPastePreview();
  }

  void _handleDesktopHoverExit() {
    if (_mobile) {
      return;
    }
    _desktopHoverPosition = null;
    _pastePreviewSyncedRevision = null;
    widget.controller.clearPastePreview();
  }

  void _pasteAtCurrentCursor(Size size) {
    if (!_mobile || !widget.controller.pastePending) {
      return;
    }
    widget.controller.pasteAtRow(_rowAt(_cursorY(size), size));
    _pastePreviewSyncedRevision = widget.controller.projectRevision;
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
        widget.controller.navigationEnabled && !widget.controller.isBusy;
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
        if (_latestCanvasSize != size) {
          _latestCanvasSize = size;
          _schedulePastePreviewSync();
        }
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
          final markerTop = math.max(rect.top, y - spacing / 2).toDouble();
          final markerBottom = math
              .min(rect.bottom, y + spacing / 2)
              .toDouble();
          if (markerBottom <= markerTop) {
            continue;
          }
          final lane = columnLayout.laneForColumn(marker.column);
          markerSemantics.add(
            Positioned(
              left: lane.left,
              top: markerTop,
              width: lane.width,
              height: markerBottom - markerTop,
              child: FocusableActionDetector(
                enabled:
                    widget.controller.markerInteractionEnabled &&
                    !widget.controller.isBusy,
                shortcuts: const {
                  SingleActivator(LogicalKeyboardKey.enter, shift: true):
                      _MarkerLongPressIntent(),
                },
                actions: {
                  ActivateIntent: CallbackAction<ActivateIntent>(
                    onInvoke: (_) {
                      _handleMarkerActivate(marker.column, marker.row);
                      return null;
                    },
                  ),
                  _MarkerLongPressIntent:
                      CallbackAction<_MarkerLongPressIntent>(
                        onInvoke: (_) {
                          _handleMarkerSelection(marker.column, marker.row);
                          return null;
                        },
                      ),
                },
                onShowFocusHighlight: (value) {
                  if (value) {
                    if (_focusHighlightedMarkerId != marker.id) {
                      setState(() => _focusHighlightedMarkerId = marker.id);
                    }
                  } else if (_focusHighlightedMarkerId == marker.id) {
                    setState(() => _focusHighlightedMarkerId = null);
                  }
                },
                child: Semantics(
                  button: true,
                  label: _markerSemanticLabel(marker),
                  onTap:
                      widget.controller.markerInteractionEnabled &&
                          !widget.controller.isBusy &&
                          !widget.controller.pastePending
                      ? () => _handleMarkerActivate(marker.column, marker.row)
                      : null,
                  onLongPress:
                      widget.controller.markerInteractionEnabled &&
                          !widget.controller.isBusy
                      ? () => _handleMarkerSelection(marker.column, marker.row)
                      : null,
                  child: Stack(
                    fit: StackFit.expand,
                    clipBehavior: Clip.none,
                    children: [
                      const SizedBox.expand(),
                      if (_focusHighlightedMarkerId == marker.id)
                        Center(
                          child: _MarkerFocusRing(
                            key: ValueKey('marker-focus-${marker.id}'),
                            style: editorMarkerStyle(marker.type),
                            mobile: _mobile,
                          ),
                        ),
                    ],
                  ),
                ),
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
                MouseRegion(
                  onEnter: (event) =>
                      _updateDesktopPastePreview(event.localPosition, size),
                  onHover: (event) =>
                      _updateDesktopPastePreview(event.localPosition, size),
                  onExit: (_) => _handleDesktopHoverExit(),
                  child: Listener(
                    onPointerSignal: (event) {
                      if (event is PointerScrollEvent) {
                        _handleScroll(event);
                      }
                    },
                    onPointerDown: (event) {
                      if (!_mobile) {
                        _updateDesktopPastePreview(event.localPosition, size);
                      }
                      if (event.buttons == kMiddleMouseButton) {
                        widget.controller.moveToRow(
                          _rowAt(event.localPosition.dy, size).toDouble(),
                        );
                      }
                    },
                    onPointerMove: (event) {
                      if (!_mobile) {
                        _updateDesktopPastePreview(event.localPosition, size);
                      }
                    },
                    child: GestureDetector(
                      key: const ValueKey('editor-canvas'),
                      behavior: HitTestBehavior.opaque,
                      onTapUp: (details) => _handleTap(details, size),
                      onLongPressStart: (details) =>
                          _handleLongPress(details, size),
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
                ),
                if (_mobile)
                  Positioned(
                    right: 8,
                    top: 40,
                    child: _NavigationRail(
                      controller: widget.controller,
                      navigation: navigation,
                    ),
                  )
                else
                  Positioned(
                    right: 8,
                    top: 0,
                    bottom: 0,
                    child: Align(
                      alignment: Alignment.centerRight,
                      child: _NavigationRail(
                        controller: widget.controller,
                        navigation: navigation,
                      ),
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
                if (widget.controller.chartInteractionMode !=
                    EditorChartInteractionMode.normal)
                  Positioned(
                    left: 8,
                    right: _mobile ? 8 : null,
                    bottom: 56,
                    child: Align(
                      alignment: Alignment.bottomLeft,
                      child: _ChartInteractionControls(
                        controller: widget.controller,
                        mobile: _mobile,
                        onPasteAtCurrentCursor: _mobile
                            ? () => _pasteAtCurrentCursor(size)
                            : null,
                      ),
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

class _MarkerFocusRing extends StatelessWidget {
  const _MarkerFocusRing({
    super.key,
    required this.style,
    required this.mobile,
  });

  final EditorMarkerStyle style;
  final bool mobile;

  @override
  Widget build(BuildContext context) {
    final size = editorMarkerSize(mobile: mobile, diamond: style.isDiamond) + 8;
    final border = Border.all(color: EditorColors.judgement, width: 2);
    return IgnorePointer(
      child: SizedBox.square(
        dimension: size,
        child: Center(
          child: style.isDiamond
              ? Transform.rotate(
                  angle: math.pi / 4,
                  child: SizedBox.square(
                    dimension: size / math.sqrt2,
                    child: DecoratedBox(
                      decoration: BoxDecoration(border: border),
                    ),
                  ),
                )
              : DecoratedBox(
                  decoration: BoxDecoration(
                    border: border,
                    borderRadius: BorderRadius.circular(size / 2),
                  ),
                ),
        ),
      ),
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

class _UsageGuideButton extends StatelessWidget {
  const _UsageGuideButton({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    final enabled = controller.generalActionsEnabled && !controller.isBusy;
    return IconButton(
      key: const ValueKey('usage-guide-button'),
      onPressed: enabled
          ? () => Navigator.of(context).push(
              PageRouteBuilder<void>(
                opaque: true,
                pageBuilder: (context, animation, secondaryAnimation) =>
                    const UsageGuideScreen(),
                transitionsBuilder:
                    (context, animation, secondaryAnimation, child) =>
                        FadeTransition(opacity: animation, child: child),
                transitionDuration: const Duration(milliseconds: 180),
              ),
            )
          : null,
      tooltip: enabled
          ? '使用指南'
          : controller.isPlaying
          ? '播放中'
          : '请先退出选择或粘贴模式',
      style: IconButton.styleFrom(
        backgroundColor: enabled
            ? EditorColors.accent
            : EditorColors.surfaceRaised,
        foregroundColor: enabled
            ? EditorColors.canvas
            : EditorColors.textMuted.withValues(alpha: 0.42),
        minimumSize: const Size(40, 40),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
      ),
      icon: const Icon(Icons.help_outline, size: 20),
    );
  }
}

class _NavigationRailState extends State<_NavigationRail> {  final _soundMenu = MenuController();
  final _waveformMenu = MenuController();

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
    if (widget.controller.isPlaying && _waveformMenu.isOpen) {
      _waveformMenu.close();
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
            _UsageGuideButton(controller: controller),
            const SizedBox(width: 24, child: Divider(height: 8)),
            MenuAnchor(
              controller: _waveformMenu,
              alignmentOffset: const Offset(-266, -36),
              menuChildren: [
                SizedBox(
                  key: const ValueKey('waveform-settings-menu'),
                  width: 252,
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(14, 10, 14, 12),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Text(
                          '波形设置',
                          style: Theme.of(context).textTheme.titleSmall,
                        ),
                        const SizedBox(height: 8),
                        SwitchListTile.adaptive(
                          key: const ValueKey('waveform-visible'),
                          contentPadding: EdgeInsets.zero,
                          dense: true,
                          title: const Text('显示波形'),
                          value: controller.waveformVisible,
                          onChanged: (_) => controller.toggleWaveform(),
                        ),
                        const SizedBox(height: 10),
                        Text(
                          '滤波',
                          style: Theme.of(context).textTheme.labelMedium,
                        ),
                        const SizedBox(height: 4),
                        SegmentedButton<WaveformFilter>(
                          key: const ValueKey('waveform-filter'),
                          segments: const [
                            ButtonSegment(
                              value: WaveformFilter.none,
                              label: Text('关闭'),
                            ),
                            ButtonSegment(
                              value: WaveformFilter.highPass,
                              label: Text('高通'),
                            ),
                            ButtonSegment(
                              value: WaveformFilter.lowPass,
                              label: Text('低通'),
                            ),
                          ],
                          selected: {controller.waveformFilter},
                          showSelectedIcon: false,
                          onSelectionChanged: controller.isPlaying
                              ? null
                              : (value) =>
                                    controller.setWaveformFilter(value.single),
                        ),
                        const SizedBox(height: 6),
                        Row(
                          children: [
                            const Text('滤波强度'),
                            const Spacer(),
                            Text(
                              '${(controller.waveformFilterStrength * 100).round()}%',
                              style: Theme.of(context).textTheme.bodySmall,
                            ),
                          ],
                        ),
                        Slider(
                          key: const ValueKey('waveform-filter-strength'),
                          value: controller.waveformFilterStrength,
                          onChanged:
                              controller.isPlaying ||
                                  !controller.waveformFilterEnabled
                              ? null
                              : controller.setWaveformFilterStrength,
                        ),
                        SwitchListTile.adaptive(
                          key: const ValueKey('waveform-overlay-original'),
                          contentPadding: EdgeInsets.zero,
                          dense: true,
                          title: const Text('叠加原始波形'),
                          value: controller.waveformOverlayOriginal,
                          onChanged:
                              controller.isPlaying ||
                                  !controller.waveformFilterEnabled
                              ? null
                              : controller.setWaveformOverlayOriginal,
                        ),
                      ],
                    ),
                  ),
                ),
              ],
              builder: (context, menu, child) => IconButton(
                key: const ValueKey('waveform-toggle-button'),
                onPressed: controller.isPlaying
                    ? controller.toggleWaveform
                    : controller.isBusy
                    ? null
                    : () => menu.isOpen ? menu.close() : menu.open(),
                tooltip: controller.isPlaying
                    ? controller.waveformVisible
                          ? '隐藏波形'
                          : '显示波形'
                    : '波形设置',
                icon: Icon(
                  controller.waveformVisible
                      ? Icons.graphic_eq
                      : Icons.hide_source_outlined,
                  size: 20,
                  color: controller.waveformVisible
                      ? EditorColors.accent
                      : null,
                ),
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
                          onSelectionChanged:
                              controller.generalActionsEnabled &&
                                  !controller.isBusy
                              ? (value) => controller.setPlaybackSoundLanguage(
                                  value.single,
                                )
                              : null,
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
                          onSelectionChanged:
                              controller.generalActionsEnabled &&
                                  !controller.isBusy
                              ? (value) => controller.setPlaybackDanceMode(
                                  value.single,
                                )
                              : null,
                        ),
                        const SizedBox(height: 8),
                        SwitchListTile.adaptive(
                          key: const ValueKey('transition-sounds-switch'),
                          contentPadding: EdgeInsets.zero,
                          dense: true,
                          title: const Text('切换音效'),
                          value: controller.transitionSoundsEnabled,
                          onChanged:
                              controller.generalActionsEnabled &&
                                  !controller.isBusy
                              ? controller.setTransitionSoundsEnabled
                              : null,
                        ),
                      ],
                    ),
                  ),
                ),
              ],
              builder: (context, menu, child) => IconButton(
                key: const ValueKey('playback-sound-button'),
                onPressed:
                    controller.isPlaying || !controller.generalActionsEnabled
                    ? null
                    : () => menu.isOpen ? menu.close() : menu.open(),
                tooltip: '音效切换',
                icon: const Icon(Icons.spatial_audio_off_outlined, size: 20),
              ),
            ),
            const SizedBox(width: 24, child: Divider(height: 5)),
            IconButton(
              onPressed: controller.navigationEnabled && navigation.canToBegin
                  ? () => controller.moveToRow(navigation.toBeginRow)
                  : null,
              tooltip: '跳到开头',
              icon: const Icon(Icons.vertical_align_top, size: 20),
            ),
            IconButton(
              onPressed: controller.navigationEnabled && navigation.canPageUp
                  ? () => controller.moveToRow(navigation.pageUpRow)
                  : null,
              tooltip: '上翻一页',
              icon: const Icon(Icons.keyboard_arrow_up, size: 20),
            ),
            IconButton(
              key: const ValueKey('playback-button'),
              onPressed:
                  controller.hasAudio &&
                      !controller.isRhythmTesting &&
                      (controller.isChartPlaying ||
                          (controller.generalActionsEnabled &&
                              !controller.isBusy))
                  ? controller.togglePlayback
                  : null,
              tooltip: controller.isChartPlaying
                  ? '暂停'
                  : controller.isRhythmTesting
                  ? '节奏测试中'
                  : !controller.hasAudio
                  ? '请先加载音频'
                  : !controller.generalActionsEnabled
                  ? '请先退出选择或粘贴模式'
                  : '播放',
              icon: Icon(
                controller.isChartPlaying
                    ? Icons.pause_rounded
                    : Icons.play_arrow_rounded,
                size: 20,
                color: controller.isChartPlaying ? EditorColors.accent : null,
              ),
            ),
            IconButton(
              onPressed: controller.navigationEnabled && navigation.canPageDown
                  ? () => controller.moveToRow(navigation.pageDownRow)
                  : null,
              tooltip: '下翻一页',
              icon: const Icon(Icons.keyboard_arrow_down, size: 20),
            ),
            IconButton(
              onPressed: controller.navigationEnabled && navigation.canToEnd
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

class _ChartInteractionControls extends StatelessWidget {
  const _ChartInteractionControls({
    required this.controller,
    required this.mobile,
    this.onPasteAtCurrentCursor,
  });

  final EditorController controller;
  final bool mobile;
  final VoidCallback? onPasteAtCurrentCursor;

  @override
  Widget build(BuildContext context) {
    final enabled = controller.selectionCommandEnabled && !controller.isBusy;
    final exit = IconButton(
      key: const ValueKey('selection-exit'),
      onPressed: enabled ? controller.exitChartInteraction : null,
      tooltip: controller.pastePending ? '退出待粘贴' : '退出选择',
      icon: const Icon(Icons.close, size: 20),
    );
    return DecoratedBox(
      decoration: _canvasControlSurfaceDecoration,
      child: controller.pastePending
          ? mobile
                ? Semantics(
                    container: true,
                    liveRegion: true,
                    label: _mobilePasteStatus(controller),
                    child: Padding(
                      key: const ValueKey('paste-pending-controls'),
                      padding: const EdgeInsets.only(left: 12),
                      child: Wrap(
                        crossAxisAlignment: WrapCrossAlignment.center,
                        spacing: 8,
                        children: [
                          Text('${controller.clipboardCount} 个舞步'),
                          FilledButton.icon(
                            key: const ValueKey('paste-at-current-cursor'),
                            onPressed: enabled && controller.hasClipboard
                                ? onPasteAtCurrentCursor
                                : null,
                            icon: const Icon(Icons.content_paste, size: 20),
                            label: const Text('粘贴到当前判定线'),
                            style: FilledButton.styleFrom(
                              minimumSize: const Size(0, 48),
                              visualDensity: VisualDensity.standard,
                            ),
                          ),
                          exit,
                        ],
                      ),
                    ),
                  )
                : Row(
                    key: const ValueKey('paste-pending-controls'),
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Padding(
                        padding: const EdgeInsets.only(left: 12, right: 4),
                        child: Text('点击谱面放置 ${controller.clipboardCount} 个舞步'),
                      ),
                      exit,
                    ],
                  )
          : Row(
              key: const ValueKey('selection-controls'),
              mainAxisSize: MainAxisSize.min,
              children: [
                IconButton(
                  key: const ValueKey('selection-copy'),
                  onPressed: enabled && controller.canCopySelection
                      ? controller.copySelection
                      : null,
                  tooltip: '复制',
                  icon: const Icon(Icons.copy_outlined, size: 20),
                ),
                IconButton(
                  key: const ValueKey('selection-cut'),
                  onPressed: enabled && controller.canCutSelection
                      ? controller.cutSelection
                      : null,
                  tooltip: '剪切',
                  icon: const Icon(Icons.content_cut_outlined, size: 20),
                ),
                exit,
              ],
            ),
    );
  }

  String _mobilePasteStatus(EditorController controller) {
    final row = controller.pastePreviewAnchorRow;
    final preview = controller.pastePreview;
    if (row == null || preview == null) {
      return '正在计算当前判定线的粘贴位置';
    }
    return preview.valid
        ? '第 $row 行，可以粘贴 ${controller.clipboardCount} 个舞步'
        : '第 $row 行，当前位置不能粘贴';
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
            onPressed: controller.historyNavigationEnabled && controller.canUndo
                ? controller.undo
                : null,
            tooltip: '撤销',
            icon: const Icon(Icons.undo, size: 20),
          ),
          IconButton(
            onPressed: controller.historyNavigationEnabled && controller.canRedo
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

    // Everything that follows is timeline content. Keep it below the column
    // header even when a fixed-size marker overlaps the first visible rows.
    // Headers are painted after restoring the canvas so they remain a stable
    // chrome layer rather than an accidental overflow mask.
    canvas.save();
    canvas.clipRect(chart);
    _drawColumnBackgrounds(canvas, columnLayout);
    if (controller.waveformVisible &&
        controller.canvasWaveformBlocks.isNotEmpty) {
      if (!controller.waveformFilterEnabled ||
          controller.waveformOverlayOriginal) {
        _drawWaveform(
          canvas,
          chart,
          columnLayout,
          cursorY,
          spacing,
          paintCurrentRow,
          controller.canvasWaveformBlocks,
          EditorColors.waveform,
          controller.waveformFilterEnabled
              ? EditorWaveformLayer.overlayOriginal
              : EditorWaveformLayer.original,
        );
      }
      if (controller.waveformFilterEnabled &&
          controller.canvasFilteredWaveformBlocks.isNotEmpty) {
        _drawWaveform(
          canvas,
          chart,
          columnLayout,
          cursorY,
          spacing,
          paintCurrentRow,
          controller.canvasFilteredWaveformBlocks,
          EditorColors.waveformFilter,
          EditorWaveformLayer.filtered,
        );
      }
    }
    _drawShowtimeRanges(canvas, chart, cursorY, spacing, paintCurrentRow);
    _drawGrid(canvas, chart, cursorY, spacing, paintCurrentRow);
    _drawSelections(canvas, chart, cursorY, spacing, paintCurrentRow);
    _drawPastePreview(
      canvas,
      chart,
      columnLayout,
      cursorY,
      spacing,
      paintCurrentRow,
    );
    _drawMarkers(
      canvas,
      chart,
      columnLayout,
      cursorY,
      spacing,
      paintCurrentRow,
    );
    _drawCursor(canvas, chart, cursorY);
    canvas.restore();
    _drawHeaders(canvas, chart, columnLayout);
  }

  void _drawSelections(
    Canvas canvas,
    Rect chart,
    double cursorY,
    double spacing,
    double paintCurrentRow,
  ) {
    for (final selection in controller.selections) {
      final band = projectEditorSelectionBand(
        selection: selection,
        chart: chart,
        cursorY: cursorY,
        currentRow: paintCurrentRow,
        rowSpacing: spacing,
      );
      if (band == null) {
        continue;
      }
      canvas.drawRect(
        band,
        Paint()..color = EditorColors.accent.withValues(alpha: 0.10),
      );
      canvas.drawRect(
        band,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = EditorColors.accent.withValues(alpha: 0.85),
      );
    }
  }

  void _drawPastePreview(
    Canvas canvas,
    Rect chart,
    _CanvasColumnLayout columnLayout,
    double cursorY,
    double spacing,
    double paintCurrentRow,
  ) {
    final preview = controller.pastePreview;
    if (preview == null) {
      return;
    }
    final color = preview.valid
        ? EditorColors.markerPlayer
        : EditorColors.danger;
    final envelope = projectEditorSelectionBand(
      selection: ChartSelection(
        active: true,
        danceMoveDrillId: 0,
        firstRow: preview.firstRow,
        lastRow: preview.lastRow,
      ),
      chart: chart,
      cursorY: cursorY,
      currentRow: paintCurrentRow,
      rowSpacing: spacing,
    );
    if (envelope != null) {
      canvas.drawRect(envelope, Paint()..color = color.withValues(alpha: 0.07));
      _drawPreviewBorder(canvas, envelope, color, dashed: !preview.valid);
    }
    for (final selection in preview.selections) {
      final band = projectEditorSelectionBand(
        selection: selection,
        chart: chart,
        cursorY: cursorY,
        currentRow: paintCurrentRow,
        rowSpacing: spacing,
      );
      if (band != null) {
        canvas.drawRect(band, Paint()..color = color.withValues(alpha: 0.08));
        _drawPreviewBorder(canvas, band, color, dashed: !preview.valid);
      }
    }
    _drawMarkerList(
      canvas,
      chart,
      columnLayout,
      cursorY,
      spacing,
      paintCurrentRow,
      preview.markers,
      opacity: preview.valid ? 0.58 : 0.36,
      fillOverride: color,
    );
  }

  void _drawPreviewBorder(
    Canvas canvas,
    Rect rect,
    Color color, {
    required bool dashed,
  }) {
    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = color.withValues(alpha: 0.9);
    if (!dashed) {
      canvas.drawRect(rect, paint);
      return;
    }
    const dash = 7.0;
    const gap = 5.0;
    for (var x = rect.left; x < rect.right; x += dash + gap) {
      final right = math.min(x + dash, rect.right);
      canvas.drawLine(Offset(x, rect.top), Offset(right, rect.top), paint);
      canvas.drawLine(
        Offset(x, rect.bottom),
        Offset(right, rect.bottom),
        paint,
      );
    }
    for (var y = rect.top; y < rect.bottom; y += dash + gap) {
      final bottom = math.min(y + dash, rect.bottom);
      canvas.drawLine(Offset(rect.left, y), Offset(rect.left, bottom), paint);
      canvas.drawLine(Offset(rect.right, y), Offset(rect.right, bottom), paint);
    }
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
    List<WaveformBlock> blocks,
    Color color,
    EditorWaveformLayer layer,
  ) {
    final first = blocks.first;
    final waveformPaint = Paint()..color = color;

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
        color,
        layer,
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
        color,
        layer,
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
    Color color,
    EditorWaveformLayer layer,
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
    paint.color = color.withValues(
      alpha: editorWaveformRowOpacity(maximum, layer),
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
    _drawMarkerList(
      canvas,
      chart,
      columnLayout,
      cursorY,
      spacing,
      paintCurrentRow,
      controller.markers,
    );
  }

  void _drawMarkerList(
    Canvas canvas,
    Rect chart,
    _CanvasColumnLayout columnLayout,
    double cursorY,
    double spacing,
    double paintCurrentRow,
    List<ChartMarker> markers, {
    double opacity = 1,
    Color? fillOverride,
  }) {
    final circleSize = editorMarkerSize(mobile: mobile, diamond: false);
    final diamondSize = editorMarkerSize(mobile: mobile, diamond: true);
    for (final marker in markers) {
      final style = editorMarkerStyle(marker.type);
      final fill = (fillOverride ?? style.fill).withValues(alpha: opacity);
      final center = Offset(
        columnLayout.centerForColumn(marker.column),
        cursorY + (marker.row - paintCurrentRow) * spacing,
      );
      if (center.dy < chart.top - diamondSize ||
          center.dy > chart.bottom + diamondSize) {
        continue;
      }
      if (style.isDiamond) {
        final half = diamondSize / 2;
        final path = Path()
          ..moveTo(center.dx, center.dy - half)
          ..lineTo(center.dx + half, center.dy)
          ..lineTo(center.dx, center.dy + half)
          ..lineTo(center.dx - half, center.dy)
          ..close();
        canvas.drawPath(path, Paint()..color = fill);
        continue;
      }

      final radius = circleSize / 2;
      canvas.drawCircle(center, radius, Paint()..color = fill);

      final number = marker.type == OTGY_CHART_MARKER_SHOWTIME_MOVE
          ? marker.showNCount
          : marker.ordinal;
      if (style.showsNumber && number != null) {
        final text = '$number';
        final fontSize = editorMarkerFontSize(
          circleSize: circleSize,
          textLength: text.length,
        );
        _paintText(
          canvas,
          text,
          center,
          TextStyle(
            color: EditorColors.markerText.withValues(alpha: opacity),
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
