import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  late Directory directory;
  late LocalProjectStore store;
  late EditorController controller;
  late NativeChartEditor fixtureEditor;

  void addDanceMove(NativeChartEditor editor, int firstRow) {
    for (final row in [0, 2, 4, 6, 6]) {
      expect(
        editor.click(OTGY_CHART_COLUMN_NPC, firstRow + row).changed,
        isTrue,
      );
    }
    for (final row in [8, 10]) {
      expect(
        editor.click(OTGY_CHART_COLUMN_PLAYER, firstRow + row).changed,
        isTrue,
      );
    }
  }

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-selection-widget-');
    store = await LocalProjectStore.openAt(directory);
    final editor = NativeChartEditor.createEmpty();
    addDanceMove(editor, 0);
    addDanceMove(editor, 32);
    fixtureEditor = editor;
    controller = EditorController(editor: editor);
  });

  tearDown(() async {
    controller.dispose();
    await directory.delete(recursive: true);
  });

  void selectionWidgetTest(
    String description,
    Future<void> Function(WidgetTester) body,
  ) {
    testWidgets(description, (tester) async {
      final previous = debugDefaultTargetPlatformOverride;
      try {
        await body(tester);
      } finally {
        await tester.pumpWidget(const SizedBox.shrink());
        debugDefaultTargetPlatformOverride = previous;
      }
    });
  }

  void usePlatform(TargetPlatform platform) {
    debugDefaultTargetPlatformOverride = platform;
  }

  void extendTimeline() {
    controller.dispose();
    final editor = NativeChartEditor.createEmpty();
    for (final row in [0, 32, 160]) {
      addDanceMove(editor, row);
    }
    controller = EditorController(editor: editor);
  }

  Future<void> openEditor(
    WidgetTester tester, {
    Size size = const Size(390, 844),
  }) async {
    await tester.binding.setSurfaceSize(size);
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pumpAndSettle();
  }

  Offset cellAtRow(WidgetTester tester, String column, int row) {
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final header = tester.getRect(
      find.byKey(ValueKey('column-header-$column')),
    );
    return Offset(
      header.center.dx,
      canvas.top +
          editorCursorY(canvas.size) +
          (row - controller.currentRow) * controller.rowSpacing,
    );
  }

  Future<void> tapRow(
    WidgetTester tester,
    String column,
    int row, {
    bool longPress = false,
  }) async {
    controller.moveToRow(row.toDouble());
    await tester.pumpAndSettle();
    final point = cellAtRow(tester, column, row);
    if (longPress) {
      await tester.longPressAt(point);
    } else {
      await tester.tapAt(point);
    }
    await tester.pumpAndSettle();
  }

  Future<void> tapControl(WidgetTester tester, String key) async {
    final finder = find.byKey(ValueKey(key));
    expect(finder, findsOneWidget);
    final button = tester.widget(finder);
    expect(
      button is IconButton
          ? button.onPressed
          : (button as ButtonStyleButton).onPressed,
      isNotNull,
    );
    await tester.tap(finder);
    await tester.pumpAndSettle();
  }

  Future<void> pasteAtCursor(WidgetTester tester, int row) async {
    controller.moveToRow(row.toDouble());
    await tester.pumpAndSettle();
    await tapControl(tester, 'paste-at-current-cursor');
  }

  Future<void> shortcut(
    WidgetTester tester,
    LogicalKeyboardKey key, {
    bool meta = false,
    bool shift = false,
  }) async {
    final modifier = meta
        ? LogicalKeyboardKey.metaLeft
        : LogicalKeyboardKey.controlLeft;
    await tester.sendKeyDownEvent(modifier);
    if (shift) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
    }
    await tester.sendKeyEvent(key);
    if (shift) {
      await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
    }
    await tester.sendKeyUpEvent(modifier);
    await tester.pumpAndSettle();
  }

  VoidCallback? onPressedFor(WidgetTester tester, String key) {
    final target = find.byKey(ValueKey(key));
    expect(target, findsOneWidget);
    final widget = tester.widget(target);
    if (widget is IconButton) return widget.onPressed;
    if (widget is ButtonStyleButton) return widget.onPressed;
    return tester
        .widget<TextButton>(
          find.descendant(of: target, matching: find.byType(TextButton)),
        )
        .onPressed;
  }

  void expectSessionToolbarDisabled(WidgetTester tester) {
    for (final key in [
      'audio-picker',
      'analyze-audio',
      'bpm-menu',
      'offset-menu',
      'toggle-timing-controls',
      'export-audio',
      'rhythm-test',
      'chart-menu',
      'save-chart',
      'clear-chart',
      'export-slk',
      'playback-button',
      'playback-sound-button',
    ]) {
      if (key == 'toggle-timing-controls' &&
          debugDefaultTargetPlatformOverride == TargetPlatform.macOS) {
        // Desktop layouts show timing controls directly, without a toggle.
        expect(find.byKey(ValueKey(key)), findsNothing);
        continue;
      }
      expect(onPressedFor(tester, key), isNull, reason: key);
    }
    expect(controller.generalActionsEnabled, isFalse);
  }

  Future<void> loadFixtureAudio() => controller.loadAudioBytes(
    File('../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3')
        .readAsBytesSync(),
    'fixture.mp3',
  );

  for (final platform in [TargetPlatform.macOS, TargetPlatform.android]) {
    selectionWidgetTest(
      '$platform selection gates general controls and retains navigation',
      (tester) async {
        usePlatform(platform);
        await loadFixtureAudio();
        await openEditor(
          tester,
          size: platform == TargetPlatform.macOS
              ? const Size(600, 844)
              : const Size(390, 844),
        );
        await tapRow(tester, 'NPC', 0, longPress: true);
        expectSessionToolbarDisabled(tester);
        expect(controller.selectionCommandEnabled, isTrue);
        expect(controller.navigationEnabled, isTrue);
        expect(controller.historyNavigationEnabled, isTrue);
        expect(controller.markerInteractionEnabled, isTrue);
        expect(onPressedFor(tester, 'song-time'), isNotNull);
        for (final tooltip in ['跳到开头', '上翻一页', '下翻一页', '跳到末尾']) {
          final button = tester.widget<IconButton>(
            find.ancestor(
              of: find.byTooltip(tooltip),
              matching: find.byType(IconButton),
            ),
          );
          final sessionCallback = button.onPressed;
          controller.exitChartInteraction();
          await tester.pumpAndSettle();
          expect(
            tester
                    .widget<IconButton>(
                      find.ancestor(
                        of: find.byTooltip(tooltip),
                        matching: find.byType(IconButton),
                      ),
                    )
                    .onPressed !=
                null,
            sessionCallback != null,
            reason: tooltip,
          );
          controller.enterSelectionMode();
          controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
          await tester.pumpAndSettle();
        }
        for (final key in [
          'selection-exit',
          'selection-copy',
          'selection-cut',
        ]) {
          expect(onPressedFor(tester, key), isNotNull, reason: key);
        }
      },
    );

    selectionWidgetTest(
      '$platform pending paste retains its placement input and gates toolbars',
      (tester) async {
        usePlatform(platform);
        await loadFixtureAudio();
        await openEditor(
          tester,
          size: platform == TargetPlatform.macOS
              ? const Size(600, 844)
              : const Size(390, 844),
        );
        await tapRow(tester, 'NPC', 0, longPress: true);
        await tapControl(tester, 'selection-copy');
        expectSessionToolbarDisabled(tester);
        expect(onPressedFor(tester, 'selection-exit'), isNotNull);
        controller.moveToRow(16);
        await tester.pumpAndSettle();
        if (platform == TargetPlatform.android) {
          expect(onPressedFor(tester, 'paste-at-current-cursor'), isNotNull);
          expect(controller.pastePreviewAnchorRow, 16);
        } else {
          final mouse = await tester.createGesture(
            kind: PointerDeviceKind.mouse,
          );
          await mouse.addPointer(location: Offset.zero);
          await mouse.moveTo(cellAtRow(tester, 'NPC', 17));
          await tester.pumpAndSettle();
          expect(controller.pastePreviewAnchorRow, 17);
          await mouse.moveTo(cellAtRow(tester, 'NPC', 18));
          await tester.pumpAndSettle();
          expect(controller.pastePreviewAnchorRow, 18);
          await mouse.removePointer();
          await tester.pumpAndSettle();
        }
      },
    );
  }

  test(
    'session controller blacklist preserves project and history exactly',
    () async {
      await loadFixtureAudio();
      final formal = controller.encodeFormalDraft();
      final recovery = controller.encodeRecoverySnapshot();
      final slk = File('../example/z3116_lv5.slk').readAsBytesSync();
      for (final pending in [false, true]) {
        controller.enterSelectionMode();
        controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
        if (pending) controller.copySelection();
        final revision = controller.projectRevision;
        final canUndo = controller.canUndo;
        final canRedo = controller.canRedo;
        final language = controller.playbackSoundLanguage;
        final danceMode = controller.playbackDanceMode;
        final transitionSounds = controller.transitionSoundsEnabled;
        final operations = <String, Future<void> Function()>{
          'setBpm': () async => controller.setBpm(123),
          'setOffset': () async => controller.setOffset(1),
          'clearChart': () async => controller.clearChart(),
          'importSlkBytes': () async =>
              controller.importSlkBytes(slk, 'fixture.slk'),
          'loadFormalDraft': () async => controller.loadFormalDraft(
            payload: formal,
            draftId: 'blocked',
            draftName: '不可替换',
          ),
          'restoreRecovery': () async => controller.restoreRecovery(recovery),
          'clickCell': () async =>
              controller.clickCell(OTGY_CHART_COLUMN_NPC, 0),
          'analyzeAudio': () async {
            await expectLater(controller.analyzeAudio(), throwsStateError);
          },
          'exportSlk': () async {
            expect(controller.exportSlk, throwsStateError);
          },
          'exportAudio': () async {
            var saved = false;
            await expectLater(
              controller.exportAudio(
                quality: 1,
                baseName: 'blocked',
                save: (result, isCancelled, onProgress) async {
                  saved = true;
                },
              ),
              throwsStateError,
            );
            expect(saved, isFalse);
          },
          'setPlaybackSoundLanguage': () async => controller
              .setPlaybackSoundLanguage(PlaybackSoundLanguage.english),
          'setPlaybackDanceMode': () async =>
              controller.setPlaybackDanceMode(PlaybackDanceMode.eightKey),
          'setTransitionSoundsEnabled': () async =>
              controller.setTransitionSoundsEnabled(true),
          'togglePlayback': controller.togglePlayback,
          'toggleRhythmTest': controller.toggleRhythmTest,
        };
        for (final operation in operations.entries) {
          await operation.value();
          expect(controller.projectRevision, revision, reason: operation.key);
          expect(
            controller.encodeFormalDraft(),
            orderedEquals(formal),
            reason: operation.key,
          );
          expect(controller.canUndo, canUndo, reason: operation.key);
          expect(controller.canRedo, canRedo, reason: operation.key);
          expect(
            controller.playbackSoundLanguage,
            language,
            reason: operation.key,
          );
          expect(
            controller.playbackDanceMode,
            danceMode,
            reason: operation.key,
          );
          expect(
            controller.transitionSoundsEnabled,
            transitionSounds,
            reason: operation.key,
          );
          expect(controller.isPlaying, isFalse, reason: operation.key);
          expect(controller.isRhythmTesting, isFalse, reason: operation.key);
          expect(controller.isBusy, isFalse, reason: operation.key);
          expect(controller.isExportingAudio, isFalse, reason: operation.key);
          expect(
            controller.sourceKind,
            WorkspaceSourceKind.untitled,
            reason: operation.key,
          );
          expect(
            controller.chartInteractionMode,
            pending
                ? EditorChartInteractionMode.pastePending
                : EditorChartInteractionMode.selection,
            reason: operation.key,
          );
        }
        controller.exitChartInteraction();
      }
    },
  );

  selectionWidgetTest('session keyboard whitelist and Space playback gate', (
    tester,
  ) async {
    usePlatform(TargetPlatform.macOS);
    await loadFixtureAudio();
    await openEditor(tester);
    final before = controller.encodeFormalDraft();
    await tapRow(tester, 'NPC', 0, longPress: true);
    await tester.sendKeyEvent(LogicalKeyboardKey.space);
    await tester.pumpAndSettle();
    expect(controller.isPlaying, isFalse);
    await shortcut(tester, LogicalKeyboardKey.digit0, meta: true);
    expect(controller.zoom, 10);
    await shortcut(tester, LogicalKeyboardKey.equal, meta: true);
    expect(controller.zoom, 10.25);
    await shortcut(tester, LogicalKeyboardKey.minus, meta: true);
    expect(controller.zoom, 10);
    controller.exitChartInteraction();
    await tester.pumpAndSettle();
    for (final meta in [false, true]) {
      final revision = controller.projectRevision;
      await tapRow(tester, 'NPC', 0, longPress: true);
      await shortcut(tester, LogicalKeyboardKey.keyC, meta: meta);
      expect(controller.pastePending, isTrue);
      expect(controller.hasClipboard, isTrue);
      controller.pasteAtRow(64);
      await tester.pumpAndSettle();
      expect(controller.projectRevision, revision + 1);
      await shortcut(tester, LogicalKeyboardKey.keyZ, meta: meta);
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, revision + 2);
      expect(controller.pastePending, isTrue);
      await tester.sendKeyEvent(LogicalKeyboardKey.space);
      await tester.pumpAndSettle();
      expect(controller.isPlaying, isFalse);
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );
      expect(controller.hasClipboard, isFalse);
      await tapRow(tester, 'NPC', 0, longPress: true);
      await shortcut(tester, LogicalKeyboardKey.keyX, meta: meta);
      expect(controller.pastePending, isTrue);
      expect(controller.cutPending, isTrue);
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );
      expect(controller.hasSelection, isFalse);
      expect(controller.hasClipboard, isFalse);
      expect(controller.cutPending, isFalse);
    }
  });

  selectionWidgetTest(
    'open chart menu disables its entries when a session begins',
    (tester) async {
      usePlatform(TargetPlatform.macOS);
      await openEditor(tester);
      await tapControl(tester, 'chart-menu');
      for (final label in ['导入 SLK', '打开草稿', '导出诊断包（含当前谱面）']) {
        expect(
          tester
              .widget<MenuItemButton>(
                find.widgetWithText(MenuItemButton, label),
              )
              .onPressed,
          isNotNull,
        );
      }
      controller.enterSelectionMode();
      await tester.pumpAndSettle();
      for (final label in ['导入 SLK', '打开草稿', '导出诊断包（含当前谱面）']) {
        expect(
          tester
              .widget<MenuItemButton>(
                find.widgetWithText(MenuItemButton, label),
              )
              .onPressed,
          isNull,
          reason: label,
        );
      }
    },
  );

  selectionWidgetTest('compact Mac status fits through session Zoom changes', (
    tester,
  ) async {
    usePlatform(TargetPlatform.macOS);
    await loadFixtureAudio();
    await openEditor(tester, size: const Size(390, 844));
    await tapRow(tester, 'NPC', 0, longPress: true);
    for (final zoom in [10.0, 10.25, 19.0]) {
      controller.setZoom(zoom);
      await tester.pumpAndSettle();
      expect(controller.zoom, zoom);
      expect(tester.takeException(), isNull);
      final time = tester.getRect(find.byKey(const ValueKey('song-time')));
      expect(time.left, greaterThanOrEqualTo(0));
      expect(time.right, lessThanOrEqualTo(390));
    }
  });

  selectionWidgetTest(
    'open sound menu disables its options during a chart session',
    (tester) async {
      usePlatform(TargetPlatform.macOS);
      await openEditor(tester, size: const Size(1000, 700));
      await tester.tap(find.byKey(const ValueKey('playback-sound-button')));
      await tester.pumpAndSettle();
      final language = find.byKey(const ValueKey('playback-sound-language'));
      final danceMode = find.byKey(const ValueKey('playback-dance-mode'));
      final transition = find.byKey(const ValueKey('transition-sounds-switch'));
      void expectOptions(bool enabled) {
        expect(
          tester
                  .widget<SegmentedButton<PlaybackSoundLanguage>>(language)
                  .onSelectionChanged !=
              null,
          enabled,
        );
        expect(
          tester
                  .widget<SegmentedButton<PlaybackDanceMode>>(danceMode)
                  .onSelectionChanged !=
              null,
          enabled,
        );
        expect(
          tester.widget<SwitchListTile>(transition).onChanged != null,
          enabled,
        );
      }

      expectOptions(true);
      controller.enterSelectionMode();
      await tester.pumpAndSettle();
      expectOptions(false);
      controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
      controller.copySelection();
      await tester.pumpAndSettle();
      expectOptions(false);
      controller.exitChartInteraction();
      await tester.pumpAndSettle();
      expectOptions(true);
    },
  );

  List<Object?> markerGeometry(ChartMarker marker, int firstRow) => [
    marker.row - firstRow,
    marker.column,
    marker.type,
    marker.showNCount,
    marker.ordinal,
  ];

  test('selection band projects the native span and fractional cursor', () {
    const selection = ChartSelection(
      active: true,
      danceMoveDrillId: 7,
      firstRow: 16,
      lastRow: 30,
    );
    const chart = Rect.fromLTWH(0, 28, 390, 600);
    expect(
      projectEditorSelectionBand(
        selection: selection,
        chart: chart,
        cursorY: 160,
        currentRow: 16.25,
        rowSpacing: 20,
      ),
      const Rect.fromLTRB(0, 145, 390, 445),
    );
    expect(
      projectEditorSelectionBand(
        selection: selection,
        chart: chart,
        cursorY: 160,
        currentRow: 100,
        rowSpacing: 20,
      ),
      isNull,
    );
  });

  test('mode exit and draft replacement discard all session state', () {
    final before = controller.encodeFormalDraft();
    controller.copySelection();
    controller.cutSelection();
    controller.pasteAtRow(64);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 0);
    expect(controller.chartInteractionMode, EditorChartInteractionMode.normal);

    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    expect(
      controller.chartInteractionMode,
      EditorChartInteractionMode.pastePending,
    );
    expect(controller.hasClipboard, isTrue);
    controller.exitChartInteraction();
    expect(controller.chartInteractionMode, EditorChartInteractionMode.normal);
    expect(controller.hasSelection, isFalse);
    expect(controller.hasClipboard, isFalse);

    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    controller.loadFormalDraft(
      payload: before,
      draftId: 'blocked-replacement',
      draftName: '会话内不可替换',
    );
    expect(controller.pastePending, isTrue);
    expect(controller.hasSelection, isTrue);
    expect(controller.hasClipboard, isTrue);
    expect(controller.sourceKind, WorkspaceSourceKind.untitled);
    expect(controller.projectRevision, 0);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    controller.exitChartInteraction();
    controller.loadFormalDraft(
      payload: before,
      draftId: 'replacement',
      draftName: '替换草稿',
    );
    expect(controller.chartInteractionMode, EditorChartInteractionMode.normal);
    expect(controller.hasSelection, isFalse);
    expect(controller.hasClipboard, isFalse);
  });

  Future<void> verifyLongPressSelection(
    WidgetTester tester,
    TargetPlatform platform,
  ) async {
    usePlatform(platform);
    await openEditor(
      tester,
      size: platform == TargetPlatform.android
          ? const Size(390, 844)
          : const Size(1000, 700),
    );
    final before = controller.encodeFormalDraft();
    final revision = controller.projectRevision;
    final zoom = controller.zoom;

    await tapRow(tester, '玩家', 0, longPress: true);
    expect(
      controller.chartInteractionMode,
      EditorChartInteractionMode.selection,
    );
    expect(controller.selections.map((item) => item.firstRow), [0]);
    expect(find.byKey(const ValueKey('selection-mode')), findsNothing);

    await tapRow(tester, 'Showtime', 33);
    expect(controller.selections.map((item) => item.firstRow), [0]);
    await tapRow(tester, 'Showtime', 32);
    expect(controller.selections.map((item) => item.firstRow), [0, 32]);
    expect(controller.selections.map((item) => item.lastRow), [14, 46]);
    await tapRow(tester, 'Showtime', 8);
    expect(controller.selections.map((item) => item.firstRow), [32]);
    await tapRow(tester, 'NPC', 8);
    expect(controller.selections.map((item) => item.firstRow), [0, 32]);
    for (final column in ['NPC', 'Showtime', '玩家']) {
      await tapRow(tester, column, 5);
      expect(controller.selections.map((item) => item.firstRow), [0, 32]);
    }
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, revision);
    expect(controller.zoom, zoom);
  }

  selectionWidgetTest('mobile long press enters selection', (tester) async {
    await verifyLongPressSelection(tester, TargetPlatform.android);
  });

  selectionWidgetTest('desktop long press enters selection', (tester) async {
    await verifyLongPressSelection(tester, TargetPlatform.macOS);
  });

  for (final platform in [TargetPlatform.macOS, TargetPlatform.android]) {
    selectionWidgetTest(
      '$platform selects Showtime references across columns without editing',
      (tester) async {
        usePlatform(platform);
        controller.dispose();
        final editor = NativeChartEditor.createEmpty();
        addDanceMove(editor, 0);
        expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
        controller = EditorController(editor: editor);
        await openEditor(tester);
        final before = controller.encodeFormalDraft();
        final revision = controller.projectRevision;
        final canUndo = controller.canUndo;
        final canRedo = controller.canRedo;
        final showD = controller.showtimeMoves.single;
        await tapRow(tester, '玩家', showD.row, longPress: true);
        expect(controller.selections.single.firstRow, 0);
        expect(controller.selections.single.lastRow, 14);
        for (final column in ['NPC', 'Showtime', '玩家']) {
          await tapRow(tester, column, 16);
          expect(controller.selections.single.firstRow, 0);
          expect(find.text('该标记不属于可选择的舞步'), findsOneWidget);
          await tapRow(tester, column, 5);
          expect(controller.selections.single.firstRow, 0);
          await tapRow(tester, column, showD.row);
          expect(controller.selections, isEmpty);
          await tapRow(tester, column, showD.row);
          expect(controller.selections.single.firstRow, 0);
        }
        expect(controller.encodeFormalDraft(), orderedEquals(before));
        expect(controller.projectRevision, revision);
        expect(controller.canUndo, canUndo);
        expect(controller.canRedo, canRedo);
        expect(tester.takeException(), isNull);
      },
    );
  }

  selectionWidgetTest(
    'blank long press enters an empty selection without editing',
    (tester) async {
      usePlatform(TargetPlatform.android);
      controller.clearChart();
      final revision = controller.projectRevision;
      await openEditor(tester);

      await tapRow(tester, 'NPC', 0, longPress: true);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.selection,
      );
      expect(controller.selections, isEmpty);
      expect(controller.markers, isEmpty);
      expect(controller.projectRevision, revision);
      expect(
        tester
            .widget<IconButton>(find.byKey(const ValueKey('selection-copy')))
            .onPressed,
        isNull,
      );
      expect(
        tester
            .widget<IconButton>(find.byKey(const ValueKey('selection-cut')))
            .onPressed,
        isNull,
      );
    },
  );

  selectionWidgetTest('copy stays paste pending across success and conflict', (
    tester,
  ) async {
    usePlatform(TargetPlatform.macOS);
    await openEditor(tester, size: const Size(1000, 700));
    final source = controller.encodeFormalDraft();
    final sourceMarkers = List<ChartMarker>.of(controller.markers);

    await tapRow(tester, 'NPC', 0, longPress: true);
    await tapRow(tester, 'NPC', 32);
    expect(controller.selections.map((item) => item.firstRow), [0, 32]);

    await tapRow(tester, 'NPC', 0);
    expect(controller.selections.map((item) => item.firstRow), [32]);
    await tapRow(tester, 'NPC', 0);
    expect(controller.selections.map((item) => item.firstRow), [0, 32]);

    await shortcut(tester, LogicalKeyboardKey.keyC, meta: true);
    expect(
      controller.chartInteractionMode,
      EditorChartInteractionMode.pastePending,
    );
    expect(controller.clipboardCount, 2);
    expect(find.byKey(const ValueKey('selection-controls')), findsNothing);
    expect(
      find.byKey(const ValueKey('paste-pending-controls')),
      findsOneWidget,
    );
    expect(find.text('点击谱面放置 2 个舞步'), findsOneWidget);
    expect(controller.encodeFormalDraft(), orderedEquals(source));
    expect(controller.projectRevision, 0);

    await shortcut(tester, LogicalKeyboardKey.keyV);
    await shortcut(tester, LogicalKeyboardKey.keyV, meta: true);
    expect(controller.encodeFormalDraft(), orderedEquals(source));
    expect(controller.projectRevision, 0);
    expect(controller.pastePending, isTrue);

    await tapRow(tester, 'NPC', 64);
    expect(controller.projectRevision, 1);
    expect(controller.pastePending, isTrue);
    expect(controller.hasClipboard, isTrue);
    expect(controller.clipboardCount, 2);
    final afterFirst = controller.encodeFormalDraft();
    final firstPaste = controller.markers
        .where((marker) => marker.row >= 64 && marker.row <= 110)
        .toList();
    expect(
      firstPaste.map((marker) => markerGeometry(marker, 64)).toList(),
      sourceMarkers.map((marker) => markerGeometry(marker, 0)).toList(),
    );
    expect(
      firstPaste
          .map((marker) => marker.id)
          .toSet()
          .intersection(sourceMarkers.map((marker) => marker.id).toSet()),
      isEmpty,
    );

    await tapRow(tester, 'NPC', 64);
    expect(controller.encodeFormalDraft(), orderedEquals(afterFirst));
    expect(controller.projectRevision, 1);
    expect(controller.pastePending, isTrue);
    expect(controller.hasClipboard, isTrue);
    expect(find.text('粘贴范围内存在其他标记'), findsOneWidget);

    await tapRow(tester, 'NPC', 112);
    expect(controller.projectRevision, 2);
    expect(controller.pastePending, isTrue);
    final secondPaste = controller.markers
        .where((marker) => marker.row >= 112 && marker.row <= 158)
        .toList();
    expect(
      secondPaste.map((marker) => markerGeometry(marker, 112)).toList(),
      sourceMarkers.map((marker) => markerGeometry(marker, 0)).toList(),
    );
    expect(
      secondPaste
          .map((marker) => marker.id)
          .toSet()
          .intersection(
            [
              ...sourceMarkers,
              ...firstPaste,
            ].map((marker) => marker.id).toSet(),
          ),
      isEmpty,
    );

    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(controller.chartInteractionMode, EditorChartInteractionMode.normal);
    expect(controller.selections, isEmpty);
    expect(controller.hasClipboard, isFalse);
    expect(find.byKey(const ValueKey('paste-pending-controls')), findsNothing);
  });

  selectionWidgetTest(
    'cut deletes source only on its first legal canvas placement',
    (tester) async {
      usePlatform(TargetPlatform.android);
      await openEditor(tester);
      final before = controller.encodeFormalDraft();
      final sourceMarkers = controller.markers
          .where((marker) => marker.row <= 14)
          .toList();
      final sourceIds = sourceMarkers.map((marker) => marker.id).toSet();
      final markerCount = controller.markers.length;

      await tapRow(tester, 'NPC', 0, longPress: true);
      await tapControl(tester, 'selection-cut');
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.pastePending,
      );
      expect(controller.cutPending, isTrue);
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, 0);

      await pasteAtCursor(tester, 32);
      expect(controller.cutPending, isTrue);
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, 0);
      expect(controller.pastePending, isTrue);

      await pasteAtCursor(tester, 64);
      expect(controller.cutPending, isFalse);
      expect(controller.pastePending, isTrue);
      expect(controller.projectRevision, 1);
      expect(
        controller.markers
            .map((marker) => marker.id)
            .toSet()
            .intersection(sourceIds),
        isEmpty,
      );
      expect(controller.markers.where((marker) => marker.row <= 14), isEmpty);
      expect(
        controller.markers
            .where((marker) => marker.row >= 64 && marker.row <= 78)
            .map((marker) => markerGeometry(marker, 64)),
        sourceMarkers.map((marker) => markerGeometry(marker, 0)),
      );

      await tester.pump(const Duration(seconds: 4));
      await tester.pumpAndSettle();
      await tester.tap(find.byTooltip('撤销'));
      await tester.pumpAndSettle();
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.cutPending, isFalse);
      expect(controller.pastePending, isTrue);

      await pasteAtCursor(tester, 64);
      expect(controller.pastePending, isTrue);
      expect(controller.cutPending, isFalse);
      expect(
        controller.markers
            .where((marker) => marker.row <= 14)
            .map((marker) => markerGeometry(marker, 0)),
        sourceMarkers.map((marker) => markerGeometry(marker, 0)),
      );
      expect(controller.markers, hasLength(markerCount + sourceMarkers.length));
    },
  );

  selectionWidgetTest(
    'focused marker keeps Enter activation separate from Shift+Enter long press',
    (tester) async {
      usePlatform(TargetPlatform.macOS);
      final previousHighlightStrategy = FocusManager.instance.highlightStrategy;
      FocusManager.instance.highlightStrategy =
          FocusHighlightStrategy.alwaysTraditional;
      try {
        await openEditor(tester, size: const Size(1000, 700));
        final before = controller.encodeFormalDraft();
        final markerModel = controller.markers.singleWhere(
          (marker) => marker.column == OTGY_CHART_COLUMN_NPC && marker.row == 0,
        );
        final marker = find.byWidgetPredicate(
          (widget) =>
              widget is Semantics &&
              widget.properties.label == 'NPC 起点，第 0 个十六分行',
        );
        expect(marker, findsOneWidget);
        Focus.of(tester.element(marker)).requestFocus();
        await tester.pumpAndSettle();
        expect(
          find.byKey(ValueKey('marker-focus-${markerModel.id}')),
          findsOneWidget,
        );

        await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
        await tester.sendKeyEvent(LogicalKeyboardKey.enter);
        await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
        await tester.pumpAndSettle();
        expect(
          controller.chartInteractionMode,
          EditorChartInteractionMode.selection,
        );
        expect(controller.selections.map((item) => item.firstRow), [0]);
        expect(controller.encodeFormalDraft(), orderedEquals(before));
        expect(controller.projectRevision, 0);

        await tester.sendKeyEvent(LogicalKeyboardKey.escape);
        await tester.pumpAndSettle();
        expect(
          controller.chartInteractionMode,
          EditorChartInteractionMode.normal,
        );
        expect(controller.selections, isEmpty);

        await tester.sendKeyEvent(LogicalKeyboardKey.enter);
        await tester.pumpAndSettle();
        expect(
          controller.chartInteractionMode,
          EditorChartInteractionMode.normal,
        );
        expect(controller.projectRevision, 1);
        expect(controller.encodeFormalDraft(), isNot(orderedEquals(before)));
      } finally {
        FocusManager.instance.highlightStrategy = previousHighlightStrategy;
      }
    },
  );

  selectionWidgetTest(
    'native preview follows hover and the clicked canvas row',
    (tester) async {
      usePlatform(TargetPlatform.macOS);
      await openEditor(tester, size: const Size(1000, 700));
      await tapRow(tester, 'NPC', 0, longPress: true);
      await tapControl(tester, 'selection-copy');
      controller.moveToRow(64);
      await tester.pumpAndSettle();

      final expected = NativeChartEditor.createEmpty();
      addTearDown(expected.dispose);
      addDanceMove(expected, 0);
      addDanceMove(expected, 32);
      expected.select(OTGY_CHART_COLUMN_NPC, 0);
      expected.copySelection();
      final preview = expected.pastePreview(66);
      final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
      await mouse.addPointer(location: Offset.zero);
      await mouse.moveTo(cellAtRow(tester, 'NPC', 66));
      await tester.pumpAndSettle();
      expect(controller.pastePreviewAnchorRow, 66);
      expect(controller.pastePreview!.valid, preview.valid);
      expect(controller.pastePreview!.firstRow, preview.firstRow);
      expect(controller.pastePreview!.lastRow, preview.lastRow);
      expect(
        controller.pastePreview!.selections.map((s) => (s.firstRow, s.lastRow)),
        preview.selections.map((s) => (s.firstRow, s.lastRow)),
      );
      expect(
        controller.pastePreview!.markers.map(
          (marker) => markerGeometry(marker, 66),
        ),
        preview.markers.map((marker) => markerGeometry(marker, 66)),
      );
      expect(controller.projectRevision, 0);

      await mouse.down(cellAtRow(tester, 'NPC', 66));
      await mouse.up();
      await tester.pumpAndSettle();
      expect(controller.currentRow, 64);
      expect(controller.projectRevision, 1);
      expect(controller.pastePreview!.valid, isFalse);
      expect(controller.pastePending, isTrue);
      expect(expected.paste(66).changed, isTrue);
      expect(
        controller.markers
            .where((marker) => marker.row >= 66)
            .map((marker) => markerGeometry(marker, 66)),
        expected
            .markers()
            .where((marker) => marker.row >= 66)
            .map((marker) => markerGeometry(marker, 66)),
      );
      await mouse.removePointer();
      await tester.pumpAndSettle();
      expect(controller.pastePreview, isNull);
    },
  );

  for (final platform in [TargetPlatform.android, TargetPlatform.iOS]) {
    selectionWidgetTest(
      '$platform previews the fixed cursor and only the button submits',
      (tester) async {
        usePlatform(platform);
        extendTimeline();
        await openEditor(tester);
        final canvas = tester.getRect(
          find.byKey(const ValueKey('editor-canvas')),
        );
        await tapRow(tester, 'NPC', 0, longPress: true);
        await tapControl(tester, 'selection-copy');
        expect(controller.pastePreviewAnchorRow, 0);
        expect(controller.pastePreview!.valid, isFalse);
        final button = find.byKey(const ValueKey('paste-at-current-cursor'));
        expect(tester.getSize(button).height, greaterThanOrEqualTo(48));
        expect(canvas.contains(tester.getRect(button).topLeft), isTrue);
        expect(canvas.contains(tester.getRect(button).bottomRight), isTrue);
        expect(tester.widget<FilledButton>(button).onPressed, isNotNull);
        expect(tester.takeException(), isNull);

        controller.moveToRow(64.49);
        await tester.pumpAndSettle();
        expect(controller.pastePreviewAnchorRow, 64);
        expect(controller.pastePreview!.valid, isTrue);
        controller.moveToRow(64.5);
        await tester.pumpAndSettle();
        expect(controller.pastePreviewAnchorRow, 65);
        await controller.setZoom(controller.zoom - 0.25);
        await tester.pumpAndSettle();
        expect(controller.pastePreviewAnchorRow, 65);
        final before = controller.encodeFormalDraft();
        final touch = await tester.startGesture(cellAtRow(tester, 'NPC', 66));
        await tester.pump();
        expect(controller.pastePreviewAnchorRow, 65);
        await touch.cancel();
        await tester.pumpAndSettle();
        await tester.tapAt(cellAtRow(tester, 'NPC', 66));
        await tester.pumpAndSettle();
        final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
        await mouse.addPointer(location: Offset.zero);
        await mouse.moveTo(cellAtRow(tester, 'NPC', 66));
        await mouse.down(cellAtRow(tester, 'NPC', 66));
        await mouse.up();
        await mouse.removePointer();
        await tester.pumpAndSettle();
        expect(controller.pastePreviewAnchorRow, 65);
        expect(controller.projectRevision, 0);
        expect(controller.encodeFormalDraft(), orderedEquals(before));

        // The button must read the latest row, even before its preview frame.
        controller.moveToRow(80.5);
        await tester.tap(button);
        await tester.pumpAndSettle();
        expect(controller.currentRow, 80.5);
        expect(controller.projectRevision, 1);
        expect(controller.markers.any((marker) => marker.row == 81), isTrue);
        expect(controller.pastePreviewAnchorRow, 81);
        expect(controller.pastePreview!.valid, isFalse);
        expect(controller.pastePending, isTrue);
        expect(tester.widget<FilledButton>(button).onPressed, isNotNull);
        final after = controller.encodeFormalDraft();
        await tapControl(tester, 'paste-at-current-cursor');
        expect(controller.encodeFormalDraft(), orderedEquals(after));
        expect(controller.projectRevision, 1);
        expect(find.text('粘贴范围内存在其他标记'), findsOneWidget);

        controller.undo();
        await tester.pumpAndSettle();
        expect(controller.pastePreviewAnchorRow, 81);
        expect(controller.pastePreview!.valid, isTrue);
        controller.redo();
        await tester.pumpAndSettle();
        expect(controller.pastePreview!.valid, isFalse);
        controller.moveToRow(112);
        await tester.pumpAndSettle();
        await tapControl(tester, 'paste-at-current-cursor');
        expect(controller.projectRevision, 4);
        expect(controller.currentRow, 112);
        expect(controller.pastePending, isTrue);
        controller.moveToRow(128);
        controller.exitChartInteraction();
        await tester.pumpAndSettle();
        expect(controller.pastePreview, isNull);
        expect(controller.hasClipboard, isFalse);
      },
    );
  }

  for (final platform in [TargetPlatform.android, TargetPlatform.iOS]) {
    selectionWidgetTest(
      '$platform reprojects the fixed cursor after navigation and zoom',
      (tester) async {
        usePlatform(platform);
        extendTimeline();
        await openEditor(tester, size: const Size(1000, 700));
        await tapRow(tester, 'NPC', 0, longPress: true);
        await tapControl(tester, 'selection-copy');
        expect(controller.pastePreviewAnchorRow, 0);
        expect(controller.pastePreview!.valid, isFalse);

        await controller.setZoom(controller.zoom - 0.25);
        await tester.pumpAndSettle();
        expect(controller.pastePreviewAnchorRow, controller.currentRow.round());
        expect(controller.pastePreview, isNotNull);

        await tester.tap(find.byTooltip('跳到末尾'));
        await tester.pumpAndSettle();
        expect(controller.currentRow, controller.timelineRange.endRow);
        expect(controller.pastePreviewAnchorRow, controller.currentRow.round());
        // The tail move owns the end row, so the envelope there is occupied.
        expect(controller.pastePreview!.valid, isFalse);

        await tester.tap(find.byTooltip('上翻一页'));
        await tester.pumpAndSettle();
        expect(
          controller.currentRow,
          lessThan(controller.timelineRange.endRow),
        );
        expect(controller.pastePreviewAnchorRow, controller.currentRow.round());
        expect(controller.pastePreview, isNotNull);

        await tester.tap(find.byTooltip('跳到开头'));
        await tester.pumpAndSettle();
        expect(controller.currentRow, 0);
        expect(controller.pastePreviewAnchorRow, 0);
        expect(controller.pastePreview!.valid, isFalse);
        expect(controller.pastePending, isTrue);
        expect(tester.takeException(), isNull);
      },
    );
  }

  selectionWidgetTest(
    'mobile anchor stops at the timeline end its own batch created',
    (tester) async {
      usePlatform(TargetPlatform.android);
      await openEditor(tester);
      await tapRow(tester, 'NPC', 0, longPress: true);
      await tapRow(tester, 'NPC', 32);
      await tapControl(tester, 'selection-copy');
      expect(controller.pastePreviewAnchorRow, controller.currentRow.round());
      expect(controller.pastePreview!.valid, isFalse);
      // The default fixture ends on the four-bar base range, so this batch
      // lands on the last row the fixed cursor can reach.
      expect(controller.timelineRange.endRow, 64);
      await pasteAtCursor(tester, 64);
      expect(controller.projectRevision, 1);
      expect(controller.timelineRange.endRow, 110);

      // The timeline end is the last marker row, so the fixed cursor cannot
      // reach the next free row. The button stays enabled and the shared core
      // refuses the batch without touching the session.
      await pasteAtCursor(tester, 112);
      expect(controller.currentRow, 110);
      expect(controller.pastePreviewAnchorRow, 110);
      expect(controller.pastePreview!.valid, isFalse);
      expect(controller.projectRevision, 1);
      expect(controller.pastePending, isTrue);
      expect(controller.hasClipboard, isTrue);
      expect(find.text('粘贴范围内存在其他标记'), findsOneWidget);
    },
  );

  selectionWidgetTest(
    'desktop marker semantics and Enter do not submit pending paste',
    (tester) async {
      usePlatform(TargetPlatform.macOS);
      await openEditor(tester);
      await tapRow(tester, 'NPC', 0, longPress: true);
      await tapControl(tester, 'selection-copy');
      final before = controller.encodeFormalDraft();
      final marker = find.byWidgetPredicate(
        (widget) =>
            widget is Semantics &&
            widget.properties.label == 'NPC 起点，第 0 个十六分行',
      );
      expect(marker, findsOneWidget);
      expect(tester.widget<Semantics>(marker).properties.onTap, isNull);
      Focus.of(tester.element(marker)).requestFocus();
      await tester.pumpAndSettle();
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.pumpAndSettle();
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, 0);
      expect(controller.pastePending, isTrue);
    },
  );

  selectionWidgetTest(
    'mobile marker semantics and Enter cannot bypass cursor submission',
    (tester) async {
      usePlatform(TargetPlatform.android);
      await openEditor(tester);
      await tapRow(tester, 'NPC', 0, longPress: true);
      await tapControl(tester, 'selection-copy');
      final before = controller.encodeFormalDraft();
      final marker = find.byWidgetPredicate(
        (widget) =>
            widget is Semantics &&
            widget.properties.label == 'NPC 起点，第 0 个十六分行',
      );
      expect(marker, findsOneWidget);
      expect(tester.widget<Semantics>(marker).properties.onTap, isNull);
      Focus.of(tester.element(marker)).requestFocus();
      await tester.pumpAndSettle();
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.pumpAndSettle();
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, 0);
      expect(controller.pastePreviewAnchorRow, 0);
      expect(
        find.byWidgetPredicate(
          (widget) =>
              widget is Semantics &&
              widget.properties.label == '第 0 行，当前位置不能粘贴',
        ),
        findsOneWidget,
      );
    },
  );

  selectionWidgetTest(
    'desktop pending paste rejects export because busy and sessions are exclusive',
    (tester) async {
      usePlatform(TargetPlatform.macOS);
      final audio = File(
        '../native/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3',
      ).readAsBytesSync();
      await controller.loadAudioBytes(audio, 'fixture.mp3');
      await openEditor(tester);
      await tapRow(tester, 'NPC', 0, longPress: true);
      await tapControl(tester, 'selection-copy');
      controller.moveToRow(16);
      await tester.pumpAndSettle();

      final point = cellAtRow(tester, 'NPC', 16);
      final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
      await mouse.addPointer(location: point);
      await mouse.moveTo(point);
      await tester.pumpAndSettle();
      final anchorRow = controller.pastePreviewAnchorRow;
      expect(anchorRow, 16);
      expect(controller.pastePreview, isNotNull);

      final preview = controller.pastePreview;
      var saveEntered = false;
      // Busy operations and chart sessions are mutually exclusive: exporting
      // cannot create a busy overlay over a pending paste anymore.
      await expectLater(
        controller.exportAudio(
          quality: 1,
          baseName: 'fixture',
          save: (result, isCancelled, onProgress) async {
            saveEntered = true;
          },
        ),
        throwsStateError,
      );
      await tester.pumpAndSettle();

      expect(saveEntered, isFalse);
      expect(controller.isExportingAudio, isFalse);
      expect(controller.isBusy, isFalse);
      expect(controller.pastePreviewAnchorRow, anchorRow);
      expect(controller.pastePreview, same(preview));
      expect(controller.pastePreview, isNotNull);
      await mouse.removePointer();
      await tester.pumpAndSettle();
      expect(controller.pastePreview, isNull);
      expect(tester.takeException(), isNull);
    },
  );

  selectionWidgetTest(
    'compact desktop reprojects a stationary mouse after navigation and zoom',
    (tester) async {
      usePlatform(TargetPlatform.macOS);
      extendTimeline();
      await openEditor(tester);
      await tapRow(tester, 'NPC', 0, longPress: true);
      await tapControl(tester, 'selection-copy');
      expect(
        find.byKey(const ValueKey('paste-at-current-cursor')),
        findsNothing,
      );
      controller.moveToRow(64);
      await tester.pumpAndSettle();
      final point = cellAtRow(tester, 'NPC', 66);
      final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
      await mouse.addPointer(location: point);
      await mouse.moveTo(point);
      await tester.pumpAndSettle();
      expect(controller.pastePreviewAnchorRow, 66);
      controller.moveToRow(80);
      await tester.pumpAndSettle();
      expect(controller.pastePreviewAnchorRow, 82);
      await controller.setZoom(controller.zoom - 1);
      await tester.pumpAndSettle();
      final canvas = tester.getRect(
        find.byKey(const ValueKey('editor-canvas')),
      );
      final row = projectEditorRow(
        y: point.dy - canvas.top,
        cursorY: editorCursorY(canvas.size),
        currentRow: controller.currentRow,
        rowSpacing: controller.rowSpacing,
      );
      expect(controller.pastePreviewAnchorRow, row);
      await mouse.down(point);
      await mouse.up();
      await tester.pumpAndSettle();
      expect(controller.pastePreview!.valid, isFalse);
      controller.undo();
      await tester.pumpAndSettle();
      expect(controller.pastePreview!.valid, isTrue);
      await mouse.removePointer();
      await tester.pumpAndSettle();
      expect(controller.pastePreview, isNull);
      expect(tester.takeException(), isNull);
    },
  );

  test(
    'pending cut blocks history and rejects a changed source atomically',
    () {
      controller.enterSelectionMode();
      controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
      controller.cutSelection();
      final before = controller.encodeFormalDraft();
      controller.undo();
      controller.redo();
      expect(controller.canUndo, isFalse);
      expect(controller.canRedo, isFalse);
      expect(controller.projectRevision, 0);
      expect(controller.encodeFormalDraft(), orderedEquals(before));

      controller.clickCell(OTGY_CHART_COLUMN_NPC, 0);
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, 0);
      expect(controller.cutPending, isTrue);
      // Ordinary editing is blocked during the session. Mutate the retained
      // native fixture directly to exercise the core's stale-source guard.
      expect(fixtureEditor.click(OTGY_CHART_COLUMN_NPC, 0).changed, isTrue);
      final changed = controller.encodeFormalDraft();
      final recovery = controller.encodeRecoverySnapshot();
      final revision = controller.projectRevision;
      controller.updatePastePreview(64);
      expect(controller.pastePreview!.code, 21);
      controller.pasteAtRow(64);
      expect(controller.notice!.message, '剪切来源已变化，原内容未删除');
      expect(controller.encodeFormalDraft(), orderedEquals(changed));
      expect(controller.encodeRecoverySnapshot(), orderedEquals(recovery));
      expect(controller.projectRevision, revision);
      expect(controller.cutPending, isTrue);
      expect(controller.pastePending, isTrue);
      controller.exitChartInteraction();
      expect(controller.cutPending, isFalse);
      expect(controller.encodeFormalDraft(), orderedEquals(changed));
    },
  );

  selectionWidgetTest(
    'text clipboard shortcuts take priority over chart clipboard',
    (tester) async {
      usePlatform(TargetPlatform.android);
      String? clipboardText;
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        (call) async {
          if (call.method == 'Clipboard.setData') {
            clipboardText = (call.arguments as Map)['text'] as String;
          } else if (call.method == 'Clipboard.getData') {
            return {'text': clipboardText};
          } else if (call.method == 'Clipboard.hasStrings') {
            return {'value': clipboardText?.isNotEmpty ?? false};
          }
          return null;
        },
      );
      addTearDown(
        () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
          SystemChannels.platform,
          null,
        ),
      );
      await openEditor(tester);
      await tapRow(tester, 'NPC', 0, longPress: true);
      final before = controller.encodeFormalDraft();
      controller.exitChartInteraction();
      await tester.pumpAndSettle();

      await tester.tap(find.byKey(const ValueKey('bpm-menu')));
      await tester.pumpAndSettle();
      final input = find.byKey(const ValueKey('bpm-input'));
      await tester.tap(input);
      await tester.pumpAndSettle();
      final field = tester.widget<TextField>(input);
      final textBefore = field.controller!.text;
      field.controller!.selection = TextSelection(
        baseOffset: 0,
        extentOffset: field.controller!.text.length,
      );
      await shortcut(tester, LogicalKeyboardKey.keyC);
      expect(clipboardText, textBefore);
      expect(field.controller!.text, textBefore);
      expect(controller.hasClipboard, isFalse);
      await shortcut(tester, LogicalKeyboardKey.keyX);
      expect(clipboardText, textBefore);
      expect(field.controller!.text, isEmpty);
      expect(controller.hasClipboard, isFalse);
      await shortcut(tester, LogicalKeyboardKey.keyV);
      expect(field.controller!.text, textBefore);
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.projectRevision, 0);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );
      expect(controller.hasSelection, isFalse);
      expect(controller.hasClipboard, isFalse);
      await tester.tapAt(const Offset(2, 2));
      await tester.pumpAndSettle();
      await tapRow(tester, 'NPC', 0, longPress: true);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.selection,
      );
      expect(controller.hasSelection, isTrue);
      expect(controller.hasClipboard, isFalse);
    },
  );
}
