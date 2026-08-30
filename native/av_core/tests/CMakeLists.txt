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
target_compile_definitions(av_core_pcm16_test PRIVATE
  OTGY_PCM16_HASH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mp3"
)

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

set(OTGY_OGG_VORBIS_FIXTURE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ogg_vorbis"
)

function(otgy_assert_ogg_vorbis_fixture_sha256 file_name expected_sha256)
  file(SHA256 "${OTGY_OGG_VORBIS_FIXTURE_DIR}/${file_name}" actual_sha256)
  if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR
      "Unexpected SHA-256 for Ogg Vorbis fixture ${file_name}: ${actual_sha256}"
    )
  endif()
endfunction()

otgy_assert_ogg_vorbis_fixture_sha256(
  generated_mono_8000.ogg
  516e6044a16511f4e997469dfe4e4048a0f6590bb450bbe87f140002a0c6727b
)
otgy_assert_ogg_vorbis_fixture_sha256(
  generated_mono_8000.planar.pcm16le
  47f4ad0d6677be6728ddc280eb6269f2d8e5c50de935d84f798be1da0f176f56
)
otgy_assert_ogg_vorbis_fixture_sha256(
  generated_stereo_11025.ogg
  02d91d9bb70e72d07d3b3571c4c43a8cc7508a7faab2dc2bb631a8a18c728bf5
)
otgy_assert_ogg_vorbis_fixture_sha256(
  generated_stereo_11025.planar.pcm16le
  176275320fa9703e04e26040b18e0f5e5e91c6405e75f2b317c9acbd8ff8f58e
)
otgy_assert_ogg_vorbis_fixture_sha256(
  generated_three_channel_16000.ogg
  c3575aad47f742e20b6762909309e4d22eacd85a0bb40ccf25afeb3df816c363
)
otgy_assert_ogg_vorbis_fixture_sha256(
  generated_three_channel_16000.planar.pcm16le
  ed097f42dfeb0679fddd4ae6d2f3ce987e520c0509c4bc6abd1c744311f69987
)

add_executable(av_core_ogg_vorbis_decoder_test
  ogg_vorbis_decoder_test.cpp
)
target_link_libraries(av_core_ogg_vorbis_decoder_test
  PRIVATE OneTwoGoYeah::av_core
)
target_compile_definitions(av_core_ogg_vorbis_decoder_test PRIVATE
  OTGY_OGG_VORBIS_FIXTURE_DIR="${OTGY_OGG_VORBIS_FIXTURE_DIR}"
)

if(MSVC)
  target_compile_options(av_core_ogg_vorbis_decoder_test PRIVATE
    /W4 /permissive-
  )
else()
  target_compile_options(av_core_ogg_vorbis_decoder_test PRIVATE
    -Wall -Wextra -Wpedantic
  )
endif()

add_test(NAME av_core.ogg_vorbis_decoder
  COMMAND av_core_ogg_vorbis_decoder_test
)

