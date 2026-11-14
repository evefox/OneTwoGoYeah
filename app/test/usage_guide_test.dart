import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_ui_preferences.dart';
import 'package:one_two_go_yeah/src/ui/editor_keymap.dart';
import 'package:one_two_go_yeah/src/ui/usage_guide_content.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  late Directory temporaryDirectory;
  late LocalProjectStore store;
  late EditorController controller;

  setUp(() async {
    temporaryDirectory = await Directory.systemTemp.createTemp(
      'otgy-usage-guide-',
    );
    store = await LocalProjectStore.openAt(temporaryDirectory);
    controller = EditorController();
  });

  tearDown(() async {
    controller.dispose();
    if (await temporaryDirectory.exists()) {
      await temporaryDirectory.delete(recursive: true);
    }
  });

  Future<void> pumpEditor(
    WidgetTester tester, {
    Size size = const Size(1280, 800),
    LocalUiPreferences? preferences,
  }) async {
    await tester.binding.setSurfaceSize(size);
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(
        controller: controller,
        storeFactory: () async => store,
        preferences: preferences,
      ),
    );
    await tester.pump();
  }

  Future<void> settle(WidgetTester tester) async {
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
  }

  Future<void> openGuide(WidgetTester tester) async {
    await tester.tap(find.byKey(const ValueKey('usage-guide-button')));
    await settle(tester);
  }

  Future<void> openShortcutTopic(WidgetTester tester) async {
    await openGuide(tester);
    await tester.tap(find.byKey(const ValueKey('usage-guide-bottom-toggle')));
    await settle(tester);
    await tester.tap(find.byKey(const ValueKey('usage-guide-topic-1')));
    await settle(tester);
  }

  test('guide slides use bundled WebP images', () {
    for (final slide in usageGuideSlides) {
      for (final asset in [slide.desktopAsset, slide.mobileAsset]) {
        expect(asset.endsWith('.webp'), isTrue, reason: asset);
        expect(File(asset).existsSync(), isTrue, reason: asset);
      }
    }
    for (final platform in ['desktop', 'mobile']) {
      final pngs = Directory('assets/guide/$platform')
          .listSync()
          .whereType<File>()
          .where((file) => file.path.endsWith('.png'));
      expect(pngs, isEmpty, reason: 'PNG sources belong in guide_source');
    }
  });

  testWidgets('opens full-screen help without touching the draft', (
    tester,
  ) async {
    await pumpEditor(tester);

    expect(find.byKey(const ValueKey('usage-guide-button')), findsOneWidget);
    final revision = controller.projectRevision;
    expect(controller.isDirty, isFalse);

    await openGuide(tester);

    expect(find.byKey(const ValueKey('usage-guide-screen')), findsOneWidget);
    expect(find.byKey(const ValueKey('usage-guide-carousel')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('usage-guide-bottom-toggle')),
      findsOneWidget,
    );
    for (var index = 0; index < 6; index++) {
      expect(find.byKey(ValueKey('usage-guide-dot-$index')), findsOneWidget);
    }
    final firstSlide = tester.widget<Image>(
      find.byKey(const ValueKey('usage-guide-slide-0')),
    );
    final mobile =
        defaultTargetPlatform == TargetPlatform.iOS ||
        defaultTargetPlatform == TargetPlatform.android;
    expect(
      (firstSlide.image as AssetImage).assetName,
      mobile
          ? 'assets/guide/mobile/guide_overview.webp'
          : 'assets/guide/desktop/guide_overview.webp',
    );
    expect(find.text('左右滑动查看各区域'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('usage-guide-next')));
    await settle(tester);
    expect(
      tester
          .widget<PageView>(find.byKey(const ValueKey('usage-guide-carousel')))
          .controller!
          .page,
      closeTo(1, 0.01),
    );

    await tester.tap(find.byKey(const ValueKey('usage-guide-bottom-toggle')));
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-cards')), findsOneWidget);
    for (var index = 0; index < 3; index++) {
      expect(find.byKey(ValueKey('usage-guide-topic-$index')), findsOneWidget);
    }

    await tester.tap(find.byKey(const ValueKey('usage-guide-topic-0')));
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-detail')), findsOneWidget);
    expect(find.text('如何复制与粘贴'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('usage-guide-close')));
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-screen')), findsOneWidget);
    expect(find.byKey(const ValueKey('usage-guide-cards')), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('usage-guide-close')));
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-screen')), findsNothing);
    expect(controller.projectRevision, revision);
  });

  testWidgets('swiping down opens the extra help cards', (tester) async {
    await pumpEditor(tester);
    await openGuide(tester);

    await tester.drag(
      find.byKey(const ValueKey('usage-guide-vertical')),
      const Offset(0, -600),
    );
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-cards')), findsOneWidget);

    await tester.drag(
      find.byKey(const ValueKey('usage-guide-vertical')),
      const Offset(0, 600),
    );
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-carousel')), findsOneWidget);
  });

  testWidgets('compact layout keeps the images and vertical cards', (
    tester,
  ) async {
    await pumpEditor(tester, size: const Size(480, 760));
    await openGuide(tester);

    expect(find.byKey(const ValueKey('usage-guide-carousel')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('usage-guide-bottom-toggle')));
    await settle(tester);
    for (var index = 0; index < 3; index++) {
      expect(find.byKey(ValueKey('usage-guide-topic-$index')), findsOneWidget);
    }

    await tester.tap(find.byKey(const ValueKey('usage-guide-topic-2')));
    await settle(tester);
    expect(find.text('常见问题'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('usage-guide-close')));
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-cards')), findsOneWidget);
  });

  testWidgets('the gesture slide uses the blank editing-grid image', (
    tester,
  ) async {
    await pumpEditor(tester);
    await openGuide(tester);

    final carousel = tester.widget<PageView>(
      find.byKey(const ValueKey('usage-guide-carousel')),
    );
    carousel.controller!.jumpToPage(4);
    await settle(tester);

    final slide = tester.widget<Image>(
      find.byKey(const ValueKey('usage-guide-slide-4')),
    );
    final mobile =
        defaultTargetPlatform == TargetPlatform.iOS ||
        defaultTargetPlatform == TargetPlatform.android;
    expect(
      (slide.image as AssetImage).assetName,
      mobile
          ? 'assets/guide/mobile/guide_gestures.webp'
          : 'assets/guide/desktop/guide_gestures.webp',
    );
    expect(find.text('双指调整网格间距；长按选择后可复制或剪切'), findsOneWidget);
  });

  testWidgets('guide entry is disabled while a selection session is active', (
    tester,
  ) async {
    await pumpEditor(tester);
    controller.enterSelectionMode();
    await tester.pump();

    final button = tester.widget<IconButton>(
      find.byKey(const ValueKey('usage-guide-button')),
    );
    expect(button.onPressed, isNull);
    expect(
      tester
          .widget<Tooltip>(find.byKey(const ValueKey('usage-guide-tooltip')))
          .message,
      '请先退出选择或粘贴模式',
    );

    controller.exitChartInteraction();
    await tester.pump();
    expect(
      tester
          .widget<IconButton>(find.byKey(const ValueKey('usage-guide-button')))
          .onPressed,
      isNotNull,
    );
  });

  testWidgets('first launch keeps the help reminder for two minutes', (
    tester,
  ) async {
    final preferences = LocalUiPreferences.inMemory();
    await pumpEditor(tester, preferences: preferences);

    expect(find.byKey(const ValueKey('usage-guide-hint')), findsOneWidget);
    expect(preferences.guideHintSeen, isTrue);

    await tester.pump(const Duration(minutes: 1, seconds: 59));
    expect(find.byKey(const ValueKey('usage-guide-hint')), findsOneWidget);

    await tester.pump(const Duration(seconds: 1));
    await tester.pump();
    expect(find.byKey(const ValueKey('usage-guide-hint')), findsNothing);
  });

  testWidgets('returning launch dismisses the reminder after five seconds', (
    tester,
  ) async {
    await pumpEditor(
      tester,
      preferences: LocalUiPreferences.inMemory(guideHintSeen: true),
    );

    expect(find.byKey(const ValueKey('usage-guide-hint')), findsOneWidget);
    await tester.pump(const Duration(seconds: 4));
    expect(find.byKey(const ValueKey('usage-guide-hint')), findsOneWidget);
    await tester.pump(const Duration(seconds: 1));
    await tester.pump();
    expect(find.byKey(const ValueKey('usage-guide-hint')), findsNothing);
  });

  testWidgets('opening the guide dismisses the reminder immediately', (
    tester,
  ) async {
    await pumpEditor(tester, preferences: LocalUiPreferences.inMemory());

    expect(find.byKey(const ValueKey('usage-guide-hint')), findsOneWidget);
    await openGuide(tester);
    expect(find.byKey(const ValueKey('usage-guide-hint')), findsNothing);

    await tester.tap(find.byKey(const ValueKey('usage-guide-close')));
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-hint')), findsNothing);
  });

  testWidgets('the reminder is not shown for a project with chart content', (
    tester,
  ) async {
    final editor = NativeChartEditor.createEmpty();
    expect(editor.click(OTGY_CHART_COLUMN_NPC, 0).changed, isTrue);
    controller.dispose();
    controller = EditorController(editor: editor);

    await pumpEditor(tester, preferences: LocalUiPreferences.inMemory());

    expect(find.byKey(const ValueKey('usage-guide-hint')), findsNothing);
  });

  testWidgets('desktop shortcut page lists desktop controls only', (
    tester,
  ) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
    try {
      await pumpEditor(tester);
      await openShortcutTopic(tester);

      expect(
        find.byKey(const ValueKey('shortcut-row-playPause')),
        findsOneWidget,
      );
      expect(find.text('⌘Z 或 ⌃Z'), findsOneWidget);
      expect(find.text('鼠标中键'), findsOneWidget);
      expect(find.text('单指拖动'), findsNothing);
      expect(find.text('双指捏合'), findsNothing);
      expect(find.text('长按'), findsNothing);
    } finally {
      debugDefaultTargetPlatformOverride = null;
    }
  });

  testWidgets('mobile shortcut page lists touch controls only', (tester) async {
    await pumpEditor(tester);
    await openShortcutTopic(tester);

    expect(find.text('单指拖动'), findsOneWidget);
    expect(find.text('双指捏合'), findsOneWidget);
    expect(find.byKey(const ValueKey('shortcut-row-playPause')), findsNothing);
    expect(find.text('鼠标中键'), findsNothing);
    expect(find.byKey(const ValueKey('shortcut-reset-all')), findsNothing);
  });

  testWidgets('rebinding a shortcut updates the list and the preferences', (
    tester,
  ) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    final preferences = LocalUiPreferences.inMemory();
    try {
      await pumpEditor(tester, preferences: preferences);
      await openShortcutTopic(tester);

      await tester.tap(find.byKey(const ValueKey('shortcut-row-undo')));
      await settle(tester);
      expect(
        find.byKey(const ValueKey('shortcut-capture-dialog')),
        findsOneWidget,
      );

      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.f9);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await settle(tester);
      expect(find.text('Ctrl + F9'), findsOneWidget);

      await tester.tap(find.byKey(const ValueKey('shortcut-capture-apply')));
      await settle(tester);

      expect(find.text('Ctrl + F9'), findsOneWidget);
      expect(find.text('Ctrl + Z'), findsNothing);
      final stored =
          preferences.shortcuts!['bindings']! as Map<String, Object?>;
      expect(stored['undo'], [
        {'key': LogicalKeyboardKey.f9.keyId, 'ctrl': true},
      ]);
    } finally {
      debugDefaultTargetPlatformOverride = null;
    }
  });

  testWidgets('a conflicting chord is rejected before applying', (
    tester,
  ) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    try {
      await pumpEditor(tester);
      await openShortcutTopic(tester);

      await tester.tap(find.byKey(const ValueKey('shortcut-row-undo')));
      await settle(tester);
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.keyY);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await settle(tester);

      expect(find.text('该组合已用于「重做」'), findsOneWidget);
      expect(
        tester
            .widget<FilledButton>(
              find.byKey(const ValueKey('shortcut-capture-apply')),
            )
            .onPressed,
        isNull,
      );
    } finally {
      debugDefaultTargetPlatformOverride = null;
    }
  });

  testWidgets('restore defaults puts the listed shortcuts back', (
    tester,
  ) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    final custom = EditorKeymap.defaults(TargetPlatform.windows).withBindings(
      EditorShortcutAction.zoomIn,
      [ShortcutBinding(keyId: LogicalKeyboardKey.f9.keyId)],
    );
    try {
      await pumpEditor(
        tester,
        preferences: LocalUiPreferences.inMemory(shortcuts: custom.toJson()),
      );
      await openShortcutTopic(tester);

      expect(find.text('F9'), findsOneWidget);
      final reset = find.byKey(const ValueKey('shortcut-reset-all'));
      await tester.ensureVisible(reset);
      await tester.pump();
      await tester.tap(reset);
      await settle(tester);
      expect(find.text('小键盘 +'), findsOneWidget);
      expect(find.text('F9'), findsNothing);
    } finally {
      debugDefaultTargetPlatformOverride = null;
    }
  });

  testWidgets('a rebound zoom key dispatches the same zoom command', (
    tester,
  ) async {
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    final keymap = EditorKeymap.defaults(TargetPlatform.windows).withBindings(
      EditorShortcutAction.zoomIn,
      [ShortcutBinding(keyId: LogicalKeyboardKey.f9.keyId)],
    );
    try {
      await pumpEditor(
        tester,
        preferences: LocalUiPreferences.inMemory(shortcuts: keymap.toJson()),
      );
      final before = controller.zoom;

      await tester.sendKeyEvent(LogicalKeyboardKey.f9);
      await tester.pump();
      expect(controller.zoom, before + 0.25);

      // The replaced default no longer fires.
      await tester.sendKeyEvent(LogicalKeyboardKey.numpadAdd);
      await tester.pump();
      expect(controller.zoom, before + 0.25);
    } finally {
      debugDefaultTargetPlatformOverride = null;
    }
  });
}
