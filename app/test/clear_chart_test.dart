import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('clearing drops the draft identity and unsaved marker', () {
    final controller = EditorController(editor: NativeChartEditor.createEmpty());
    addTearDown(controller.dispose);
    controller.clickCell(OTGY_CHART_COLUMN_NPC, 0);
    expect(controller.markers, isNotEmpty);
    final payload = controller.encodeFormalDraft();
    controller.markSaved(
      draftId: 'draft-1',
      draftName: '我的草稿',
      payload: payload,
    );
    expect(controller.chartName, '我的草稿');
    expect(controller.isDirty, isFalse);

    controller.clearChart();
    expect(controller.markers, isEmpty);
    expect(controller.sourceKind, WorkspaceSourceKind.untitled);
    expect(controller.draftId, isNull);
    expect(controller.chartName, '未命名草稿');
    expect(controller.isDirty, isFalse);
  });
}
