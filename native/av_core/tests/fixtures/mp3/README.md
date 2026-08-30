<!-- SPDX-License-Identifier: CC0-1.0 -->

# Generated MP3 golden fixtures

These fixtures were authored for OneTwoGoYeah on 2026-08-30. The source
signals and the resulting MP3 and PCM fixture files in this directory are
released under CC0-1.0. They contain only generated mathematical waveforms and
no sampled third-party audio.

The files were generated once with FFmpeg `8.0.1` linked to libmp3lame
`3.100`, plus the LAME `3.100` CLI for the VBR stereo fixture.
Ordinary builds and tests read the committed bytes and never regenerate them.
The generation commands were:

```sh
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi \
  -i "aevalsrc=0.45*sin(2*PI*440*t):s=8000:d=1.008" \
  -map_metadata -1 -c:a libmp3lame -b:a 24k -write_xing 0 \
  -id3v2_version 0 -y generated_mono_8000.mp3

ffmpeg -nostdin -hide_banner -loglevel error -f lavfi \
  -i "aevalsrc=0.42*sin(2*PI*330*t)|0.38*sin(2*PI*660*t):s=11025:d=0.7314285714285714:c=stereo" \
  -map_metadata -1 -c:a pcm_s16le -y generated_stereo_11025.wav
lame --silent --noreplaygain -V 6 \
  generated_stereo_11025.wav generated_stereo_11025.mp3
```

Each source is exactly 8064 samples long before MP3 encoder delay and padding.
The golden PCM files store all Left signed PCM16 samples in little-endian byte
order, followed by all Right samples. They were captured by compiling the
checked ArrowVortex `LoadMp3.cpp` against the pinned libmad configuration, then
independently matched against `av_core` byte for byte.
The stereo input intentionally contains a VBR Xing frame; matching the
reference decoder excludes that non-audio frame from the 9216 output frames.
The 9216-frame result is also an exact number of the reference loader's
1024-frame reads, so these decoder goldens do not encode its unrelated
incomplete-consumer-block behavior as a core decoding rule.

| Fixture | Encoded SHA-256 | Expected rate | Expected frames | Planar PCM16LE SHA-256 |
|---|---|---:|---:|---|
| `generated_mono_8000.mp3` | `95a6b9e9919d08bceae73026b830e942468d1adf22dcb78a9c81076270e76922` | 8000 Hz | 9216 | `b1d85a34af1e9e16f7530f6efdbde26e3f8e5e5a71fc8c7345aed2345a77a451` |
| `generated_stereo_11025.mp3` | `e601f9f7584db0932524384778fcd1440e102525660ffc7799462dcdb59ef2a6` | 11025 Hz | 9216 | `a5ba7143a10ad4cc9d235d00069e81ea41029127e29a4bfbf04a15a10f9ad460` |

Golden changes require an explicit compatibility review against the
ArrowVortex reference behavior described in `TESTING.md`.
