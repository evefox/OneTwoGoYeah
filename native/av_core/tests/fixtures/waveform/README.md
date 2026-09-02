<!-- SPDX-License-Identifier: CC0-1.0 -->

# ArrowVortex waveform golden fixture

This directory contains the first isolated ArrowVortex waveform compatibility
fixtures. The Alpha bytes were captured once per combination between
2026-08-30 and 2026-09-02 from the generated CC0 stereo PCM fixture and are
released under CC0-1.0. Ordinary builds and tests read the committed bytes and
never regenerate them.

The captures used a standalone C++17 harness containing the relevant
`sampleEdges`, uniform or amplitude luminance, signed or rectified edge-shape,
and 2x/3x/4x integer anti-aliasing operations copied from the checked local
ArrowVortex snapshot. It did not call `av_core`. The harness was compiled with
Apple Clang 17.0.0, `-O0`, and `-fno-fast-math`. The checked reference sources
were:

| Reference source | SHA-256 |
|---|---|
| `src/Editor/Waveform.cpp` | `7cff67c1e16d948d2e6ca8ed0677d7500a0af7a92e0975a0dcf017f0414628fc` |
| `src/Editor/View.cpp` | `8b5e3546208d684c8598e14dfc972b6ef229b8596fd270d8bf9c8b3fafac7e41` |
| `src/Editor/Butterworth.cpp` | `2e8dbf36826fa6c6472d2c4b64c26f6961f2199672ce9cdbd52962b2828fa2c0` |
| `src/Editor/Butterworth.h` | `ca8003fba956682b91f0b78a7e288af1c5992c2ae84e06de7d66da4415fdc446` |

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
                    signed / amplitude, rectified / amplitude,
                    captured separately
sampling / anti-aliasing = 1x / level 0 for all four combinations,
                           2x / level 1 for signed / uniform and
                           rectified / uniform and signed / amplitude and
                           rectified / amplitude,
                           3x / level 2 for signed / uniform and
                           rectified / uniform and signed / amplitude and
                           rectified / amplitude,
                           4x / level 3 for signed / uniform and
                           rectified / uniform and signed / amplitude and
                           rectified / amplitude
pixels per second = 594
first source frame = 2375
channel width = 256
channel spacing = 24
tile height = 128
```

The high-pass and low-pass captures additionally compiled the checked
ArrowVortex `Butterworth.cpp` and `Butterworth.h` directly into the standalone
harness. Both use strength `0.75`, which maps to normalized cutoffs `0.70` and
`0.035` respectively. Filtering starts at frame zero, processes each complete
channel, and then normalizes that channel using the reference maximum-positive
sample and 32-bit integer arithmetic. The complete planar filtered PCM
SHA-256 values are:

| Filter | Complete PCM SHA-256 |
|---|---|
| High-pass, strength 0.75 | `e046a52aee510667a6009cd74a94f98c77c9ad40abf7178fed80dad4e845943d` |
| Low-pass, strength 0.75 | `28790ffd999e777ffd4e753f8c9132437429a5227d18360a70e93a8394fa5548` |

The output stores the complete row-major Left Alpha plane followed by the
complete row-major Right Alpha plane. Each plane is `256 * 128` bytes, so every
file contains exactly 65536 bytes. The 1x uniform fixtures contain only `0` or
`255`. Both uniform 2x fixtures contain `0`, `63`, `127`, `191`, and `255`, as
produced by the integer average of each 2x2 Alpha block. Both amplitude 1x
fixtures contain transparent `0` plus the four captured row luminance values
`95`, `97`, `105`, and `107`. The amplitude 2x fixtures contain transparent
`0` plus intermediate values ranging from `20` to `107` for signed shape and
from `24` to `107` for rectified shape, produced by applying amplitude
luminance to the supersampled rows before the 2x2 integer average. Both
uniform 3x fixtures contain `0`, `28`, `56`, `85`, `113`, `141`, `170`, `198`,
`226`, and `255`, produced by the integer average of each 3x3 binary Alpha
block. The amplitude 3x fixtures contain transparent `0` plus intermediate
values ranging from `6` to `100` for signed shape and from `10` to `102` for
rectified shape, produced by applying amplitude luminance to the supersampled
rows before the 3x3 integer average.
Both uniform 4x fixtures contain `0`, `15`, `31`, `47`, `63`, `79`,
`95`, `111`, `127`, `143`, `159`, `175`, `191`, `207`, `223`, `239`, and
`255`, produced by the integer average of each 4x4 binary Alpha block.
The amplitude 4x fixtures contain transparent `0` plus intermediate values
ranging from `2` to `96` for signed shape and from `6` to `96` for rectified
shape, produced by applying amplitude luminance to the supersampled rows
before the 4x4 integer average.

| Fixture | SHA-256 |
|---|---|
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_1x.alpha` | `aa2bf6026ef8ca04de51f89e79eb8fefc4de73f58cbbfd02924d82473c5d5fe6` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_1x.alpha` | `9ba7cb0c431b92bcbbed2c0b9d332c51a0c0ded9ce4b520c4d8e82c37a9d7161` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_1x.alpha` | `f085dceeff95f50a60f97b8b9875489ad51e662874dc68cd80c01ee71f98e8c7` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_1x.alpha` | `5272c1d45caf622592a74dc5129d290c9c36eb6b715107e39621996e4c5c6065` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_2x.alpha` | `2243a44abad0fcec5ec1114895c16068bcc8248f3560a02027fcf3dc1daff9e5` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_2x.alpha` | `f912279c24fa66d0c508c455d970b7640b074a2dc8ce64c9e3220947a476dc88` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_2x.alpha` | `1218049f713c135283487d9553ab5e3e0d28cdc800062f3c02d7ffc2591ef7b9` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_2x.alpha` | `44eb3412dfb110695eb0cfa87635283e7eee360b904c76858ae79830d6c47f03` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_3x.alpha` | `c5a9044914fa046e9d05884e35f450b0192d172882f6225cb6eccfaea050485f` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_3x.alpha` | `b624da9bc4692a14cba56b625ce021df1ad6a7b3a9c084d715c5b2f3b15a523f` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_3x.alpha` | `a8960ca342b30cb1a20353290dd006c416228ff300bb2255a6924bf3401caa7f` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_3x.alpha` | `295b3071d96e86e7fa27244989387ec57a549de0ac1412171ef0eb88f27bb646` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_4x.alpha` | `0b732ac7f11a5ea7dd923d46e6ada4befa9f6333af19ed9d07344e70bb09e2e6` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_4x.alpha` | `241130adba788392aebb91d3f6dbbca4e465045fe32bdce8a69c07a3dc7d07f6` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_4x.alpha` | `de0fc96e7b8eaff68ffbff35af5cf378867bd1f176c83aa2c29a9e1a34a3268f` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_4x.alpha` | `8928312aca9acae6dd462238fbeb1838006e80c9524bb6c81828b5de2fd44a43` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_2x.high_pass_strength075.alpha` | `41935f8b37100f47db6e7620262d4255d69649bcbfe2c8407e18543967145f78` |
| `generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_2x.low_pass_strength075.alpha` | `c338ce4ff4e8ad54f07cc407e9f525b78a738ace522110aa8e547a59fa417007` |

Golden changes require an explicit compatibility review against the
ArrowVortex reference behavior described in `TESTING.md`.
