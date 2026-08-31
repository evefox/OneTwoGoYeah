import AVFoundation
import Cocoa
import Darwin
import FlutterMacOS

class MainFlutterWindow: NSWindow, NSWindowDelegate {
  private var playback: PcmPlaybackOutput?
  private var lifecycleChannel: FlutterMethodChannel?
  private var fileChannel: FlutterMethodChannel?
  private var closeRequestPending = false
  private var closeAllowed = false

  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = frame
    contentViewController = flutterViewController
    setFrame(windowFrame, display: true)

    RegisterGeneratedPlugins(registry: flutterViewController)
    playback = PcmPlaybackOutput(
      messenger: flutterViewController.engine.binaryMessenger
    )
    lifecycleChannel = FlutterMethodChannel(
      name: "io.onetwogoyeah/lifecycle",
      binaryMessenger: flutterViewController.engine.binaryMessenger
    )
    fileChannel = FlutterMethodChannel(
      name: "io.onetwogoyeah/files",
      binaryMessenger: flutterViewController.engine.binaryMessenger
    )
    fileChannel?.setMethodCallHandler(handleFileCall)
    super.awakeFromNib()
    delegate = self
  }

  func windowShouldClose(_ sender: NSWindow) -> Bool {
    if closeAllowed {
      return true
    }
    guard !closeRequestPending, let lifecycleChannel else {
      return false
    }
    closeRequestPending = true
    lifecycleChannel.invokeMethod("requestClose", arguments: nil) { [weak self] result in
      guard let self else { return }
      self.closeRequestPending = false
      if result as? Bool == true {
        self.closeAllowed = true
        self.close()
      }
    }
    return false
  }

  private func handleFileCall(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    guard call.method == "linkNoReplace" else {
      result(FlutterMethodNotImplemented)
      return
    }
    guard
      let arguments = call.arguments as? [String: Any],
      let source = arguments["sourcePath"] as? String,
      let target = arguments["targetPath"] as? String
    else {
      result(FlutterError(code: "invalid-arguments", message: "File paths are required.", details: nil))
      return
    }
    let status = source.withCString { sourcePointer in
      target.withCString { targetPointer in
        Darwin.link(sourcePointer, targetPointer)
      }
    }
    if status == 0 {
      result(true)
      return
    }
    let code = errno
    if code == EEXIST {
      result(false)
    } else {
      result(FlutterError(
        code: "file-publish-error",
        message: String(cString: strerror(code)),
        details: code
      ))
    }
  }
}

final class PlaybackSessionState {
  private(set) var generation: UInt64 = 0
  private(set) var totalFrames: AVAudioFramePosition = 0
  private(set) var playedFrames: AVAudioFramePosition = 0
  private(set) var playing = false

  @discardableResult
  func start(totalFrames: AVAudioFramePosition) -> UInt64 {
    generation &+= 1
    self.totalFrames = totalFrames
    playedFrames = 0
    playing = true
    return generation
  }

  func updatePlayedFrames(_ frames: AVAudioFramePosition) {
    playedFrames = min(max(0, frames), totalFrames)
  }

  func pause() {
    generation &+= 1
    playing = false
  }

  @discardableResult
  func complete(generation: UInt64, frameCount: AVAudioFramePosition) -> Bool {
    guard generation == self.generation, playing else {
      return false
    }
    updatePlayedFrames(frameCount)
    playing = false
    return true
  }

  func reset() {
    generation &+= 1
    totalFrames = 0
    playedFrames = 0
    playing = false
  }
}

private final class PcmPlaybackOutput {
  private let engine = AVAudioEngine()
  private let player = AVAudioPlayerNode()
  private let session = PlaybackSessionState()
  private var buffer: AVAudioPCMBuffer?

  init(messenger: FlutterBinaryMessenger) {
    let channel = FlutterMethodChannel(
      name: "io.onetwogoyeah/playback",
      binaryMessenger: messenger
    )
    channel.setMethodCallHandler { [weak self] call, result in
      self?.handle(call, result: result)
    }
  }

  deinit {
    dispose()
  }

  private func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    do {
      switch call.method {
      case "start":
        guard
          let arguments = call.arguments as? [String: Any],
          let sampleRate = arguments["sampleRate"] as? Int,
          let typedData = arguments["pcm16le"] as? FlutterStandardTypedData
        else {
          throw PlaybackError.invalidArguments
        }
        try start(sampleRate: sampleRate, data: typedData.data)
        result(nil)
      case "query":
        result(state())
      case "pause":
        result(pause())
      case "dispose":
        dispose()
        result(nil)
      default:
        result(FlutterMethodNotImplemented)
      }
    } catch {
      result(FlutterError(code: "playback-error", message: error.localizedDescription, details: nil))
    }
  }

  private func start(sampleRate: Int, data: Data) throws {
    guard sampleRate > 0, !data.isEmpty, data.count % 4 == 0 else {
      throw PlaybackError.invalidArguments
    }
    dispose()
    guard let format = AVAudioFormat(
      commonFormat: .pcmFormatInt16,
      sampleRate: Double(sampleRate),
      channels: 2,
      interleaved: true
    ) else {
      throw PlaybackError.invalidFormat
    }
    let frameCount = AVAudioFrameCount(data.count / 4)
    guard let nextBuffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount) else {
      throw PlaybackError.allocationFailed
    }
    nextBuffer.frameLength = frameCount
    guard let destination = nextBuffer.audioBufferList.pointee.mBuffers.mData else {
      throw PlaybackError.allocationFailed
    }
    data.copyBytes(to: destination.assumingMemoryBound(to: UInt8.self), count: data.count)
    engine.attach(player)
    engine.connect(player, to: engine.mainMixerNode, format: format)
    try engine.start()
    buffer = nextBuffer
    let scheduledFrames = AVAudioFramePosition(frameCount)
    let generation = session.start(totalFrames: scheduledFrames)
    player.scheduleBuffer(nextBuffer) { [weak self] in
      DispatchQueue.main.async {
        self?.session.complete(
          generation: generation,
          frameCount: scheduledFrames
        )
      }
    }
    player.play()
  }

  private func state() -> [String: Any] {
    if session.playing,
       let nodeTime = player.lastRenderTime,
       let playerTime = player.playerTime(forNodeTime: nodeTime) {
      session.updatePlayedFrames(playerTime.sampleTime)
    }
    return [
      "playedFrames": session.playedFrames,
      "playing": session.playing && player.isPlaying,
    ]
  }

  private func pause() -> [String: Any] {
    _ = state()
    session.pause()
    player.stop()
    engine.stop()
    buffer = nil
    return ["playedFrames": session.playedFrames, "playing": false]
  }

  private func dispose() {
    session.reset()
    player.stop()
    if engine.attachedNodes.contains(player) {
      engine.detach(player)
    }
    engine.stop()
    buffer = nil
  }
}

private enum PlaybackError: LocalizedError {
  case invalidArguments
  case invalidFormat
  case allocationFailed

  var errorDescription: String? {
    switch self {
    case .invalidArguments: return "Invalid PCM playback arguments."
    case .invalidFormat: return "Unable to create the PCM playback format."
    case .allocationFailed: return "Unable to allocate the PCM playback buffer."
    }
  }
}
