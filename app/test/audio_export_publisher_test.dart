import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/services.dart';
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

  test('falls back to direct no-replace writing when temporary publication fails', () async {
    final time = DateTime(2026, 8, 31, 9, 5, 7);
    final selected = File(
      '${directory.path}${Platform.pathSeparator}fallback_20260831_090507.ogg',
    );
    final bytes = Uint8List.fromList([1, 2, 3, 4]);
    var writeCalls = 0;

    final published = await AudioExportPublisher.publish(
      bytes: bytes,
      selectedPath: selected.path,
      baseName: 'fallback',
      localTime: time,
      isCancelled: () => false,
      linkTemporaryFile: (_, _) async {
        throw PlatformException(code: 'file-publish-error');
      },
      writeBytesNoReplace: (data, target, isCancelled, onProgress) async {
        writeCalls++;
        final destination = File(target);
        if (await destination.exists()) {
          return false;
        }
        await destination.writeAsBytes(data, flush: true);
        onProgress?.call(data.length, data.length);
        return true;
      },
    );

    expect(writeCalls, 1);
    expect(published.path, selected.path);
    expect(await published.readAsBytes(), bytes);
    expect(
      directory.listSync().where((item) => item.path.contains('.otgy-tmp')),
      isEmpty,
    );
  });

  test(
    'accepts an atomic publisher that consumes the temporary file',
    () async {
      final selected = File(
        '${directory.path}${Platform.pathSeparator}renamed_20260831_090507.ogg',
      );

      final published = await AudioExportPublisher.publish(
        bytes: Uint8List.fromList([4, 5, 6]),
        selectedPath: selected.path,
        baseName: 'renamed',
        localTime: DateTime(2026, 8, 31, 9, 5, 7),
        isCancelled: () => false,
        linkTemporaryFile: (source, target) async {
          await File(source).rename(target);
          return true;
        },
      );

      expect(published.path, selected.path);
      expect(await published.readAsBytes(), [4, 5, 6]);
      expect(
        directory.listSync().where((item) => item.path.contains('.otgy-tmp')),
        isEmpty,
      );
    },
  );

  test(
    'Android document picker receives the canonical name and MIME type',
    () async {
      String? method;
      Map<String, Object?>? arguments;

      final uri = await AudioExportPublisher.selectAndroidDocument(
        suggestedName: '曲目_20260902_120000.ogg',
        mimeType: 'audio/ogg',
        invokeMethod: (nextMethod, nextArguments) async {
          method = nextMethod;
          arguments = nextArguments;
          return 'content://documents/export.ogg';
        },
      );

      expect(uri, 'content://documents/export.ogg');
      expect(method, 'createDocument');
      expect(arguments, <String, Object?>{
        'suggestedName': '曲目_20260902_120000.ogg',
        'mimeType': 'audio/ogg',
      });
    },
  );

  test('Android document picker cancellation returns no destination', () async {
    final uri = await AudioExportPublisher.selectAndroidDocument(
      suggestedName: 'cancelled.ogg',
      mimeType: 'audio/ogg',
      invokeMethod: (_, _) async => null,
    );

    expect(uri, isNull);
  });

  test(
    'Android document publication writes chunks and finishes once',
    () async {
      final bytes = Uint8List.fromList(
        List<int>.generate(300000, (index) => index & 0xff),
      );
      final written = BytesBuilder(copy: false);
      final methods = <String>[];
      final progress = <(int, int)>[];

      await AudioExportPublisher.publishAndroidDocument(
        bytes: bytes,
        documentUri: 'content://documents/export.ogg',
        isCancelled: () => false,
        onProgress: (completed, total) => progress.add((completed, total)),
        invokeMethod: (method, arguments) async {
          methods.add(method);
          switch (method) {
            case 'openDocumentWrite':
              expect(arguments['uri'], 'content://documents/export.ogg');
              return 17;
            case 'writeDocumentChunk':
              expect(arguments['session'], 17);
              written.add(arguments['bytes']! as Uint8List);
              break;
            case 'finishDocumentWrite':
              expect(arguments['session'], 17);
              break;
            default:
              fail('Unexpected files channel method: $method');
          }
          return null;
        },
      );

      expect(methods, [
        'openDocumentWrite',
        'writeDocumentChunk',
        'writeDocumentChunk',
        'finishDocumentWrite',
      ]);
      expect(written.takeBytes(), bytes);
      expect(progress, [
        (0, bytes.length),
        (262144, bytes.length),
        (bytes.length, bytes.length),
      ]);
    },
  );

  test(
    'Android document cancellation aborts and deletes partial output',
    () async {
      var cancelled = false;
      final methods = <String>[];

      await expectLater(
        AudioExportPublisher.publishAndroidDocument(
          bytes: Uint8List(300000),
          documentUri: 'content://documents/cancelled.ogg',
          isCancelled: () => cancelled,
          invokeMethod: (method, arguments) async {
            methods.add(method);
            switch (method) {
              case 'openDocumentWrite':
                return 23;
              case 'writeDocumentChunk':
                cancelled = true;
                break;
              case 'abortDocumentWrite':
                expect(arguments['session'], 23);
                expect(arguments['uri'], 'content://documents/cancelled.ogg');
                break;
              default:
                fail('Unexpected files channel method: $method');
            }
            return null;
          },
        ),
        throwsA(isA<AudioExportPublishCancelled>()),
      );
      expect(methods, [
        'openDocumentWrite',
        'writeDocumentChunk',
        'abortDocumentWrite',
      ]);
    },
  );

  test(
    'Android document open failure deletes the reserved destination',
    () async {
      final methods = <String>[];

      await expectLater(
        AudioExportPublisher.publishAndroidDocument(
          bytes: Uint8List.fromList([1, 2, 3]),
          documentUri: 'content://documents/failed.ogg',
          isCancelled: () => false,
          invokeMethod: (method, arguments) async {
            methods.add(method);
            if (method == 'openDocumentWrite') {
              return null;
            }
            if (method == 'deleteDocument') {
              expect(arguments['uri'], 'content://documents/failed.ogg');
              return null;
            }
            fail('Unexpected files channel method: $method');
          },
        ),
        throwsA(isA<FileSystemException>()),
      );
      expect(methods, ['openDocumentWrite', 'deleteDocument']);
    },
  );
}

Future<bool> _linkForTest(String source, String target) async {
  final destination = File(target);
  if (await destination.exists()) {
    return false;
  }
  await File(source).copy(target);
  return true;
}
