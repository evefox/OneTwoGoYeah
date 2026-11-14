# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

project(
  OneTwoGoYeahNative
  VERSION 0.1.0
  DESCRIPTION "OneTwoGoYeah shared native core"
  LANGUAGES C CXX
)

include(CTest)

# MSVC otherwise decodes UTF-8 sources with the system code page, which
# corrupts non-ASCII paths and diagnostic text at compile time.
if(MSVC)
  add_compile_options(/utf-8)
endif()

add_subdirectory(third_party)
add_subdirectory(av_core)
add_subdirectory(av_c_api)
add_subdirectory(tools/avtool)
