import 'dart:io';

import 'package:flutter/services.dart';
import 'package:otgy_native/otgy_native.dart';

final class AudioExportPublishCancelled implements Exception {
  const AudioExportPublishCancelled();
}

typedef LinkTemporaryFile = Future<bool> Function(
  String temporaryPath,
  String targetPath,
);

typedef WriteBytesNoReplace = Future<bool> Function(
  Uint8List bytes,
  String targetPath,
  bool Function() isCancelled,
  void Function(int completedUnits, int totalUnits)? onProgress,
);

abstract final class AudioExportPublisher {
  static const _chunkSize = 256 * 1024;
  static const _filesChannel = MethodChannel('io.onetwogoyeah/files');

  static Future<File> publish({
    required Uint8List bytes,
    required String selectedPath,
    required String baseName,
    required DateTime localTime,
    required bool Function() isCancelled,
    void Function(int completedUnits, int totalUnits)? onProgress,
    LinkTemporaryFile linkTemporaryFile = _linkTemporaryFile,
    WriteBytesNoReplace? writeBytesNoReplace,
  }) async {
    var target = File(selectedPath);
    // A sandboxed macOS save panel can authorize only the selected path, not
    // a sibling temporary name. Keep the normal atomic path first and use the
    // platform writer only when that path is unavailable.
    final directWriter =
        writeBytesNoReplace ?? (Platform.isMacOS ? _writeBytesNoReplace : null);
    File? temporary;
    try {
      temporary = await _createTemporaryFile(target);
    } on FileSystemException {
      if (directWriter == null) {
        rethrow;
      }
      return _publishDirect(
        bytes: bytes,
        target: target,
        baseName: baseName,
        localTime: localTime,
        isCancelled: isCancelled,
        onProgress: onProgress,
        writeBytesNoReplace: directWriter,
      );
    }
    var published = false;
    try {
      final sink = await temporary.open(mode: FileMode.writeOnly);
      try {
        var completed = 0;
        onProgress?.call(completed, bytes.length);
        while (completed < bytes.length) {
          if (isCancelled()) {
            throw const AudioExportPublishCancelled();
          }
          final end = (completed + _chunkSize).clamp(0, bytes.length);
          await sink.writeFrom(bytes, completed, end);
          completed = end;
          onProgress?.call(completed, bytes.length);
        }
        await sink.flush();
      } finally {
        await sink.close();
      }
      if (isCancelled()) {
        throw const AudioExportPublishCancelled();
      }
      try {
        for (var collisionIndex = 2; ; collisionIndex++) {
          if (isCancelled()) {
            throw const AudioExportPublishCancelled();
          }
          if (await linkTemporaryFile(temporary.path, target.path)) {
            if (await temporary.exists()) {
              await temporary.delete();
            }
            published = true;
            return target;
          }
          final name = OtgyNative.makeAudioExportFileName(
            baseName: baseName,
            localTime: localTime,
            collisionIndex: collisionIndex,
          );
          target = File('${target.parent.path}${Platform.pathSeparator}$name');
        }
      } on PlatformException catch (error) {
        if (directWriter == null || error.code != 'file-publish-error') {
          rethrow;
        }
        return await _publishDirect(
          bytes: bytes,
          target: target,
          baseName: baseName,
          localTime: localTime,
          isCancelled: isCancelled,
          onProgress: onProgress,
          writeBytesNoReplace: directWriter,
        );
      }
    } finally {
      if (!published && await temporary.exists()) {
        await temporary.delete();
      }
    }
  }

  static Future<File> _publishDirect({
    required Uint8List bytes,
    required File target,
    required String baseName,
    required DateTime localTime,
    required bool Function() isCancelled,
    required void Function(int completedUnits, int totalUnits)? onProgress,
    required WriteBytesNoReplace writeBytesNoReplace,
  }) async {
    for (var collisionIndex = 1; ; collisionIndex++) {
      if (isCancelled()) {
        throw const AudioExportPublishCancelled();
      }
      if (await writeBytesNoReplace(
        bytes,
        target.path,
        isCancelled,
        onProgress,
      )) {
        return target;
      }
      final name = OtgyNative.makeAudioExportFileName(
        baseName: baseName,
        localTime: localTime,
        collisionIndex: collisionIndex + 1,
      );
      target = File('${target.parent.path}${Platform.pathSeparator}$name');
    }
  }

  static Future<bool> _linkTemporaryFile(
    String temporaryPath,
    String targetPath,
  ) async {
    final published = await _filesChannel.invokeMethod<bool>(
      'linkNoReplace',
      <String, String>{'sourcePath': temporaryPath, 'targetPath': targetPath},
    );
    if (published == null) {
      throw const FileSystemException(
        'Platform file publisher returned no result.',
      );
    }
    return published;
  }

  static Future<bool> _writeBytesNoReplace(
    Uint8List bytes,
    String targetPath,
    bool Function() isCancelled,
    void Function(int completedUnits, int totalUnits)? onProgress,
  ) async {
    if (isCancelled()) {
      throw const AudioExportPublishCancelled();
    }
    final published = await _filesChannel.invokeMethod<bool>(
      'writeNoReplace',
      <String, Object>{'targetPath': targetPath, 'bytes': bytes},
    );
    if (published == null) {
      throw const FileSystemException(
        'Platform file publisher returned no result.',
      );
    }
    if (published) {
      onProgress?.call(bytes.length, bytes.length);
    }
    return published;
  }

  static Future<File> _createTemporaryFile(File target) async {
    final prefix = '.${target.uri.pathSegments.last}.otgy-tmp';
    final directory = target.parent;
    for (var attempt = 0; ; attempt++) {
      final token = DateTime.now().microsecondsSinceEpoch + attempt;
      final candidate = File(
        '${directory.path}${Platform.pathSeparator}$prefix-$token',
      );
      try {
        return await candidate.create(exclusive: true);
      } on PathExistsException {
        continue;
      }
    }
  }
}
