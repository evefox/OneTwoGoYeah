import 'package:flutter/services.dart';

final class SimulationPlaybackDeviceState {
  const SimulationPlaybackDeviceState({
    required this.playedFrames,
    required this.playing,
    required this.finished,
  });

  final int playedFrames;
  final bool playing;
  final bool finished;
}

abstract interface class SimulationPlaybackOutput {
  Future<void> start({
    required int engineHandle,
    required int sampleRate,
    int blockFrames = 256,
  });

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
    int blockFrames = 256,
  }) {
    return _channel.invokeMethod<void>('start', {
      'engineHandle': engineHandle,
      'sampleRate': sampleRate,
      'blockFrames': blockFrames,
    });
  }

  @override
  Future<void> pause() => _channel.invokeMethod<void>('pause');

  @override
  Future<void> resume() => _channel.invokeMethod<void>('resume');

  @override
  Future<SimulationPlaybackDeviceState> query() async {
    final value =
        await _channel.invokeMapMethod<String, Object?>('query');
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
