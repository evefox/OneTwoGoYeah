import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/ui/editor_keymap.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  late Directory directory;
  late LocalProjectStore store;
  late EditorController controller;

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-desktop-keys-');
    store = await LocalProjectStore.openAt(directory);
    controller = EditorController();
  });

  tearDown(() async {
    controller.dispose();
    if (await directory.exists()) {
      await directory.delete(recursive: true);
    }
  });

  void useTwoMoves() {
    controller.dispose();
    final editor = NativeChartEditor.createEmpty();
    addDanceMove(editor, 0);
    addDanceMove(editor, 32);
    controller = EditorController(editor: editor);
  }

  /// Retries a shortcut until its dialog appears. The screen opens its draft
  /// store with real file I/O, which advances one step per runAsync cycle.
  Future<void> pressUntilVisible(
    WidgetTester tester,
    Future<void> Function() press,
    Finder target,
  ) async {
    for (var attempt = 0; attempt < 20; attempt++) {
      await press();
      if (target.evaluate().isNotEmpty) {
        return;
      }
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 10)),
      );
      await tester.pump();
    }
  }

  Future<void> openEditor(WidgetTester tester) async {
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pumpAndSettle();
  }

  Future<void> press(
    WidgetTester tester,
    LogicalKeyboardKey key, {
    bool control = false,
    bool meta = false,
    bool shift = false,
  }) async {
    if (control) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
    }
    if (meta) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.metaLeft);
    }
    if (shift) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
    }
    await tester.sendKeyEvent(key);
    if (shift) {
      await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
    }
    if (meta) {
      await tester.sendKeyUpEvent(LogicalKeyboardKey.metaLeft);
    }
    if (control) {
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
    }
    await tester.pumpAndSettle();
  }

  Future<void> secondaryTap(WidgetTester tester) async {
    await tester.tapAt(
      tester.getCenter(find.byKey(const ValueKey('editor-canvas'))),
      kind: PointerDeviceKind.mouse,
      buttons: kSecondaryButton,
    );
    await tester.pumpAndSettle();
  }

  testWidgets('Ctrl+A selects every move and enters selection mode', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    useTwoMoves();
    try {
      await openEditor(tester);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );

      await press(tester, LogicalKeyboardKey.keyA, meta: true);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.selection,
      );
      expect(controller.selectionCount, 2);

      await press(tester, LogicalKeyboardKey.keyA, meta: true);
      expect(controller.selectionCount, 2);

      await press(tester, LogicalKeyboardKey.home, meta: true);
      expect(controller.currentRow, 0);
      await press(tester, LogicalKeyboardKey.end, meta: true);
      expect(controller.currentRow, 46);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('right click clears the selection and exits an empty session', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    useTwoMoves();
    try {
      await openEditor(tester);
      await press(tester, LogicalKeyboardKey.keyA, meta: true);
      expect(controller.selectionCount, 2);

      // The first right click only clears the selection.
      await secondaryTap(tester);
      expect(controller.selectionCount, 0);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.selection,
      );

      // With nothing selected the same gesture leaves the session.
      await secondaryTap(tester);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );

      // A pending paste exits directly.
      await press(tester, LogicalKeyboardKey.keyA, meta: true);
      controller.copySelection();
      await tester.pumpAndSettle();
      expect(controller.pastePending, isTrue);
      await secondaryTap(tester);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('Ctrl+O opens the draft list', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      final dialog = find.text('本地草稿');
      await pressUntilVisible(
        tester,
        () => press(tester, LogicalKeyboardKey.keyO, meta: true),
        dialog,
      );
      expect(dialog, findsOneWidget);
      await tester.tap(find.text('关闭'));
      await tester.pumpAndSettle();
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('an empty chart cannot be saved by shortcut', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await press(tester, LogicalKeyboardKey.keyS, meta: true);
      expect(find.text('保存草稿'), findsNothing);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('save shortcut opens the naming dialog with chart content', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      expect(controller.placeNpcMarker(ChartNpcMarker.blockStart, 0), isTrue);
      await tester.pumpAndSettle();
      final dialog = find.text('保存草稿');
      await pressUntilVisible(
        tester,
        () => press(tester, LogicalKeyboardKey.keyS, meta: true),
        dialog,
      );
      expect(dialog, findsOneWidget);
      await tester.tap(find.text('取消'));
      await tester.pumpAndSettle();
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('save-as shortcut opens the naming dialog with chart content', (
    tester,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      expect(controller.placeNpcMarker(ChartNpcMarker.blockStart, 0), isTrue);
      await tester.pumpAndSettle();
      final dialog = find.text('保存草稿');
      await pressUntilVisible(
        tester,
        () => press(tester, LogicalKeyboardKey.keyS, meta: true, shift: true),
        dialog,
      );
      expect(dialog, findsOneWidget);
      await tester.tap(find.text('取消'));
      await tester.pumpAndSettle();
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('F1 opens the usage guide', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      await press(tester, LogicalKeyboardKey.f1);
      expect(find.byKey(const ValueKey('usage-guide-screen')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('usage-guide-close')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('usage-guide-screen')), findsNothing);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('scale shortcuts reuse the navigation gate', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await openEditor(tester);
      final before = controller.scale;

      await press(tester, LogicalKeyboardKey.numpadAdd, meta: true);
      expect(controller.scale, closeTo(before + 0.25, 0.0001));

      await press(tester, LogicalKeyboardKey.numpadSubtract, meta: true);
      expect(controller.scale, closeTo(before, 0.0001));
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  testWidgets('mobile ignores the desktop entry shortcuts', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    useTwoMoves();
    try {
      await openEditor(tester);
      final scale = controller.scale;

      await press(tester, LogicalKeyboardKey.keyA, control: true);
      expect(controller.selectionCount, 0);
      expect(
        controller.chartInteractionMode,
        EditorChartInteractionMode.normal,
      );

      await press(tester, LogicalKeyboardKey.keyO, control: true);
      expect(find.text('本地草稿'), findsNothing);

      await press(tester, LogicalKeyboardKey.f1);
      expect(find.byKey(const ValueKey('usage-guide-screen')), findsNothing);

      await press(tester, LogicalKeyboardKey.numpadAdd, control: true);
      expect(controller.scale, scale);
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });

  test('desktop entry defaults follow ArrowVortex', () {
    const windows = TargetPlatform.windows;
    final win = EditorKeymap.defaults(windows);
    expect(
      win.bindingsFor(EditorShortcutAction.selectAll).single.keyId,
      LogicalKeyboardKey.keyA.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.cursorSelectionStart).single.keyId,
      LogicalKeyboardKey.home.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.cursorSelectionEnd).single.keyId,
      LogicalKeyboardKey.end.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.scaleIn).single.keyId,
      LogicalKeyboardKey.numpadAdd.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.scaleOut).single.keyId,
      LogicalKeyboardKey.numpadSubtract.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.openDraft).single.keyId,
      LogicalKeyboardKey.keyO.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.saveDraft).single.keyId,
      LogicalKeyboardKey.keyS.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.saveDraftAs).single.keyId,
      LogicalKeyboardKey.keyS.keyId,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.saveDraftAs).single.shift,
      isTrue,
    );
    expect(
      win.bindingsFor(EditorShortcutAction.openGuide).single.keyId,
      LogicalKeyboardKey.f1.keyId,
    );
  });
}

void addDanceMove(NativeChartEditor editor, int firstRow) {
  for (final row in [0, 2, 4, 6, 6]) {
    expect(editor.click(OTGY_CHART_COLUMN_NPC, firstRow + row).changed, isTrue);
  }
  for (final row in [8, 10]) {
    expect(
      editor.click(OTGY_CHART_COLUMN_PLAYER, firstRow + row).changed,
      isTrue,
    );
  }
}
