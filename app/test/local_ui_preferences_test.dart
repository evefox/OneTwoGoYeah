import 'dart:io';

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
  });
}
