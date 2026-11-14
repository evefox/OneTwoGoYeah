import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_ui_preferences.dart';

void main() {
  late Directory directory;

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-ui-preferences-');
  });

  tearDown(() async {
    if (await directory.exists()) {
      await directory.delete(recursive: true);
    }
  });

  test('defaults to first use when no preferences file exists', () async {
    final preferences = await LocalUiPreferences.openAt(directory);
    expect(preferences.guideHintSeen, isFalse);
  });

  test('persists the one-time reminder flag', () async {
    final preferences = await LocalUiPreferences.openAt(directory);
    await preferences.markGuideHintSeen();

    expect(preferences.guideHintSeen, isTrue);
    final reloaded = await LocalUiPreferences.openAt(directory);
    expect(reloaded.guideHintSeen, isTrue);
  });

  test('treats a corrupted preferences file as first use', () async {
    final file = File(
      '${directory.path}${Platform.pathSeparator}'
      '${LocalUiPreferences.fileName}',
    );
    await file.writeAsString('{not json');

    final preferences = await LocalUiPreferences.openAt(directory);
    expect(preferences.guideHintSeen, isFalse);
    expect(preferences.shortcuts, isNull);
  });

  test('persists the keymap beside the reminder flag', () async {
    final preferences = await LocalUiPreferences.openAt(directory);
    await preferences.markGuideHintSeen();
    await preferences.saveShortcuts({
      'version': 1,
      'bindings': {
        'undo': [
          {'key': LogicalKeyboardKey.f9.keyId, 'ctrl': true},
        ],
      },
    });

    final reloaded = await LocalUiPreferences.openAt(directory);
    expect(reloaded.guideHintSeen, isTrue);
    expect(reloaded.shortcuts, isNotNull);
    final bindings = reloaded.shortcuts!['bindings']! as Map<String, Object?>;
    expect(bindings['undo'], [
      {'key': LogicalKeyboardKey.f9.keyId, 'ctrl': true},
    ]);
  });

  test('keeps a stored keymap when the reminder is marked later', () async {
    final preferences = await LocalUiPreferences.openAt(directory);
    await preferences.saveShortcuts({
      'version': 1,
      'bindings': <String, Object?>{},
    });
    await preferences.markGuideHintSeen();

    final reloaded = await LocalUiPreferences.openAt(directory);
    expect(reloaded.guideHintSeen, isTrue);
    expect(reloaded.shortcuts, isNotNull);
    expect(reloaded.shortcuts!['bindings'], isEmpty);
  });
}
