import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

const _root = '../../../native';

const _madSources = <String>[
  '$_root/third_party/libmad/src/bit.c',
  '$_root/third_party/libmad/src/decoder.c',
  '$_root/third_party/libmad/src/fixed.c',
  '$_root/third_party/libmad/src/frame.c',
  '$_root/third_party/libmad/src/huffman.c',
  '$_root/third_party/libmad/src/layer12.c',
  '$_root/third_party/libmad/src/layer3.c',
  '$_root/third_party/libmad/src/stream.c',
  '$_root/third_party/libmad/src/synth.c',
  '$_root/third_party/libmad/src/timer.c',
  '$_root/third_party/libmad/src/version.c',
];

const _oggSources = <String>[
  '$_root/third_party/libogg/src/bitwise.c',
  '$_root/third_party/libogg/src/framing.c',
];

final _vorbisSources = <String>[
  'analysis.c',
  'bitrate.c',
  'block.c',
  'codebook.c',
  'envelope.c',
  'floor0.c',
  'floor1.c',
  'info.c',
  'lookup.c',
  'lpc.c',
  'lsp.c',
  'mapping0.c',
  'mdct.c',
  'psy.c',
  'registry.c',
  'res0.c',
  'sharedbook.c',
  'smallft.c',
  'synthesis.c',
  'vorbisenc.c',
  'vorbisfile.c',
  'window.c',
].map((name) => '$_root/third_party/libvorbis/src/$name').toList();

final _coreSources = <String>[
  'audio/audio_end.cpp',
  'audio/audio_export.cpp',
  'audio/audio_export_name.cpp',
  'audio/audio_export_quality.cpp',
  'audio/mp3_decoder.cpp',
  'audio/ogg_vorbis_decoder.cpp',
  'audio/ogg_vorbis_encoder.cpp',
  'audio/pcm16.cpp',
  'audio/pcm16_hash.cpp',
  'audio/pcm_offset.cpp',
  'chart/chart_draft.cpp',
  'chart/chart_editor.cpp',
  'chart/chart_projection.cpp',
  'chart/chart_validator.cpp',
  'coordination/operation_gate.cpp',
  'playback/playback_position.cpp',
  'playback/playback_pcm.cpp',
  'playback/playback_engine.cpp',
  'playback/playback_judgement.cpp',
  'playback/playback_plan.cpp',
  'playback/playback_sound.cpp',
  'project/project_serialization.cpp',
  'slk/slk_chart_draft_projection.cpp',
  'slk/slk_dance_move_drill_analyzer.cpp',
  'slk/slk_drill_group_analyzer.cpp',
  'slk/slk_export.cpp',
  'slk/slk_import.cpp',
  'slk/slk_parser.cpp',
  'slk/slk_position.cpp',
  'slk/slk_showtime_analyzer.cpp',
  'timing/arrowvortex/Aubio.cpp',
  'timing/arrowvortex/FFT.cpp',
  'timing/arrowvortex/FindOnsets.cpp',
  'timing/bpm_detector.cpp',
  'timing/offset_detector.cpp',
  'timing/onset_detector.cpp',
  'timing/sync_analyzer.cpp',
  'timing/timing_conversion.cpp',
  'timing/timing_grid.cpp',
  'timing/timeline_navigation.cpp',
  'timing/timeline_range.cpp',
  'version.cpp',
  'waveform/waveform_tile.cpp',
].map((name) => '$_root/av_core/src/$name').toList();

