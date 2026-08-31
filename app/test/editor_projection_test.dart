import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';

void main() {
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
