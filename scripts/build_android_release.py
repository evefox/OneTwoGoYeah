#!/usr/bin/env python3
"""Build the arm64-only Android release APK.

A universal APK bundled arm64-v8a, armeabi-v7a and x86_64 native libraries
(about 57 MB). The personal release target is a modern arm64 phone, so this
wrapper keeps only arm64-v8a. Pass any extra Flutter options after ``--``.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
FLUTTER = ROOT / "build" / "flutter-sdk" / "bin" / (
    "flutter.bat" if os.name == "nt" else "flutter"
)


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "--":
        args = args[1:]
    command = [
        str(FLUTTER),
        "build",
        "apk",
        "--release",
        "--target-platform",
        "android-arm64",
        *args,
    ]
    return subprocess.run(command, cwd=APP, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
