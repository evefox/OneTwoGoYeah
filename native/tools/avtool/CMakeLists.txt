# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(avtool
  main.cpp
)
target_link_libraries(avtool PRIVATE OneTwoGoYeah::av_core)
target_compile_features(avtool PRIVATE cxx_std_17)
set_target_properties(avtool PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(avtool PRIVATE /W4 /permissive-)
else()
  target_compile_options(avtool PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_test(NAME avtool.version COMMAND avtool --version)
  set_tests_properties(avtool.version PROPERTIES
    PASS_REGULAR_EXPRESSION "^avtool 0\\.1\\.0"
  )

  add_executable(avtool_direct_decode_probe
    tests/direct_decode_probe.cpp
  )
  target_link_libraries(avtool_direct_decode_probe
    PRIVATE OneTwoGoYeah::av_core
  )
  target_compile_features(avtool_direct_decode_probe PRIVATE cxx_std_17)
  set_target_properties(avtool_direct_decode_probe PROPERTIES
    CXX_EXTENSIONS OFF
  )

  if(MSVC)
    target_compile_options(avtool_direct_decode_probe PRIVATE
      /W4 /permissive-
    )
  else()
    target_compile_options(avtool_direct_decode_probe PRIVATE
      -Wall -Wextra -Wpedantic
    )
  endif()

  function(otgy_add_avtool_decode_test test_name relative_path codec)
    add_test(NAME "avtool.decode_${test_name}"
      COMMAND "${CMAKE_COMMAND}"
        "-DAVTOOL=$<TARGET_FILE:avtool>"
        "-DDIRECT_PROBE=$<TARGET_FILE:avtool_direct_decode_probe>"
        "-DAUDIO=${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/${relative_path}"
        "-DCODEC=${codec}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_decode.cmake"
    )
  endfunction()

  otgy_add_avtool_decode_test(
    mp3_mono mp3/generated_mono_8000.mp3 mp3
  )
  otgy_add_avtool_decode_test(
    mp3_stereo mp3/generated_stereo_11025.mp3 mp3
  )
  otgy_add_avtool_decode_test(
    ogg_mono ogg_vorbis/generated_mono_8000.ogg ogg-vorbis
  )
  otgy_add_avtool_decode_test(
    ogg_stereo ogg_vorbis/generated_stereo_11025.ogg ogg-vorbis
  )
  otgy_add_avtool_decode_test(
    ogg_three_channel
    ogg_vorbis/generated_three_channel_16000.ogg
    ogg-vorbis
  )

  set(uppercase_mp3 "${CMAKE_CURRENT_BINARY_DIR}/uppercase_audio.MP3")
  configure_file(
    "${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/mp3/generated_mono_8000.mp3"
    "${uppercase_mp3}"
    COPYONLY
  )
  add_test(NAME avtool.decode_uppercase_extension
    COMMAND "${CMAKE_COMMAND}"
      "-DAVTOOL=$<TARGET_FILE:avtool>"
      "-DDIRECT_PROBE=$<TARGET_FILE:avtool_direct_decode_probe>"
      "-DAUDIO=${uppercase_mp3}"
      "-DCODEC=mp3"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_decode.cmake"
  )

  set(invalid_mp3 "${CMAKE_CURRENT_BINARY_DIR}/invalid_audio.mp3")
  configure_file(
    "${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/ogg_vorbis/generated_mono_8000.ogg"
    "${invalid_mp3}"
    COPYONLY
  )
  add_test(NAME avtool.decode_errors
    COMMAND "${CMAKE_COMMAND}"
      "-DAVTOOL=$<TARGET_FILE:avtool>"
      "-DUNSUPPORTED_AUDIO=${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/mp3/generated_mono_8000.planar.pcm16le"
      "-DINVALID_MP3=${invalid_mp3}"
      "-DMISSING_MP3=${CMAKE_CURRENT_BINARY_DIR}/missing_audio.mp3"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/decode_errors.cmake"
  )

  add_executable(avtool_direct_export_probe
    tests/direct_export_probe.cpp
  )
  target_link_libraries(avtool_direct_export_probe
    PRIVATE OneTwoGoYeah::av_core
  )
  target_compile_features(avtool_direct_export_probe PRIVATE cxx_std_17)
  set_target_properties(avtool_direct_export_probe PROPERTIES
    CXX_EXTENSIONS OFF
  )

  if(MSVC)
    target_compile_options(avtool_direct_export_probe PRIVATE
      /W4 /permissive-
    )
  else()
    target_compile_options(avtool_direct_export_probe PRIVATE
      -Wall -Wextra -Wpedantic
    )
  endif()

  function(otgy_add_avtool_export_test test_name relative_path codec offset
           quality)
    add_test(NAME "avtool.export_audio_${test_name}"
      COMMAND "${CMAKE_COMMAND}"
        "-DAVTOOL=$<TARGET_FILE:avtool>"
        "-DDIRECT_PROBE=$<TARGET_FILE:avtool_direct_export_probe>"
        "-DAUDIO=${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/${relative_path}"
        "-DCODEC=${codec}"
        "-DOFFSET=${offset}"
        "-DQUALITY=${quality}"
        "-DDIRECT_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${test_name}.direct.ogg"
        "-DCLI_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${test_name}.cli.ogg"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_export.cmake"
    )
  endfunction()

  otgy_add_avtool_export_test(
    mp3_zero mp3/generated_mono_8000.mp3 mp3 0 1
  )
  otgy_add_avtool_export_test(
    ogg_positive_half
    ogg_vorbis/generated_stereo_11025.ogg
    ogg-vorbis
    0.00013605442176870748
    3
  )
  otgy_add_avtool_export_test(
    ogg_negative_half_quality5
    ogg_export/generated_stereo_11025.quality5.ogg
    ogg-vorbis
    -0.00013605442176870748
    5
  )

  set(export_invalid_ogg
    "${CMAKE_CURRENT_BINARY_DIR}/export_invalid_audio.ogg"
  )
  configure_file(
    "${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/ogg_vorbis/generated_stereo_11025.planar.pcm16le"
    "${export_invalid_ogg}"
    COPYONLY
  )
  add_test(NAME avtool.export_audio_errors
    COMMAND "${CMAKE_COMMAND}"
      "-DAVTOOL=$<TARGET_FILE:avtool>"
      "-DAUDIO=${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/ogg_vorbis/generated_stereo_11025.ogg"
      "-DUNSUPPORTED_AUDIO=${PROJECT_SOURCE_DIR}/av_core/tests/fixtures/ogg_vorbis/generated_stereo_11025.planar.pcm16le"
      "-DINVALID_OGG=${export_invalid_ogg}"
      "-DMISSING_OGG=${CMAKE_CURRENT_BINARY_DIR}/export_missing_audio.ogg"
      "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/export_error_output.ogg"
      "-DEXISTING_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/export_existing_output.ogg"
      "-DMISSING_PARENT_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/missing_export_directory/output.ogg"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/export_errors.cmake"
  )
endif()
