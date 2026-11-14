# SPDX-License-Identifier: GPL-3.0-or-later

add_library(otgy_c_api SHARED
  src/analysis.cpp
  src/audio.cpp
  src/base.cpp
  src/chart.cpp
  src/timing.cpp
  src/waveform.cpp
)
add_library(OneTwoGoYeah::c_api ALIAS otgy_c_api)

target_compile_features(otgy_c_api PRIVATE cxx_std_17)
target_compile_definitions(otgy_c_api PRIVATE OTGY_C_API_BUILD=1)
target_include_directories(otgy_c_api
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(otgy_c_api PRIVATE OneTwoGoYeah::av_core)

set_target_properties(otgy_c_api PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden
  VERSION 5.0.0
  SOVERSION 5
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
  target_compile_options(otgy_c_api PRIVATE /W4 /permissive- /fp:strict)
else()
  target_compile_options(otgy_c_api PRIVATE
    -Wall -Wextra -Wpedantic -fno-fast-math
  )
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
