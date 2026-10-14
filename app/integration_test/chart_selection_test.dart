import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  WidgetController.hitTestWarningShouldBeFatal = true;

  late Directory directory;
  late LocalProjectStore store;
  late EditorController controller;

  void addDanceMove(NativeChartEditor editor) {
    for (final row in [0, 2, 4, 6, 6]) {
      expect(editor.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    for (final row in [8, 10]) {
      expect(editor.click(OTGY_CHART_COLUMN_PLAYER, row).changed, isTrue);
    }
  }

  Future<void> openEditor(
    WidgetTester tester, {
    bool withShowtime = false,
  }) async {
    directory = await Directory.systemTemp.createTemp('otgy-device-selection-');
    store = await LocalProjectStore.openAt(directory);
    final editor = NativeChartEditor.createEmpty();
    addDanceMove(editor);
    if (withShowtime) {
      expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
      expect(editor.showtimeMoves(), hasLength(1));
    }
    controller = EditorController(editor: editor);
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pumpAndSettle();
    addTearDown(() async {
      await tester.pumpWidget(const SizedBox.shrink());
      controller.dispose();
      await directory.delete(recursive: true);
    });
  }

  Offset npcAtCursor(WidgetTester tester) {
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final header = tester.getRect(
      find.byKey(const ValueKey('column-header-NPC')),
    );
    return Offset(header.center.dx, canvas.top + editorCursorY(canvas.size));
  }

  Future<void> tapControl(WidgetTester tester, String key) async {
    final finder = find.byKey(ValueKey(key));
    expect(finder, findsOneWidget);
    expect(tester.widget<IconButton>(finder).onPressed, isNotNull);
    await tester.tap(finder);
    await tester.pumpAndSettle();
  }

  List<Object> geometry(ChartMarker marker, int firstRow) => [
    marker.row - firstRow,
    marker.column,
    marker.type,
  ];

  testWidgets(
    'Android long press and clipboard controls preserve whole moves',
    (tester) async {
      await openEditor(tester);
      final source = controller.encodeFormalDraft();
      final sourceMarkers = List<ChartMarker>.of(controller.markers);
      final revision = controller.projectRevision;
      final zoom = controller.zoom;

      await tester.longPressAt(npcAtCursor(tester));
      await tester.pumpAndSettle();
      expect(controller.selectionMode, isTrue);
      expect(controller.selection.active, isTrue);
      expect(controller.selection.firstRow, 0);
      expect(controller.selection.lastRow, 14);
      expect(controller.encodeFormalDraft(), orderedEquals(source));
      expect(controller.projectRevision, revision);
      expect(controller.zoom, zoom);
      expect(controller.scale, 4);

      controller.moveToRow(2);
      await tester.pumpAndSettle();
      await tester.tapAt(npcAtCursor(tester));
      await tester.pumpAndSettle();
      expect(controller.encodeFormalDraft(), orderedEquals(source));
      expect(controller.projectRevision, revision);
      await tapControl(tester, 'selection-copy');
      expect(controller.canPaste, isTrue);
      expect(controller.projectRevision, revision);
      expect(controller.isDirty, isFalse);

      controller.moveToRow(32);
      await tester.pumpAndSettle();
      await tapControl(tester, 'selection-paste');
      expect(controller.projectRevision, revision + 1);
      expect(controller.isDirty, isTrue);
      expect(controller.markers, hasLength(sourceMarkers.length * 2));
      expect(controller.selection.firstRow, 32);
      expect(controller.selection.lastRow, 46);
      final once = controller.encodeFormalDraft();
      final firstPaste = controller.markers.where((m) => m.row >= 32).toList();
      expect(
        firstPaste.map((m) => geometry(m, 32)),
        orderedEquals(sourceMarkers.map((m) => orderedEquals(geometry(m, 0)))),
      );
      expect(
        firstPaste
            .map((m) => m.id)
            .toSet()
            .intersection(sourceMarkers.map((m) => m.id).toSet()),
        isEmpty,
      );
      final firstDrill = controller.selection.danceMoveDrillId;

      controller.moveToRow(48);
      await tester.pumpAndSettle();
      await tapControl(tester, 'selection-paste');
      expect(controller.markers, hasLength(sourceMarkers.length * 3));
      expect(controller.selection.danceMoveDrillId, isNot(firstDrill));
      final secondPaste = controller.markers.where((m) => m.row >= 48).toList();
      expect(
        secondPaste.map((m) => geometry(m, 48)),
        orderedEquals(sourceMarkers.map((m) => orderedEquals(geometry(m, 0)))),
      );
      expect(
        secondPaste
            .map((m) => m.id)
            .toSet()
            .intersection(
              [...sourceMarkers, ...firstPaste].map((m) => m.id).toSet(),
            ),
        isEmpty,
      );

      await tester.tap(find.byTooltip('撤销'));
      await tester.pumpAndSettle();
      expect(controller.encodeFormalDraft(), orderedEquals(once));
      expect(controller.selection.active, isFalse);

      controller.moveToRow(32);
      await tester.pumpAndSettle();
      final beforeConflict = controller.projectRevision;
      await tapControl(tester, 'selection-paste');
      expect(controller.encodeFormalDraft(), orderedEquals(once));
      expect(controller.projectRevision, beforeConflict);
      expect(find.text('粘贴范围内存在其他标记'), findsOneWidget);
      await tester.pump(const Duration(seconds: 5));
      await tester.pumpAndSettle();
      expect(find.text('粘贴范围内存在其他标记'), findsNothing);

      await tester.longPressAt(npcAtCursor(tester));
      await tester.pumpAndSettle();
      expect(controller.selectionMode, isTrue);
      expect(controller.selection.firstRow, 32);
      await tapControl(tester, 'selection-cut');
      expect(controller.encodeFormalDraft(), orderedEquals(source));
      expect(controller.selection.active, isFalse);
      expect(controller.canPaste, isTrue);
      await tester.tap(find.byTooltip('撤销'));
      await tester.pumpAndSettle();
      expect(controller.encodeFormalDraft(), orderedEquals(once));

      await tapControl(tester, 'selection-mode');
      expect(controller.selectionMode, isFalse);
      expect(controller.selection.active, isFalse);
      expect(controller.canPaste, isFalse);
      await tapControl(tester, 'selection-mode');
      expect(
        tester
            .widget<IconButton>(find.byKey(const ValueKey('selection-paste')))
            .onPressed,
        isNull,
      );
      expect(controller.encodeFormalDraft(), orderedEquals(once));
    },
  );

  testWidgets('Android deleting a whole group allows Showtime regeneration', (
    tester,
  ) async {
    await openEditor(tester);
    controller.clearChart();
    for (final row in [2, 3, 4, 5]) {
      controller.clickCell(OTGY_CHART_COLUMN_NPC, row);
    }
    for (final row in [6, 7]) {
      controller.clickCell(OTGY_CHART_COLUMN_PLAYER, row);
    }
    for (final row in [10, 11, 12, 12]) {
      controller.clickCell(OTGY_CHART_COLUMN_NPC, row);
    }
    for (final row in [13, 14]) {
      controller.clickCell(OTGY_CHART_COLUMN_PLAYER, row);
    }
    controller.clickCell(OTGY_CHART_COLUMN_SHOWTIME, 16);
    expect(controller.showtimeMoves, hasLength(1));
    controller.clickCell(OTGY_CHART_COLUMN_NPC, 2);
    expect(controller.showtimeMoves, isEmpty);
    final beforeDelete = controller.encodeFormalDraft();
    controller.clickCell(OTGY_CHART_COLUMN_PLAYER, 6);
    final remaining = controller.encodeFormalDraft();
    controller.undo();
    expect(controller.encodeFormalDraft(), orderedEquals(beforeDelete));
    controller.redo();
    expect(controller.encodeFormalDraft(), orderedEquals(remaining));

    controller.moveToRow(16);
    await tester.pumpAndSettle();
    final canvas = tester.getRect(find.byKey(const ValueKey('editor-canvas')));
    final header = tester.getRect(
      find.byKey(const ValueKey('column-header-Showtime')),
    );
    await tester.tapAt(
      Offset(header.center.dx, canvas.top + editorCursorY(canvas.size)),
    );
    await tester.pumpAndSettle();
    expect(controller.showtimeMoves, hasLength(1));
    expect(controller.showtimeMoves.single.row, 17);
    expect(controller.showtimeMoves.single.showNCount, 1);
    controller.undo();
    expect(controller.encodeFormalDraft(), orderedEquals(remaining));
  });

  testWidgets('Android cut removes stale Showtime in the same undo step', (
    tester,
  ) async {
    await openEditor(tester, withShowtime: true);
    final before = controller.encodeFormalDraft();
    final revision = controller.projectRevision;
    await tester.longPressAt(npcAtCursor(tester));
    await tester.pumpAndSettle();
    await tapControl(tester, 'selection-cut');
    expect(controller.markers, isEmpty);
    expect(controller.showtimeMoves, isEmpty);
    expect(controller.projectRevision, revision + 1);
    expect(controller.canPaste, isTrue);
    await tester.tap(find.byTooltip('撤销'));
    await tester.pumpAndSettle();
    expect(controller.encodeFormalDraft(), orderedEquals(before));
    expect(controller.showtimeMoves, hasLength(1));
  });
}
