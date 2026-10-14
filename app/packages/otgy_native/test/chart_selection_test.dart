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
  selection.firstRow,
  selection.lastRow,
];

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
    final snapshot = source.encodeRecovery();
    final editor = NativeChartEditor.decodeRecovery(snapshot);
    addTearDown(editor.dispose);

    expect(editor.hasSelection, isFalse);
    expect(editor.hasClipboard, isFalse);
    expect(selectionFields(editor.selection), [false, 0, 0, 0]);
    expect(editor.copySelection().code, 17);
    expect(editor.clearSelection().changed, isFalse);
    expect(editor.clearClipboard().changed, isFalse);

    expect(editor.click(OTGY_CHART_COLUMN_NPC, 200).changed, isTrue);
    final future = editor.encodeRecovery();
    editor.undo();
    expect(editor.canUndo, isFalse);
    expect(editor.canRedo, isTrue);

    final selected = editor.select(OTGY_CHART_COLUMN_PLAYER, 24);
    expect(selected.code, 0);
    expect(selected.changed, isTrue);
    expect(selected.selection.active, isTrue);
    expect(selected.selection.danceMoveDrillId, greaterThan(0));
    expect(selected.selection.firstRow, 16);
    expect(selected.selection.lastRow, 30);
    final savedSelection = editor.selection;
    expect(editor.hasSelection, isTrue);
    expect(editor.select(OTGY_CHART_COLUMN_NPC, 16).changed, isFalse);
    expect(editor.copySelection().code, 0);
    expect(editor.hasClipboard, isTrue);

    final cleared = editor.clearClipboard();
    expect(cleared.code, 0);
    expect(cleared.changed, isTrue);
    expect(selectionFields(cleared.selection), selectionFields(savedSelection));
    expect(selectionFields(editor.selection), selectionFields(savedSelection));
    expect(editor.hasClipboard, isFalse);
    expect(editor.clearClipboard().changed, isFalse);
    expect(editor.clearSelection().changed, isTrue);
    expect(editor.clearSelection().changed, isFalse);
    expect(editor.copySelection().code, 17);
    expect(editor.cutSelection().code, 17);
    expect(editor.paste(300).code, 18);
    expect(editor.hasSelection, isFalse);
    expect(editor.select(OTGY_CHART_COLUMN_NPC, 999).changed, isFalse);
    editor.select(OTGY_CHART_COLUMN_NPC, 16);
    expect(editor.select(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
    expect(editor.hasSelection, isFalse);

    expect(editor.encodeRecovery(), snapshot);
    expect(editor.canUndo, isFalse);
    expect(editor.canRedo, isTrue);
    expect(editor.redo().changed, isTrue);
    expect(editor.encodeRecovery(), future);
    expect(editor.undo().changed, isTrue);
    expect(editor.encodeRecovery(), snapshot);
    expect(editor.undo().code, 15);
    // Both getter and command results own values after their native arena ends.
    expect(
      selectionFields(selected.selection),
      selectionFields(savedSelection),
    );
    expect(savedSelection.firstRow, 16);
    expect(savedSelection.lastRow, 30);
  });

  test('an incomplete draft can be selected and clipboard stays local', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    editor.click(OTGY_CHART_COLUMN_NPC, 3);
    final selected = editor.select(OTGY_CHART_COLUMN_NPC, 3);
    expect(selected.code, 0);
    expect(selected.selection.firstRow, 3);
    expect(selected.selection.lastRow, 3);
    expect(editor.copySelection().changed, isTrue);
    expect(editor.hasClipboard, isTrue);

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

  test('repeated pastes preserve offsets and allocate new identities', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    addDanceMove(editor, 16);
    final source = editor.markers();
    final before = editor.encodeRecovery();
    final originalSelection = editor
        .select(OTGY_CHART_COLUMN_NPC, 16)
        .selection;
    editor.copySelection();

    final first = editor.paste(200);
    expect(first.code, 0);
    expect(first.changed, isTrue);
    final firstSelection = editor.selection;
    expect(firstSelection.firstRow, 200);
    expect(firstSelection.lastRow, 214);
    expect(
      firstSelection.danceMoveDrillId,
      isNot(originalSelection.danceMoveDrillId),
    );
    final afterFirst = editor.encodeRecovery();
    final second = editor.paste(300);
    expect(second.code, 0);
    expect(second.changed, isTrue);
    expect(editor.selection.firstRow, 300);
    expect(editor.selection.lastRow, 314);
    expect(
      editor.selection.danceMoveDrillId,
      isNot(
        isIn([
          originalSelection.danceMoveDrillId,
          firstSelection.danceMoveDrillId,
        ]),
      ),
    );

    final markers = editor.markers();
    final firstMarkers = markers
        .where((marker) => marker.row >= 200 && marker.row <= 214)
        .toList();
    final secondMarkers = markers
        .where((marker) => marker.row >= 300 && marker.row <= 314)
        .toList();
    final geometry = source
        .map((marker) => markerGeometry(marker, 16))
        .toList();
    expect(firstMarkers.map((marker) => markerGeometry(marker, 200)), geometry);
    expect(
      secondMarkers.map((marker) => markerGeometry(marker, 300)),
      geometry,
    );
    expect(
      markers.map((marker) => marker.id).toSet(),
      hasLength(source.length * 3),
    );
    expect(
      firstMarkers.map((marker) => marker.id),
      contains(first.primaryEventId),
    );
    expect(
      secondMarkers.map((marker) => marker.id),
      contains(second.primaryEventId),
    );
    expect(editor.hasClipboard, isTrue);

    expect(editor.undo().changed, isTrue);
    expect(editor.encodeRecovery(), afterFirst);
    expect(editor.undo().changed, isTrue);
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
    final refused = editor.paste(20);
    expect(refused.code, 19);
    expect(refused.changed, isFalse);
    expect(refused.primaryEventId, 0);
    expect(editor.encodeRecovery(), before);
    expect(editor.selection.firstRow, 16);
    expect(editor.hasClipboard, isTrue);

    editor.click(OTGY_CHART_COLUMN_NPC, 16);
    expect(editor.paste(200).changed, isTrue);
    expect(
      editor
          .markers()
          .where((marker) => marker.row >= 200)
          .map((marker) => markerGeometry(marker, 200)),
      source.map((marker) => markerGeometry(marker, 16)),
    );
  });

  for (final cut in [false, true]) {
    test(
      '${cut ? 'cut' : 'edit'} deletes invalidated Showtime with one undo',
      () {
        final editor = NativeChartEditor.createEmpty();
        addTearDown(editor.dispose);
        addDanceMove(editor, 0);
        addDanceMove(editor, 16);
        expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 32).changed, isTrue);
        expect(editor.showtimeMoves(), hasLength(2));
        final showStart = editor.markers().singleWhere(
          (marker) => marker.type == OTGY_CHART_MARKER_SHOWTIME_START,
        );
        final move = editor.showtimeMoves().last;
        final selected = editor.select(OTGY_CHART_COLUMN_SHOWTIME, move.row);
        final retainedSelection = selected.selection;
        expect(retainedSelection.firstRow, 16);
        expect(retainedSelection.lastRow, 30);
        expect(
          editor.select(OTGY_CHART_COLUMN_SHOWTIME, showStart.row).code,
          17,
        );
        expect(
          selectionFields(editor.selection),
          selectionFields(retainedSelection),
        );
        final before = editor.encodeRecovery();

        final result = cut
            ? editor.cutSelection()
            : editor.click(OTGY_CHART_COLUMN_NPC, 16);
        expect(result.code, 0);
        expect(result.changed, isTrue);
        expect(editor.showtimeMoves(), isEmpty);
        expect(
          editor.markers().where(
            (marker) => marker.column == OTGY_CHART_COLUMN_SHOWTIME,
          ),
          isEmpty,
        );
        if (cut) {
          expect(editor.hasClipboard, isTrue);
          expect(editor.hasSelection, isFalse);
          expect(editor.markers().every((marker) => marker.row < 16), isTrue);
        }
        final after = editor.encodeRecovery();
        expect(editor.undo().changed, isTrue);
        expect(editor.encodeRecovery(), before);
        expect(editor.showtimeMoves(), hasLength(2));
        expect(editor.redo().changed, isTrue);
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
    expect(editor.click(OTGY_CHART_COLUMN_NPC, 2).changed, isTrue);
    expect(editor.showtimeMoves(), isEmpty);
    final before = editor.encodeRecovery();
    expect(editor.click(OTGY_CHART_COLUMN_PLAYER, 6).changed, isTrue);
    final remaining = editor.encodeRecovery();
    expect(editor.undo().changed, isTrue);
    expect(editor.encodeRecovery(), orderedEquals(before));
    expect(editor.redo().changed, isTrue);
    expect(editor.encodeRecovery(), orderedEquals(remaining));
    expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 16).changed, isTrue);
    final move = editor.showtimeMoves().single;
    expect(move.row, 17);
    expect(move.showNCount, 1);
    expect(editor.undo().changed, isTrue);
    expect(editor.encodeRecovery(), orderedEquals(remaining));
  });

  test('stale Showtime continuation returns code 20 without an edit', () {
    final editor = NativeChartEditor.createEmpty();
    addTearDown(editor.dispose);
    addDanceMove(editor, 0);
    addDanceMove(editor, 40);
    expect(editor.click(OTGY_CHART_COLUMN_SHOWTIME, 64).changed, isTrue);
    addDanceMove(editor, 16);
    final before = editor.encodeRecovery();
    final refused = editor.click(OTGY_CHART_COLUMN_SHOWTIME, 112);
    expect(refused.code, 20);
    expect(refused.changed, isFalse);
    expect(editor.encodeRecovery(), before);
    expect(editor.showtimeMoves(), hasLength(2));
  });

  test('selection and clipboard calls reject a disposed editor', () {
    final editor = NativeChartEditor.createEmpty();
    editor.dispose();
    for (final operation in <Object? Function()>[
      () => editor.selection,
      () => editor.hasSelection,
      () => editor.hasClipboard,
      () => editor.select(OTGY_CHART_COLUMN_NPC, 0),
      editor.clearSelection,
      editor.copySelection,
      editor.clearClipboard,
      editor.cutSelection,
      () => editor.paste(100),
    ]) {
      expect(operation, throwsStateError);
    }
  });
}
