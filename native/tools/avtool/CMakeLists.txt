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
endif()
