import AVFoundation
import Cocoa
import Darwin
import FlutterMacOS

class MainFlutterWindow: NSWindow, NSWindowDelegate {
  private var playback: PcmPlaybackOutput?
  private var simulationPlayback: SimulationPlaybackOutput?
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
    simulationPlayback = SimulationPlaybackOutput(
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
    guard call.method == "linkNoReplace" || call.method == "writeNoReplace" else {
      result(FlutterMethodNotImplemented)
      return
    }
    guard
      let arguments = call.arguments as? [String: Any],
      let target = arguments["targetPath"] as? String
    else {
      result(FlutterError(code: "invalid-arguments", message: "File paths are required.", details: nil))
      return
    }
    if call.method == "writeNoReplace" {
      guard let typedData = arguments["bytes"] as? FlutterStandardTypedData else {
        result(FlutterError(code: "invalid-arguments", message: "Output bytes are required.", details: nil))
        return
      }
      let code = writeNoReplace(data: typedData.data, path: target)
      if code == 0 {
        result(true)
      } else if code == EEXIST {
        result(false)
      } else {
        result(FlutterError(
          code: "file-publish-error",
          message: String(cString: strerror(code)),
          details: code
        ))
      }
      return
    }
    guard let source = arguments["sourcePath"] as? String else {
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
      return
    }

    // Some volumes (and the macOS sandbox) reject hard links even though an
    // atomic exclusive rename is supported. Keep the same no-replace
    // semantics while avoiding a partially visible destination.
    let renameStatus = source.withCString { sourcePointer in
      target.withCString { targetPointer in
        Darwin.renameatx_np(
          AT_FDCWD,
          sourcePointer,
          AT_FDCWD,
          targetPointer,
          UInt32(RENAME_EXCL)
        )
      }
    }
    if renameStatus == 0 {
      result(true)
      return
    }
    let renameCode = errno
    if renameCode == EEXIST {
      result(false)
      return
    }
    result(FlutterError(
      code: "file-publish-error",
      message: String(cString: strerror(renameCode)),
      details: renameCode
    ))
  }

