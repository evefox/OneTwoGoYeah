# otgy_native

Private Dart FFI bindings for OneTwoGoYeah's shared native core.

The package build and link hooks compile `native/av_core` and the thin
`native/av_c_api` adapter from the repository root, then expose the stable C ABI
through generated bindings in `lib/`. It intentionally contains no second copy
of the native implementation and no per-platform plugin source trees.

## Layout

- `hook/build.dart`: builds the shared C ABI as a Dart native asset.
- `hook/link.dart`: links the asset for the target Flutter platform.
- `lib/otgy_native.dart`: ownership-safe Dart wrapper used by the app.
- `lib/otgy_native_bindings_generated.dart`: generated declarations for
  `native/av_c_api/include/otgy/av_c_api.h`.
- `test/`: direct FFI consistency tests.

Regenerate bindings from this directory with:

```sh
dart run ffigen --config ffigen.yaml
```

The package and application are offline at runtime. Platform-specific audio,
file-picker, lifecycle, and window adapters live in the main Flutter app.
