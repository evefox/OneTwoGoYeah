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
  }) async {
    var target = File(selectedPath);
    final temporary = await _createTemporaryFile(target);
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
      for (var collisionIndex = 2; ; collisionIndex++) {
        if (isCancelled()) {
          throw const AudioExportPublishCancelled();
        }
        if (await linkTemporaryFile(temporary.path, target.path)) {
          await temporary.delete();
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
    } finally {
      if (!published && await temporary.exists()) {
        await temporary.delete();
      }
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
