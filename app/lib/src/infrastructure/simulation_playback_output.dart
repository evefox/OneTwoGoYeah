import 'package:flutter/services.dart';

final class SimulationPlaybackDeviceState {
  const SimulationPlaybackDeviceState({
    required this.playedFrames,
    required this.playing,
    required this.finished,
    this.latencyMs = 0,
    this.bufferFrames = 0,
  });

  final int playedFrames;
  final bool playing;
  final bool finished;
  final int latencyMs;
  final int bufferFrames;
}

abstract interface class SimulationPlaybackOutput {
  Future<void> start({
    required int engineHandle,
    required int sampleRate,
    int blockFrames = 128,
  });

  /// The device's preferred output sample rate, or 0 when unknown.
  Future<int> preferredSampleRate();

  /// The device's preferred frames per output buffer, or 0 when unknown.
  Future<int> preferredFramesPerBuffer();

  Future<void> pause();
  Future<void> resume();
  Future<SimulationPlaybackDeviceState> query();
  Future<void> stop();
  Future<void> dispose();
}

final class MethodChannelSimulationPlaybackOutput
    implements SimulationPlaybackOutput {
  const MethodChannelSimulationPlaybackOutput();

  static const _channel = MethodChannel('io.onetwogoyeah/simulation-playback');

  @override
  Future<void> start({
    required int engineHandle,
    required int sampleRate,
    int blockFrames = 128,
  }) {
    return _channel.invokeMethod<void>('start', {
      'engineHandle': engineHandle,
      'sampleRate': sampleRate,
      'blockFrames': blockFrames,
    });
  }

  @override
  Future<int> preferredSampleRate() async {
    try {
      return await _channel.invokeMethod<int>('preferredSampleRate') ?? 0;
    } on MissingPluginException {
      return 0;
    } on PlatformException {
      return 0;
    }
  }

  @override
  Future<int> preferredFramesPerBuffer() async {
    try {
      return await _channel.invokeMethod<int>('preferredFramesPerBuffer') ?? 0;
    } on MissingPluginException {
      return 0;
    } on PlatformException {
      return 0;
    }
  }

  @override
  Future<void> pause() => _channel.invokeMethod<void>('pause');

  @override
  Future<void> resume() => _channel.invokeMethod<void>('resume');

  @override
  Future<SimulationPlaybackDeviceState> query() async {
    final value = await _channel.invokeMapMethod<String, Object?>('query');
    if (value == null || value['playedFrames'] is! int) {
      throw PlatformException(
        code: 'invalid-simulation-playback-state',
        message: 'The platform returned an invalid simulation state.',
      );
    }
    return SimulationPlaybackDeviceState(
      playedFrames: value['playedFrames'] as int,
      playing: value['playing'] == true,
      finished: value['finished'] == true,
      latencyMs: value['latencyMs'] is int ? value['latencyMs'] as int : 0,
      bufferFrames: value['bufferFrames'] is int
          ? value['bufferFrames'] as int
          : 0,
    );
  }

  @override
  Future<void> stop() async {
    try {
      await _channel.invokeMethod<void>('stop');
    } on MissingPluginException {
      // Widget tests and unsupported embedders have no simulation channel.
    }
  }

  @override
  Future<void> dispose() => stop();
}
