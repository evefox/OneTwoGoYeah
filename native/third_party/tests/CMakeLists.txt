# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(audio_codec_snapshot_test
  audio_codec_snapshot_test.c
)
target_compile_features(audio_codec_snapshot_test PRIVATE c_std_90)
target_link_libraries(audio_codec_snapshot_test PRIVATE
  otgy_audio_codecs
)
set_target_properties(audio_codec_snapshot_test PROPERTIES
  C_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(audio_codec_snapshot_test PRIVATE /W4)
else()
  target_compile_options(audio_codec_snapshot_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -fno-fast-math
  )
endif()

add_test(NAME third_party.audio_codec_snapshot
  COMMAND audio_codec_snapshot_test
)
