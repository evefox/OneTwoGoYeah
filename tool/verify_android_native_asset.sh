#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 path/to/app-debug.apk" >&2
  exit 2
fi

apk=$1
if [ ! -f "$apk" ]; then
  echo "APK not found: $apk" >&2
  exit 2
fi

readelf_bin=$(command -v readelf || true)
if [ -z "$readelf_bin" ]; then
  readelf_bin=$(command -v llvm-readelf || true)
fi
if [ -z "$readelf_bin" ]; then
  # Android NDK ships llvm-readelf, but it is often not on PATH on macOS.
  # Prefer explicitly configured NDK roots, then the standard macOS SDK path.
  for ndk_root in "${ANDROID_NDK_ROOT:-}" "${ANDROID_NDK_HOME:-}" \
      "/Users/$(id -un)/Library/Android/sdk/ndk"; do
    if [ -n "$ndk_root" ] && [ -d "$ndk_root" ]; then
      candidate=$(find "$ndk_root" -type f -path '*/toolchains/llvm/prebuilt/*/bin/llvm-readelf' -perm -111 -print -quit 2>/dev/null || true)
      if [ -n "$candidate" ]; then
        readelf_bin=$candidate
        break
      fi
    fi
  done
fi
if [ -z "$readelf_bin" ]; then
  echo "readelf or llvm-readelf is required (install binutils or Android NDK)" >&2
  exit 2
fi

temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/otgy-android-native.XXXXXX")
trap 'rm -rf "$temporary_dir"' EXIT INT TERM

for abi in arm64-v8a armeabi-v7a x86_64; do
  library_entry="lib/$abi/libotgy_native.so"
  library_path="$temporary_dir/$abi.so"
  if ! unzip -p "$apk" "$library_entry" > "$library_path"; then
    echo "missing native asset: $library_entry" >&2
    exit 1
  fi
  if unzip -Z1 "$apk" | grep -Fxq "lib/$abi/libc++_shared.so"; then
    echo "unexpected shared C++ runtime asset: lib/$abi/libc++_shared.so" >&2
    exit 1
  fi
  if "$readelf_bin" -d "$library_path" | grep -Fq 'libc++_shared.so'; then
    echo "$library_entry still depends on libc++_shared.so" >&2
    exit 1
  fi
  if ! "$readelf_bin" -d "$library_path" | grep -Fq 'Shared library: [libm.so]'; then
    echo "$library_entry is missing its Android libm dependency" >&2
    exit 1
  fi
  if ! "$readelf_bin" -Ws "$library_path" | grep -Eq '[[:space:]]otgy_chart_create_empty$'; then
    echo "$library_entry does not export otgy_chart_create_empty" >&2
    exit 1
  fi
  for symbol in \
      Java_io_onetwogoyeah_one_1two_1go_1yeah_PlaybackEngineBridge_renderNative \
      Java_io_onetwogoyeah_one_1two_1go_1yeah_PlaybackEngineBridge_isFinishedNative; do
    if ! "$readelf_bin" -Ws "$library_path" | grep -Eq "[[:space:]]$symbol$"; then
      echo "$library_entry does not export $symbol" >&2
      exit 1
    fi
  done
done

echo "Android native assets have no unresolved shared libc++ dependency and export the playback JNI bridge."
