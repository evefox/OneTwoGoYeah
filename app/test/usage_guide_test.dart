import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';

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
  }) async {
    await tester.binding.setSurfaceSize(size);
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
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
    for (var index = 0; index < 5; index++) {
      expect(
        find.byKey(ValueKey('usage-guide-dot-$index')),
        findsOneWidget,
      );
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
          ? 'assets/guide/mobile/guide_overview.png'
          : 'assets/guide/desktop/guide_overview.png',
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
      expect(
        find.byKey(ValueKey('usage-guide-topic-$index')),
        findsOneWidget,
      );
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
      expect(
        find.byKey(ValueKey('usage-guide-topic-$index')),
        findsOneWidget,
      );
    }

    await tester.tap(find.byKey(const ValueKey('usage-guide-topic-2')));
    await settle(tester);
    expect(find.text('常见问题'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('usage-guide-close')));
    await settle(tester);
    expect(find.byKey(const ValueKey('usage-guide-cards')), findsOneWidget);
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
    expect(button.tooltip, '请先退出选择或粘贴模式');

    controller.exitChartInteraction();
    await tester.pump();
    expect(
      tester
          .widget<IconButton>(
            find.byKey(const ValueKey('usage-guide-button')),
          )
          .onPressed,
      isNotNull,
    );
  });
}
