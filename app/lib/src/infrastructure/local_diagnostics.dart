import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:otgy_native/otgy_native.dart';
import 'package:path_provider/path_provider.dart';

typedef DiagnosticRecorder = void Function(
  String event,
  Map<String, Object?> fields,
);

final class LocalDiagnostics {
  LocalDiagnostics._(
    this._directory,
    this.identity,
    this.maxFileBytes,
    this.maxFiles,
  ) : session = _newSession();

  static const detailedEnabled = bool.fromEnvironment('OTGY_DIAGNOSTICS');
  static LocalDiagnostics? instance;
  static String? _initializationFailure;
  static String? get initializationFailure => _initializationFailure;

  static Map<String, Object?> sanitizeFields(Map<String, Object?> fields) =>
      _cleanMap(fields);

  static String sanitizeText(String text) => _cleanString(text);

  static const _maxPending = 128;
  static const _maxRecordBytes = 16384;

  final Directory _directory;
  final Map<String, Object?> identity;
  final String session;
  final int maxFileBytes;
  final int maxFiles;
  final _pending = ListQueue<Uint8List>();
  Future<void> _ioTail = Future<void>.value();
  Future<void>? _writer;
  int _sequence = 0;
  int _droppedRecords = 0;
  String? _failure;

  String? get failure => _failure;
  int get droppedRecords => _droppedRecords;

  static String get buildMode => kReleaseMode
      ? 'release'
      : kProfileMode
      ? 'profile'
      : 'debug';

  static Map<String, Object?> get buildIdentity {
    final values = <String, Object?>{
      'appVersion': const String.fromEnvironment('OTGY_APP_VERSION'),
      'buildId': const String.fromEnvironment('OTGY_BUILD_ID'),
      'buildTime': const String.fromEnvironment('OTGY_BUILD_TIME'),
      'apiVersion': null,
      'coreVersion': null,
      'operatingSystem': Platform.operatingSystem,
      'operatingSystemVersion': Platform.operatingSystemVersion,
      'buildMode': buildMode,
    };
    try {
      values['apiVersion'] = OtgyNative.apiVersion;
      values['coreVersion'] = OtgyNative.coreVersion;
    } catch (error) {
      values['nativeVersionFailure'] = _errorSummary(error);
    }
    return Map<String, Object?>.unmodifiable(_cleanMap(values));
  }

  static String _newSession() =>
      '${DateTime.now().toUtc().microsecondsSinceEpoch}-'
      '${Random.secure().nextInt(0x7fffffff).toRadixString(16)}';

  static Future<LocalDiagnostics?> initialize() async {
    if (!detailedEnabled) return null;
    _initializationFailure = null;
    try {
      final support = await getApplicationSupportDirectory();
      final logger = await openAt(
        Directory('${support.path}${Platform.pathSeparator}diagnostics'),
        identity: buildIdentity,
      );
      if (logger.failure != null) {
        _initializationFailure = logger.failure;
      }
      instance = logger;
      return logger;
    } catch (error) {
      _initializationFailure = _errorSummary(error);
      return null;
    }
  }

  static Map<String, Object?> snapshotBundle({
    required Uint8List recoveryPayload,
    required Map<String, Object?> state,
  }) {
    final payload = Uint8List.fromList(recoveryPayload);
    final captureState = _cleanMap(state);
    final capturedAt = DateTime.now().toUtc().toIso8601String();
    final failure = initializationFailure;
    return {
      'schemaVersion': 1,
      'identity': buildIdentity,
      'session': _newSession(),
      'createdAt': DateTime.now().toUtc().toIso8601String(),
      'capturedAt': capturedAt,
      'captureState': captureState,
      'snapshot': {
        'encoding': 'base64',
        'kind': 'recovery',
        'payload': base64Encode(payload),
      },
      'logs': <Map<String, Object?>>[],
      'detailedDiagnosticsEnabled': detailedEnabled,
      'storageFailed': failure != null,
      'initializationFailure': failure,
      'failure': failure,
      'droppedRecords': 0,
      'corruptLines': 0,
    };
  }

