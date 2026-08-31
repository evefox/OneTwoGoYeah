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

The app performs no runtime network requests and has no account, telemetry,
analytics, or update-check integration. Dependency resolution and initial SDK
downloads are development-time operations and can be cached for offline builds.

Product behavior and architectural constraints are defined by the repository
root `PRODUCT.md`, `FUNCTIONAL_SPEC.md`, `ARCHITECTURE.md`, and `TESTING.md`.
