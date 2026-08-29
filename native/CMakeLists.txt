# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

project(
  OneTwoGoYeahNative
  VERSION 0.1.0
  DESCRIPTION "OneTwoGoYeah shared native core"
  LANGUAGES C CXX
)

include(CTest)

add_subdirectory(third_party)
add_subdirectory(av_core)
add_subdirectory(tools/avtool)
