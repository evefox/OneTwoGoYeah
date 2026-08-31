import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:path_provider/path_provider.dart';

const draftNameMaxCharacters = 80;

String normalizeDraftName(String value) {
  final normalized = value.trim();
  if (normalized.isEmpty) {
    throw const FormatException('草稿名不能为空');
  }
  if (normalized.runes.length > draftNameMaxCharacters) {
    throw const FormatException('草稿名不能超过 80 个字符');
  }
  return normalized;
}

final class DraftSummary {
  const DraftSummary({
    required this.id,
    required this.name,
    required this.updatedAt,
    required this.payload,
  });

  final String id;
  final String name;
  final DateTime updatedAt;
  final Uint8List payload;
}

final class SessionStart {
  const SessionStart({required this.wasInterrupted, this.recoveryPayload});

  final bool wasInterrupted;
  final Uint8List? recoveryPayload;
}

final class LocalProjectStore {
  LocalProjectStore._(this._directory);

  static const _recordVersion = 1;
  static const _formalKind = 'formal-draft';
  static const _recoveryKind = 'recovery';

  final Directory _directory;
  final Random _random = Random.secure();

  static Future<LocalProjectStore> open() async {
    final support = await getApplicationSupportDirectory();
    final directory = Directory(
      '${support.path}${Platform.pathSeparator}OneTwoGoYeah',
    );
    await directory.create(recursive: true);
    final store = LocalProjectStore._(directory);
    await store._removeStaleTemporaryFiles();
    return store;
  }

  static Future<LocalProjectStore> openAt(Directory directory) async {
    await directory.create(recursive: true);
    final store = LocalProjectStore._(directory);
    await store._removeStaleTemporaryFiles();
    return store;
  }

  Future<SessionStart> beginSession() async {
    final marker = File(_path('active-session'));
    final wasInterrupted = await marker.exists();
    final recovery = wasInterrupted ? await readRecovery() : null;
    await marker.writeAsString(
      DateTime.now().toUtc().toIso8601String(),
      flush: true,
    );
    return SessionStart(
      wasInterrupted: wasInterrupted,
      recoveryPayload: recovery,
    );
  }

  Future<void> endSessionNormally() async {
    final marker = File(_path('active-session'));
    if (await marker.exists()) {
      await marker.delete();
    }
  }

  Future<List<DraftSummary>> listDrafts() async {
    final records = await _readRecords(_formalKind);
    final latest = <String, DraftSummary>{};
    for (final record in records) {
      final id = record.id;
      final previous = latest[id];
      if (previous == null || record.updatedAt.isAfter(previous.updatedAt)) {
        latest[id] = record;
      }
    }
    final result = latest.values.toList()
      ..sort((left, right) => right.updatedAt.compareTo(left.updatedAt));
    return List.unmodifiable(result);
  }

  Future<DraftSummary?> findByName(String name) async {
    final normalized = normalizeDraftName(name);
    for (final draft in await listDrafts()) {
      if (draft.name == normalized) {
        return draft;
      }
    }
    return null;
  }

  Future<DraftSummary> saveDraft({
    String? id,
    required String name,
    required Uint8List payload,
  }) async {
    final normalized = normalizeDraftName(name);
    final draftId = id ?? _newId();
    final updatedAt = DateTime.now().toUtc();
    final record = <String, Object>{
      'version': _recordVersion,
      'kind': _formalKind,
      'id': draftId,
      'name': normalized,
      'updatedAt': updatedAt.toIso8601String(),
      'payload': base64Encode(payload),
    };
    final finalFile = await _publishRecord(
      prefix: 'draft-$draftId',
      record: record,
    );
    await _removeSuperseded(
      prefix: 'draft-$draftId-',
      keepPath: finalFile.path,
    );
    return DraftSummary(
      id: draftId,
      name: normalized,
      updatedAt: updatedAt,
      payload: Uint8List.fromList(payload),
    );
  }

