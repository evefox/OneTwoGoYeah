# ArrowVortex Offset fixtures

These Offset fixtures are released under CC0-1.0. They use the generated CC0
PCM inputs already documented by the MP3, Ogg Vorbis, onset, and BPM fixture
sets.

The independent capture executable compiled the fixed ArrowVortex
`FindTempo.cpp`, `FindOnsets.cpp`, `Aubio.cpp`, `FFT.cpp`, and `Polyfit.h`
directly. It ran the original onset, strength, BPM, base Offset, and offbeat
selection paths. The App value was then captured with the exact sign boundary
used by `DialogAdjustSync.cpp`: `appOffset = -detectorOffset`.

Each `.offsets` record is 16 bytes: little-endian IEEE-754 binary64 detector
Offset bits followed by little-endian binary64 App Offset bits. Candidate
order is the corresponding BPM compatibility order. The expected pairs are:

- original 11025 Hz PCM: `0.089977324263038547`, `-0.089977324263038547`
- repeated 11025 Hz PCM:
  - `0.090521541950113374`, `-0.090521541950113374`
  - `0.093242630385487524`, `-0.093242630385487524`
  - `0.088526077097505665`, `-0.088526077097505665`
- generated 16000 Hz Ogg PCM: base Offset `0.0` loses the slope comparison to
  its offbeat, producing `0.25`, `-0.25`

Fixed reference SHA-256 values remain those in the onset and BPM fixture
READMEs. `DialogAdjustSync.cpp` has SHA-256
`200e679f7302fd7ab915a147fa8d7303d4ae3afdb7e060d7df53da14a37863a0`.

Fixture SHA-256 values:

- `generated_stereo_11025.offsets`: `027eefae9b0552a2b96b01e94ce3a882b635f5ac2656ca4f8b9426e51fea5401`
- `generated_repeated_stereo_11025.offsets`: `283ab516f94d60e2ca8deaf04e8b282ef19faa845df501630a0cf4d65739a886`
- `generated_three_channel_16000.offsets`: `9171bfc33d967c3b318de9e4c07a46784631f53e15c11562ae16ff8be7ce3478`

Ordinary tests never regenerate these files.
