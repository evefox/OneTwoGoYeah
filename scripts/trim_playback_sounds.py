#!/usr/bin/env python3
"""Generate the trimmed playback-sound assets used by the hidden sound test.

The hidden test menu can switch between the shipped WAVs and a trimmed
variant. Only clips that start with digital silence differ; every other clip
keeps using the shipped asset. This script removes the leading all-zero frames
from the Chinese clips that have them and writes the result to
``app/assets/sounds_trimmed/zh``. It fails if any output does not exactly match
the source suffix after the removed frames.
"""

from __future__ import annotations

import sys
import wave
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
# Bundled clips without leading silence and the unbundled originals that are
# replaced by the trimmed variant.
SOURCE_DIRS = (
    REPOSITORY / "app" / "assets" / "sounds" / "zh",
    REPOSITORY / "app" / "assets" / "sounds_original" / "zh",
)
TARGET = REPOSITORY / "app" / "assets" / "sounds_trimmed" / "zh"

# Leading runs shorter than this are sample-level offsets with no audible
# effect; keep those clips byte-identical to the shipped asset.
MINIMUM_SILENT_MS = 1.0


def first_non_silent_frame(frames: bytes, block_align: int) -> int:
    for offset in range(0, len(frames), block_align):
        if any(frames[offset : offset + block_align]):
            return offset // block_align
    return len(frames) // block_align


def read_clip(path: Path) -> tuple[wave._wave_params, bytes]:
    with wave.open(str(path), "rb") as reader:
        return reader.getparams(), reader.readframes(reader.getnframes())


def write_clip(path: Path, params: wave._wave_params, frames: bytes) -> None:
    with wave.open(str(path), "wb") as writer:
        writer.setparams(params)
        writer.writeframes(frames)


def main() -> int:
    for directory in SOURCE_DIRS:
        if not directory.is_dir():
            print(f"missing source directory: {directory}", file=sys.stderr)
            return 1

    TARGET.mkdir(parents=True, exist_ok=True)
    for stale in TARGET.glob("*.wav"):
        stale.unlink()

    written: list[tuple[str, float, int]] = []
    for source in sorted(
        clip for directory in SOURCE_DIRS for clip in directory.glob("*.wav")
    ):
        params, frames = read_clip(source)
        block_align = params.sampwidth * params.nchannels
        silent_frames = first_non_silent_frame(frames, block_align)
        silent_ms = silent_frames * 1000.0 / params.framerate
        if silent_ms < MINIMUM_SILENT_MS:
            continue

        trimmed = frames[silent_frames * block_align :]
        target = TARGET / source.name
        write_clip(target, params, trimmed)

        written_params, written_frames = read_clip(target)
        expected = (
            written_params.nchannels == params.nchannels
            and written_params.sampwidth == params.sampwidth
            and written_params.framerate == params.framerate
            and written_frames == trimmed
        )
        first_frame = written_frames[:block_align]
        if not expected or not any(first_frame):
            print(f"verification failed: {target}", file=sys.stderr)
            return 1

        written.append(
            (
                source.name,
                silent_frames * 1000.0 / params.framerate,
                len(trimmed) // block_align,
            )
        )

    if not written:
        print("no clip starts with digital silence; nothing to generate.")
        return 0

    for name, milliseconds, frames in written:
        print(f"{name}: removed {milliseconds:.2f} ms ({frames} frames remain)")
    print(f"wrote {len(written)} trimmed clips to {TARGET.relative_to(REPOSITORY)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
