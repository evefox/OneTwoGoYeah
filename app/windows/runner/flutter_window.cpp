#include "flutter_window.h"

#include <flutter/method_result_functions.h>

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

class SimulationPlayback final {
 public:
  SimulationPlayback() = default;
  ~SimulationPlayback() { Stop(); }

  void Handle(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (call.method_name() == "start") {
      const auto* arguments =
          std::get_if<flutter::EncodableMap>(call.arguments());
      const auto engine_handle =
          arguments == nullptr
              ? std::nullopt
              : ReadInt64(*arguments, "engineHandle");
      const auto sample_rate =
          arguments == nullptr ? std::nullopt
                               : ReadInt64(*arguments, "sampleRate");
      if (!engine_handle || !sample_rate || *sample_rate <= 0) {
        result->Error("invalid-arguments",
                      "engineHandle and sampleRate are required.");
        return;
      }
      std::string error;
      if (!Start(*engine_handle, static_cast<std::uint32_t>(*sample_rate),
                 error)) {
        result->Error("simulation-playback-error", error);
        return;
      }
      result->Success();
      return;
    }
    if (call.method_name() == "pause") {
      Pause();
      result->Success();
      return;
    }
    if (call.method_name() == "resume") {
      Resume();
      result->Success();
      return;
    }
    if (call.method_name() == "query") {
      result->Success(State());
      return;
    }
    if (call.method_name() == "stop") {
      Stop();
      result->Success();
      return;
    }
    result->NotImplemented();
  }

 private:
  using RenderFn = void (*)(void*, std::uint8_t*, std::uint32_t);
  using IsFinishedFn = std::uint8_t (*)(const void*);
  using RenderedFramesFn = std::uint64_t (*)(const void*);

