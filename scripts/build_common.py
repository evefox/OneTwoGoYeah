#!/usr/bin/env python3
"""Shared helpers for the Android packaging entry points."""

from __future__ import annotations

from pathlib import Path
import re
import zipfile


def read_version(app):
    """Return ``(version, build name, build number)`` from the app pubspec."""
    lines = (app / "pubspec.yaml").read_text(encoding="utf-8").splitlines()
    values = [line.partition(":")[2].split("#", 1)[0].strip()
              for line in lines if re.match(r"^version\s*:", line)]
    if len(values) != 1:
        raise ValueError("pubspec.yaml must contain exactly one top-level version")
    version = values[0]
    if version.startswith(("'", '"')):
        if len(version) < 2 or version[-1] != version[0]:
            raise ValueError("invalid quoted pubspec version")
        version = version[1:-1]
    if not re.fullmatch(
        r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?\+[0-9]+", version
    ):
        raise ValueError("pubspec version must be major.minor.patch[-prerelease]+build")
    name, number = version.rsplit("+", 1)
    return version, name, number


def apk_abis(apk):
    """Return the ABI directories packaged under ``lib/`` in an APK."""
    with zipfile.ZipFile(apk) as archive:
        return {
            entry.split("/")[1]
            for entry in archive.namelist()
            if entry.startswith("lib/") and entry.count("/") >= 2
        }


def release_apk_name(version_name):
    """Return the shareable file name for an arm64 Android release."""
    return f"OneTwoGoYeah-{version_name}-arm64.apk"
