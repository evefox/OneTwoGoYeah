import 'package:flutter/services.dart';

final class PlaybackDeviceState {
  const PlaybackDeviceState({
    required this.playedFrames,
    required this.playing,
  });

  final int playedFrames;
  final bool playing;
}

abstract interface class PlaybackOutput {
  Future<void> start({
    required int sampleRate,
    required Uint8List interleavedPcm16le,
  });

  Future<PlaybackDeviceState> query();
  Future<PlaybackDeviceState> pause();
  Future<void> dispose();
}

final class MethodChannelPlaybackOutput implements PlaybackOutput {
  const MethodChannelPlaybackOutput();

  static const _channel = MethodChannel('io.onetwogoyeah/playback');

  @override
  Future<void> start({
    required int sampleRate,
    required Uint8List interleavedPcm16le,
  }) {
    return _channel.invokeMethod<void>('start', {
      'sampleRate': sampleRate,
      'pcm16le': interleavedPcm16le,
    });
  }

  @override
  Future<PlaybackDeviceState> query() async =>
      _decode(await _channel.invokeMapMethod<String, Object?>('query'));

  @override
  Future<PlaybackDeviceState> pause() async =>
      _decode(await _channel.invokeMapMethod<String, Object?>('pause'));

  @override
  Future<void> dispose() async {
    try {
      await _channel.invokeMethod<void>('dispose');
    } on MissingPluginException {
      // Widget tests and unsupported embedders have no device output channel.
    }
  }

  PlaybackDeviceState _decode(Map<String, Object?>? value) {
    if (value == null || value['playedFrames'] is! int) {
      throw PlatformException(
        code: 'invalid-playback-state',
        message: 'The platform returned an invalid playback state.',
      );
    }
    return PlaybackDeviceState(
      playedFrames: value['playedFrames'] as int,
      playing: value['playing'] == true,
    );
  }
}
