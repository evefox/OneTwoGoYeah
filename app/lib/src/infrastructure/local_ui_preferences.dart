import 'dart:convert';
import 'dart:io';

import 'package:path_provider/path_provider.dart';

/// Small, best-effort preferences that are independent of any project draft.
///
/// The only current value records whether the usage-guide reminder has been
/// shown once. It lives beside the local project store so the app stays fully
/// offline and never has to touch `ChartDraft`, dirty state, or BPM/Offset.
final class LocalUiPreferences {
  LocalUiPreferences._(this._file, this._guideHintSeen);

  static const fileName = 'ui-preferences.json';
  static const directoryName = 'OneTwoGoYeah';

  final File? _file;
  bool _guideHintSeen;

  bool get guideHintSeen => _guideHintSeen;

  /// Session-only preferences for tests and for callers that do not have a
  /// writable application-support directory.
  factory LocalUiPreferences.inMemory({bool guideHintSeen = false}) =>
      LocalUiPreferences._(null, guideHintSeen);

  static Future<LocalUiPreferences> load() async {
    try {
      final support = await getApplicationSupportDirectory();
      final directory = Directory(
        '${support.path}${Platform.pathSeparator}$directoryName',
      );
      return await openAt(directory);
    } on Object {
      // If preferences cannot be loaded, behave like a returning launch so a
      // transient storage failure cannot turn the two-minute hint into a loop.
      return LocalUiPreferences._(null, true);
    }
  }

  static Future<LocalUiPreferences> openAt(Directory directory) async {
    final file = File('${directory.path}${Platform.pathSeparator}$fileName');
    var seen = false;
    try {
      if (await file.exists()) {
        final decoded = jsonDecode(await file.readAsString());
        seen =
            decoded is Map<String, dynamic> && decoded['guideHintSeen'] == true;
      }
    } on FormatException {
      seen = false;
    } on FileSystemException {
      seen = false;
    }
    return LocalUiPreferences._(file, seen);
  }

  Future<void> markGuideHintSeen() async {
    if (_guideHintSeen) {
      return;
    }
    _guideHintSeen = true;
    final file = _file;
    if (file == null) {
      return;
    }
    try {
      await file.parent.create(recursive: true);
      await file.writeAsString(
        jsonEncode({'guideHintSeen': true}),
        flush: true,
      );
    } on FileSystemException {
      // The reminder is a convenience, not project data. Failure to persist
      // only means the next launch may use the longer first-use duration.
    }
  }
}
