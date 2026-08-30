# SPDX-License-Identifier: GPL-3.0-or-later

add_library(av_core STATIC
  src/audio/mp3_decoder.cpp
  src/audio/ogg_vorbis_decoder.cpp
  src/audio/pcm16.cpp
  src/audio/pcm16_hash.cpp
  src/version.cpp
)
add_library(OneTwoGoYeah::av_core ALIAS av_core)

target_compile_features(av_core PUBLIC cxx_std_17)
target_include_directories(av_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
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
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