  static std::optional<std::int64_t> ReadInt64(
      const flutter::EncodableMap& arguments, const char* key) {
    const auto found = arguments.find(flutter::EncodableValue(key));
    if (found == arguments.end()) {
      return std::nullopt;
    }
    if (const auto* value = std::get_if<std::int32_t>(&found->second)) {
      return *value;
    }
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
      return *value;
    }
    return std::nullopt;
  }

  bool LoadSymbols() {
    if (render_fn_ != nullptr) {
      return true;
    }
    HMODULE module = GetModuleHandleW(L"otgy_native.dll");
    if (module == nullptr) {
      module = LoadLibraryW(L"otgy_native.dll");
    }
    if (module == nullptr) {
      return false;
    }
#pragma warning(push)
#pragma warning(disable : 4191)
    render_fn_ = reinterpret_cast<RenderFn>(
        GetProcAddress(module, "otgy_playback_engine_render"));
    finished_fn_ = reinterpret_cast<IsFinishedFn>(
        GetProcAddress(module, "otgy_playback_engine_is_finished"));
    frames_fn_ = reinterpret_cast<RenderedFramesFn>(
        GetProcAddress(module, "otgy_playback_engine_get_rendered_frames"));
#pragma warning(pop)
    return render_fn_ != nullptr && finished_fn_ != nullptr &&
           frames_fn_ != nullptr;
  }

  bool Start(std::int64_t engine_handle, std::uint32_t sample_rate,
             std::string& error) {
    Stop();
    if (!LoadSymbols()) {
      error = "The simulation playback engine is unavailable.";
      return false;
    }
    engine_ = reinterpret_cast<void*>(
        static_cast<std::intptr_t>(engine_handle));
    if (engine_ == nullptr) {
      error = "The simulation playback engine handle is invalid.";
      return false;
    }
    sample_rate_ = sample_rate;
    {
      std::lock_guard<std::mutex> lock(start_mutex_);
      start_complete_ = false;
      start_ok_ = false;
      start_error_.clear();
    }
    running_.store(true);
    thread_ = std::thread([this]() { ThreadMain(); });
    std::unique_lock<std::mutex> lock(start_mutex_);
    const bool started =
        start_cv_.wait_for(lock, std::chrono::seconds(3),
                           [this]() { return start_complete_; }) &&
        start_ok_;
    if (!started) {
      error = start_error_.empty() ? "Unable to start simulation audio."
                                   : start_error_;
      lock.unlock();
      running_.store(false);
      if (thread_.joinable()) {
        thread_.join();
      }
      engine_ = nullptr;
      return false;
    }
    return true;
  }

  void Pause() {
    if (!running_.load()) {
      return;
    }
    paused_.store(true);
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (auto* client = client_.load()) {
      client->Stop();
    }
  }

  void Resume() {
    if (!running_.load()) {
      return;
    }
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (auto* client = client_.load()) {
      client->Start();
    }
    paused_.store(false);
  }

  void Stop() {
    running_.store(false);
    {
      std::lock_guard<std::mutex> lock(event_mutex_);
      if (HANDLE event = event_.load()) {
        SetEvent(event);
      }
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    engine_ = nullptr;
    paused_.store(false);
    sample_rate_ = 0;
  }

  flutter::EncodableValue State() const {
    std::uint64_t rendered = 0;
    bool finished = true;
    if (engine_ != nullptr) {
      if (frames_fn_ != nullptr) {
        rendered = frames_fn_(engine_);
      }
      if (finished_fn_ != nullptr) {
        finished = finished_fn_(engine_) != 0;
      }
    }
    const auto buffer = static_cast<std::uint64_t>(buffer_frames_.load());
    const auto played = rendered > buffer ? rendered - buffer : 0;
    flutter::EncodableMap map;
    map[flutter::EncodableValue("playedFrames")] =
        flutter::EncodableValue(static_cast<std::int64_t>(played));
    map[flutter::EncodableValue("playing")] = flutter::EncodableValue(
        running_.load() && !paused_.load() && !finished);
    map[flutter::EncodableValue("finished")] =
        flutter::EncodableValue(finished);
    return flutter::EncodableValue(std::move(map));
  }

  void ReportStart(bool ok, const std::string& error) {
    std::lock_guard<std::mutex> lock(start_mutex_);
    start_ok_ = ok;
    start_error_ = error;
    start_complete_ = true;
    start_cv_.notify_all();
  }

  void ThreadMain() {
    const HRESULT com =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
      ReportStart(false, "Unable to initialize COM for simulation audio.");
      return;
    }
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render_client = nullptr;
    HANDLE event = nullptr;
    HANDLE mmcss = nullptr;
    DWORD task_index = 0;
    std::string error;
    do {
      if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        error = "Unable to create the audio device enumerator.";
        break;
      }
      if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                     &device))) {
        error = "Unable to open the default audio output.";
        break;
      }
      if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&client)))) {
        error = "Unable to activate the audio client.";
        break;
      }
      WAVEFORMATEX format{};
      format.wFormatTag = WAVE_FORMAT_PCM;
      format.nChannels = 2;
      format.nSamplesPerSec = sample_rate_;
      format.wBitsPerSample = 16;
      format.nBlockAlign = 4;
      format.nAvgBytesPerSec = sample_rate_ * 4;
      format.cbSize = 0;
      constexpr REFERENCE_TIME kBufferDuration = 100000;  // 10 ms
      if (FAILED(client->Initialize(
              AUDCLNT_SHAREMODE_SHARED,
              AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
              kBufferDuration, 0, &format, nullptr))) {
        error = "Unable to initialize the audio stream.";
        break;
      }
      event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (event == nullptr || FAILED(client->SetEventHandle(event))) {
        error = "Unable to create the audio event.";
        break;
      }
      if (FAILED(client->GetService(IID_PPV_ARGS(&render_client)))) {
        error = "Unable to access the audio render client.";
        break;
      }
      UINT32 buffer_frames = 0;
      if (FAILED(client->GetBufferSize(&buffer_frames)) ||
          buffer_frames == 0) {
        error = "Unable to query the audio buffer size.";
        break;
      }
      if (FAILED(client->Start())) {
        error = "Unable to start the audio stream.";
        break;
      }
      event_.store(event);
      client_.store(client);
      render_client_.store(render_client);
      buffer_frames_.store(buffer_frames);
      mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
      ReportStart(true, {});
      while (running_.load()) {
        const DWORD wait = WaitForSingleObject(event, 100);
        if (!running_.load()) {
          break;
        }
        if (wait != WAIT_OBJECT_0 || paused_.load()) {
          continue;
        }
        if (finished_fn_ != nullptr && finished_fn_(engine_) != 0) {
          break;
        }
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding)) ||
            padding >= buffer_frames) {
          continue;
        }
        const UINT32 available = buffer_frames - padding;
        BYTE* data = nullptr;
        if (FAILED(render_client->GetBuffer(available, &data))) {
          break;
        }
        render_fn_(engine_, data, available);
        render_client->ReleaseBuffer(available, 0);
      }
      client->Stop();
    } while (false);

    if (event != nullptr) {
      std::lock_guard<std::mutex> lock(event_mutex_);
      event_.store(nullptr);
      CloseHandle(event);
    }
    if (render_client != nullptr) {
      render_client_.store(nullptr);
      render_client->Release();
    }
    if (client != nullptr) {
      std::lock_guard<std::mutex> lock(client_mutex_);
      client_.store(nullptr);
      client->Release();
    }
    if (device != nullptr) {
      device->Release();
    }
    if (enumerator != nullptr) {
      enumerator->Release();
    }
    if (mmcss != nullptr) {
      AvRevertMmThreadCharacteristics(mmcss);
    }
    CoUninitialize();
    if (!error.empty()) {
      ReportStart(false, error);
    }
  }

  void* engine_ = nullptr;
  RenderFn render_fn_ = nullptr;
  IsFinishedFn finished_fn_ = nullptr;
  RenderedFramesFn frames_fn_ = nullptr;
  std::uint32_t sample_rate_ = 0;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<HANDLE> event_{nullptr};
  std::atomic<IAudioClient*> client_{nullptr};
  std::atomic<IAudioRenderClient*> render_client_{nullptr};
  std::atomic<UINT32> buffer_frames_{0};
  std::mutex start_mutex_;
  std::condition_variable start_cv_;
  std::mutex client_mutex_;
  std::mutex event_mutex_;
  bool start_complete_ = false;
  bool start_ok_ = false;
  std::string start_error_;
};

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
  simulation_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          flutter_controller_->engine()->messenger(),
          "io.onetwogoyeah/simulation-playback",
          &flutter::StandardMethodCodec::GetInstance());
  simulation_playback_ = new SimulationPlayback();
  simulation_channel_->SetMethodCallHandler(
      [this](const auto& call, auto result) {
        if (simulation_playback_ == nullptr) {
          result->NotImplemented();
          return;
        }
        simulation_playback_->Handle(call, std::move(result));
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
  delete simulation_playback_;
  simulation_playback_ = nullptr;
  simulation_channel_.reset();
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
