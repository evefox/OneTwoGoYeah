#!/usr/bin/env python3
"""Verify Android release packaging without invoking Flutter."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
import zipfile
from unittest.mock import patch


SCRIPTS = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(SCRIPTS))

import build_common

SCRIPT = SCRIPTS / "build_android_release.py"
source = SCRIPT.read_text(encoding="utf-8")
release = types.ModuleType("android_release_builder")
release.__file__ = str(SCRIPT)
exec(compile(source, str(SCRIPT), "exec"), release.__dict__)


def write_apk(path, entries):
    with zipfile.ZipFile(path, "w") as archive:
        for entry in entries:
            archive.writestr(entry, b"x")


class ReleaseBuildTests(unittest.TestCase):
    def invoke(self, arguments):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", [str(SCRIPT), *arguments]), \
                redirect_stdout(output), redirect_stderr(errors), \
                patch.object(release.subprocess, "run", side_effect=AssertionError(
                    "No subprocess may run in dry-run tests")):
            try:
                code = release.main()
            except SystemExit as error:
                code = error.code
        return code, output.getvalue(), errors.getvalue()

    def test_release_name(self):
        self.assertEqual(build_common.release_apk_name("1.0.0"),
                         "OneTwoGoYeah-1.0.0-arm64.apk")
        self.assertEqual(build_common.release_apk_name("1.2.3-beta.1"),
                         "OneTwoGoYeah-1.2.3-beta.1-arm64.apk")

    def test_apk_abis(self):
        cases = {"single": (["lib/arm64-v8a/libapp.so", "lib/arm64-v8a/libflutter.so"],
                            {"arm64-v8a"}),
                 "multiple": (["lib/arm64-v8a/libapp.so", "lib/armeabi-v7a/libapp.so",
                               "lib/x86_64/libapp.so"],
                              {"arm64-v8a", "armeabi-v7a", "x86_64"}),
                 "none": (["assets/flutter_assets/AssetManifest.json"], set())}
        with tempfile.TemporaryDirectory() as directory:
            for label, (entries, expected) in cases.items():
                with self.subTest(label=label):
                    apk = Path(directory) / f"{label}.apk"
                    write_apk(apk, entries)
                    self.assertEqual(build_common.apk_abis(apk), expected)

    def test_version_comes_from_the_shared_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            for text, expected in (("version: 2.3.4+56\n", ("2.3.4+56", "2.3.4", "56")),
                                   ("version: '3.4.5-beta.2+7' # comment\n",
                                    ("3.4.5-beta.2+7", "3.4.5-beta.2", "7"))):
                with self.subTest(version=text):
                    (app / "pubspec.yaml").write_text(text, encoding="utf-8")
                    self.assertEqual(build_common.read_version(app), expected)
            for text in ("", "version: 1.2.3\n", "version: 1.2.3+4\nversion: 2.3.4+5\n"):
                with self.subTest(version=text):
                    (app / "pubspec.yaml").write_text(text, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        build_common.read_version(app)

    def test_dry_run_prints_fixed_arm64_command(self):
        version, name, _ = build_common.read_version(release.APP)
        code, output, errors = self.invoke(["--dry-run"])
        self.assertEqual(code, 0, errors)
        payload = json.loads(output)
        self.assertEqual(payload["workingDirectory"], str(release.APP))
        self.assertEqual(payload["version"], version)
        self.assertEqual(payload["command"][-2:], ["--target-platform", "android-arm64"])
        self.assertEqual(payload["output"],
                         str(release.RELEASES / build_common.release_apk_name(name)))

    def test_rejects_every_passthrough_argument(self):
        for arguments in (["--split-per-abi"], ["--target-platform", "android-arm64"],
                          ["--debug"], ["--"], ["--", "--split-per-abi"]):
            with self.subTest(arguments=arguments):
                code, output, _ = self.invoke(arguments)
                self.assertEqual(code, 2)
                self.assertEqual(output, "")

    def test_successful_build_copies_the_named_arm64_apk(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            built = base / "app-release.apk"
            write_apk(built, ["lib/arm64-v8a/libapp.so"])
            releases = base / "releases"

            def fake_run(command, cwd, check):
                self.assertEqual(command[-2:], ["--target-platform", "android-arm64"])
                return types.SimpleNamespace(returncode=0)

            output, errors = io.StringIO(), io.StringIO()
            with patch.object(sys, "argv", [str(SCRIPT)]), \
                    redirect_stdout(output), redirect_stderr(errors), \
                    patch.object(release, "BUILT_APK", built), \
                    patch.object(release, "RELEASES", releases), \
                    patch.object(release.subprocess, "run", side_effect=fake_run):
                code = release.main()
            self.assertEqual(code, 0, errors.getvalue())
            version, name, _ = build_common.read_version(release.APP)
            target = releases / build_common.release_apk_name(name)
            self.assertTrue(target.is_file())
            self.assertEqual(target.read_bytes(), built.read_bytes())
            self.assertIn(str(target), output.getvalue())
            self.assertIn(release.sha256(target), output.getvalue())

    def test_rejects_non_arm64_output(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            built = base / "app-release.apk"
            write_apk(built, ["lib/arm64-v8a/libapp.so", "lib/x86_64/libapp.so"])
            releases = base / "releases"
            output, errors = io.StringIO(), io.StringIO()
            with patch.object(sys, "argv", [str(SCRIPT)]), \
                    redirect_stdout(output), redirect_stderr(errors), \
                    patch.object(release, "BUILT_APK", built), \
                    patch.object(release, "RELEASES", releases), \
                    patch.object(release.subprocess, "run",
                                 return_value=types.SimpleNamespace(returncode=0)):
                code = release.main()
            self.assertEqual(code, 1)
            self.assertIn("x86_64", errors.getvalue())
            self.assertFalse(releases.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
