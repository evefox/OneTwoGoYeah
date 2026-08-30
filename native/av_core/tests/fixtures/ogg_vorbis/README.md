<!-- SPDX-License-Identifier: CC0-1.0 -->

# Generated Ogg Vorbis golden fixtures

These fixtures were authored for OneTwoGoYeah on 2026-08-30. The mathematical
source signals and the resulting Ogg Vorbis and PCM fixture files in this
directory are released under CC0-1.0. They contain no sampled third-party
audio.

All three inputs contain exactly 8192 source frames and were encoded once at
Vorbis VBR quality `0.3` with the checked libVorbis `1.3.5` `vorbisenc.c` and
mode/codebook data from the local ArrowVortex reference snapshot. The encoder
was linked to the repository-pinned libVorbis/libogg core and used fixed Ogg
serial numbers `81001`, `81002`, and `81003`. Ordinary builds and tests read
the committed bytes and never build the encoder or regenerate these files.

The generated source channels were:

```text
channel 0 = 0.40 * sin(2 * pi * 220 * frame / sampleRate)
channel 1 = 0.30 * sin(2 * pi * 440 * frame / sampleRate)
channel 2 = 0.20 * sin(2 * pi * 880 * frame / sampleRate)
```

The mono fixture contains channel 0, the stereo fixture contains channels 0
and 1, and the three-channel fixture contains all three. The last fixture
therefore proves that canonical decoding selects encoded channels zero and one
rather than mixing or selecting channel two.

The golden PCM files store every Left signed PCM16 sample in little-endian byte
order, followed by every Right sample. They were captured by compiling the
checked ArrowVortex `LoadOgg.cpp` against the pinned libVorbis/libogg targets,
then independently matched against `av_core` byte for byte.

| Fixture | Encoded SHA-256 | Expected rate | Expected frames | Planar PCM16LE SHA-256 |
|---|---|---:|---:|---|
| `generated_mono_8000.ogg` | `516e6044a16511f4e997469dfe4e4048a0f6590bb450bbe87f140002a0c6727b` | 8000 Hz | 8192 | `47f4ad0d6677be6728ddc280eb6269f2d8e5c50de935d84f798be1da0f176f56` |
| `generated_stereo_11025.ogg` | `02d91d9bb70e72d07d3b3571c4c43a8cc7508a7faab2dc2bb631a8a18c728bf5` | 11025 Hz | 8192 | `176275320fa9703e04e26040b18e0f5e5e91c6405e75f2b317c9acbd8ff8f58e` |
| `generated_three_channel_16000.ogg` | `c3575aad47f742e20b6762909309e4d22eacd85a0bb40ccf25afeb3df816c363` | 16000 Hz | 8192 | `ed097f42dfeb0679fddd4ae6d2f3ce987e520c0509c4bc6abd1c744311f69987` |

Golden changes require an explicit compatibility review against the
ArrowVortex reference behavior described in `TESTING.md`.