  Future<void> deleteDraft(String id) async {
    final files = await _dataFiles();
    for (final file in files) {
      if (_baseName(file.path).startsWith('draft-$id-')) {
        await file.delete();
      }
    }
  }

  Future<void> writeRecovery(Uint8List payload) async {
    final record = <String, Object>{
      'version': _recordVersion,
      'kind': _recoveryKind,
      'updatedAt': DateTime.now().toUtc().toIso8601String(),
      'payload': base64Encode(payload),
    };
    final finalFile = await _publishRecord(prefix: 'recovery', record: record);
    await _removeSuperseded(prefix: 'recovery-', keepPath: finalFile.path);
  }

  Future<Uint8List?> readRecovery() async {
    final records = await _readRecords(_recoveryKind);
    if (records.isEmpty) {
      return null;
    }
    records.sort((left, right) => right.updatedAt.compareTo(left.updatedAt));
    return records.first.payload;
  }

  Future<void> deleteRecovery() async {
    final files = await _dataFiles();
    for (final file in files) {
      if (_baseName(file.path).startsWith('recovery-')) {
        await file.delete();
      }
    }
  }

  Future<File> _publishRecord({
    required String prefix,
    required Map<String, Object> record,
  }) async {
    final token = '${DateTime.now().microsecondsSinceEpoch}-${_randomHex()}';
    final temporary = File(_path('.tmp-$prefix-$token'));
    final finalFile = File(_path('$prefix-$token.otgydata'));
    await temporary.writeAsString(jsonEncode(record), flush: true);
    return temporary.rename(finalFile.path);
  }

  Future<List<DraftSummary>> _readRecords(String kind) async {
    final result = <DraftSummary>[];
    for (final file in await _dataFiles()) {
      try {
        final decoded = jsonDecode(await file.readAsString());
        if (decoded is! Map<String, dynamic> ||
            decoded['version'] != _recordVersion ||
            decoded['kind'] != kind ||
            decoded['updatedAt'] is! String ||
            decoded['payload'] is! String) {
          continue;
        }
        final updatedAt = DateTime.parse(decoded['updatedAt'] as String)
            .toUtc();
        final payload = Uint8List.fromList(
          base64Decode(decoded['payload'] as String),
        );
        if (kind == _formalKind) {
          if (decoded['id'] is! String || decoded['name'] is! String) {
            continue;
          }
          result.add(
            DraftSummary(
              id: decoded['id'] as String,
              name: normalizeDraftName(decoded['name'] as String),
              updatedAt: updatedAt,
              payload: payload,
            ),
          );
        } else {
          result.add(
            DraftSummary(
              id: _recoveryKind,
              name: _recoveryKind,
              updatedAt: updatedAt,
              payload: payload,
            ),
          );
        }
      } on Object {
        // A truncated or corrupt record is ignored; the previous immutable
        // generation remains available.
      }
    }
    return result;
  }

  Future<List<File>> _dataFiles() async {
    final result = <File>[];
    await for (final entity in _directory.list(followLinks: false)) {
      if (entity is File && entity.path.endsWith('.otgydata')) {
        result.add(entity);
      }
    }
    return result;
  }

  Future<void> _removeSuperseded({
    required String prefix,
    required String keepPath,
  }) async {
    for (final file in await _dataFiles()) {
      if (file.path != keepPath && _baseName(file.path).startsWith(prefix)) {
        await file.delete();
      }
    }
  }

  Future<void> _removeStaleTemporaryFiles() async {
    await for (final entity in _directory.list(followLinks: false)) {
      if (entity is File && _baseName(entity.path).startsWith('.tmp-')) {
        await entity.delete();
      }
    }
  }

  String _newId() => '${DateTime.now().microsecondsSinceEpoch}-${_randomHex()}';

  String _randomHex() => List.generate(
    16,
    (_) => _random.nextInt(256).toRadixString(16).padLeft(2, '0'),
  ).join();

  String _path(String name) =>
      '${_directory.path}${Platform.pathSeparator}$name';

  static String _baseName(String path) =>
      path.split(Platform.pathSeparator).last;
}
