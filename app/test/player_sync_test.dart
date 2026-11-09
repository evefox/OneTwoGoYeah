import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:otgy_native/otgy_native.dart';

const clearedMessage = '演示已修改；对应练习无法自动修正，已清空练习 N/S，保留起始标记。';

EditorController playerController() {
  final source = NativeChartEditor.createEmpty();
  try {
    for (final row in [0, 10, 14, 18, 18]) {
      expect(source.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    expect(source.click(OTGY_CHART_COLUMN_PLAYER, 20).changed, isTrue);
    expect(source.click(OTGY_CHART_COLUMN_PLAYER, 30).changed, isTrue);
    return EditorController(
      editor: NativeChartEditor.decodeRecovery(source.encodeRecovery()),
    );
  } finally {
    source.dispose();
  }
}

/// A shorter player offset makes inserting an earlier NPC-N project onto the
/// PLY-C row, which is one of the remaining safe-clear fallbacks.
EditorController collapsingPlayerController() {
  final source = NativeChartEditor.createEmpty();
  try {
    for (final row in [0, 10, 14, 18, 18]) {
      expect(source.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    expect(source.click(OTGY_CHART_COLUMN_PLAYER, 20).changed, isTrue);
    expect(source.click(OTGY_CHART_COLUMN_PLAYER, 22).changed, isTrue);
    return EditorController(
      editor: NativeChartEditor.decodeRecovery(source.encodeRecovery()),
    );
  } finally {
    source.dispose();
  }
}

Iterable<int> playerRows(EditorController controller) => controller.markers
    .where((marker) => marker.column == OTGY_CHART_COLUMN_PLAYER)
    .map((marker) => marker.row);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('controller synchronization is one history without a clear notice', () {
    final controller = playerController();
    addTearDown(controller.dispose);
    final before = controller.encodeRecoverySnapshot();
    final revision = controller.projectRevision;
    expect(controller.canUndo, isFalse);
    expect(controller.notice, isNull);
    controller.clickCell(OTGY_CHART_COLUMN_NPC, 8);
    expect(playerRows(controller), [20, 28, 30, 34, 38]);
    expect(controller.notice, isNull);
    expect(controller.projectRevision, revision + 1);
    final synced = controller.encodeRecoverySnapshot();
    controller.undo();
    expect(controller.encodeRecoverySnapshot(), orderedEquals(before));
    expect(controller.canUndo, isFalse);
    expect(controller.canRedo, isTrue);
    controller.redo();
    expect(controller.encodeRecoverySnapshot(), orderedEquals(synced));
    expect(controller.canRedo, isFalse);
    expect(controller.notice, isNull);
  });

  test('controller clear notice does not create a second history entry', () {
    final controller = collapsingPlayerController();
    addTearDown(controller.dispose);
    final before = controller.encodeRecoverySnapshot();
    final revision = controller.projectRevision;
    controller.clickCell(OTGY_CHART_COLUMN_NPC, 8);
    expect(playerRows(controller), [20]);
    expect(controller.notice?.message, clearedMessage);
    expect(controller.notice?.kind, EditorNoticeKind.info);
    expect(controller.projectRevision, revision + 1);
    final cleared = controller.encodeRecoverySnapshot();
    final firstNotice = controller.notice;
    controller.dismissNotice();
    controller.undo();
    expect(controller.encodeRecoverySnapshot(), orderedEquals(before));
    expect(controller.canUndo, isFalse);
    expect(controller.notice, isNull);
    controller.redo();
    expect(controller.encodeRecoverySnapshot(), orderedEquals(cleared));
    expect(controller.canRedo, isFalse);
    expect(controller.notice, isNull);
    controller.undo();
    controller.clickCell(OTGY_CHART_COLUMN_NPC, 8);
    expect(controller.notice?.message, clearedMessage);
    expect(identical(controller.notice, firstNotice), isFalse);
  });

  test(
    'failed controller clicks preserve history and never report a clear',
    () {
      final controller = playerController();
      addTearDown(controller.dispose);
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 8);
      final synced = controller.encodeRecoverySnapshot();
      controller.undo();
      final before = controller.encodeRecoverySnapshot();
      final revision = controller.projectRevision;
      for (final (column, row, message) in [
        (OTGY_CHART_COLUMN_NPC, 19, '不能在结束符后追加'),
        (OTGY_CHART_COLUMN_PLAYER, 14, '当前位置已有标记'),
        (OTGY_CHART_COLUMN_PLAYER, 38, '自动生成标记无法编辑'),
      ]) {
        controller.dismissNotice();
        controller.clickCell(column, row);
        expect(controller.notice?.message, message);
        expect(controller.notice?.message, isNot(clearedMessage));
        expect(controller.encodeRecoverySnapshot(), orderedEquals(before));
        expect(controller.projectRevision, revision);
        expect(controller.canUndo, isFalse);
        expect(controller.canRedo, isTrue);
      }
      controller.dismissNotice();
      controller.redo();
      expect(controller.encodeRecoverySnapshot(), orderedEquals(synced));
      expect(controller.notice, isNull);
    },
  );

  test(
    'manual clear and corresponding block deletion do not report fallback',
    () {
      final controller = playerController();
      addTearDown(controller.dispose);
      controller.clickCell(OTGY_CHART_COLUMN_PLAYER, 30);
      expect(playerRows(controller), [20]);
      expect(controller.notice, isNull);
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 8);
      expect(playerRows(controller), [20]);
      expect(controller.notice, isNull);
      final beforeDelete = controller.encodeRecoverySnapshot();
      controller.clickCell(OTGY_CHART_COLUMN_NPC, 0);
      expect(controller.markers, isEmpty);
      expect(controller.notice, isNull);
      controller.undo();
      expect(controller.encodeRecoverySnapshot(), orderedEquals(beforeDelete));
      expect(controller.notice, isNull);
    },
  );

  testWidgets('repeated fallback shows one non-blocking notice each time', (
    tester,
  ) async {
    final directory = (await tester.runAsync(
      () => Directory.systemTemp.createTemp('otgy-player-sync-'),
    ))!;
    final controller = collapsingPlayerController();
    try {
      final store = (await tester.runAsync(
        () => LocalProjectStore.openAt(directory),
      ))!;
      await tester.pumpWidget(
        OneTwoGoYeahApp(
          controller: controller,
          storeFactory: () async => store,
        ),
      );
      await tester.pumpAndSettle();
      final before = controller.encodeRecoverySnapshot();
      for (var attempt = 0; attempt < 2; attempt++) {
        controller.clickCell(OTGY_CHART_COLUMN_NPC, 8);
        await tester.pumpAndSettle();
        expect(find.byType(SnackBar), findsOneWidget);
        expect(find.text(clearedMessage), findsOneWidget);
        expect(find.byType(AlertDialog), findsNothing);
        expect(controller.notice, isNull);
        expect(playerRows(controller), [20]);
        await tester.pump(const Duration(seconds: 3));
        await tester.pumpAndSettle();
        expect(find.byType(SnackBar), findsNothing);
        controller.undo();
        await tester.pumpAndSettle();
        expect(controller.encodeRecoverySnapshot(), orderedEquals(before));
        expect(controller.canUndo, isFalse);
        expect(find.text(clearedMessage), findsNothing);
      }
      expect(tester.takeException(), isNull);
    } finally {
      await tester.pumpWidget(const SizedBox.shrink());
      controller.dispose();
      await tester.runAsync(() => directory.delete(recursive: true));
    }
  }, timeout: const Timeout(Duration(seconds: 30)));
}
