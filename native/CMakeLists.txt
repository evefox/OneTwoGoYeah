# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)

project(
  OneTwoGoYeahNative
  VERSION 0.1.0
  DESCRIPTION "OneTwoGoYeah shared native core"
  LANGUAGES CXX
)

include(CTest)

add_subdirectory(av_core)
add_subdirectory(tools/avtool)
