#!/usr/bin/env python3
"""Build a diagnostic-enabled Release application without installing it."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True

import build_common


ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
TARGETS = ("apk", "macos", "windows", "ios")


def read_version():
    return build_common.read_version(APP)


def git_output(*arguments):
    result = subprocess.run(
        ["git", "-C", str(ROOT), *arguments],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def build_identity():
    commit = git_output("rev-parse", "HEAD").decode("ascii")
    if not re.fullmatch(r"[0-9a-f]{40,64}", commit):
        raise ValueError("git did not return a valid commit identity")
    dirty = bool(git_output("status", "--porcelain", "--untracked-files=normal"))
    now = datetime.now(timezone.utc)
    timestamp = now.isoformat(timespec="microseconds").replace("+00:00", "Z")
    stamp = now.strftime("%Y%m%dT%H%M%S.%fZ")
    return f"{commit}-{'dirty' if dirty else 'clean'}-{stamp}", timestamp


def passthrough_arguments(target, arguments):
    """Allow only platform packaging options; identity overrides cannot pass."""
    allowed = {
        "apk": set(),
        "ios": {"--no-codesign"},
        "macos": set(),
        "windows": set(),
    }
    result = []
    for argument in arguments:
        if argument in allowed[target]:
            if argument in result:
                raise ValueError(f"duplicate Flutter option: {argument}")
            result.append(argument)
        else:
            raise ValueError(
                f"unsupported Flutter option: {argument}; only platform packaging "
                "options are allowed, never mode, version, defines or identity overrides"
            )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("target", choices=TARGETS)
    parser.add_argument("--flutter", help="Flutter executable (default: cached SDK, then PATH)")
    parser.add_argument("--dry-run", action="store_true", help="print JSON command without running Flutter")
    argv = sys.argv[1:]
    separator = argv.index("--") if "--" in argv else len(argv)
    options = parser.parse_args(argv[:separator])
    try:
        extra = passthrough_arguments(options.target, argv[separator + 1:])
        version, name, number = read_version()
        build_id, build_time = build_identity()
        cached = ROOT / "build" / "flutter-sdk" / "bin" / (
            "flutter.bat" if sys.platform == "win32" else "flutter"
        )
        flutter = options.flutter or (str(cached) if cached.is_file() else "flutter")
        command = [
            flutter, "--suppress-analytics", "--no-version-check", "build",
            options.target, "--release", "--no-pub",
            f"--build-name={name}", f"--build-number={number}",
            "--dart-define=OTGY_DIAGNOSTICS=true",
            f"--dart-define=OTGY_APP_VERSION={version}",
            f"--dart-define=OTGY_BUILD_ID={build_id}",
            f"--dart-define=OTGY_BUILD_TIME={build_time}",
            *(["--target-platform=android-arm64"] if options.target == "apk" else []),
            *extra,
        ]
        print(json.dumps({"workingDirectory": str(APP), "command": command}, indent=2), flush=True)
        if options.dry_run:
            return 0
        return subprocess.run(command, cwd=APP, check=False).returncode
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    sys.exit(main())
