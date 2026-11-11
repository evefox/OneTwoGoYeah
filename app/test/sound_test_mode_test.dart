import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:one_two_go_yeah/src/ui/editor_canvas.dart';

const _trimmedClips = <String>{
  'f1-6.wav',
  'f1-8.wav',
  'f1-9.wav',
  'm1-4.wav',
  'm1-6.wav',
  'm1-7.wav',
  'm1-9.wav',
};

void main() {
  late Directory temporaryDirectory;
  late LocalProjectStore store;
  late EditorController controller;

  setUp(() async {
    soundTestMenuEnabled = false;
    temporaryDirectory = await Directory.systemTemp.createTemp(
      'otgy-sound-test-',
    );
    store = await LocalProjectStore.openAt(temporaryDirectory);
    controller = EditorController();
  });

  tearDown(() async {
    controller.dispose();
    if (await temporaryDirectory.exists()) {
      await temporaryDirectory.delete(recursive: true);
    }
  });

  Future<void> pumpEditor(
    WidgetTester tester, {
    Size size = const Size(1280, 800),
  }) async {
    await tester.binding.setSurfaceSize(size);
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pump();
  }

  testWidgets('the hidden sound test entry is disabled by default', (
    tester,
  ) async {
    await pumpEditor(tester);

    await tester.longPress(find.byKey(const ValueKey('usage-guide-button')));
    await tester.pumpAndSettle();

    expect(
      find.byKey(const ValueKey('playback-sound-test-dialog')),
      findsNothing,
    );
    // Without a long-press handler the press is a slow tap, so the normal
    // usage-guide action still runs.
    expect(find.byKey(const ValueKey('usage-guide-screen')), findsOneWidget);
  });

  testWidgets('long-pressing help opens the hidden sound test menu', (
    tester,
  ) async {
    soundTestMenuEnabled = true;
    await pumpEditor(tester);

    expect(controller.playbackSoundVariant, PlaybackSoundVariant.trimmed);

    await tester.longPress(find.byKey(const ValueKey('usage-guide-button')));
    await tester.pumpAndSettle();

    expect(
      find.byKey(const ValueKey('playback-sound-test-dialog')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('usage-guide-screen')), findsNothing);
    expect(find.text('音效测试'), findsOneWidget);
    expect(find.text('音效版本'), findsOneWidget);
    expect(find.text('原始'), findsOneWidget);
    expect(find.text('裁切'), findsOneWidget);

    await tester.tap(find.text('原始'));
    await tester.pump();
    expect(controller.playbackSoundVariant, PlaybackSoundVariant.original);

    await tester.tap(find.text('关闭'));
    await tester.pumpAndSettle();
    expect(
      find.byKey(const ValueKey('playback-sound-test-dialog')),
      findsNothing,
    );
  });

  testWidgets('the hidden sound test menu fits a phone layout', (tester) async {
    soundTestMenuEnabled = true;
    await pumpEditor(tester, size: const Size(390, 844));

    await tester.longPress(find.byKey(const ValueKey('usage-guide-button')));
    await tester.pumpAndSettle();

    expect(tester.takeException(), isNull);
    expect(
      find.byKey(const ValueKey('playback-sound-test-dialog')),
      findsOneWidget,
    );
  });

  testWidgets('short-pressing help still opens the guide', (tester) async {
    await pumpEditor(tester);

    await tester.tap(find.byKey(const ValueKey('usage-guide-button')));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('usage-guide-screen')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('playback-sound-test-dialog')),
      findsNothing,
    );
  });

  testWidgets('the hidden sound test menu follows the general gate', (
    tester,
  ) async {
    soundTestMenuEnabled = true;
    await pumpEditor(tester);
    controller.enterSelectionMode();
    await tester.pump();

    await tester.longPress(find.byKey(const ValueKey('usage-guide-button')));
    await tester.pumpAndSettle();

    expect(
      find.byKey(const ValueKey('playback-sound-test-dialog')),
      findsNothing,
    );
  });

  test('the variant is session-scoped and blocked by the selection gate', () {
    expect(controller.playbackSoundVariant, PlaybackSoundVariant.trimmed);
    controller.setPlaybackSoundVariant(PlaybackSoundVariant.original);
    expect(controller.playbackSoundVariant, PlaybackSoundVariant.original);

    controller.enterSelectionMode();
    controller.setPlaybackSoundVariant(PlaybackSoundVariant.trimmed);
    expect(controller.playbackSoundVariant, PlaybackSoundVariant.original);

    controller.exitChartInteraction();
    controller.setPlaybackSoundVariant(PlaybackSoundVariant.trimmed);
    expect(controller.playbackSoundVariant, PlaybackSoundVariant.trimmed);
  });

  test('trimmed clips only remove leading digital silence', () {
    final directory = Directory('assets/sounds_trimmed/zh');
    expect(directory.existsSync(), isTrue);
    final generated = directory
        .listSync()
        .whereType<File>()
        .map((file) => file.uri.pathSegments.last)
        .where((name) => name.endsWith('.wav'))
        .toSet();
    expect(generated, _trimmedClips);

    for (final name in _trimmedClips) {
      expect(
        File('assets/sounds/zh/$name').existsSync(),
        isFalse,
        reason: '$name must stay out of the bundled sound directory',
      );
      final original = _readWav(
        File('assets/sounds_original/zh/$name').readAsBytesSync(),
      );
      final trimmed = _readWav(
        File('assets/sounds_trimmed/zh/$name').readAsBytesSync(),
      );

      expect(trimmed.channels, original.channels, reason: name);
      expect(trimmed.sampleRate, original.sampleRate, reason: name);
      expect(trimmed.bitsPerSample, original.bitsPerSample, reason: name);
      expect(trimmed.bitsPerSample, 16, reason: name);

      final blockAlign = original.channels * original.bitsPerSample ~/ 8;
      final removedBytes = original.data.length - trimmed.data.length;
      expect(removedBytes, greaterThan(0), reason: name);
      expect(removedBytes % blockAlign, 0, reason: name);
      expect(
        trimmed.data,
        orderedEquals(original.data.sublist(removedBytes)),
        reason: '$name must keep every sample after the removed silence',
      );
      expect(
        trimmed.data.sublist(0, blockAlign).any((byte) => byte != 0),
        isTrue,
        reason: '$name must start on a non-silent frame',
      );

      final removedMs = removedBytes / blockAlign / original.sampleRate * 1000;
      expect(removedMs, greaterThanOrEqualTo(1), reason: name);
      expect(removedMs, lessThan(31), reason: name);
    }
  });

  test('shared clips ship once outside the language directories', () {
    for (final name in const ['space_bar', 'turn_1', 'turn_2']) {
      expect(
        File('assets/sounds/common/$name.wav').existsSync(),
        isTrue,
        reason: name,
      );
      expect(
        File('assets/sounds/en/$name.wav').existsSync(),
        isFalse,
        reason: '$name must not be duplicated under en',
      );
      expect(
        File('assets/sounds/zh/$name.wav').existsSync(),
        isFalse,
        reason: '$name must not be duplicated under zh',
      );
    }
  });

  test('English clips have no trimmed variant', () {
    expect(Directory('assets/sounds_trimmed/en').existsSync(), isFalse);
  });
}

