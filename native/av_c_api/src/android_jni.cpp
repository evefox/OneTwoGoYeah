// SPDX-License-Identifier: GPL-3.0-or-later

#include "otgy/av_c_api.h"

#include <jni.h>

#include <cstdint>

namespace {

otgy_playback_engine* engine_from(jlong handle) noexcept {
  return reinterpret_cast<otgy_playback_engine*>(
      static_cast<std::intptr_t>(handle));
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_io_onetwogoyeah_one_1two_1go_1yeah_PlaybackEngineBridge_renderNative(
    JNIEnv* env, jobject /*caller*/, jlong handle, jbyteArray buffer,
    jint frame_count) {
  if (env == nullptr || buffer == nullptr || frame_count <= 0) {
    return;
  }
  auto* engine = engine_from(handle);
  if (engine == nullptr) {
    return;
  }
  const auto required = static_cast<jsize>(frame_count) * 4;
  if (env->GetArrayLength(buffer) < required) {
    return;
  }
  auto* data =
      static_cast<jbyte*>(env->GetPrimitiveArrayCritical(buffer, nullptr));
  if (data == nullptr) {
    return;
  }
  otgy_playback_engine_render(engine, reinterpret_cast<std::uint8_t*>(data),
                              static_cast<std::uint32_t>(frame_count));
  env->ReleasePrimitiveArrayCritical(buffer, data, 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_onetwogoyeah_one_1two_1go_1yeah_PlaybackEngineBridge_isFinishedNative(
    JNIEnv* /*env*/, jobject /*caller*/, jlong handle) {
  auto* engine = engine_from(handle);
  if (engine == nullptr) {
    return JNI_TRUE;
  }
  otgy_playback_engine_state state{};
  otgy_playback_engine_get_state(engine, &state);
  return state.finished != 0U ? JNI_TRUE : JNI_FALSE;
}
