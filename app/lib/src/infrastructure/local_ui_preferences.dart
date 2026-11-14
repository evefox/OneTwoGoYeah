import 'dart:convert';
import 'dart:io';

import 'package:path_provider/path_provider.dart';

/// Small, best-effort preferences that are independent of any project draft.
///
/// Two values live here: whether the usage-guide reminder has been shown once,
/// and the desktop keymap the player rebound from the usage guide. Both sit
/// beside the local project store so the app stays fully offline and never has
/// to touch `ChartDraft`, dirty state, BPM/Offset, or any domain command.
final class LocalUiPreferences {
  LocalUiPreferences._(this._file, this._guideHintSeen, this._shortcuts);

  static const fileName = 'ui-preferences.json';
  static const directoryName = 'OneTwoGoYeah';

  final File? _file;
  bool _guideHintSeen;
  Map<String, Object?>? _shortcuts;

  bool get guideHintSeen => _guideHintSeen;

  /// Raw keymap document owned by the usage guide, or null when the player has
  /// never rebound a key. The UI layer decodes it so this store stays free of
  /// Flutter key types.
  Map<String, Object?>? get shortcuts => _shortcuts;

  /// Session-only preferences for tests and for callers that do not have a
  /// writable application-support directory.
  factory LocalUiPreferences.inMemory({
    bool guideHintSeen = false,
    Map<String, Object?>? shortcuts,
  }) => LocalUiPreferences._(null, guideHintSeen, shortcuts);

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
      return LocalUiPreferences._(null, true, null);
    }
  }

  static Future<LocalUiPreferences> openAt(Directory directory) async {
    final file = File('${directory.path}${Platform.pathSeparator}$fileName');
    var seen = false;
    Map<String, Object?>? shortcuts;
    try {
      if (await file.exists()) {
        final decoded = jsonDecode(await file.readAsString());
        if (decoded is Map<String, dynamic>) {
          seen = decoded['guideHintSeen'] == true;
          final storedShortcuts = decoded['shortcuts'];
          if (storedShortcuts is Map) {
            shortcuts = Map<String, Object?>.from(storedShortcuts);
          }
        }
      }
    } on FormatException {
      seen = false;
      shortcuts = null;
    } on FileSystemException {
      seen = false;
      shortcuts = null;
    }
    return LocalUiPreferences._(file, seen, shortcuts);
  }

  Future<void> markGuideHintSeen() async {
    if (_guideHintSeen) {
      return;
    }
    _guideHintSeen = true;
    await _write();
  }

  /// Persists the keymap document. Rewriting the whole file keeps
  /// `guideHintSeen` alongside the shortcuts instead of dropping it.
  Future<void> saveShortcuts(Map<String, Object?> shortcuts) async {
    _shortcuts = Map<String, Object?>.unmodifiable(shortcuts);
    await _write();
  }

  Future<void> _write() async {
    final file = _file;
    if (file == null) {
      return;
    }
    try {
      await file.parent.create(recursive: true);
      await file.writeAsString(
        jsonEncode({
          if (_guideHintSeen) 'guideHintSeen': true,
          if (_shortcuts != null) 'shortcuts': _shortcuts,
        }),
        flush: true,
      );
    } on FileSystemException {
      // Preferences are a convenience, not project data. Failure to persist
      // only means the next launch may use the longer first-use duration or
      // the default keymap.
    }
  }
}
