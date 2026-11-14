import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/ui/draft_save_target.dart';

void main() {
  DraftSummary summary(String id, String name) => DraftSummary(
    id: id,
    name: name,
    updatedAt: DateTime.utc(2026, 11, 15),
    payload: Uint8List(0),
  );

  test('an existing same-name draft requires confirmation', () {
    final target = resolveDraftSaveTarget(summary('draft-1', 'dup'));
    expect(target.needsConfirmation, isTrue);
    expect(target.existingId, 'draft-1');
  });

  test('a free name saves without confirmation', () {
    final target = resolveDraftSaveTarget(null);
    expect(target.needsConfirmation, isFalse);
    expect(target.existingId, isNull);
  });
}