  private func writeNoReplace(data: Data, path: String) -> Int32 {
    let descriptor = path.withCString {
      Darwin.open(
        $0,
        O_WRONLY | O_CREAT | O_EXCL,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
      )
    }
    guard descriptor >= 0 else {
      return errno
    }

    var failureCode: Int32 = 0
    let wroteAll = data.withUnsafeBytes { (buffer: UnsafeRawBufferPointer) -> Bool in
      guard let baseAddress = buffer.baseAddress else {
        return true
      }
      var offset = 0
      while offset < buffer.count {
        let written = Darwin.write(
          descriptor,
          baseAddress.advanced(by: offset),
          buffer.count - offset
        )
        if written <= 0 {
          failureCode = written < 0 ? errno : EIO
          return false
        }
        offset += written
      }
      return true
    }
    if wroteAll && Darwin.fsync(descriptor) != 0 {
      failureCode = errno
    }
    let closeStatus = Darwin.close(descriptor)
    if wroteAll && failureCode == 0 && closeStatus != 0 {
      failureCode = errno
    }
    guard failureCode == 0 else {
      _ = path.withCString { Darwin.unlink($0) }
      return failureCode
    }
    return 0
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

private typealias SimulationRenderFn = @convention(c) (
  UnsafeMutableRawPointer?, UnsafeMutablePointer<UInt8>?, UInt32
) -> Void
private typealias SimulationIsFinishedFn = @convention(c) (UnsafeRawPointer?) -> UInt8
private typealias SimulationRenderedFramesFn = @convention(c) (UnsafeRawPointer?) -> UInt64

private final class SimulationPlaybackOutput: NSObject {
  private let engine = AVAudioEngine()
  private var sourceNode: AVAudioSourceNode?
  private var handle: UnsafeMutableRawPointer?
  private var renderFn: SimulationRenderFn?
  private var isFinishedFn: SimulationIsFinishedFn?
  private var renderedFramesFn: SimulationRenderedFramesFn?
  private var outputLatencyFrames: UInt64 = 0
  private var playing = false
  private var paused = false

  init(messenger: FlutterBinaryMessenger) {
    super.init()
    let symbols = SimulationPlaybackOutput.loadSymbols()
    renderFn = symbols?.0
    isFinishedFn = symbols?.1
    renderedFramesFn = symbols?.2
    let channel = FlutterMethodChannel(
      name: "io.onetwogoyeah/simulation-playback",
      binaryMessenger: messenger
    )
    channel.setMethodCallHandler { [weak self] call, result in
      self?.handle(call, result: result)
    }
  }

  deinit {
    stop()
  }

  private func handle(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) {
    do {
      switch call.method {
      case "start":
        guard
          let arguments = call.arguments as? [String: Any],
          let rawHandle = arguments["engineHandle"] as? NSNumber,
          let sampleRate = arguments["sampleRate"] as? Int
        else {
          throw SimulationPlaybackError.invalidArguments
        }
        try start(
          engineHandle: rawHandle.int64Value,
          sampleRate: sampleRate
        )
        result(nil)
      case "pause":
        pause()
        result(nil)
      case "resume":
        resume()
        result(nil)
      case "query":
        result(state())
      case "stop":
        stop()
        result(nil)
      default:
        result(FlutterMethodNotImplemented)
      }
    } catch {
      result(
        FlutterError(
          code: "simulation-playback-error",
          message: error.localizedDescription,
          details: nil
        )
      )
    }
  }

  private func start(engineHandle: Int64, sampleRate: Int) throws {
    stop()
    guard
      engineHandle != 0,
      sampleRate > 0,
      let newHandle = UnsafeMutableRawPointer(bitPattern: UInt(engineHandle)),
      let render = renderFn,
      let format = AVAudioFormat(
        commonFormat: .pcmFormatInt16,
        sampleRate: Double(sampleRate),
        channels: 2,
        interleaved: true
      )
    else {
      throw SimulationPlaybackError.unavailable
    }
    let node = AVAudioSourceNode(format: format) {
      _, _, frameCount, audioBufferList -> OSStatus in
      let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
      if let data = buffers[0].mData {
        render(
          newHandle,
          data.assumingMemoryBound(to: UInt8.self),
          AVAudioFrameCount(frameCount)
        )
      }
      return noErr
    }
    engine.attach(node)
    engine.connect(node, to: engine.mainMixerNode, format: format)
    try engine.start()
    sourceNode = node
    handle = newHandle
    outputLatencyFrames = UInt64(
      max(0.0, engine.outputNode.presentationLatency * Double(sampleRate))
    )
    playing = true
    paused = false
  }

  private func pause() {
    guard playing, !paused else {
      return
    }
    engine.pause()
    paused = true
  }

  private func resume() {
    guard playing, paused else {
      return
    }
    do {
      try engine.start()
      paused = false
    } catch {
    }
  }

  private func stop() {
    playing = false
    paused = false
    if engine.isRunning {
      engine.stop()
    }
    if let node = sourceNode {
      engine.detach(node)
    }
    sourceNode = nil
    handle = nil
    outputLatencyFrames = 0
  }

  private func state() -> [String: Any] {
    guard
      let handle = handle,
      let renderedFramesFn = renderedFramesFn,
      let isFinishedFn = isFinishedFn
    else {
      return ["playedFrames": 0, "playing": false, "finished": true, "latencyMs": 0]
    }
    let rendered = renderedFramesFn(handle)
    let played = rendered > outputLatencyFrames
      ? rendered - outputLatencyFrames
      : 0
    let finished = isFinishedFn(handle) != 0
    return [
      "playedFrames": Int(played),
      "playing": playing && !paused && !finished,
      "finished": finished,
      "latencyMs": Int(
        (max(0.0, engine.outputNode.presentationLatency) * 1000).rounded()
      ),
    ]
  }

  private static func loadSymbols() -> (
    SimulationRenderFn, SimulationIsFinishedFn, SimulationRenderedFramesFn
  )? {
    var candidates: [UnsafeMutableRawPointer?] = []
    if let frameworks = Bundle.main.privateFrameworksPath {
      candidates.append(
        dlopen(
          "\(frameworks)/otgy_native.framework/otgy_native",
          RTLD_NOW | RTLD_LOCAL
        )
      )
      candidates.append(
        dlopen("\(frameworks)/libotgy_native.dylib", RTLD_NOW | RTLD_LOCAL)
      )
    }
    candidates.append(dlopen(nil, RTLD_NOW))
    for candidate in candidates {
      guard
        let library = candidate,
        let render = dlsym(library, "otgy_playback_engine_render"),
        let finished = dlsym(
          library,
          "otgy_playback_engine_is_finished"
        ),
        let frames = dlsym(
          library,
          "otgy_playback_engine_get_rendered_frames"
        )
      else {
        continue
      }
      return (
        unsafeBitCast(render, to: SimulationRenderFn.self),
        unsafeBitCast(finished, to: SimulationIsFinishedFn.self),
        unsafeBitCast(frames, to: SimulationRenderedFramesFn.self)
      )
    }
    return nil
  }
}

private enum SimulationPlaybackError: LocalizedError {
  case invalidArguments
  case unavailable

  var errorDescription: String? {
    switch self {
    case .invalidArguments: return "Invalid simulation playback arguments."
    case .unavailable: return "The simulation playback engine is unavailable."
    }
  }
}
