# ArrowVortex BPM candidate fixtures

These candidate fixtures are released under CC0-1.0. Their PCM inputs are the
generated CC0 files already documented by the MP3 and onset fixture sets.

The independent capture executable compiled the fixed ArrowVortex
`FindTempo.cpp`, `FindOnsets.cpp`, `Aubio.cpp`, `FFT.cpp`, and `Polyfit.h`
directly. It reproduced `TempoDetectorImp::exec` through the onset-strength
update, then called the original `CalculateBPM` without calling
`CalculateOffset`. The PCM mix was exactly
`(float)((int)left + (int)right) / 65536.0f` and onset detection used one
thread. Candidate capture was repeated with BPM coarse-scan thread counts 1,
2, 8, and 16; every output was byte-identical.

Each `.bpm_candidates` record is 16 bytes: little-endian IEEE-754 binary64 BPM
bits followed by little-endian binary64 fitness bits. The original 9216-frame
PCM produces the reference fallback `120.0, 1.0`. The repeated 32768-frame PCM
produces three candidates in compatibility order:

1. `129.24970691676435, 0.0089304293574241252`
2. `129.50274079874706, 0.0089239658120640712`
3. `129.0, 0.0088604295138801949`

Fixed reference source SHA-256 values:

- `FindTempo.cpp`: `3d360299a2f1e81941360ab97c4e24fdea07ac22808120371d2de1a9bc8192a3`
- `FindTempo.h`: `6faf1a4fb49f6ab1d24d00082de070e1f9e6768457b297f984c65e457c35ea52`
- `Polyfit.h`: `0f8cc8063f2aee7798fe91ac961fa707fed7d1e3357d81a039e1de20eda37df1`

Fixture SHA-256 values:

- `generated_stereo_11025.bpm_candidates`: `b4a9748d0815a3fcd8cd8ad086f64dced7bc024c8893ecfc696e552aa3983af6`
- `generated_repeated_stereo_11025.bpm_candidates`: `3738124e87bbeedf557e2e94d01505c67fe03b87a2c9366c16b6974324757c0e`

Ordinary tests never regenerate these files.
