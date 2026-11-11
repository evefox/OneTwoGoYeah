#!/usr/bin/env python3
"""Generate the bundled WebP usage-guide images from the PNG sources.

The annotated screenshots are kept lossless in ``app/assets/guide_source`` so
they remain editable, while the app bundle only ships WebP copies from
``app/assets/guide``. The two directories are not interchangeable at runtime:
only ``assets/guide`` is declared in pubspec.yaml.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

REPOSITORY = Path(__file__).resolve().parent.parent
SOURCE = REPOSITORY / "app" / "assets" / "guide_source"
TARGET = REPOSITORY / "app" / "assets" / "guide"
QUALITY = 95


def main() -> int:
    for platform in ("desktop", "mobile"):
        source_dir = SOURCE / platform
        target_dir = TARGET / platform
        if not source_dir.is_dir():
            print(f"missing source directory: {source_dir}", file=sys.stderr)
            return 1
        target_dir.mkdir(parents=True, exist_ok=True)
        for stale in target_dir.glob("*.webp"):
            stale.unlink()

        written = 0
        for source in sorted(source_dir.glob("*.png")):
            image = Image.open(source).convert("RGB")
            target = target_dir / f"{source.stem}.webp"
            image.save(target, "WEBP", quality=QUALITY, method=6)
            written += 1
            if Image.open(target).size != image.size:
                print(f"verification failed: {target}", file=sys.stderr)
                return 1
        if written == 0:
            print(f"no PNG sources in {source_dir}", file=sys.stderr)
            return 1

    print(f"wrote WebP guide images to {TARGET.relative_to(REPOSITORY)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
