import AVFoundation
import Darwin
import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  private var playback: PcmPlaybackOutput?
  private var simulationPlayback: SimulationPlaybackOutput?
  private var fileChannel: FlutterMethodChannel?
  private var documentWriter: IOSDocumentWriter?

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
    simulationPlayback = SimulationPlaybackOutput(
      messenger: registrar.messenger()
    )
    documentWriter = IOSDocumentWriter(presenterProvider: { registrar.viewController })
    fileChannel = FlutterMethodChannel(
      name: "io.onetwogoyeah/files",
      binaryMessenger: registrar.messenger()
    )
    fileChannel?.setMethodCallHandler(handleFileCall)
  }

  private func handleFileCall(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    guard call.method == "linkNoReplace" else {
      guard let documentWriter else {
        result(FlutterMethodNotImplemented)
        return
      }
      documentWriter.handle(call, result: result)
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

  override func applicationWillTerminate(_ application: UIApplication) {
    documentWriter?.dispose()
    super.applicationWillTerminate(application)
  }
}

final class DocumentWriteSession {
  private var handle: FileHandle?

  init(url: URL) throws {
    let handle = try FileHandle(forWritingTo: url)
    do {
      try handle.truncate(atOffset: 0)
    } catch {
      try? handle.close()
      throw error
    }
    self.handle = handle
  }

  func write(_ data: Data) throws {
    guard let handle else {
      throw DocumentSaveError.closedSession
    }
    try handle.write(contentsOf: data)
  }

  func finish() throws {
    guard let handle else {
      throw DocumentSaveError.closedSession
    }
    self.handle = nil
    do {
      try handle.synchronize()
      try handle.close()
    } catch {
      try? handle.close()
      throw error
    }
  }

  func close() {
    guard let handle else {
      return
    }
    self.handle = nil
    try? handle.close()
  }

  deinit {
    close()
  }
}

final class IOSDocumentWriter: NSObject, UIDocumentPickerDelegate {
  private struct PendingPicker {
    let result: FlutterResult
    let placeholderDirectory: URL
  }

  private struct AuthorizedDocument {
    let url: URL
    let securityScoped: Bool
  }

  private struct ActiveSession {
    let documentKey: String
    let writer: DocumentWriteSession
  }

  private let presenterProvider: () -> UIViewController?
  private var pendingPicker: PendingPicker?
  private var authorizedDocuments: [String: AuthorizedDocument] = [:]
  private var sessions: [Int: ActiveSession] = [:]
  private var nextSession = 1

  init(presenterProvider: @escaping () -> UIViewController?) {
    self.presenterProvider = presenterProvider
  }

  func resolvePresenter() -> UIViewController? {
    presenterProvider()
  }

  func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    do {
      switch call.method {
      case "createDocument":
        try createDocument(call, result: result)
      case "openDocumentWrite":
        try openDocumentWrite(call, result: result)
      case "writeDocumentChunk":
        try writeDocumentChunk(call, result: result)
      case "finishDocumentWrite":
        try finishDocumentWrite(call, result: result)
      case "commitDocument":
        try commitDocument(call, result: result)
      case "abortDocumentWrite":
        try abortDocumentWrite(call, result: result)
      case "deleteDocument":
        try deleteDocument(call, result: result)
      default:
        result(FlutterMethodNotImplemented)
      }
    } catch {
      result(flutterError(for: error, method: call.method))
    }
  }

  private func createDocument(_ call: FlutterMethodCall, result: @escaping FlutterResult) throws {
    guard pendingPicker == nil else {
      throw DocumentSaveError.pickerAlreadyActive
    }
    guard
      let arguments = call.arguments as? [String: Any],
      let suggestedName = arguments["suggestedName"] as? String,
      !suggestedName.isEmpty,
      URL(fileURLWithPath: suggestedName).lastPathComponent == suggestedName,
      let mimeType = arguments["mimeType"] as? String,
      !mimeType.isEmpty
    else {
      throw DocumentSaveError.invalidArguments
    }
    guard let presenter = resolvePresenter() else {
      throw DocumentSaveError.noViewController
    }

    let directory = FileManager.default.temporaryDirectory
      .appendingPathComponent("otgy-document-export-\(UUID().uuidString)", isDirectory: true)
    try FileManager.default.createDirectory(
      at: directory,
      withIntermediateDirectories: false
    )
    let placeholder = directory.appendingPathComponent(suggestedName, isDirectory: false)
    guard FileManager.default.createFile(atPath: placeholder.path, contents: Data()) else {
      try? FileManager.default.removeItem(at: directory)
      throw DocumentSaveError.placeholderCreationFailed
    }

    let picker = UIDocumentPickerViewController(forExporting: [placeholder], asCopy: true)
    picker.delegate = self
    pendingPicker = PendingPicker(result: result, placeholderDirectory: directory)
    presenter.present(picker, animated: true)
  }

  func documentPicker(
    _ controller: UIDocumentPickerViewController,
    didPickDocumentsAt urls: [URL]
  ) {
    guard let pending = takePendingPicker() else {
      return
    }
    guard let url = urls.first else {
      pending.result(nil)
      return
    }
    let key = url.absoluteString
    let securityScoped = url.startAccessingSecurityScopedResource()
    authorizedDocuments[key] = AuthorizedDocument(
      url: url,
      securityScoped: securityScoped
    )
    pending.result(key)
  }

  func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
    takePendingPicker()?.result(nil)
  }

  private func openDocumentWrite(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) throws {
    let key = try requiredDocumentKey(call)
    guard let document = authorizedDocuments[key] else {
      throw DocumentSaveError.unauthorizedDocument
    }
    let id = nextSession
    nextSession += 1
    sessions[id] = ActiveSession(
      documentKey: key,
      writer: try DocumentWriteSession(url: document.url)
    )
    result(id)
  }

  private func writeDocumentChunk(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) throws {
    let session = try requiredSession(call)
    guard
      let arguments = call.arguments as? [String: Any],
      let bytes = arguments["bytes"] as? FlutterStandardTypedData
    else {
      throw DocumentSaveError.invalidArguments
    }
    try session.writer.write(bytes.data)
    result(nil)
  }

  private func finishDocumentWrite(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) throws {
    let id = try requiredSessionID(call)
    guard let session = sessions.removeValue(forKey: id) else {
      throw DocumentSaveError.unknownSession
    }
    do {
      try session.writer.finish()
      result(nil)
    } catch {
      session.writer.close()
      throw error
    }
  }
  private func commitDocument(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) throws {
    releaseDocument(try requiredDocumentKey(call))
    result(nil)
  }

  private func abortDocumentWrite(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) throws {
    let id = try requiredSessionID(call)
    let session = sessions.removeValue(forKey: id)
    session?.writer.close()
    let key: String
    if let session {
      key = session.documentKey
    } else {
      key = try requiredDocumentKey(call)
    }
    try removeDocument(key)
    result(nil)
  }

  private func deleteDocument(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) throws {
    try removeDocument(try requiredDocumentKey(call))
    result(nil)
  }

  private func requiredSession(_ call: FlutterMethodCall) throws -> ActiveSession {
    let id = try requiredSessionID(call)
    guard let session = sessions[id] else {
      throw DocumentSaveError.unknownSession
    }
    return session
  }

  private func requiredSessionID(_ call: FlutterMethodCall) throws -> Int {
    guard
      let arguments = call.arguments as? [String: Any],
      let id = arguments["session"] as? Int
    else {
      throw DocumentSaveError.invalidArguments
    }
    return id
  }

  private func requiredDocumentKey(_ call: FlutterMethodCall) throws -> String {
    guard
      let arguments = call.arguments as? [String: Any],
      let key = arguments["uri"] as? String,
      let url = URL(string: key),
      url.isFileURL
    else {
      throw DocumentSaveError.invalidArguments
    }
    return key
  }

  private func takePendingPicker() -> PendingPicker? {
    guard let pending = pendingPicker else {
      return nil
    }
    pendingPicker = nil
    try? FileManager.default.removeItem(at: pending.placeholderDirectory)
    return pending
  }

  private func removeDocument(_ key: String) throws {
    guard let document = authorizedDocuments[key] else {
      return
    }
    defer { releaseDocument(key) }
    do {
      try FileManager.default.removeItem(at: document.url)
    } catch let error as CocoaError where error.code == .fileNoSuchFile {
      return
    }
  }

  private func releaseDocument(_ key: String) {
    guard let document = authorizedDocuments.removeValue(forKey: key) else {
      return
    }
    if document.securityScoped {
      document.url.stopAccessingSecurityScopedResource()
    }
  }

  func dispose() {
    for session in sessions.values {
      session.writer.close()
    }
    sessions.removeAll()
    for key in Array(authorizedDocuments.keys) {
      try? removeDocument(key)
    }
    if let pending = takePendingPicker() {
      pending.result(nil)
    }
  }

  private func flutterError(for error: Error, method: String) -> FlutterError {
    let nsError = error as NSError
    return FlutterError(
      code: "document-save-error",
      message: error.localizedDescription,
      details: [
        "method": method,
        "exception": String(reflecting: type(of: error)),
        "domain": nsError.domain,
        "code": nsError.code,
        "description": String(describing: error),
      ]
    )
  }
}

private enum DocumentSaveError: LocalizedError {
  case invalidArguments
  case pickerAlreadyActive
  case noViewController
  case placeholderCreationFailed
  case unauthorizedDocument
  case unknownSession
  case closedSession

  var errorDescription: String? {
    switch self {
    case .invalidArguments: return "Invalid document save arguments."
    case .pickerAlreadyActive: return "Another document picker is already active."
    case .noViewController: return "No view controller is available for the document picker."
    case .placeholderCreationFailed: return "Unable to create the export placeholder."
    case .unauthorizedDocument: return "The selected document is no longer authorized."
    case .unknownSession: return "The document write session does not exist."
    case .closedSession: return "The document write session is already closed."
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
    let session = AVAudioSession.sharedInstance()
    try session.setCategory(.playback, mode: .default)
    try session.setPreferredIOBufferDuration(0.005)
    try session.setActive(true)
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
    try? AVAudioSession.sharedInstance().setActive(false)
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
