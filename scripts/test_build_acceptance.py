#!/usr/bin/env python3
"""Verify acceptance commands without invoking Flutter or creating bytecode."""

from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().with_name("build-acceptance.py")
ROOT = SCRIPT.parent.parent
source = SCRIPT.read_text(encoding="utf-8")
builder = types.ModuleType("acceptance_builder")
builder.__file__ = str(SCRIPT)
exec(compile(source, str(SCRIPT), "exec"), builder.__dict__)
COMMIT = "1234567890abcdef" * 2 + "12345678"


class AcceptanceBuildTests(unittest.TestCase):
    def invoke(self, arguments):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", [str(SCRIPT), *arguments]), \
                redirect_stdout(output), redirect_stderr(errors), \
                patch.object(builder.subprocess, "run", side_effect=AssertionError(
                    "No subprocess may run in mocked command tests")):
            try:
                code = builder.main()
            except SystemExit as error:
                code = error.code
        return code, output.getvalue(), errors.getvalue()

    def test_real_four_platform_dry_runs_outside_repository(self):
        version, name, number = builder.read_version()
        commit = builder.git_output("rev-parse", "HEAD").decode("ascii")
        dirty = bool(builder.git_output("status", "--porcelain", "--untracked-files=normal"))
        with tempfile.TemporaryDirectory() as directory:
            missing_flutter = str(Path(directory) / "missing-flutter")
            for target in builder.TARGETS:
                with self.subTest(target=target):
                    arguments = [sys.executable, str(SCRIPT), target, "--dry-run",
                                 "--flutter", missing_flutter]
                    if target == "ios":
                        arguments += ["--", "--no-codesign"]
                    before = datetime.now(timezone.utc)
                    result = subprocess.run(arguments, cwd=directory, text=True,
                                            capture_output=True, check=False)
                    after = datetime.now(timezone.utc)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    command = json.loads(result.stdout)
                    self.assertEqual(command["workingDirectory"], str(ROOT / "app"))
                    argv = command["command"]
                    self.assertEqual(argv[:7], [missing_flutter, "--suppress-analytics",
                                     "--no-version-check", "build", target, "--release", "--no-pub"])
                    self.assertIn(f"--build-name={name}", argv)
                    self.assertIn(f"--build-number={number}", argv)
                    defines = dict(item.removeprefix("--dart-define=").split("=", 1)
                                   for item in argv if item.startswith("--dart-define="))
                    self.assertEqual(set(defines), {"OTGY_DIAGNOSTICS", "OTGY_APP_VERSION",
                                     "OTGY_BUILD_ID", "OTGY_BUILD_TIME"})
                    self.assertEqual(defines["OTGY_DIAGNOSTICS"], "true")
                    self.assertEqual(defines["OTGY_APP_VERSION"], version)
                    instant = datetime.fromisoformat(defines["OTGY_BUILD_TIME"].replace("Z", "+00:00"))
                    self.assertLessEqual(before, instant)
                    self.assertLessEqual(instant, after)
                    expected = f"{commit}-{'dirty' if dirty else 'clean'}-{instant.strftime('%Y%m%dT%H%M%S.%fZ')}"
                    self.assertEqual(defines["OTGY_BUILD_ID"], expected)
                    self.assertEqual(argv.count("--release"), 1)
                    if target == "apk":
                        self.assertEqual(argv[-1], "--target-platform=android-arm64")
                    if target == "ios":
                        self.assertEqual(argv[-1], "--no-codesign")

    def test_version_changes_and_invalid_versions(self):
        valid = [("version: 2.3.4+56\n", ("2.3.4+56", "2.3.4", "56")),
                 ("version: '3.4.5-beta.2+7' # comment\n", ("3.4.5-beta.2+7", "3.4.5-beta.2", "7")),
                 ('version: "4.5.6+8"\n', ("4.5.6+8", "4.5.6", "8"))]
        invalid = ["", "  version: 1.2.3+4\n", "version: 1.2.3+4\nversion: 2.3.4+5\n",
                   "version: 1.2.3\n", "version: 1.2+3\n", "version: 1.2.3+x\n",
                   "version: '1.2.3+4\n", "version: 1.2.3+4 extra\n"]
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            with patch.object(builder, "APP", app), \
                    patch.object(builder, "build_identity", return_value=("safe-id", "safe-time")):
                for text, expected in valid:
                    with self.subTest(version=text):
                        (app / "pubspec.yaml").write_text(text, encoding="utf-8")
                        self.assertEqual(builder.read_version(), expected)
                        code, output, errors = self.invoke(["apk", "--dry-run"])
                        self.assertEqual(code, 0, errors)
                        argv = json.loads(output)["command"]
                        self.assertIn(f"--build-name={expected[1]}", argv)
                        self.assertIn(f"--build-number={expected[2]}", argv)
                        self.assertIn(f"--dart-define=OTGY_APP_VERSION={expected[0]}", argv)
                for text in invalid:
                    with self.subTest(version=text):
                        (app / "pubspec.yaml").write_text(text, encoding="utf-8")
                        code, output, errors = self.invoke(["apk", "--dry-run"])
                        self.assertEqual(code, 2)
                        self.assertEqual(output, "")
                        self.assertIn("version", errors)
                (app / "pubspec.yaml").unlink()
                code, output, _ = self.invoke(["apk", "--dry-run"])
                self.assertEqual(code, 2)
                self.assertEqual(output, "")

    def test_identity_clean_dirty_and_invalid_commit(self):
        for status in (b"", b" M file", b"?? file"):
            with self.subTest(status=status), patch.object(builder, "git_output", side_effect=[COMMIT.encode(), status]):
                identity, timestamp = builder.build_identity()
                instant = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
                self.assertTrue(timestamp.endswith("Z"))
                self.assertEqual(identity, f"{COMMIT}-{'dirty' if status else 'clean'}-{instant.strftime('%Y%m%dT%H%M%S.%fZ')}")
        with patch.object(builder, "git_output", return_value=b"invalid"):
            with self.assertRaises(ValueError):
                builder.build_identity()

    def test_packaging_whitelist(self):
        with patch.object(builder, "build_identity", return_value=("safe-id", "safe-time")):
            code, output, errors = self.invoke(["ios", "--dry-run", "--", "--no-codesign"])
            self.assertEqual(code, 0, errors)
            self.assertEqual(json.loads(output)["command"][-1], "--no-codesign")
            for target in ("apk", "macos", "windows"):
                with self.subTest(target=target):
                    code, output, errors = self.invoke([target, "--dry-run"])
                    self.assertEqual(code, 0, errors)
                    argv = json.loads(output)["command"]
                    self.assertNotIn("--no-codesign", argv)
                    self.assertEqual("--target-platform=android-arm64" in argv,
                                     target == "apk")

    def test_reject_overrides_before_subprocess(self):
        options = ["--debug", "--profile", "--release", "--build-name", "--build-number",
                   "--dart-define-from-file", "--config-only", "--target", "--flavor"]
        forbidden = [[option] for option in options] + [[option, "override"] for option in options]
        forbidden += [[f"{option}=override"] for option in options]
        for key in ("OTGY_DIAGNOSTICS", "OTGY_APP_VERSION", "OTGY_BUILD_ID", "OTGY_BUILD_TIME"):
            forbidden += [[f"--dart-define={key}=false"], ["--dart-define", f"{key}=false"]]
        forbidden += [["--unknown"], ["--target-plat=android-arm64"], ["--"],
                      ["--split-per-abi"], ["--split-per-abi", "--split-per-abi"],
                      ["--split-per-abi=true"],
                      ["--target-platform", "android-arm64"], ["--target-platform=android-arm64"],
                      ["--target-platform"], ["--target-platform="], ["--target-platform=android-arm64,android-arm64"],
                      ["--target-platform=android-arm64,"], ["--target-platform=linux-x64"],
                      ["--target-platform=android-arm64", "--target-platform", "android-x64"],
                      ["--no-codesign"], ["--no-codesign", "--no-codesign"]]
        for target in builder.TARGETS:
            for options in forbidden:
                if target == "ios" and options == ["--no-codesign"]:
                    continue
                with self.subTest(target=target, options=options):
                    code, output, errors = self.invoke([target, "--dry-run", "--", *options])
                    self.assertEqual(code, 2, errors)
                    self.assertEqual(output, "")
        for arguments in (["appbundle", "--dry-run"], ["apk", "--dry"],
                          ["apk", "--dry-run", "--flut", "missing"],
                          ["apk", "--dry-run", "--release"]):
            with self.subTest(arguments=arguments):
                code, output, _ = self.invoke(arguments)
                self.assertEqual(code, 2)
                self.assertEqual(output, "")


if __name__ == "__main__":
    unittest.main(verbosity=2)