  static Future<LocalDiagnostics> openAt(
    Directory directory, {
    required Map<String, Object?> identity,
    int maxFileBytes = 262144,
    int maxFiles = 4,
  }) async {
    if (maxFileBytes <= 0) throw ArgumentError.value(maxFileBytes);
    if (maxFiles <= 0) throw ArgumentError.value(maxFiles);
    final logger = LocalDiagnostics._(
      directory,
      Map<String, Object?>.unmodifiable(_cleanMap(identity)),
      maxFileBytes,
      maxFiles,
    );
    try {
      await directory.create(recursive: true);
      for (final entry in await directory.list(followLinks: false).toList()) {
        if (entry is! File) continue;
        final name = entry.uri.pathSegments.last;
        final match = RegExp(r'^diagnostics-(\d+)\.jsonl$').firstMatch(name);
        if (match == null) continue;
        final index = int.parse(match[1]!);
        if (index >= maxFiles) {
          await entry.delete();
        } else if (await entry.length() > maxFileBytes) {
          final reader = await entry.open(mode: FileMode.read);
          late Uint8List prefix;
          try {
            prefix = await reader.read(maxFileBytes);
          } finally {
            await reader.close();
          }
          final writer = await entry.open(mode: FileMode.append);
          try {
            await writer.truncate(prefix.lastIndexOf(10) + 1);
          } finally {
            await writer.close();
          }
        }
      }
    } catch (error) {
      logger._storageFailed(error);
    }
    return logger;
  }

  /// Fields are diagnostic metadata, never user file contents or clipboard data.
  void record(String event, Map<String, Object?> fields) {
    try {
      final sequence = ++_sequence;
      if (_pending.length >= _maxPending) {
        _droppedRecords++;
        return;
      }
      final record = <String, Object?>{
        'time': DateTime.now().toUtc().toIso8601String(),
        'sequence': sequence,
        'session': session,
        'event': _cleanString(event),
        'fields': _cleanMap({
          ...fields,
          if (event == 'startup') 'identity': identity,
        }),
      };
      final bytes = _encodeRecord(record);
      if (bytes == null) {
        _droppedRecords++;
        return;
      }
      _pending.add(bytes);
      _startWriter();
    } catch (_) {
      _droppedRecords++;
    }
  }

  void recordException(String operation, Object error, StackTrace stack) {
    try {
      record('exception', {
        'operation': operation,
        ..._errorFields(error),
        'stack': sanitizeText(stack.toString()),
      });
    } catch (_) {
      _droppedRecords++;
    }
  }

  Future<void> flush() async {
    while (_writer != null) {
      await _writer;
    }
    await _ioTail;
  }

  Future<Map<String, Object?>> exportBundle({
    required Uint8List recoveryPayload,
    required Map<String, Object?> state,
  }) async {
    final payload = Uint8List.fromList(recoveryPayload);
    final captureState = _cleanMap(state);
    final capturedAt = DateTime.now().toUtc().toIso8601String();
    await flush();
    return _serialize(() async {
      final logs = <Map<String, Object?>>[];
      var corruptLines = 0;
      for (var index = maxFiles - 1; index >= 0; index--) {
        try {
          final file = _file(index);
          if (!await file.exists()) continue;
          final bytes = await file.readAsBytes();
          final text = utf8.decode(bytes, allowMalformed: true);
          for (final line in const LineSplitter().convert(text)) {
            if (line.trim().isEmpty) continue;
            try {
              final parsed = jsonDecode(line);
              if (parsed is! Map<String, dynamic>) {
                corruptLines++;
                continue;
              }
              logs.add(_cleanRecord(parsed));
            } catch (_) {
              corruptLines++;
            }
          }
        } catch (error) {
          _storageFailed(error);
        }
      }
      return {
        'schemaVersion': 1,
        'identity': identity,
        'session': session,
        'createdAt': DateTime.now().toUtc().toIso8601String(),
        'capturedAt': capturedAt,
        'captureState': captureState,
        'snapshot': {
          'encoding': 'base64',
          'kind': 'recovery',
          'payload': base64Encode(payload),
        },
        'logs': logs,
        'corruptLines': corruptLines,
        'droppedRecords': _droppedRecords,
        'detailedDiagnosticsEnabled': detailedEnabled,
        'storageFailed': _failure != null || initializationFailure != null,
        'initializationFailure': initializationFailure,
        'failure': _failure ?? initializationFailure,
      };
    });
  }

