import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test('macOS sandbox grants access to user-selected files', () {
    for (final path in [
      'macos/Runner/DebugProfile.entitlements',
      'macos/Runner/Release.entitlements',
    ]) {
      final entitlements = File(path).readAsStringSync();
      expect(
        entitlements,
        contains('com.apple.security.files.user-selected.read-write'),
        reason: path,
      );
    }
  });
}
