import AVFoundation
import Darwin
import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  private var playback: PcmPlaybackOutput?
  private var fileChannel: FlutterMethodChannel?

  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }

  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)
    guard let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "OtgyPlayback") else {
      return
    }
    playback = PcmPlaybackOutput(messenger: registrar.messenger())
    fileChannel = FlutterMethodChannel(
      name: "io.onetwogoyeah/files",
      binaryMessenger: registrar.messenger()
    )
    fileChannel?.setMethodCallHandler(handleFileCall)
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

private final class PcmPlaybackOutput: NSObject {
  private let engine = AVAudioEngine()
  private let player = AVAudioPlayerNode()
  private var buffer: AVAudioPCMBuffer?
  private var totalFrames: AVAudioFramePosition = 0
  private var playedFrames: AVAudioFramePosition = 0
  private var playing = false
  private var generation: UInt64 = 0

  init(messenger: FlutterBinaryMessenger) {
    super.init()
    let channel = FlutterMethodChannel(
      name: "io.onetwogoyeah/playback",
      binaryMessenger: messenger
    )
    channel.setMethodCallHandler { [weak self] call, result in
      self?.handle(call, result: result)
    }
    NotificationCenter.default.addObserver(
      self,
      selector: #selector(interrupted),
      name: AVAudioSession.interruptionNotification,
      object: nil
    )
  }

  deinit {
    dispose()
    NotificationCenter.default.removeObserver(self)
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
    let nextBuffer = try PcmPlaybackBufferBuilder.make(
      sampleRate: sampleRate,
      data: data
    )
    let session = AVAudioSession.sharedInstance()
    try session.setCategory(.playback, mode: .default)
    try session.setActive(true)
    engine.attach(player)
    engine.connect(player, to: engine.mainMixerNode, format: nextBuffer.format)
    try engine.start()
    buffer = nextBuffer
    let frameCount = nextBuffer.frameLength
    totalFrames = AVAudioFramePosition(frameCount)
    playedFrames = 0
    playing = true
    let scheduledGeneration = generation
    let scheduledFrames = totalFrames
    player.scheduleBuffer(nextBuffer) { [weak self] in
      DispatchQueue.main.async {
        guard let self, self.generation == scheduledGeneration else {
          return
        }
        self.playedFrames = scheduledFrames
        self.playing = false
      }
    }
    player.play()
  }

  private func state() -> [String: Any] {
    if playing,
       let nodeTime = player.lastRenderTime,
       let playerTime = player.playerTime(forNodeTime: nodeTime) {
      playedFrames = min(max(0, playerTime.sampleTime), totalFrames)
    }
    return ["playedFrames": playedFrames, "playing": playing && player.isPlaying]
  }

  private func pause() -> [String: Any] {
    _ = state()
    generation &+= 1
    playing = false
    player.stop()
    engine.stop()
    buffer = nil
    return ["playedFrames": playedFrames, "playing": false]
  }

  @objc private func interrupted() {
    _ = pause()
  }

  private func dispose() {
    generation &+= 1
    player.stop()
    if engine.attachedNodes.contains(player) {
      engine.detach(player)
    }
    engine.stop()
    buffer = nil
    totalFrames = 0
    playedFrames = 0
    playing = false
    try? AVAudioSession.sharedInstance().setActive(false)
  }
}

enum PcmPlaybackBufferBuilder {
  static func make(sampleRate: Int, data: Data) throws -> AVAudioPCMBuffer {
    guard sampleRate > 0, !data.isEmpty, data.count % 4 == 0 else {
      throw PlaybackError.invalidArguments
    }
    guard
      let format = AVAudioFormat(
        standardFormatWithSampleRate: Double(sampleRate),
        channels: 2
      )
    else {
      throw PlaybackError.invalidFormat
    }
    let frameCountValue = data.count / 4
    let frameCount = AVAudioFrameCount(frameCountValue)
    guard
      Int(frameCount) == frameCountValue,
      let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount),
      let channels = buffer.floatChannelData
    else {
      throw PlaybackError.allocationFailed
    }
    data.withUnsafeBytes { rawBuffer in
      let bytes = rawBuffer.bindMemory(to: UInt8.self)
      for frame in 0..<frameCountValue {
        let offset = frame * 4
        let leftBits = UInt16(bytes[offset]) | UInt16(bytes[offset + 1]) << 8
        let rightBits = UInt16(bytes[offset + 2]) | UInt16(bytes[offset + 3]) << 8
        channels[0][frame] = Float(Int16(bitPattern: leftBits)) / 32768
        channels[1][frame] = Float(Int16(bitPattern: rightBits)) / 32768
      }
    }
    buffer.frameLength = frameCount
    return buffer
  }
}

enum PlaybackError: LocalizedError {
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
