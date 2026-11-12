import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:otgy_native/otgy_native.dart';

EditorController partialController() {
  final source = NativeChartEditor.createEmpty();
  try {
    for (final row in [0, 2, 4, 6, 6]) {
      expect(source.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    for (final row in [8, 10]) {
      expect(source.click(OTGY_CHART_COLUMN_PLAYER, row).changed, isTrue);
    }
    for (final row in [40, 42, 44, 46, 46]) {
      expect(source.click(OTGY_CHART_COLUMN_NPC, row).changed, isTrue);
    }
    for (final row in [48, 50]) {
      expect(source.click(OTGY_CHART_COLUMN_PLAYER, row).changed, isTrue);
    }
    // Later content caps generation at the first SHOW-D anchor.
    expect(source.click(OTGY_CHART_COLUMN_NPC, 80).changed, isTrue);
    return EditorController(
      editor: NativeChartEditor.decodeRecovery(source.encodeRecovery()),
    );
  } finally {
    source.dispose();
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('partial completion raises an informational prompt', () {
    final controller = partialController();
    addTearDown(controller.dispose);
    expect(controller.showtimePartialPromptPending, isFalse);
    controller.clickCell(OTGY_CHART_COLUMN_SHOWTIME, 56);
    expect(controller.showtimePartialPromptPending, isTrue);
    expect(controller.showtimeMoves, hasLength(1));
    controller.acknowledgeShowtimePartialPrompt();
    expect(controller.showtimePartialPromptPending, isFalse);
    expect(controller.showtimeMoves, hasLength(1));
    expect(
      controller.markers.where(
        (marker) => marker.column == OTGY_CHART_COLUMN_SHOWTIME,
      ),
      isNotEmpty,
    );
  });
}
