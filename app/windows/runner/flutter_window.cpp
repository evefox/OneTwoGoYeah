#include "flutter_window.h"

#include <flutter/method_result_functions.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "flutter/generated_plugin_registrant.h"

namespace {

std::optional<std::wstring> Utf16FromUtf8(const std::string& value) {
  if (value.empty() ||
      value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  const int input_length = static_cast<int>(value.size());
  const int output_length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), input_length, nullptr, 0);
  if (output_length <= 0) {
    return std::nullopt;
  }
  std::wstring output(static_cast<std::size_t>(output_length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          input_length, output.data(), output_length) !=
      output_length) {
    return std::nullopt;
  }
  return output;
}

}  // namespace

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  playback_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          flutter_controller_->engine()->messenger(),
          "io.onetwogoyeah/playback",
          &flutter::StandardMethodCodec::GetInstance());
  playback_channel_->SetMethodCallHandler(
      [this](const auto& call, auto result) {
        HandlePlaybackMethod(call, std::move(result));
      });
  lifecycle_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          flutter_controller_->engine()->messenger(),
          "io.onetwogoyeah/lifecycle",
          &flutter::StandardMethodCodec::GetInstance());
  file_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          flutter_controller_->engine()->messenger(),
          "io.onetwogoyeah/files",
          &flutter::StandardMethodCodec::GetInstance());
  file_channel_->SetMethodCallHandler([this](const auto& call, auto result) {
    HandleFileMethod(call, std::move(result));
  });
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  DisposePlayback();
  file_channel_.reset();
  lifecycle_channel_.reset();
  playback_channel_.reset();
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

void FlutterWindow::HandleFileMethod(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (call.method_name() != "linkNoReplace") {
    result->NotImplemented();
    return;
  }
  const auto* arguments =
      std::get_if<flutter::EncodableMap>(call.arguments());
  if (arguments == nullptr) {
    result->Error("invalid-arguments", "File paths are required.");
    return;
  }
  const auto source_iterator =
      arguments->find(flutter::EncodableValue("sourcePath"));
  const auto target_iterator =
      arguments->find(flutter::EncodableValue("targetPath"));
  if (source_iterator == arguments->end() ||
      target_iterator == arguments->end()) {
    result->Error("invalid-arguments", "File paths are required.");
    return;
  }
  const auto* source = std::get_if<std::string>(&source_iterator->second);
  const auto* target = std::get_if<std::string>(&target_iterator->second);
  if (source == nullptr || target == nullptr) {
    result->Error("invalid-arguments", "File paths are invalid.");
    return;
  }
  const auto source_wide = Utf16FromUtf8(*source);
  const auto target_wide = Utf16FromUtf8(*target);
  if (!source_wide || !target_wide) {
    result->Error("invalid-arguments", "File paths are not valid UTF-8.");
    return;
  }
  if (CreateHardLinkW(target_wide->c_str(), source_wide->c_str(), nullptr)) {
    result->Success(flutter::EncodableValue(true));
    return;
  }
  const DWORD error = GetLastError();
  if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
    result->Success(flutter::EncodableValue(false));
    return;
  }
  result->Error("file-publish-error",
                "Unable to atomically publish the output file.",
                flutter::EncodableValue(static_cast<std::int64_t>(error)));
}

void FlutterWindow::RequestClose() {
  if (close_request_pending_ || lifecycle_channel_ == nullptr) {
    return;
  }
  close_request_pending_ = true;
  lifecycle_channel_->InvokeMethod(
      "requestClose", nullptr,
      std::make_unique<
          flutter::MethodResultFunctions<flutter::EncodableValue>>(
          [this](const flutter::EncodableValue* result) {
            close_request_pending_ = false;
            const auto* allowed =
                result == nullptr ? nullptr : std::get_if<bool>(result);
            if (allowed != nullptr && *allowed) {
              close_allowed_ = true;
              PostMessage(GetHandle(), WM_CLOSE, 0U, 0U);
            }
          },
          [this](const std::string&, const std::string&,
                 const flutter::EncodableValue*) {
            close_request_pending_ = false;
          },
          [this]() { close_request_pending_ = false; }));
}

