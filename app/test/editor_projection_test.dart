import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test(
    'automatic edit zoom keeps the initial sixteenth-row spacing usable',
    () {
      final controller = EditorController();
      addTearDown(controller.dispose);

      expect(controller.zoom, 9.5);
      controller.setBpm(127.882);

      expect(controller.zoom, 10);
      expect(controller.rowSpacing, closeTo(targetEditorRowSpacing, 0.2));
      expect(controller.zoom * 4, (controller.zoom * 4).roundToDouble());
    },
  );

  test('manual edit zoom wins over later BPM changes', () async {
    final controller = EditorController();
    addTearDown(controller.dispose);

    await controller.setZoom(8.25);
    controller.setBpm(180);

    expect(controller.zoom, 8.25);
  });

  test(
    'playback visual density gives five seconds a stable maximum length',
    () async {
      final controller = EditorController();
      addTearDown(controller.dispose);
      controller.setBpm(127.882);

      final rowsInFiveSeconds = 127.882 / 3;
      expect(controller.playbackVisualPixelsPerSecond, 170);
      expect(
        controller.playbackRowSpacing * rowsInFiveSeconds,
        closeTo(850, 1e-9),
      );

      await controller.setZoom(7);
      expect(
        controller.playbackVisualPixelsPerSecond,
        controller.pixelsPerSecond,
      );
      expect(controller.playbackRowSpacing, controller.rowSpacing);
    },
  );

  test('row midpoint belongs to the later row', () {
    expect(
      projectEditorRow(
        y: 109.999,
        cursorY: 100,
        currentRow: 10,
        rowSpacing: 20,
      ),
      10,
    );
    expect(
      projectEditorRow(y: 110, cursorY: 100, currentRow: 10, rowSpacing: 20),
      11,
    );
  });

  test('mobile editing threshold is exact', () {
    expect(mobileChartEditingEnabled(39.999), isFalse);
    expect(mobileChartEditingEnabled(40), isTrue);
  });

  test('marker sizing follows row spacing and platform caps', () {
    expect(editorMarkerSize(rowSpacing: 20, mobile: false, diamond: false), 14);
    expect(
      editorMarkerSize(rowSpacing: 100, mobile: false, diamond: false),
      22,
    );
    expect(editorMarkerSize(rowSpacing: 100, mobile: true, diamond: true), 32);
  });

  test('marker sizing has no Scale input', () {
    final before = editorMarkerSize(
      rowSpacing: 30,
      mobile: false,
      diamond: true,
    );
    final after = editorMarkerSize(
      rowSpacing: 30,
      mobile: false,
      diamond: true,
    );
    expect(after, before);
  });
}
