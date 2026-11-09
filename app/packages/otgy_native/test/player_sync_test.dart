import 'package:otgy_native/otgy_native.dart';
import 'package:test/test.dart';

NativeChartEditor playerFixture() {
  final editor = NativeChartEditor.createEmpty();
  for (final row in [0, 10, 14, 18, 18]) {
    expect(editor.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
  }
  expect(editor.click(OTGY_CHART_COLUMN_PLAYER, 20).changed, isTrue);
  expect(editor.click(OTGY_CHART_COLUMN_PLAYER, 30).changed, isTrue);
  return editor;
}

Iterable<int> playerRows(NativeChartEditor editor) => editor
    .markers()
    .where((marker) => marker.column == OTGY_CHART_COLUMN_PLAYER)
    .map((marker) => marker.row);

void expectProjectRoundTrip(NativeChartEditor editor) {
  const bpm = 100.00000000000003;
  const offset = -0.12345678901234566;
  final recovery = editor.encodeRecovery();
  final restored = NativeChartEditor.decodeRecovery(recovery);
  final formal = editor.encodeFormal(bpm: bpm, offset: offset);
  final decoded = NativeChartEditor.decodeFormal(formal);
  try {
    expect(restored.encodeRecovery(), orderedEquals(recovery));
    expect(decoded.bpm, bpm);
    expect(decoded.offset, offset);
    expect(decoded.editor.encodeRecovery(), orderedEquals(recovery));
    expect(
      decoded.editor.encodeFormal(bpm: decoded.bpm, offset: decoded.offset),
      orderedEquals(formal),
    );
    for (final loaded in [restored, decoded.editor]) {
      expect(loaded.canUndo, isFalse);
      expect(loaded.canRedo, isFalse);
      expect(playerRows(loaded), playerRows(editor));
      final savedPlayers = loaded.markers().where(
        (marker) => marker.column == OTGY_CHART_COLUMN_PLAYER,
      );
      expect(loaded.click(OTGY_CHART_COLUMN_NPC, 200).changed, isTrue);
      expect(
        loaded
            .markers()
            .where((marker) => marker.column == OTGY_CHART_COLUMN_PLAYER)
            .map((marker) => [marker.id, marker.row, marker.type]),
        savedPlayers.map((marker) => [marker.id, marker.row, marker.type]),
      );
      loaded.undo();
      expect(loaded.encodeRecovery(), orderedEquals(recovery));
      expect(loaded.canUndo, isFalse);
    }
    expect(editor.encodeRecovery(), orderedEquals(recovery));
  } finally {
    restored.dispose();
    decoded.editor.dispose();
  }
}

void main() {
  test(
    'native click effects preserve offset and sync a leading N-to-S conversion',
    () {
      final editor = playerFixture();
      addTearDown(editor.dispose);
      final before = editor.encodeRecovery();
      final inserted = editor.click(OTGY_CHART_COLUMN_NPC, 8);
      expect(inserted.code, 0);
      expect(inserted.changed, isTrue);
      expect(inserted.playerActionsCleared, isFalse);
      expect(playerRows(editor), [20, 28, 30, 34, 38]);
      expectProjectRoundTrip(editor);
      final insertedSnapshot = editor.encodeRecovery();
      final converted = editor.click(OTGY_CHART_COLUMN_NPC, 8);
      expect(converted.code, 0);
      expect(converted.changed, isTrue);
      expect(converted.playerActionsCleared, isFalse);
      expect(playerRows(editor), [20, 28, 30, 34, 38]);
      expectProjectRoundTrip(editor);
      final convertedSnapshot = editor.encodeRecovery();
      expect(editor.undo().playerActionsCleared, isFalse);
      expect(editor.encodeRecovery(), orderedEquals(insertedSnapshot));
      expect(editor.redo().playerActionsCleared, isFalse);
      expect(editor.encodeRecovery(), orderedEquals(convertedSnapshot));
      editor.undo();
      editor.undo();
      expect(editor.encodeRecovery(), orderedEquals(before));
      expect(
        editor.click(OTGY_CHART_COLUMN_NPC, 0).playerActionsCleared,
        isFalse,
      );
      expect(editor.markers(), isEmpty);
    },
  );

  test(
    'cleared and C-only player blocks are not filled by later NPC edits',
    () {
      final editor = playerFixture();
      addTearDown(editor.dispose);
      final before = editor.encodeRecovery();
      final manualClear = editor.click(OTGY_CHART_COLUMN_PLAYER, 30);
      expect(manualClear.changed, isTrue);
      expect(manualClear.playerActionsCleared, isFalse);
      expect(playerRows(editor), [20]);
      final npcEdit = editor.click(OTGY_CHART_COLUMN_NPC, 8);
      expect(npcEdit.changed, isTrue);
      expect(npcEdit.playerActionsCleared, isFalse);
      expect(playerRows(editor), [20]);
      expectProjectRoundTrip(editor);
      editor.undo();
      editor.undo();
      expect(editor.encodeRecovery(), orderedEquals(before));
      final removed = editor.click(OTGY_CHART_COLUMN_NPC, 0);
      expect(removed.changed, isTrue);
      expect(removed.playerActionsCleared, isFalse);
      expect(editor.markers(), isEmpty);
      editor.undo();
      expect(editor.encodeRecovery(), orderedEquals(before));
    },
  );

  test('invalid columns never edit or change selection state', () {
    final source = playerFixture();
    addTearDown(source.dispose);
    final editor = NativeChartEditor.decodeRecovery(source.encodeRecovery());
    addTearDown(editor.dispose);
    final before = editor.encodeRecovery();
    expect(editor.canUndo, isFalse);
    expect(editor.select(OTGY_CHART_COLUMN_NPC, 10).changed, isTrue);
    List<Object> selectionFields(ChartSelection selection) => [
      selection.active,
      selection.danceMoveDrillId,
      selection.firstRow,
      selection.lastRow,
    ];
    final selected = editor.selections.map(selectionFields).toList();
    expect(selected, isNotEmpty);
    for (final invalidColumn in [256, 257, 258]) {
      for (final row in [10, 200]) {
        final click = editor.click(invalidColumn, row);
        expect(click.code, isNot(0));
        expect(click.changed, isFalse);
        expect(click.playerActionsCleared, isFalse);
        expect(editor.encodeRecovery(), orderedEquals(before));
        // The decoded fixture starts with zero history, so false proves that
        // the invalid request did not add even one undo entry.
        expect(editor.canUndo, isFalse);
        expect(editor.canRedo, isFalse);
        expect(editor.selections.map(selectionFields), selected);
        final selection = editor.select(invalidColumn, row);
        expect(selection.code, 0);
        expect(selection.changed, isFalse);
        expect(editor.encodeRecovery(), orderedEquals(before));
        expect(editor.canUndo, isFalse);
        expect(editor.canRedo, isFalse);
        expect(editor.selections.map(selectionFields), selected);
      }
    }
  });

  test('failed clicks report no clear and preserve both history stacks', () {
    final editor = playerFixture();
    addTearDown(editor.dispose);
    editor.click(OTGY_CHART_COLUMN_NPC, 8);
    final future = editor.encodeRecovery();
    editor.undo();
    final before = editor.encodeRecovery();
    expect(editor.canUndo, isTrue);
    expect(editor.canRedo, isTrue);
    final rejected = editor.click(OTGY_CHART_COLUMN_PLAYER, 14);
    expect(rejected.code, isNot(0));
    expect(rejected.changed, isFalse);
    expect(rejected.playerActionsCleared, isFalse);
    expect(editor.encodeRecovery(), orderedEquals(before));
    expect(editor.canUndo, isTrue);
    expect(editor.canRedo, isTrue);
    editor.redo();
    expect(editor.encodeRecovery(), orderedEquals(future));
  });
}
