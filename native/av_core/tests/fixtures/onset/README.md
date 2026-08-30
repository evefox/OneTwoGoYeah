# ArrowVortex onset fixtures

These binary fixtures are released under CC0-1.0. They are derived only from
the repository's generated CC0 stereo PCM fixture and contain no third-party
audio recording.

`generated_stereo_11025.onsets` was captured by compiling the fixed
ArrowVortex `Aubio.cpp`, `FFT.cpp`, and `FindOnsets.cpp` directly, mixing the
existing `fixtures/mp3/generated_stereo_11025.planar.pcm16le` input with
`(float)((int)left + (int)right) / 65536.0f`, and calling `FindOnsets` with a
11025 Hz sample rate and one thread. It contains one onset at frame 992.

`generated_repeated_stereo_11025.planar.pcm16le` is a 32768-frame derivative.
For each planar channel, a zero-filled output receives the first 3072 frames
of the existing stereo PCM at frame starts 0, 5120, 10240, 15360, 20480, and
25600. `generated_repeated_stereo_11025.onsets` was captured from that PCM by
the same independent reference executable. It contains frames 992, 6112,
11232, 16352, 21472, and 26592 in that order.

Each `.onsets` record is 16 bytes: an unsigned 64-bit little-endian frame
index followed by the little-endian IEEE-754 binary64 bits of strength. Every
captured strength is exactly 1.0 (`0x3ff0000000000000`). Ordinary tests never
regenerate these files.

Fixed reference source SHA-256 values:

- `Aubio.cpp`: `fb65df5e3a831e56f45bb3a4e5ef1777beda2a1889d9da24348d88c1bd0db65e`
- `Aubio.h`: `bbfe70e072ab58dd0f4b5e42ea7122ce95818d5e143ab115025b50c5deadcf74`
- `FFT.cpp`: `baf391c2b3d9ef1a59e10ecbd61313ab05adc99f0699008ab1acef3b5d3cc560`
- `FindOnsets.cpp`: `dde8b2b9bfe3f75b6c16a3efc6eb87aa7986d6cec601ca1ac287e7383308a360`
- `FindOnsets.h`: `b28d17f09fdd829e0d3a7dcc8364ba912f4f89735eb60f4d1a7a36d8a17132f1`

Fixture SHA-256 values:

- `generated_stereo_11025.onsets`: `ee9c8105536060b3a64f3ee3918a207e110c110ddeb6a3d39f8b5b323294d017`
- `generated_repeated_stereo_11025.planar.pcm16le`: `b7f77e9568452852d849fce37b25f10cdb83a1fe0e383915c50c173c8641344e`
- `generated_repeated_stereo_11025.onsets`: `a7d339e0824edc1e4540ef128796839e3ebe9bf4880982978c42738a96a0e145`
