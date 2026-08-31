# SPDX-License-Identifier: GPL-3.0-or-later

add_library(av_core STATIC
  src/audio/audio_export.cpp
  src/audio/audio_export_name.cpp
  src/audio/audio_export_quality.cpp
  src/chart/chart_draft.cpp
  src/chart/chart_editor.cpp
  src/chart/chart_projection.cpp
  src/chart/chart_validator.cpp
  src/coordination/operation_gate.cpp
  src/audio/mp3_decoder.cpp
  src/audio/ogg_vorbis_decoder.cpp
  src/audio/ogg_vorbis_encoder.cpp
  src/audio/pcm16.cpp
  src/audio/pcm16_hash.cpp
  src/audio/pcm_offset.cpp
  src/playback/playback_position.cpp
  src/project/project_serialization.cpp
  src/slk/slk_dance_move_drill_analyzer.cpp
  src/slk/slk_chart_draft_projection.cpp
  src/slk/slk_drill_group_analyzer.cpp
  src/slk/slk_import.cpp
  src/slk/slk_export.cpp
  src/slk/slk_showtime_analyzer.cpp
  src/slk/slk_position.cpp
  src/slk/slk_parser.cpp
  src/timing/bpm_detector.cpp
  src/timing/arrowvortex/Aubio.cpp
  src/timing/arrowvortex/FFT.cpp
  src/timing/arrowvortex/FindOnsets.cpp
  src/timing/onset_detector.cpp
  src/timing/offset_detector.cpp
  src/timing/sync_analyzer.cpp
  src/timing/timing_conversion.cpp
  src/timing/timing_grid.cpp
  src/timing/timeline_range.cpp
  src/timing/timeline_navigation.cpp
  src/waveform/waveform_tile.cpp
  src/version.cpp
)
add_library(OneTwoGoYeah::av_core ALIAS av_core)

target_compile_features(av_core PUBLIC cxx_std_17)
target_include_directories(av_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/timing/arrowvortex/include"
)
target_link_libraries(av_core PRIVATE otgy_libmad otgy_libvorbis)

set_target_properties(av_core PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
  target_compile_options(av_core PRIVATE /W4 /permissive- /fp:strict)
else()
  target_compile_options(av_core PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math)
  set_source_files_properties(
    src/timing/arrowvortex/Aubio.cpp
    src/timing/arrowvortex/FFT.cpp
    src/timing/arrowvortex/FindOnsets.cpp
    PROPERTIES COMPILE_OPTIONS
      "-Wno-write-strings;-Wno-register;-Wno-unknown-pragmas;-Wno-unused-function;-Wno-unused-parameter;-Wno-unused-but-set-variable"
  )
  set_source_files_properties(
    src/timing/bpm_detector.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-reorder"
  )
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
