# Private ArrowVortex timing source slice

This directory contains the minimum fixed ArrowVortex source slice needed by
`av_core` onset and BPM detection. It is private implementation code and does not
appear in public headers. The project is GPL-3.0-or-later and retains the
upstream Ooura FFT copyright notice and URL in `FFT.cpp`.

The inspected reference source hashes are documented in
`tests/fixtures/onset/README.md`. `Aubio.cpp`, `FindOnsets.cpp`, and
`FindOnsets.h` remain byte-identical; `FFT.cpp` only removes trailing
whitespace, without changing tokens. The private `Aubio.h` changes only its
allocation macros so allocation failure is reported through the public
noexcept result instead of dereferencing null. Minimal `Core` and `System`
compatibility headers supply the original source's container and thread names;
the public adapter always requests one thread.

`Polyfit.h` supplies the original specialized third-order fit used by BPM
fitness normalization. Its reference hash and the independently compiled
`FindTempo.cpp` hashes are recorded with the BPM fixtures. The production BPM
adapter ports only the `CalculateBPM` path and does not compile the reference
UI/background-thread, global music, Offset, or progress wrapper.
