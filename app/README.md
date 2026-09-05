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
