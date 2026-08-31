# ArrowVortex joint sync-analysis fixtures

These fixtures are released under CC0-1.0 and use the same generated PCM and
fixed reference sources documented by the onset, BPM, and Offset fixture sets.

The independent capture runs the original ArrowVortex onset, onset-strength,
BPM, Offset, and offbeat paths in their production order. Each
`.sync_candidates` record contains four little-endian IEEE-754 binary64 values
in order: BPM, detector Offset, sign-converted App Offset, and fitness. The
files therefore lock the complete unrounded candidate pairing and order:

- `generated_stereo_11025.sync_candidates`: 32 bytes, SHA-256
  `5e22187cb80f6f5fb7ad1085d87f06a1e090b079262ffdbea3f5f33b13957d8e`
- `generated_repeated_stereo_11025.sync_candidates`: 96 bytes, SHA-256
  `f98dd123ee3d900d719995b35ee45d98050afa9eeac729880c5001216c3fda66`
- `generated_three_channel_16000.sync_candidates`: 32 bytes, SHA-256
  `17b62bc43235674ef0e8fec0099af3ec5654a5f78cb3bd12f50ef031ddce8490`

ArrowVortex's default unselected region begins at zero and requests 600
seconds; its factory then limits the frame count to the available audio. The
test constructs a lowest-supported-rate PCM at runtime to prove that the
complete pipeline ignores samples after that boundary, without committing a
600-second fixture. Ordinary tests never regenerate these files.
