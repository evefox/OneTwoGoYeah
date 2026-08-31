<!-- SPDX-License-Identifier: CC0-1.0 -->

# Deterministic Ogg Vorbis export goldens

These fixtures were authored for OneTwoGoYeah on 2026-08-31 and are released
under CC0-1.0. They contain no sampled third-party audio. The input is the
generated CC0 stereo PCM documented at
`../ogg_vorbis/generated_stereo_11025.planar.pcm16le`: 8192 planar stereo
PCM16LE frames at 11025 Hz.

Each output was encoded in process by the repository-pinned libogg/libVorbis
1.3.5 sources through `av_core`, with the fixed stream serial number, comment
layout, PCM conversion and integer quality mapping used by production export.
The three files independently lock integer qualities `1`, `3`, and `5`.
Ordinary builds and tests only read the committed bytes and never regenerate
them.

The one-time capture command, run from the repository root after configuring
`build/native-stage6`, was:

```sh
./build/native-stage6/av_core/tests/av_core_ogg_vorbis_encoder_test \
  --generate-goldens
```

| Fixture | SHA-256 |
|---|---|
| `generated_stereo_11025.quality1.ogg` | `afe984023524e4c61b738bc305bcce89da8340068604bfdd6a325348979b8bdb` |
| `generated_stereo_11025.quality3.ogg` | `f2fa9d2a8d52b50fa291339146884740a90c7050a99e8ac1ceffaae4e9a21e3a` |
| `generated_stereo_11025.quality5.ogg` | `481edb1fd70b85268ab2b3636077cb950710c340b1bd6ce3746edaa925385839` |

Golden changes require an explicit review of the fixed encoder configuration
and the Ogg export contract in `ARCHITECTURE.md` and `TESTING.md`.
