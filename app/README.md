# OneTwoGoYeah Flutter App

Cross-platform offline rhythm-chart editor for Windows, macOS, iOS, and
Android. The Flutter layer owns layout, input, local storage, platform audio,
and file dialogs; all audio, timing, waveform, chart, project, and SLK behavior
comes from the shared `native/av_core` implementation through the private
`packages/otgy_native` FFI package.

## Development

Use Flutter `3.47.2` (the version pinned by CI), then run from this directory:

```sh
flutter pub get
flutter analyze
flutter test -r expanded
```

Platform debug builds use the standard Flutter commands:

```sh
flutter build apk --debug
flutter build windows --debug
flutter build macos --debug
flutter build ios --debug --no-codesign
```

## Acceptance Release builds

Ordinary builds default to `OTGY_DIAGNOSTICS=false`. Use the standard-library
Python entry point for a diagnostic-enabled acceptance Release. From the
repository root, inspect a command first:

```sh
python3 scripts/build-acceptance.py apk --dry-run
python3 scripts/build-acceptance.py macos --dry-run
python3 scripts/build-acceptance.py windows --dry-run
python3 scripts/build-acceptance.py ios --dry-run -- --no-codesign
```

Remove `--dry-run` to build on the corresponding host with Flutter `3.47.2` and
all dependencies, SDK artifacts and platform tools already cached. The script
uses `build/flutter-sdk/bin/flutter` when present, otherwise `flutter` on PATH;
select an existing executable with `--flutter /path/to/flutter` before `--`.
It forces Release and `--no-pub`, suppresses analytics and version checks, and
only invokes `flutter build`; it never installs, starts a device or runs adb.
Dry-run prints the working directory and actual argument array as JSON without
starting Flutter or accessing the network. It does not create an application
package or verify signing or platform compilation.

The script reads the actual `app/pubspec.yaml` version and supplies its build
name and number together with `OTGY_APP_VERSION`. It enables
`OTGY_DIAGNOSTICS=true` and injects `OTGY_BUILD_ID` (full git commit, clean/dirty
status and UTC timestamp) and `OTGY_BUILD_TIME` (UTC ISO 8601). Identity values
contain no local user or SDK paths. Arguments after `--` allow only APK
`--split-per-abi` and `--target-platform` (android-arm, android-arm64, android-x64),
or iOS `--no-codesign`. Mode, build-name, build-number, dart defines/define files,
configuration and entry-point overrides are rejected before Flutter starts.
For example:

```sh
python3 scripts/build-acceptance.py apk --dry-run -- --split-per-abi --target-platform=android-arm64
```

Acceptance builds retain local sanitized JSONL logs: up to four files of
262144 bytes each, 128 queued records, and 16384 UTF-8 bytes per record, with
explicit truncation, dropped-record and storage-failure reporting. New exception
records contain only the type, safe codes and sanitized stack, without reading
exception message/details or calling error.toString(). Controller command records
contain a single state summary taken after the operation; blocked and exception
paths record the state at that point.

Both ordinary and acceptance builds provide the menu
`导出诊断包（含当前谱面）`. It explicitly exports one schema-version-1 JSON file
containing build/core/API/platform/mode identity, sanitized logs, capture state,
and the unmodified core recovery encoding of the current chart as Base64.
`capturedAt` is the UTC capture time taken before waiting for log flush;
`createdAt` is the later bundle assembly time. `detailedDiagnosticsEnabled`
reports the compile-time switch, and nullable `initializationFailure` reports a
safe initialization failure summary. Without a logger, the fallback still
exports identity, chart and state with empty logs and zero dropped/corrupt counts;
disabling detailed logs alone does not set `storageFailed`.
The chart is captured only when export is requested, including empty or
incomplete drafts without audio or a project store. Export preserves workspace,
history, selection and clipboard state and does not save recovery or session
markers. The package excludes audio, system/editor clipboard contents, private
full paths/URIs, file names and draft names. Historical logs receive path and
sensitive-field sanitization again. Historical exceptions retain only operation,
type, safe codes, sanitized stack and strictly whitelisted state (typed counters,
finite timing values, boolean flags, known source kinds and selection bounds);
message/details and unknown exception text are discarded without stringifying
unknown objects. Mobile export checks cancellation after finishing the write and
before committing, and attempts uncommitted-document cleanup before displaying
an error. Logs and exports remain local; there is no automatic upload. Existing
copyable error dialogs retain their original diagnostic details.

## Release archives

Release builds use the same pinned Flutter SDK and native codec sources as the
tests. Run the commands from this directory after `flutter pub get` has
completed:

```sh
../build/flutter-sdk/bin/flutter build apk --release
../build/flutter-sdk/bin/flutter build macos --release
```

The Android APK is written to
`build/app/outputs/flutter-apk/app-release.apk`. The macOS application is
written to `build/macos/Build/Products/Release/OneTwoGoYeah.app`; archive the
application directory as a zip or dmg without changing its contents. An
Android App Bundle can be generated with `flutter build appbundle --release`
when a store upload is needed.

The current Android release configuration uses the local debug signing key for
private device testing. It is not a store-release signing configuration. A
distributor must provide an external release keystore and must not commit
keystores, passwords, or `key.properties`. macOS distribution outside the
development machine additionally requires a Developer ID signature and Apple
notarization; those credentials remain outside this repository.

For the complete offline license and third-party notice set, see the
repository root [`LICENSE`](../LICENSE) and
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). Build provenance,
target architecture, SDK versions, and SHA-256 values should be recorded next
to each release archive.

The app performs no runtime network requests and has no account, telemetry,
analytics, or update-check integration. Dependency resolution and initial SDK
downloads are development-time operations and can be cached for offline builds.

Product behavior and architectural constraints are defined by the repository
root `PRODUCT.md`, `FUNCTIONAL_SPEC.md`, `ARCHITECTURE.md`, and `TESTING.md`.