final _apiSources = <String>[
  'analysis.cpp',
  'audio.cpp',
  'base.cpp',
  'chart.cpp',
  'timing.cpp',
  'waveform.cpp',
].map((name) => '$_root/av_c_api/src/$name').toList();

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) {
      return;
    }
    final isWindows = input.config.code.targetOS == OS.windows;
    // Android does not automatically package the NDK's shared libc++ runtime
    // for native assets. Statically link it into our one bundled library so
    // loading the FFI asset does not depend on a missing libc++_shared.so.
    final cppLinkStdLib = input.config.code.targetOS == OS.android
        ? 'c++_static'
        : null;
    final cFlags = isWindows
        ? const <String>['/W3']
        : const <String>['-fno-fast-math'];
    final cxxFlags = isWindows
        ? const <String>['/W4', '/permissive-', '/fp:strict']
        : const <String>[
            '-Wall',
            '-Wextra',
            '-Wpedantic',
            '-fno-fast-math',
            '-Wno-write-strings',
            '-Wno-register',
            '-Wno-unknown-pragmas',
            '-Wno-unused-function',
            '-Wno-unused-parameter',
            '-Wno-unused-but-set-variable',
            '-Wno-reorder',
          ];
    final route = input.config.linkingEnabled
        ? <AssetRouting>[ToLinkHook(input.packageName)]
        : const <AssetRouting>[];

    final androidOnlySources = input.config.code.targetOS == OS.android
        ? const <String>['$_root/av_c_api/src/android_jni.cpp']
        : const <String>[];

    Future<void> buildStatic(CBuilder builder) => builder.run(
      input: input,
      output: output,
      routing: route,
      linkModePreference: LinkModePreference.static,
    );

    await buildStatic(
      CBuilder.library(
        name: 'otgy_libmad',
        assetName: 'component_mad',
        sources: _madSources,
        includes: const [
          '$_root/third_party/libmad/include',
          '$_root/third_party/libmad/src',
        ],
        flags: cFlags,
        defines: const {
          'HAVE_CONFIG_H': null,
          'FPM_64BIT': null,
          'ASO_ZEROCHECK': null,
          '_CRT_SECURE_NO_WARNINGS': null,
        },
      ),
    );
    await buildStatic(
      CBuilder.library(
        name: 'otgy_libogg',
        assetName: 'component_ogg',
        sources: _oggSources,
        includes: const ['$_root/third_party/libogg/include'],
        flags: cFlags,
      ),
    );
    await buildStatic(
      CBuilder.library(
        name: 'otgy_libvorbis',
        assetName: 'component_vorbis',
        sources: _vorbisSources,
        includes: const [
          '$_root/third_party/libvorbis/include',
          '$_root/third_party/libvorbis/src',
          '$_root/third_party/libogg/include',
        ],
        flags: cFlags,
        defines: const {'_CRT_SECURE_NO_WARNINGS': null},
      ),
    );
    await buildStatic(
      CBuilder.library(
        name: 'otgy_core_api',
        assetName: 'component_core_api',
        sources: [..._coreSources, ..._apiSources, ...androidOnlySources],
        includes: const [
          '$_root/av_core/include',
          '$_root/av_core/src/timing/arrowvortex/include',
          '$_root/av_c_api/include',
          '$_root/av_c_api/src',
          '$_root/third_party/libmad/include',
          '$_root/third_party/libmad/src',
          '$_root/third_party/libogg/include',
          '$_root/third_party/libvorbis/include',
          '$_root/third_party/libvorbis/src',
        ],
        flags: cxxFlags,
        defines: const {'OTGY_C_API_BUILD': '1'},
        language: Language.cpp,
        std: 'c++17',
      ),
    );

    if (!input.config.linkingEnabled) {
      await CBuilder.library(
        name: 'otgy_native',
        assetName: 'otgy_native_bindings_generated.dart',
        sources: [..._coreSources, ..._apiSources, ...androidOnlySources],
        includes: const [
          '$_root/av_core/include',
          '$_root/av_core/src/timing/arrowvortex/include',
          '$_root/av_c_api/include',
          '$_root/av_c_api/src',
          '$_root/third_party/libmad/include',
          '$_root/third_party/libmad/src',
          '$_root/third_party/libogg/include',
          '$_root/third_party/libvorbis/include',
          '$_root/third_party/libvorbis/src',
        ],
        // libc++_static does not pull Android's separate libm dependency in.
        libraries: [
          'otgy_libmad',
          'otgy_libogg',
          'otgy_libvorbis',
          if (input.config.code.targetOS == OS.android) 'm',
        ],
        flags: cxxFlags,
        defines: const {'OTGY_C_API_BUILD': '1'},
        language: Language.cpp,
        std: 'c++17',
        cppLinkStdLib: cppLinkStdLib,
      ).run(input: input, output: output);
    }
  });
}
