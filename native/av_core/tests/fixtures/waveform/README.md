<!-- SPDX-License-Identifier: CC0-1.0 -->

# ArrowVortex waveform golden fixture

This directory contains the first isolated ArrowVortex waveform compatibility
fixtures. The Alpha bytes were captured once on 2026-08-30 from the generated
CC0 stereo PCM fixture and are released under CC0-1.0. Ordinary builds and
tests read the committed bytes and never regenerate them.

The captures used a standalone C++17 harness containing the relevant
`sampleEdges`, uniform or amplitude luminance, and signed or rectified
edge-shape operations copied from the checked local ArrowVortex snapshot. It
did not call `av_core`. The harness was compiled with Apple Clang 17.0.0,
`-O0`, and `-fno-fast-math`. The checked reference sources were:

| Reference source | SHA-256 |
|---|---|
| `src/Editor/Waveform.cpp` | `7cff67c1e16d948d2e6ca8ed0677d7500a0af7a92e0975a0dcf017f0414628fc` |
| `src/Editor/View.cpp` | `8b5e3546208d684c8598e14dfc972b6ef229b8596fd270d8bf9c8b3fafac7e41` |

Capture parameters:

```text
input = ../mp3/generated_stereo_11025.planar.pcm16le
input SHA-256 = a5ba7143a10ad4cc9d235d00069e81ea41029127e29a4bfbf04a15a10f9ad460
sample rate = 11025 Hz
frame count = 9216
UI Zoom = 11
internal Zoom = 8
Scale = 4
block index = 1
shape / luminance = signed / uniform, rectified / uniform,
                    signed / amplitude, captured separately
sampling / anti-aliasing = 1x / level 0
pixels per second = 594
first source frame = 2375
channel width = 256
channel spacing = 24
tile height = 128
```

The output stores the complete row-major Left Alpha plane followed by the
complete row-major Right Alpha plane. Each plane is `256 * 128` bytes, so every
file contains exactly 65536 bytes. The uniform fixtures contain only `0` or
`255`. The signed amplitude fixture contains transparent `0` plus the four
captured row luminance values `95`, `97`, `105`, and `107`.

| Fixture | SHA-256 |
|---|---|
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_1x.alpha` | `aa2bf6026ef8ca04de51f89e79eb8fefc4de73f58cbbfd02924d82473c5d5fe6` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_1x.alpha` | `9ba7cb0c431b92bcbbed2c0b9d332c51a0c0ded9ce4b520c4d8e82c37a9d7161` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_1x.alpha` | `f085dceeff95f50a60f97b8b9875489ad51e662874dc68cd80c01ee71f98e8c7` |

Golden changes require an explicit compatibility review against the
ArrowVortex reference behavior described in `TESTING.md`.
