# SPDX-License-Identifier: GPL-3.0-or-later

# These snapshots are copied into the repository so native builds never fetch
# or discover a platform-provided codec.  Keep the versions and compiler
# definitions here in one place; av_core consumes each target only through its
# corresponding in-process decoder or encoder adapter.
set(OTGY_LIBMAD_VERSION "0.15.1-beta")
set(OTGY_LIBOGG_PUBLIC_HEADER_REVISION "18044")
set(OTGY_LIBVORBIS_VERSION "1.3.5")

add_library(otgy_libmad STATIC
  libmad/src/bit.c
  libmad/src/decoder.c
  libmad/src/fixed.c
  libmad/src/frame.c
  libmad/src/huffman.c
  libmad/src/layer12.c
  libmad/src/layer3.c
  libmad/src/stream.c
  libmad/src/synth.c
  libmad/src/timer.c
  libmad/src/version.c
)
target_compile_features(otgy_libmad PRIVATE c_std_90)
target_compile_definitions(otgy_libmad PRIVATE
  HAVE_CONFIG_H
  FPM_64BIT
  ASO_ZEROCHECK
  _CRT_SECURE_NO_WARNINGS
)
target_include_directories(otgy_libmad
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libmad/include>"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libmad/src"
)
set_target_properties(otgy_libmad PROPERTIES
  C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

add_library(otgy_libogg STATIC
  libogg/src/bitwise.c
  libogg/src/framing.c
)
target_compile_features(otgy_libogg PRIVATE c_std_90)
target_include_directories(otgy_libogg
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libogg/include>"
)
set_target_properties(otgy_libogg PROPERTIES
  C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

# The libVorbis core and VBR setup engine share one static target.  Encoder
# mode and codebook tables are included by vorbisenc.c from this pinned source
# tree, so no platform or system codec can change the selected setup.
add_library(otgy_libvorbis STATIC
  libvorbis/src/analysis.c
  libvorbis/src/bitrate.c
  libvorbis/src/block.c
  libvorbis/src/codebook.c
  libvorbis/src/envelope.c
  libvorbis/src/floor0.c
  libvorbis/src/floor1.c
  libvorbis/src/info.c
  libvorbis/src/lookup.c
  libvorbis/src/lpc.c
  libvorbis/src/lsp.c
  libvorbis/src/mapping0.c
  libvorbis/src/mdct.c
  libvorbis/src/psy.c
  libvorbis/src/registry.c
  libvorbis/src/res0.c
  libvorbis/src/sharedbook.c
  libvorbis/src/smallft.c
  libvorbis/src/synthesis.c
  libvorbis/src/vorbisenc.c
  libvorbis/src/vorbisfile.c
  libvorbis/src/window.c
)
target_compile_features(otgy_libvorbis PRIVATE c_std_90)
target_compile_definitions(otgy_libvorbis PRIVATE
  _CRT_SECURE_NO_WARNINGS
  OTGY_PORTABLE_VORBIS_FTOI
)
target_include_directories(otgy_libvorbis
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libvorbis/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libogg/include>"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libvorbis/src"
)
target_link_libraries(otgy_libvorbis PUBLIC otgy_libogg)
set_target_properties(otgy_libvorbis PROPERTIES
  C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

# Third-party C sources must obey the same no-fast-math rule as av_core.  The
# flags are intentionally private so they do not leak into callers.
if(MSVC)
  target_compile_options(otgy_libmad PRIVATE /W3)
  target_compile_options(otgy_libogg PRIVATE /W3)
  target_compile_options(otgy_libvorbis PRIVATE
    /W3
    /fp:strict
    /Qvec-
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/libvorbis/src/otgy_deterministic_math.h"
  )
else()
  target_compile_options(otgy_libmad PRIVATE -fno-fast-math)
  target_compile_options(otgy_libogg PRIVATE -fno-fast-math)
  target_compile_options(otgy_libvorbis PRIVATE
    -fno-fast-math
    -ffp-contract=off
    -fno-vectorize
    -fno-slp-vectorize
    -include
    "${CMAKE_CURRENT_SOURCE_DIR}/libvorbis/src/otgy_deterministic_math.h"
  )
endif()

# A single dependency target lets CI force every pinned library to build even
# before all corresponding adapters are present in av_core.
add_library(otgy_audio_codecs INTERFACE)
target_link_libraries(otgy_audio_codecs INTERFACE
  otgy_libmad
  otgy_libvorbis
)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