final class _Pcm16Wav {
  const _Pcm16Wav({
    required this.channels,
    required this.sampleRate,
    required this.bitsPerSample,
    required this.data,
  });

  final int channels;
  final int sampleRate;
  final int bitsPerSample;
  final Uint8List data;
}

_Pcm16Wav _readWav(Uint8List bytes) {
  expect(String.fromCharCodes(bytes.sublist(0, 4)), 'RIFF');
  expect(String.fromCharCodes(bytes.sublist(8, 12)), 'WAVE');
  final header = ByteData.sublistView(bytes);
  var offset = 12;
  var channels = 0;
  var sampleRate = 0;
  var bitsPerSample = 0;
  while (offset + 8 <= bytes.length) {
    final chunkId = String.fromCharCodes(bytes.sublist(offset, offset + 4));
    final chunkSize = header.getUint32(offset + 4, Endian.little);
    final body = offset + 8;
    if (chunkId == 'fmt ') {
      expect(header.getUint16(body, Endian.little), 1, reason: 'PCM format');
      channels = header.getUint16(body + 2, Endian.little);
      sampleRate = header.getUint32(body + 4, Endian.little);
      bitsPerSample = header.getUint16(body + 14, Endian.little);
    } else if (chunkId == 'data') {
      return _Pcm16Wav(
        channels: channels,
        sampleRate: sampleRate,
        bitsPerSample: bitsPerSample,
        data: Uint8List.sublistView(bytes, body, body + chunkSize),
      );
    }
    offset = body + chunkSize + (chunkSize.isOdd ? 1 : 0);
  }
  throw StateError('WAV has no data chunk');
}
