# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(av_core_version_test
  version_test.cpp
)
target_link_libraries(av_core_version_test PRIVATE OneTwoGoYeah::av_core)

if(MSVC)
  target_compile_options(av_core_version_test PRIVATE /W4 /permissive-)
else()
  target_compile_options(av_core_version_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME av_core.version COMMAND av_core_version_test)

add_executable(av_core_pcm16_test
  pcm16_test.cpp
)
target_link_libraries(av_core_pcm16_test PRIVATE OneTwoGoYeah::av_core)

if(MSVC)
  target_compile_options(av_core_pcm16_test PRIVATE /W4 /permissive-)
else()
  target_compile_options(av_core_pcm16_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME av_core.pcm16 COMMAND av_core_pcm16_test)

set(OTGY_MP3_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mp3")

function(otgy_assert_fixture_sha256 file_name expected_sha256)
  file(SHA256 "${OTGY_MP3_FIXTURE_DIR}/${file_name}" actual_sha256)
  if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR
      "Unexpected SHA-256 for MP3 fixture ${file_name}: ${actual_sha256}"
    )
  endif()
endfunction()

otgy_assert_fixture_sha256(
  generated_mono_8000.mp3
  95a6b9e9919d08bceae73026b830e942468d1adf22dcb78a9c81076270e76922
)
otgy_assert_fixture_sha256(
  generated_mono_8000.planar.pcm16le
  b1d85a34af1e9e16f7530f6efdbde26e3f8e5e5a71fc8c7345aed2345a77a451
)
otgy_assert_fixture_sha256(
  generated_stereo_11025.mp3
  e601f9f7584db0932524384778fcd1440e102525660ffc7799462dcdb59ef2a6
)
otgy_assert_fixture_sha256(
  generated_stereo_11025.planar.pcm16le
  a5ba7143a10ad4cc9d235d00069e81ea41029127e29a4bfbf04a15a10f9ad460
)

add_executable(av_core_mp3_decoder_test
  mp3_decoder_test.cpp
)
target_link_libraries(av_core_mp3_decoder_test PRIVATE OneTwoGoYeah::av_core)
target_compile_definitions(av_core_mp3_decoder_test PRIVATE
  OTGY_MP3_FIXTURE_DIR="${OTGY_MP3_FIXTURE_DIR}"
)

if(MSVC)
  target_compile_options(av_core_mp3_decoder_test PRIVATE /W4 /permissive-)
else()
  target_compile_options(av_core_mp3_decoder_test PRIVATE
    -Wall -Wextra -Wpedantic
  )
endif()

add_test(NAME av_core.mp3_decoder COMMAND av_core_mp3_decoder_test)