set(OTGY_WAVEFORM_FIXTURE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/waveform"
)
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_1x.alpha"
  actual_waveform_sha256
)
if(NOT actual_waveform_sha256 STREQUAL
   "aa2bf6026ef8ca04de51f89e79eb8fefc4de73f58cbbfd02924d82473c5d5fe6")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed uniform 1x waveform fixture: ${actual_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_1x.alpha"
  actual_rectified_waveform_sha256
)
if(NOT actual_rectified_waveform_sha256 STREQUAL
   "9ba7cb0c431b92bcbbed2c0b9d332c51a0c0ded9ce4b520c4d8e82c37a9d7161")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified uniform 1x waveform fixture: ${actual_rectified_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_1x.alpha"
  actual_signed_amplitude_waveform_sha256
)
if(NOT actual_signed_amplitude_waveform_sha256 STREQUAL
   "f085dceeff95f50a60f97b8b9875489ad51e662874dc68cd80c01ee71f98e8c7")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed amplitude 1x waveform fixture: ${actual_signed_amplitude_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_1x.alpha"
  actual_rectified_amplitude_waveform_sha256
)
if(NOT actual_rectified_amplitude_waveform_sha256 STREQUAL
   "5272c1d45caf622592a74dc5129d290c9c36eb6b715107e39621996e4c5c6065")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified amplitude 1x waveform fixture: ${actual_rectified_amplitude_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_2x.alpha"
  actual_signed_uniform_2x_waveform_sha256
)
if(NOT actual_signed_uniform_2x_waveform_sha256 STREQUAL
   "2243a44abad0fcec5ec1114895c16068bcc8248f3560a02027fcf3dc1daff9e5")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed uniform 2x waveform fixture: ${actual_signed_uniform_2x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_2x.alpha"
  actual_rectified_uniform_2x_waveform_sha256
)
if(NOT actual_rectified_uniform_2x_waveform_sha256 STREQUAL
   "f912279c24fa66d0c508c455d970b7640b074a2dc8ce64c9e3220947a476dc88")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified uniform 2x waveform fixture: ${actual_rectified_uniform_2x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_2x.alpha"
  actual_signed_amplitude_2x_waveform_sha256
)
if(NOT actual_signed_amplitude_2x_waveform_sha256 STREQUAL
   "1218049f713c135283487d9553ab5e3e0d28cdc800062f3c02d7ffc2591ef7b9")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed amplitude 2x waveform fixture: ${actual_signed_amplitude_2x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_2x.alpha"
  actual_rectified_amplitude_2x_waveform_sha256
)
if(NOT actual_rectified_amplitude_2x_waveform_sha256 STREQUAL
   "44eb3412dfb110695eb0cfa87635283e7eee360b904c76858ae79830d6c47f03")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified amplitude 2x waveform fixture: ${actual_rectified_amplitude_2x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_3x.alpha"
  actual_signed_uniform_3x_waveform_sha256
)
if(NOT actual_signed_uniform_3x_waveform_sha256 STREQUAL
   "c5a9044914fa046e9d05884e35f450b0192d172882f6225cb6eccfaea050485f")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed uniform 3x waveform fixture: ${actual_signed_uniform_3x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_3x.alpha"
  actual_rectified_uniform_3x_waveform_sha256
)
if(NOT actual_rectified_uniform_3x_waveform_sha256 STREQUAL
   "b624da9bc4692a14cba56b625ce021df1ad6a7b3a9c084d715c5b2f3b15a523f")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified uniform 3x waveform fixture: ${actual_rectified_uniform_3x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_3x.alpha"
  actual_signed_amplitude_3x_waveform_sha256
)
if(NOT actual_signed_amplitude_3x_waveform_sha256 STREQUAL
   "a8960ca342b30cb1a20353290dd006c416228ff300bb2255a6924bf3401caa7f")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed amplitude 3x waveform fixture: ${actual_signed_amplitude_3x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_3x.alpha"
  actual_rectified_amplitude_3x_waveform_sha256
)
if(NOT actual_rectified_amplitude_3x_waveform_sha256 STREQUAL
   "295b3071d96e86e7fa27244989387ec57a549de0ac1412171ef0eb88f27bb646")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified amplitude 3x waveform fixture: ${actual_rectified_amplitude_3x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_4x.alpha"
  actual_signed_uniform_4x_waveform_sha256
)
if(NOT actual_signed_uniform_4x_waveform_sha256 STREQUAL
   "0b732ac7f11a5ea7dd923d46e6ada4befa9f6333af19ed9d07344e70bb09e2e6")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed uniform 4x waveform fixture: ${actual_signed_uniform_4x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_4x.alpha"
  actual_rectified_uniform_4x_waveform_sha256
)
if(NOT actual_rectified_uniform_4x_waveform_sha256 STREQUAL
   "241130adba788392aebb91d3f6dbbca4e465045fe32bdce8a69c07a3dc7d07f6")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified uniform 4x waveform fixture: ${actual_rectified_uniform_4x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_4x.alpha"
  actual_signed_amplitude_4x_waveform_sha256
)
if(NOT actual_signed_amplitude_4x_waveform_sha256 STREQUAL
   "de0fc96e7b8eaff68ffbff35af5cf378867bd1f176c83aa2c29a9e1a34a3268f")
  message(FATAL_ERROR
    "Unexpected SHA-256 for signed amplitude 4x waveform fixture: ${actual_signed_amplitude_4x_waveform_sha256}"
  )
endif()
file(SHA256
  "${OTGY_WAVEFORM_FIXTURE_DIR}/generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_4x.alpha"
  actual_rectified_amplitude_4x_waveform_sha256
)
if(NOT actual_rectified_amplitude_4x_waveform_sha256 STREQUAL
   "8928312aca9acae6dd462238fbeb1838006e80c9524bb6c81828b5de2fd44a43")
  message(FATAL_ERROR
    "Unexpected SHA-256 for rectified amplitude 4x waveform fixture: ${actual_rectified_amplitude_4x_waveform_sha256}"
  )
endif()

add_executable(av_core_waveform_tile_test
  waveform_tile_test.cpp
)
target_link_libraries(av_core_waveform_tile_test
  PRIVATE OneTwoGoYeah::av_core
)
target_compile_definitions(av_core_waveform_tile_test PRIVATE
  OTGY_MP3_FIXTURE_DIR="${OTGY_MP3_FIXTURE_DIR}"
  OTGY_WAVEFORM_FIXTURE_DIR="${OTGY_WAVEFORM_FIXTURE_DIR}"
)

if(MSVC)
  target_compile_options(av_core_waveform_tile_test PRIVATE
    /W4 /permissive-
  )
else()
  target_compile_options(av_core_waveform_tile_test PRIVATE
    -Wall -Wextra -Wpedantic
  )
endif()

add_test(NAME av_core.waveform_tile COMMAND av_core_waveform_tile_test)