void FlutterWindow::HandlePlaybackMethod(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (call.method_name() == "start") {
    const auto* arguments =
        std::get_if<flutter::EncodableMap>(call.arguments());
    if (arguments == nullptr) {
      result->Error("invalid-arguments", "Playback arguments are required.");
      return;
    }
    const auto rate_iterator =
        arguments->find(flutter::EncodableValue("sampleRate"));
    const auto pcm_iterator = arguments->find(flutter::EncodableValue("pcm16le"));
    if (rate_iterator == arguments->end() || pcm_iterator == arguments->end()) {
      result->Error("invalid-arguments", "Playback arguments are incomplete.");
      return;
    }
    const auto* rate = std::get_if<std::int32_t>(&rate_iterator->second);
    const auto* pcm =
        std::get_if<std::vector<std::uint8_t>>(&pcm_iterator->second);
    if (rate == nullptr || *rate <= 0 || pcm == nullptr) {
      result->Error("invalid-arguments", "Playback arguments are invalid.");
      return;
    }
    std::string error;
    if (!StartPlayback(static_cast<std::uint32_t>(*rate), *pcm, error)) {
      result->Error("playback-error", error);
      return;
    }
    result->Success();
    return;
  }
  if (call.method_name() == "query") {
    result->Success(PlaybackState());
    return;
  }
  if (call.method_name() == "pause") {
    if (wave_out_ != nullptr) {
      waveOutPause(wave_out_);
    }
    playback_paused_ = true;
    result->Success(PlaybackState());
    return;
  }
  if (call.method_name() == "dispose") {
    DisposePlayback();
    result->Success();
    return;
  }
  result->NotImplemented();
}

bool FlutterWindow::StartPlayback(std::uint32_t sample_rate,
                                  const std::vector<std::uint8_t>& bytes,
                                  std::string& error) {
  if (bytes.empty() || bytes.size() % 4U != 0U ||
      bytes.size() > std::numeric_limits<DWORD>::max()) {
    error = "PCM playback buffer is invalid.";
    return false;
  }
  DisposePlayback();
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2U;
  format.nSamplesPerSec = sample_rate;
  format.wBitsPerSample = 16U;
  format.nBlockAlign = 4U;
  format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
  if (waveOutOpen(&wave_out_, WAVE_MAPPER, &format, 0U, 0U,
                  CALLBACK_NULL) != MMSYSERR_NOERROR) {
    wave_out_ = nullptr;
    error = "Unable to open the Windows audio output device.";
    return false;
  }
  playback_bytes_ = bytes;
  wave_header_ = {};
  wave_header_.lpData = reinterpret_cast<LPSTR>(playback_bytes_.data());
  wave_header_.dwBufferLength = static_cast<DWORD>(playback_bytes_.size());
  if (waveOutPrepareHeader(wave_out_, &wave_header_, sizeof(wave_header_)) !=
      MMSYSERR_NOERROR) {
    error = "Unable to prepare the Windows playback buffer.";
    DisposePlayback();
    return false;
  }
  wave_header_prepared_ = true;
  if (waveOutWrite(wave_out_, &wave_header_, sizeof(wave_header_)) !=
      MMSYSERR_NOERROR) {
    error = "Unable to submit the Windows playback buffer.";
    DisposePlayback();
    return false;
  }
  playback_total_frames_ = playback_bytes_.size() / 4U;
  playback_played_frames_ = 0U;
  playback_paused_ = false;
  return true;
}

flutter::EncodableValue FlutterWindow::PlaybackState() const {
  bool position_available = wave_out_ == nullptr;
  if (wave_out_ != nullptr) {
    MMTIME position{};
    position.wType = TIME_BYTES;
    if (waveOutGetPosition(wave_out_, &position, sizeof(position)) ==
        MMSYSERR_NOERROR) {
      playback_played_frames_ =
          std::min<std::uint64_t>(position.u.cb / 4U, playback_total_frames_);
      position_available = true;
    }
  }
  const bool playing = wave_out_ != nullptr && position_available &&
                       !playback_paused_ &&
                       playback_played_frames_ < playback_total_frames_;
  return flutter::EncodableValue(flutter::EncodableMap{
      {flutter::EncodableValue("playedFrames"),
       flutter::EncodableValue(
           static_cast<std::int64_t>(playback_played_frames_))},
      {flutter::EncodableValue("playing"), flutter::EncodableValue(playing)},
  });
}

void FlutterWindow::DisposePlayback() {
  if (wave_out_ != nullptr) {
    waveOutReset(wave_out_);
    if (wave_header_prepared_) {
      waveOutUnprepareHeader(wave_out_, &wave_header_, sizeof(wave_header_));
    }
    waveOutClose(wave_out_);
  }
  wave_out_ = nullptr;
  wave_header_ = {};
  wave_header_prepared_ = false;
  playback_paused_ = true;
  playback_total_frames_ = 0U;
  playback_played_frames_ = 0U;
  playback_bytes_.clear();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_CLOSE:
      if (!close_allowed_) {
        RequestClose();
        return 0;
      }
      break;
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
