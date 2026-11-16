import 'package:otgy_native/otgy_native.dart';
import 'package:test/test.dart';

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

List<Object> selectionFields(ChartSelection selection) => [
  selection.active,
  selection.danceMoveDrillId,
  selection.drillGroupId,
  selection.role,
  selection.part,
  selection.firstRow,
  selection.lastRow,
  selection.eventCount,
];

/// First row of every selected dance move, derived from its selected units.
List<int> selectedMoveFirstRows(NativeChartEditor editor) {
  final rows = <int, int>{};
  for (final unit in editor.selections) {
    final current = rows[unit.danceMoveDrillId];
    if (current == null || unit.firstRow < current) {
      rows[unit.danceMoveDrillId] = unit.firstRow;
    }
  }
  return rows.values.toList()..sort();
}

/// Last row of every selected dance move, derived from its selected units.
List<int> selectedMoveLastRows(NativeChartEditor editor) {
  final rows = <int, int>{};
  for (final unit in editor.selections) {
    final current = rows[unit.danceMoveDrillId];
    if (current == null || unit.lastRow > current) {
      rows[unit.danceMoveDrillId] = unit.lastRow;
    }
  }
  return rows.values.toList()..sort();
}

List<Object?> markerGeometry(ChartMarker marker, int firstRow) => [
  marker.row - firstRow,
  marker.column,
  marker.type,
  marker.showNCount,
  marker.ordinal,
];

