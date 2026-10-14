import 'dart:io';

import 'package:flutter/foundation.dart';
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

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-selection-widget-');
    store = await LocalProjectStore.openAt(directory);
    final editor = NativeChartEditor.createEmpty();
    for (final row in [0, 2, 4, 6, 6]) {
      expect(editor.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    for (final row in [8, 10]) {
      expect(editor.click(OTGY_CHART_COLUMN_PLAYER, row).changed, isTrue);
    }
    controller = EditorController(editor: editor);
  });

  tearDown(() async {
    controller.dispose();
    await directory.delete(recursive: true);
  });

  Future<void> openEditor(WidgetTester tester) async {
    await tester.binding.setSurfaceSize(const Size(390, 844));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pumpAndSettle();
  }

  Offset cellAtCursor(WidgetTester tester, String column) {
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final header = tester.getRect(
      find.byKey(ValueKey('column-header-$column')),
    );
    return Offset(header.center.dx, canvas.top + editorCursorY(canvas.size));
  }

  Future<void> shortcut(
    WidgetTester tester,
    LogicalKeyboardKey key, {
    bool meta = false,
  }) async {
    final modifier = meta
        ? LogicalKeyboardKey.metaLeft
        : LogicalKeyboardKey.controlLeft;
    await tester.sendKeyDownEvent(modifier);
    await tester.sendKeyEvent(key);
    await tester.sendKeyUpEvent(modifier);
    await tester.pumpAndSettle();
  }

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

  test('mode exit and draft replacement discard session state', () {
    final before = controller.encodeFormalDraft();
    controller.copySelection();
    controller.cutSelection();
    controller.pasteAtCursor();
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 0);
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    expect(controller.canPaste, isTrue);
    controller.exitSelectionMode();
    expect(controller.selectionMode, isFalse);
    expect(controller.hasSelection, isFalse);
    controller.enterSelectionMode();
    expect(controller.canPaste, isFalse);
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    controller.loadFormalDraft(
      payload: before,
      draftId: 'replacement',
      draftName: '替换草稿',
    );
    expect(controller.selectionMode, isFalse);
    expect(controller.hasSelection, isFalse);
    controller.enterSelectionMode();
    expect(controller.canPaste, isFalse);
  });

  testWidgets('empty selection clicks never create chart content', (
    tester,
  ) async {
    controller.clearChart();
    final revision = controller.projectRevision;
    await openEditor(tester);
    await tester.tap(find.byKey(const ValueKey('selection-mode')));
    await tester.pumpAndSettle();
    expect(controller.selectionMode, isTrue);
    expect(find.byTooltip('退出选择'), findsOneWidget);
    await tester.tapAt(cellAtCursor(tester, 'NPC'));
    await tester.pumpAndSettle();
    expect(controller.markers, isEmpty);
    expect(controller.projectRevision, revision);
    expect(
      tester
          .widget<IconButton>(find.byKey(const ValueKey('selection-copy')))
          .onPressed,
      isNull,
    );
  });

  testWidgets('clicks, semantics and shortcuts share whole-move commands', (
    tester,
  ) async {
    await openEditor(tester);
    final before = controller.encodeFormalDraft();
    await tester.tap(find.byKey(const ValueKey('selection-mode')));
    await tester.pumpAndSettle();
    await tester.tapAt(cellAtCursor(tester, 'NPC'));
    await tester.pumpAndSettle();
    expect(controller.selection.firstRow, 0);
    expect(controller.selection.lastRow, 14);

    // Use the actual marker node rather than the canvas accessibility label.
    final semantics = find.byWidgetPredicate(
      (widget) =>
          widget is Semantics &&
          widget.properties.onTap != null &&
          widget.properties.label?.startsWith('NPC') == true,
    );
    expect(semantics, findsWidgets);
    tester.widget<Semantics>(semantics.first).properties.onTap!();
    await tester.pumpAndSettle();
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 0);
    expect(controller.isDirty, isFalse);

    await shortcut(tester, LogicalKeyboardKey.keyC);
    expect(controller.canPaste, isTrue);
    expect(controller.projectRevision, 0);
    controller.moveToRow(32);
    await tester.pumpAndSettle();
    await shortcut(tester, LogicalKeyboardKey.keyV, meta: true);
    expect(controller.selection.firstRow, 32);
    expect(controller.projectRevision, 1);
    final pasted = controller.encodeFormalDraft();
    await shortcut(tester, LogicalKeyboardKey.keyV);
    expect(controller.projectRevision, 1);
    expect(controller.encodeFormalDraft(), orderedEquals(pasted));
    expect(find.text('粘贴范围内存在其他标记'), findsOneWidget);
    await shortcut(tester, LogicalKeyboardKey.keyX, meta: true);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 2);
    await shortcut(tester, LogicalKeyboardKey.keyZ);
    expect(controller.encodeFormalDraft(), orderedEquals(pasted));
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(controller.selectionMode, isFalse);
    expect(controller.hasSelection, isFalse);
    expect(find.byKey(const ValueKey('selection-copy')), findsNothing);
    controller.enterSelectionMode();
    expect(controller.canPaste, isFalse);
  });

  testWidgets('toolbar focus supports selection clipboard shortcuts', (
    tester,
  ) async {
    await openEditor(tester);
    final before = controller.encodeFormalDraft();
    bool buttonHasFocus() =>
        FocusManager.instance.primaryFocus?.context
            ?.findAncestorWidgetOfExactType<IconButton>()
            ?.key ==
        const ValueKey('selection-mode');
    for (var step = 0; step < 40; step++) {
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pumpAndSettle();
      if (buttonHasFocus()) {
        break;
      }
    }
    expect(buttonHasFocus(), isTrue);
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    expect(controller.selectionMode, isTrue);
    expect(buttonHasFocus(), isTrue);
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    await tester.pumpAndSettle();
    await shortcut(tester, LogicalKeyboardKey.keyC);
    expect(controller.canPaste, isTrue);
    controller.moveToRow(32);
    await shortcut(tester, LogicalKeyboardKey.keyV, meta: true);
    expect(controller.selection.firstRow, 32);
    expect(controller.projectRevision, 1);
    await shortcut(tester, LogicalKeyboardKey.keyX);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 2);
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(controller.selectionMode, isFalse);
    expect(controller.hasSelection, isFalse);
    controller.enterSelectionMode();
    expect(controller.canPaste, isFalse);
  });

  testWidgets('text clipboard shortcuts do not edit selected chart content', (
    tester,
  ) async {
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
    controller.enterSelectionMode();
    controller.selectAt(OTGY_CHART_COLUMN_NPC, 0);
    controller.copySelection();
    final before = controller.encodeFormalDraft();
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
    await shortcut(tester, LogicalKeyboardKey.keyX);
    expect(field.controller!.text, isEmpty);
    await shortcut(tester, LogicalKeyboardKey.keyV);
    expect(field.controller!.text, textBefore);
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.projectRevision, 0);
    expect(controller.hasSelection, isTrue);
  });

  testWidgets('Android long press enters and updates selection without edits', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    try {
      await openEditor(tester);
      final before = controller.encodeFormalDraft();
      final zoom = controller.zoom;
      await tester.longPressAt(cellAtCursor(tester, 'NPC'));
      await tester.pumpAndSettle();
      expect(controller.selectionMode, isTrue);
      expect(controller.selection.lastRow, 14);
      controller.moveToRow(8);
      await tester.pumpAndSettle();
      await tester.longPressAt(cellAtCursor(tester, '玩家'));
      await tester.pumpAndSettle();
      expect(controller.selectionMode, isTrue);
      expect(controller.selection.firstRow, 0);
      expect(controller.selection.lastRow, 14);
      expect(controller.encodeFormalDraft(), orderedEquals(before));
      expect(controller.zoom, zoom);
      expect(controller.scale, 4);
      expect(controller.projectRevision, 0);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });
}
