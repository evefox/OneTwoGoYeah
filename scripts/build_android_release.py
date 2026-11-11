#!/usr/bin/env python3
"""Build and name the arm64-only Android release APK.

Release targets are modern 64-bit ARM phones. The armeabi-v7a (32-bit) and
x86_64 (emulator) native libraries are not packaged; build those by hand only
when a specific device or emulator needs them.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True

from build_common import apk_abis, read_version, release_apk_name


ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
FLUTTER = ROOT / "build" / "flutter-sdk" / "bin" / (
    "flutter.bat" if os.name == "nt" else "flutter"
)
BUILT_APK = APP / "build" / "app" / "outputs" / "flutter-apk" / "app-release.apk"
RELEASES = ROOT / "build" / "releases"
TARGET_ABIS = {"arm64-v8a"}


def build_command():
    return [
        str(FLUTTER),
        "build",
        "apk",
        "--release",
        "--target-platform",
        "android-arm64",
    ]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, allow_abbrev=False,
        epilog="Android release builds are always arm64; no Flutter options pass through.",
    )
    parser.add_argument("--dry-run", action="store_true",
                        help="print JSON command and output path without building")
    options, unknown = parser.parse_known_args()
    if "--" in sys.argv[1:]:
        unknown.append("--")
    if unknown:
        parser.error("unsupported arguments: " + " ".join(unknown)
                     + "; Android release builds are fixed to arm64")
    try:
        version, name, _ = read_version(APP)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    output = RELEASES / release_apk_name(name)
    command = build_command()
    print(json.dumps({"workingDirectory": str(APP), "version": version,
                      "command": command, "output": str(output)},
                     indent=2), flush=True)
    if options.dry_run:
        return 0
    code = subprocess.run(command, cwd=APP, check=False).returncode
    if code != 0:
        return code
    try:
        abis = apk_abis(BUILT_APK)
    except (OSError, zipfile.BadZipFile) as error:
        print(f"cannot inspect {BUILT_APK}: {error}", file=sys.stderr)
        return 1
    if abis != TARGET_ABIS:
        print(f"expected only {sorted(TARGET_ABIS)} in the APK, found: "
              f"{sorted(abis) or 'no native libraries'}", file=sys.stderr)
        return 1
    RELEASES.mkdir(parents=True, exist_ok=True)
    shutil.copy2(BUILT_APK, output)
    print(f"{output}\nsha256  {sha256(output)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
