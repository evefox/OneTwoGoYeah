import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/infrastructure/audio_export_publisher.dart';

void main() {
  late Directory directory;

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-audio-export-');
  });

  tearDown(() async {
    if (await directory.exists()) {
      await directory.delete(recursive: true);
    }
  });

  test(
    'publishes to the first free collision name without overwriting',
    () async {
      final time = DateTime(2026, 8, 31, 9, 5, 7);
      final original = File(
        '${directory.path}${Platform.pathSeparator}曲目_20260831_090507.ogg',
      );
      await original.writeAsBytes([9, 8, 7], flush: true);
      final bytes = Uint8List.fromList([1, 2, 3, 4]);

      final published = await AudioExportPublisher.publish(
        bytes: bytes,
        selectedPath: original.path,
        baseName: '曲目',
        localTime: time,
        isCancelled: () => false,
        linkTemporaryFile: _linkForTest,
      );

      expect(await original.readAsBytes(), [9, 8, 7]);
      expect(published.uri.pathSegments.last, '曲目_20260831_090507_2.ogg');
      expect(await published.readAsBytes(), bytes);
      expect(
        directory.listSync().where((item) => item.path.contains('.otgy-tmp')),
        isEmpty,
      );
    },
  );

  test('cancellation removes temporary and final output', () async {
    final selected = File(
      '${directory.path}${Platform.pathSeparator}cancelled.ogg',
    );

    await expectLater(
      AudioExportPublisher.publish(
        bytes: Uint8List(512 * 1024),
        selectedPath: selected.path,
        baseName: 'cancelled',
        localTime: DateTime(2026, 8, 31),
        isCancelled: () => true,
        linkTemporaryFile: _linkForTest,
      ),
      throwsA(isA<AudioExportPublishCancelled>()),
    );
    expect(await selected.exists(), isFalse);
    expect(directory.listSync(), isEmpty);
  });

  test('an atomic publication collision retries without overwriting', () async {
    final time = DateTime(2026, 8, 31, 9, 5, 7);
    final selected = File(
      '${directory.path}${Platform.pathSeparator}race_20260831_090507.ogg',
    );
    var firstAttempt = true;

    final published = await AudioExportPublisher.publish(
      bytes: Uint8List.fromList([1, 2, 3, 4]),
      selectedPath: selected.path,
      baseName: 'race',
      localTime: time,
      isCancelled: () => false,
      linkTemporaryFile: (source, target) async {
        if (firstAttempt) {
          firstAttempt = false;
          await File(target).writeAsBytes([9, 8, 7], flush: true);
          return false;
        }
        return _linkForTest(source, target);
      },
    );

    expect(await selected.readAsBytes(), [9, 8, 7]);
    expect(published.uri.pathSegments.last, 'race_20260831_090507_2.ogg');
    expect(await published.readAsBytes(), [1, 2, 3, 4]);
  });
}

Future<bool> _linkForTest(String source, String target) async {
  final destination = File(target);
  if (await destination.exists()) {
    return false;
  }
  await File(source).copy(target);
  return true;
}