void main() {
  test('session commands preserve the draft and both history stacks', () {
    final source = NativeChartEditor.createEmpty();
    addTearDown(source.dispose);
    addDanceMove(source, 16);
    addDanceMove(source, 48);
    final snapshot = source.encodeRecovery();
    final editor = NativeChartEditor.decodeRecovery(snapshot);
    addTearDown(editor.dispose);
    expect(editor.selections, isEmpty);
    expect(editor.copySelection().code, 17);
    expect(editor.cutSelection().code, 17);
    expect(editor.clearSelection().changed, isFalse);
    expect(editor.clearClipboard().changed, isFalse);
    editor.click(OTGY_CHART_COLUMN_NPC, 200);
    final future = editor.encodeRecovery();
    editor.undo();
    expect(editor.canUndo, isFalse);
    expect(editor.canRedo, isTrue);

    expect(editor.select(OTGY_CHART_COLUMN_PLAYER, 56).changed, isTrue);
    expect(editor.select(OTGY_CHART_COLUMN_NPC, 16).changed, isTrue);
    final savedSelection = editor.selections;
    expect(selectedMoveFirstRows(editor), [16, 48]);
    expect(selectedMoveLastRows(editor), [30, 62]);
    expect(editor.select(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
    expect(editor.selections.length, savedSelection.length - 1);
    expect(editor.select(OTGY_CHART_COLUMN_PLAYER, 16).changed, isTrue);
    expect(editor.select(OTGY_CHART_COLUMN_NPC, 17).changed, isFalse);
    expect(editor.select(OTGY_CHART_COLUMN_NPC, 999).changed, isFalse);
    expect(editor.selections.length, savedSelection.length);
    expect(selectedMoveFirstRows(editor), [16, 48]);
    expect(selectedMoveLastRows(editor), [30, 62]);
    expect(editor.select(OTGY_CHART_COLUMN_PLAYER, 24).changed, isTrue);
    expect(editor.selections.length, savedSelection.length - 1);
    // Restoring the same unit brings the selection back to the saved set.
    editor.select(OTGY_CHART_COLUMN_PLAYER, 24);
    expect(editor.selections.length, savedSelection.length);
    expect(editor.copySelection().code, 0);
    expect(editor.clipboardCount, 2);
    expect(editor.cutPending, isFalse);
    expect(editor.cutSelection().code, 0);
    expect(editor.cutPending, isTrue);
    expect(editor.clearClipboard().changed, isTrue);
    expect(editor.hasClipboard, isFalse);
    expect(editor.cutPending, isFalse);
    expect(editor.selections.length, savedSelection.length);
    expect(selectedMoveFirstRows(editor), [16, 48]);
    expect(selectedMoveLastRows(editor), [30, 62]);
    expect(editor.clearSelection().changed, isTrue);
    expect(editor.clearSelection().changed, isFalse);
    expect(editor.paste(300).code, 18);
    expect(editor.encodeRecovery(), snapshot);
    expect(editor.canUndo, isFalse);
    expect(editor.canRedo, isTrue);
    editor.redo();
    expect(editor.encodeRecovery(), future);
    editor.undo();
    expect(editor.encodeRecovery(), snapshot);
    expect(savedSelection.map((s) => s.danceMoveDrillId).toSet(), hasLength(2));
  });

  test(
    'exact marker rows select across all columns without selecting gaps',
    () {
      final editor = NativeChartEditor.createEmpty();
      addTearDown(editor.dispose);
      addDanceMove(editor, 0);
      expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
      final before = editor.encodeRecovery();
      final markers = editor.markers();
      const columns = [
        OTGY_CHART_COLUMN_NPC,
        OTGY_CHART_COLUMN_SHOWTIME,
        OTGY_CHART_COLUMN_PLAYER,
      ];
      final showStart = markers.singleWhere(
        (marker) => marker.type == OTGY_CHART_MARKER_SHOWTIME_START,
      );
      for (final marker in markers.where(
        (marker) => marker.type != OTGY_CHART_MARKER_SHOWTIME_START,
      )) {
        for (final column in columns) {
          expect(editor.select(column, marker.row).changed, isTrue);
          expect(selectedMoveFirstRows(editor), [0]);
          expect(selectedMoveLastRows(editor), [14]);
          final selected = editor.selections.map(selectionFields).toList();
          expect(editor.select(column, 5).changed, isFalse);
          expect(editor.select(column, showStart.row).code, 17);
          expect(editor.selections.map(selectionFields), selected);
          // A second activation toggles one unit, except a Showtime marker
          // which toggles every unit of its dance move.
          expect(editor.select(column, marker.row).changed, isTrue);
          if (marker.type == OTGY_CHART_MARKER_SHOWTIME_MOVE) {
            expect(editor.selections, isEmpty);
          } else {
            expect(editor.selections.length, selected.length - 1);
          }
          editor.clearSelection();
        }
      }
      expect(editor.encodeRecovery(), before);
    },
  );

  test('an incomplete draft can be selected and clipboard stays local', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    editor.click(OTGY_CHART_COLUMN_NPC, 3);
    expect(editor.select(OTGY_CHART_COLUMN_NPC, 3).code, 0);
    expect(editor.selections.single.firstRow, 3);
    expect(editor.selections.single.lastRow, 3);
    expect(editor.copySelection().changed, isTrue);
    final loaded = NativeChartEditor.decodeRecovery(editor.encodeRecovery());
    addTearDown(loaded.dispose);
    expect(loaded.hasSelection, isFalse);
    expect(loaded.hasClipboard, isFalse);
    expect(loaded.paste(50).code, 18);
    editor.clear();
    expect(editor.hasSelection, isFalse);
    expect(editor.hasClipboard, isTrue);
    expect(editor.paste(50).changed, isTrue);
    expect(editor.markers().single.row, 50);
  });

  test('repeated batch pastes preserve gaps and allocate new identities', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    addDanceMove(editor, 16);
    addDanceMove(editor, 48);
    final source = editor.markers();
    final before = editor.encodeRecovery();
    editor.select(OTGY_CHART_COLUMN_NPC, 48);
    editor.select(OTGY_CHART_COLUMN_NPC, 16);
    editor.copySelection();
    final preview = editor.pastePreview(200);
    expect(preview.valid, isTrue);
    expect(preview.selections.map((s) => s.firstRow), [
      200,
      202,
      208,
      210,
      232,
      234,
      240,
      242,
    ]);
    expect(preview.lastRow, 246);
    expect(editor.paste(200).changed, isTrue);
    final afterFirst = editor.encodeRecovery();
    expect(editor.paste(300).changed, isTrue);
    final markers = editor.markers();
    final geometry = source.map((marker) => markerGeometry(marker, 16));
    for (final anchor in [200, 300]) {
      expect(
        markers
            .where((m) => m.row >= anchor && m.row <= anchor + 46)
            .map((m) => markerGeometry(m, anchor)),
        geometry,
      );
    }
    expect(markers.map((m) => m.id).toSet(), hasLength(source.length * 3));
    expect(editor.hasClipboard, isTrue);
    editor.undo();
    expect(editor.encodeRecovery(), afterFirst);
    editor.undo();
    expect(editor.encodeRecovery(), before);
  });

  test('copy owns a snapshot and occupied paste fails atomically', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    addDanceMove(editor, 16);
    final source = editor.markers();
    editor.select(OTGY_CHART_COLUMN_NPC, 16);
    editor.copySelection();
    final before = editor.encodeRecovery();
    final selected = editor.selections.map(selectionFields).toList();
    final refused = editor.paste(20);
    expect(refused.code, 19);
    expect(refused.changed, isFalse);
    expect(refused.primaryEventId, 0);
    expect(editor.encodeRecovery(), before);
    expect(editor.selections.map(selectionFields), selected);
    editor.click(OTGY_CHART_COLUMN_NPC, 16);
    expect(editor.paste(200).changed, isTrue);
    expect(
      editor
          .markers()
          .where((m) => m.row >= 200)
          .map((m) => markerGeometry(m, 200)),
      source.map((m) => markerGeometry(m, 16)),
    );
  });

  test(
    'changed cut source rejects placement without losing pending intent',
    () {
      final editor = NativeChartEditor.createEmpty();
      addTearDown(editor.dispose);
      addDanceMove(editor, 16);
      editor.select(OTGY_CHART_COLUMN_NPC, 16);
      editor.cutSelection();
      editor.click(OTGY_CHART_COLUMN_NPC, 16);
      final changed = editor.encodeRecovery();
      expect(editor.pastePreview(200).code, 21);
      expect(editor.paste(200).code, 21);
      expect(editor.cutPending, isTrue);
      expect(editor.encodeRecovery(), changed);
      editor.clearClipboard();
      expect(editor.cutPending, isFalse);
    },
  );

  for (final cut in [false, true]) {
    test(
      '${cut ? 'cut placement' : 'edit'} removes invalidated Showtime in one undo',
      () {
        final editor = NativeChartEditor.createEmpty();
        addTearDown(editor.dispose);
        addDanceMove(editor, 0);
        addDanceMove(editor, 16);
        expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 32).changed, isTrue);
        expect(editor.showtimeMoves(), hasLength(2));
        final showStart = editor.markers().singleWhere(
          (m) => m.type == OTGY_CHART_MARKER_SHOWTIME_START,
        );
        editor.select(
          OTGY_CHART_COLUMN_SHOWTIME,
          editor.showtimeMoves().last.row,
        );
        final retained = editor.selections.map(selectionFields).toList();
        expect(
          editor.select(OTGY_CHART_COLUMN_SHOWTIME, showStart.row).code,
          17,
        );
        expect(editor.selections.map(selectionFields), retained);
        final before = editor.encodeRecovery();
        if (cut) {
          expect(editor.cutSelection().code, 0);
          expect(editor.encodeRecovery(), before);
          expect(editor.showtimeMoves(), hasLength(2));
          expect(editor.paste(200).changed, isTrue);
          expect(editor.cutPending, isFalse);
          expect(editor.hasClipboard, isTrue);
        } else {
          expect(editor.click(OTGY_CHART_COLUMN_NPC, 16).changed, isTrue);
        }
        expect(editor.showtimeMoves(), isEmpty);
        expect(
          editor.markers().where((m) => m.column == OTGY_CHART_COLUMN_SHOWTIME),
          isEmpty,
        );
        final after = editor.encodeRecovery();
        editor.undo();
        expect(editor.encodeRecovery(), before);
        expect(editor.showtimeMoves(), hasLength(2));
        editor.redo();
        expect(editor.encodeRecovery(), after);
      },
    );
  }

  test('deleting the last group event allows Showtime regeneration', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    for (final row in [2, 3, 4, 5]) {
      expect(editor.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    for (final row in [6, 7]) {
      expect(editor.click(OTGY_CHART_COLUMN_PLAYER, row).changed, isTrue);
    }
    for (final row in [10, 11, 12, 12]) {
      expect(editor.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    for (final row in [13, 14]) {
      expect(editor.click(OTGY_CHART_COLUMN_PLAYER, row).changed, isTrue);
    }
    expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
    expect(editor.showtimeMoves(), hasLength(1));
    final before = editor.encodeRecovery();
    expect(editor.click(OTGY_CHART_COLUMN_NPC, 2).changed, isTrue);
    expect(editor.showtimeMoves(), isEmpty);
    expect(editor.markers().any((marker) => marker.row == 6), isFalse);
    final remaining = editor.encodeRecovery();
    editor.undo();
    expect(editor.encodeRecovery(), orderedEquals(before));
    editor.redo();
    expect(editor.encodeRecovery(), orderedEquals(remaining));
    expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
    final move = editor.showtimeMoves().single;
    expect(move.row, 17);
    expect(move.showNCount, 1);
    editor.undo();
    expect(editor.encodeRecovery(), orderedEquals(remaining));
  });

  test('a move inserted between recorded moves clears the Showtime', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    addDanceMove(editor, 0);
    addDanceMove(editor, 40);
    expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 64).changed, isTrue);
    expect(editor.showtimeMoves(), hasLength(2));
    addDanceMove(editor, 16);
    expect(editor.showtimeMoves(), isEmpty);
    expect(
      editor.markers().where((m) => m.type == OTGY_CHART_MARKER_SHOWTIME_START),
      isEmpty,
    );
  });

  test('a move completed before recorded moves clears the Showtime', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    addDanceMove(editor, 24);
    addDanceMove(editor, 64);
    expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 96).changed, isTrue);
    expect(editor.showtimeMoves(), hasLength(2));
    addDanceMove(editor, 0);
    expect(editor.showtimeMoves(), isEmpty);
    expect(
      editor.markers().where(
        (m) =>
            m.type == OTGY_CHART_MARKER_SHOWTIME_START ||
            m.type == OTGY_CHART_MARKER_SHOWTIME_MOVE,
      ),
      isEmpty,
    );
    // The edit that completed the leading move cleared the Showtime, so one
    // undo restores it together with that click.
    editor.undo();
    expect(editor.showtimeMoves(), hasLength(2));
  });

  test('partial Showtime completion reports its own code', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    addDanceMove(editor, 0);
    addDanceMove(editor, 40);
    expect(editor.click(OTGY_CHART_COLUMN_NPC, 80).changed, isTrue);
    final result = editor.click(OTGY_CHART_COLUMN_SHOWTIME, 56);
    expect(result.code, 22);
    expect(result.changed, isTrue);
    expect(editor.showtimeMoves(), hasLength(1));
  });

  test('selection and clipboard calls reject a disposed editor', () {
    final editor = NativeChartEditor.createEmpty();
    editor.dispose();
    for (final operation in <Object? Function()>[
      () => editor.selections,
      () => editor.selectionCount,
      () => editor.hasSelection,
      () => editor.hasClipboard,
      () => editor.clipboardCount,
      () => editor.cutPending,
      () => editor.select(OTGY_CHART_COLUMN_NPC, 0),
      editor.clearSelection,
      editor.copySelection,
      editor.clearClipboard,
      editor.cutSelection,
      () => editor.pastePreview(100),
      () => editor.paste(100),
    ]) {
      expect(operation, throwsStateError);
    }
  });
}