  Uint8List? _encodeRecord(Map<String, Object?> record) {
    final limit = min(_maxRecordBytes, maxFileBytes);
    Uint8List encode() =>
        Uint8List.fromList(utf8.encode('${jsonEncode(record)}\n'));
    var bytes = encode();
    if (bytes.length <= limit) return bytes;
    record['truncated'] = true;
    record['originalBytes'] = bytes.length;
    record['fields'] = {
      'truncated': true,
      if (record['event'] == 'startup') 'identity': identity,
    };
    bytes = encode();
    if (bytes.length <= limit) return bytes;
    record['event'] = '[truncated]';
    bytes = encode();
    return bytes.length <= limit ? bytes : null;
  }

  Future<T> _serialize<T>(Future<T> Function() action) {
    final result = _ioTail.then((_) => action());
    _ioTail = result.then<void>((_) {}, onError: (Object _, StackTrace _) {});
    return result;
  }

  void _startWriter() {
    if (_writer != null) return;
    _writer =
        _serialize(() async {
          while (_pending.isNotEmpty) {
            final bytes = _pending.removeFirst();
            try {
              await _append(bytes);
            } catch (error) {
              _storageFailed(error);
              _droppedRecords++;
            }
          }
        }).whenComplete(() {
          _writer = null;
          if (_pending.isNotEmpty) _startWriter();
        });
  }

  File _file(int index) => File(
    '${_directory.path}${Platform.pathSeparator}diagnostics-$index.jsonl',
  );

  Future<void> _append(Uint8List bytes) async {
    final current = _file(0);
    final length = await current.exists() ? await current.length() : 0;
    if (length + bytes.length > maxFileBytes) {
      final oldest = _file(maxFiles - 1);
      if (await oldest.exists()) await oldest.delete();
      for (var index = maxFiles - 2; index >= 0; index--) {
        final source = _file(index);
        if (await source.exists()) await source.rename(_file(index + 1).path);
      }
    }
    await current.writeAsBytes(bytes, mode: FileMode.append, flush: true);
  }

  void _storageFailed(Object error) {
    try {
      _failure = _errorSummary(error);
    } catch (_) {
      _failure = 'Storage failure';
    }
  }

  static const _safePlatformCodes = {
    'io',
    'permission_denied',
    'access_denied',
    'not_found',
    'unavailable',
    'unsupported',
    'cancelled',
  };

  static Map<String, Object?> _cleanRecord(Map<String, dynamic> record) {
    final result = <String, Object?>{
      for (final key in [
        'time',
        'sequence',
        'session',
        'event',
        'truncated',
        'originalBytes',
      ])
        if (record.containsKey(key))
          key: _cleanValue(record[key], HashSet<Object>.identity(), 0),
    };
    final rawFields = record['fields'];
    if (rawFields is Map) {
      if (record['event'] == 'exception') {
        final exceptionFields = <String, Object?>{
          for (final key in [
            'operation',
            'type',
            'domain',
            'code',
            'status',
            'osCode',
            'stack',
          ])
            if (switch (key) {
              'domain' || 'status' || 'osCode' => rawFields[key] is int,
              'code' => rawFields[key] is int || rawFields[key] is String,
              _ => rawFields[key] is String,
            })
              key: rawFields[key],
        };
        if (rawFields['state'] is Map) {
          exceptionFields['state'] = _cleanExceptionState(
            rawFields['state'] as Map,
          );
        }
        final fields = _cleanMap(exceptionFields);
        final code = fields['code'];
        if (code is String && !_safePlatformCodes.contains(code)) {
          fields['code'] = '[redacted code]';
        }
        result['fields'] = fields;
      } else {
        result['fields'] = _cleanMap(
          rawFields.map(
            (key, value) =>
                MapEntry(key is String ? key : '[redacted key]', value),
          ),
        );
      }
    }
    return result;
  }

