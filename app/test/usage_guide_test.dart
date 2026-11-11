import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_ui_preferences.dart';
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
}
