# Pinned audio codec sources

The codec sources in this directory are repository-owned build inputs. They
were copied from the checked ArrowVortex reference snapshot at
`../ArrowVortex-release/lib` during the native bootstrap; the reference
directory is not a build-time dependency. Keeping the source here makes native
builds offline and prevents a system codec or a package-manager update from
changing decoded PCM.

Pinned inputs:

| target | pinned version or revision | license files |
| --- | --- | --- |
| libmad | 0.15.1 beta (`MAD_VERSION_MAJOR/MINOR/PATCH`, source `$Id` 8581) | `libmad/COPYING.txt`, `COPYRIGHT.txt`, `CREDITS.txt` (GPL-2.0-or-later) |
| libogg | ArrowVortex bundled snapshot (`ogg.h` revision 18044; source file revisions through 19149) | `libogg/COPYING`, `AUTHORS` (3-clause BSD) |
| libVorbis | 1.3.5 (`GENERAL_VENDOR_STRING`, source revisions 19441/19457) | `libvorbis/COPYING`, `AUTHORS` (3-clause BSD) |

The retained source/header set has this deterministic content digest:

```text
2cc53e72e738fad21c8346146dbf976e088ea327adcf653fd83ea7b374214011
```

To verify it from the repository root (the command intentionally excludes the
license and this manifest files):

```sh
find native/third_party/libmad/include \
     native/third_party/libmad/src \
     native/third_party/libogg/include \
     native/third_party/libogg/src \
     native/third_party/libvorbis/include \
     native/third_party/libvorbis/src \
     -type f -print0 | sort -z | xargs -0 shasum -a 256 | shasum -a 256
```

The current CMake targets include the MP3 decoder, Ogg framing, libVorbis core
and vorbisfile sources, plus the VBR setup engine (`vorbisenc.c`) and its pinned
mode/codebook tables for deterministic audio export. The compile definitions
are centralized in `native/third_party/CMakeLists.txt`; no target uses a
platform audio API, a subprocess, or `fast-math`. The libVorbis target also
uses an explicit deterministic libm boundary, disables floating-point
contraction/vectorization, and selects the scalar reference float-to-integer
conversion on every architecture. This keeps the pinned codec's decoded PCM
and encoded bytes identical between the supported ARM64 and x86-64 targets.