  static Map<String, Object?> _cleanExceptionState(Map state) {
    final result = <String, Object?>{};
    for (final key in [
      'revision',
      'markerCount',
      'showtimeMoveCount',
      'selectionCount',
      'clipboardCount',
      'pastePreviewAnchorRow',
      'pastePreviewCode',
    ]) {
      if (state[key] is int) result[key] = state[key];
    }
    for (final key in ['bpm', 'offset', 'currentRow']) {
      final value = state[key];
      if (value is num && value.isFinite) result[key] = value;
    }
    for (final key in [
      'dirty',
      'hasAudio',
      'canUndo',
      'canRedo',
      'hasSelection',
      'hasClipboard',
      'cutPending',
    ]) {
      if (state[key] is bool) result[key] = state[key];
    }
    final sourceKind = state['sourceKind'];
    if (sourceKind is String &&
        const {'untitled', 'importedSlk', 'savedDraft'}.contains(sourceKind)) {
      result['sourceKind'] = sourceKind;
    }
    final interactionMode = state['interactionMode'];
    if (interactionMode is String &&
        const {
          'normal',
          'selection',
          'pastePending',
        }.contains(interactionMode)) {
      result['interactionMode'] = interactionMode;
    }
    return result;
  }

  static Map<String, Object?> _errorFields(Object error) => {
    'type': error.runtimeType.toString(),
    if (error is OtgyException) ...{
      'domain': error.domain,
      'code': error.code,
      'status': error.status,
    },
    if (error is FileSystemException && error.osError != null)
      'osCode': error.osError!.errorCode,
    if (error is OSError) 'osCode': error.errorCode,
    if (error is PlatformException)
      'code': _safePlatformCodes.contains(error.code)
          ? error.code
          : '[redacted code]',
  };

  static String _errorSummary(Object error) => jsonEncode(_errorFields(error));

  static Map<String, Object?> _cleanMap(Map<String, Object?> input) =>
      _cleanValue(input, HashSet<Object>.identity(), 0) as Map<String, Object?>;

  static Object? _cleanValue(Object? value, Set<Object> seen, int depth) {
    if (value == null || value is bool || value is int) return value;
    if (value is double) return value.isFinite ? value : value.toString();
    if (value is String) return _cleanString(value);
    if (value is TypedData) return '[redacted binary]';
    if (depth >= 16 || seen.contains(value)) return '[truncated structure]';
    if (value is Map) {
      seen.add(value);
      final result = <String, Object?>{};
      for (final entry in value.entries) {
        if (entry.key is! String) continue;
        final key = _cleanString(entry.key as String);
        final normalizedKey = key.toLowerCase();
        final sensitive =
            RegExp(
              r'^(fileNames?|draftNames?|sourceNames?|targetNames?|audioNames?|audioFileNames?|audio|audioBytes|recoveryPayload|message|details|error|exception)$',
              caseSensitive: false,
            ).hasMatch(key) ||
            const {
              'selection',
              'selections',
              'objectid',
              'objectids',
              'eventid',
              'eventids',
              'primaryeventid',
              'dancemovedrillid',
              'drillid',
              'groupid',
            }.contains(normalizedKey) ||
            (normalizedKey.startsWith('clipboard') &&
                normalizedKey != 'clipboardcount');
        result[key] = sensitive
            ? '[redacted]'
            : _cleanValue(entry.value, seen, depth + 1);
      }
      seen.remove(value);
      return result;
    }
    if (value is Iterable) {
      seen.add(value);
      final result = value
          .take(256)
          .map((item) => _cleanValue(item, seen, depth + 1))
          .toList();
      seen.remove(value);
      return result;
    }
    return '[redacted object]';
  }

  static String _cleanString(String value) {
    var result = value.replaceAll(
      RegExp(
        r'''(["'])(?:(?:file|content):/{1,3}|[a-zA-Z]:[\\/]|\\\\|/)[^\r\n]*?\1''',
        caseSensitive: false,
      ),
      '[redacted path]',
    );
    result = result.replaceAll(
      RegExp(r'''\b(?:file|content):/[^\r\n"'<>]*''', caseSensitive: false),
      '[redacted uri]',
    );
    result = result.replaceAll(
      RegExp(r'''(?:\b[a-zA-Z]:[\\/]|\\\\)[^\r\n"'<>]*'''),
      '[redacted path]',
    );
    return result.replaceAllMapped(
      RegExp(r'''(^|[\s("'=])/(?!/)[^\r\n"'<>]*''', multiLine: true),
      (match) => '${match[1]}[redacted path]',
    );
  }
}
