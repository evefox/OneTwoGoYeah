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
cb753f44ecf99ac7f10e9ca9a7be4c60d00b9dda1eb31e38b3048868f0463da9
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

Only MP3 decoder sources, Ogg framing, and the libVorbis core/vorbisfile
sources are part of the current CMake targets. LibVorbis shares a few cleanup
and registry symbols with its analysis engine, so those translation units are
retained, but the VBR setup engine (`vorbisenc.c`) and its mode/codebook tables
remain excluded until the deterministic audio-export slice. The compile
definitions are centralized in `native/third_party/CMakeLists.txt`; no target
uses a platform audio API, a subprocess, or `fast-math`.
