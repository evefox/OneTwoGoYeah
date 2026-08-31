import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';

void main() {
  late Directory directory;
  late LocalProjectStore store;

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-store-');
    store = await LocalProjectStore.openAt(directory);
  });

  tearDown(() async {
    if (await directory.exists()) {
      await directory.delete(recursive: true);
    }
  });

  test('formal drafts keep stable ids and latest atomic generation', () async {
    final first = await store.saveDraft(
      name: '  第一份  ',
      payload: Uint8List.fromList([1, 2, 3]),
    );
    final second = await store.saveDraft(
      id: first.id,
      name: first.name,
      payload: Uint8List.fromList([4, 5]),
    );

    final drafts = await store.listDrafts();
    expect(drafts, hasLength(1));
    expect(second.id, first.id);
    expect(drafts.single.name, '第一份');
    expect(drafts.single.payload, [4, 5]);
    expect(await store.findByName(' 第一份 '), isNotNull);
  });

  test('draft delete and recovery storage remain separate', () async {
    final draft = await store.saveDraft(
      name: '草稿',
      payload: Uint8List.fromList([7]),
    );
    await store.writeRecovery(Uint8List.fromList([8, 9]));
    expect(await store.readRecovery(), [8, 9]);

    await store.deleteDraft(draft.id);
    expect(await store.listDrafts(), isEmpty);
    expect(await store.readRecovery(), [8, 9]);
    await store.deleteRecovery();
    expect(await store.readRecovery(), isNull);
  });

  test('session marker reports an interrupted previous session', () async {
    expect((await store.beginSession()).wasInterrupted, isFalse);
    await store.writeRecovery(Uint8List.fromList([3, 1, 4]));

    final next = await store.beginSession();
    expect(next.wasInterrupted, isTrue);
    expect(next.recoveryPayload, [3, 1, 4]);

    await store.endSessionNormally();
    expect((await store.beginSession()).wasInterrupted, isFalse);
  });

  test('draft names have one centralized normalization rule', () {
    expect(normalizeDraftName('  曲目  '), '曲目');
    expect(() => normalizeDraftName('   '), throwsFormatException);
    expect(
      () => normalizeDraftName(List.filled(81, '字').join()),
      throwsFormatException,
    );
  });
}
