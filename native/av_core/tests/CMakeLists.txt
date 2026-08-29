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
