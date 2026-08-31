# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(otgy_c_api_header_test c_header_test.c)
target_link_libraries(otgy_c_api_header_test PRIVATE OneTwoGoYeah::c_api)

if(MSVC)
  target_compile_options(otgy_c_api_header_test PRIVATE /W4)
else()
  target_compile_options(otgy_c_api_header_test PRIVATE
    -Wall -Wextra -Wpedantic
  )
endif()

add_test(NAME av_c_api.c_header COMMAND otgy_c_api_header_test)

add_executable(otgy_c_api_audio_consistency_test
  audio_consistency_test.cpp
)
target_link_libraries(otgy_c_api_audio_consistency_test PRIVATE
  OneTwoGoYeah::c_api
  OneTwoGoYeah::av_core
)
target_compile_definitions(otgy_c_api_audio_consistency_test PRIVATE
  OTGY_C_API_MP3_FIXTURE="${CMAKE_SOURCE_DIR}/av_core/tests/fixtures/mp3/generated_stereo_11025.mp3"
  OTGY_C_API_OGG_FIXTURE="${CMAKE_SOURCE_DIR}/av_core/tests/fixtures/ogg_vorbis/generated_stereo_11025.ogg"
)

if(MSVC)
  target_compile_options(otgy_c_api_audio_consistency_test PRIVATE
    /W4 /permissive-
  )
else()
  target_compile_options(otgy_c_api_audio_consistency_test PRIVATE
    -Wall -Wextra -Wpedantic
  )
endif()

add_test(NAME av_c_api.audio_consistency
  COMMAND otgy_c_api_audio_consistency_test
)

add_executable(otgy_c_api_chart_consistency_test
  chart_consistency_test.cpp
)
target_link_libraries(otgy_c_api_chart_consistency_test PRIVATE
  OneTwoGoYeah::c_api
  OneTwoGoYeah::av_core
)
target_compile_definitions(otgy_c_api_chart_consistency_test PRIVATE
  OTGY_C_API_SLK_FIXTURE="${CMAKE_SOURCE_DIR}/../example/z3116_lv5.slk"
)

if(MSVC)
  target_compile_options(otgy_c_api_chart_consistency_test PRIVATE
    /W4 /permissive-
  )
else()
  target_compile_options(otgy_c_api_chart_consistency_test PRIVATE
    -Wall -Wextra -Wpedantic
  )
endif()

add_test(NAME av_c_api.chart_consistency
  COMMAND otgy_c_api_chart_consistency_test
)
