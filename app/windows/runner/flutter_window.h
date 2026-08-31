#ifndef RUNNER_FLUTTER_WINDOW_H_
#define RUNNER_FLUTTER_WINDOW_H_

#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <mmsystem.h>

#include <memory>
#include <vector>

#include "win32_window.h"

// A window that does nothing but host a Flutter view.
class FlutterWindow : public Win32Window {
 public:
  // Creates a new FlutterWindow hosting a Flutter view running |project|.
  explicit FlutterWindow(const flutter::DartProject& project);
  virtual ~FlutterWindow();

 protected:
  // Win32Window:
  bool OnCreate() override;
  void OnDestroy() override;
  LRESULT MessageHandler(HWND window, UINT const message, WPARAM const wparam,
                         LPARAM const lparam) noexcept override;

 private:
  void HandlePlaybackMethod(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void HandleFileMethod(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void RequestClose();
  bool StartPlayback(std::uint32_t sample_rate,
                     const std::vector<std::uint8_t>& bytes,
                     std::string& error);
  flutter::EncodableValue PlaybackState() const;
  void DisposePlayback();

  // The project to run.
  flutter::DartProject project_;

  // The Flutter instance hosted by this window.
  std::unique_ptr<flutter::FlutterViewController> flutter_controller_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      playback_channel_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      lifecycle_channel_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      file_channel_;
  bool close_request_pending_ = false;
  bool close_allowed_ = false;
  HWAVEOUT wave_out_ = nullptr;
  WAVEHDR wave_header_{};
  bool wave_header_prepared_ = false;
  bool playback_paused_ = true;
  std::uint64_t playback_total_frames_ = 0;
  mutable std::uint64_t playback_played_frames_ = 0;
  std::vector<std::uint8_t> playback_bytes_;
};

#endif  // RUNNER_FLUTTER_WINDOW_H_
