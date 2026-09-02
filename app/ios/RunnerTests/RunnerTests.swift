import AVFoundation
import Flutter
import UIKit
import XCTest

@testable import Runner

class RunnerTests: XCTestCase {

  func testPlaybackBufferUsesAnEngineStandardFormatAndPreservesFrames() throws {
    let samples: [Int16] = [.min, .max, 0, -1]
    var data = Data()
    for sample in samples {
      var littleEndian = sample.littleEndian
      Swift.withUnsafeBytes(of: &littleEndian) { data.append(contentsOf: $0) }
    }

    let buffer = try PcmPlaybackBufferBuilder.make(sampleRate: 11_025, data: data)
    let engine = AVAudioEngine()
    let player = AVAudioPlayerNode()
    engine.attach(player)
    engine.connect(player, to: engine.mainMixerNode, format: buffer.format)

    XCTAssertTrue(buffer.format.isStandard)
    XCTAssertFalse(buffer.format.isInterleaved)
    XCTAssertEqual(buffer.format.sampleRate, 11_025)
    XCTAssertEqual(buffer.format.channelCount, 2)
    XCTAssertEqual(buffer.frameLength, 2)
    let channels = try XCTUnwrap(buffer.floatChannelData)
    XCTAssertEqual(channels[0][0], -1, accuracy: 1 / 32768)
    XCTAssertEqual(channels[1][0], Float(Int16.max) / 32768, accuracy: 1 / 32768)
    XCTAssertEqual(channels[0][1], 0, accuracy: 1 / 32768)
    XCTAssertEqual(channels[1][1], -1 / 32768, accuracy: 1 / 32768)
  }

  func testPlaybackBufferRejectsMalformedInterleavedPcm() {
    XCTAssertThrowsError(
      try PcmPlaybackBufferBuilder.make(sampleRate: 44_100, data: Data([0, 1, 2]))
    )
  }

  func testDocumentWriteSessionTruncatesAndWritesChunks() throws {
    let directory = FileManager.default.temporaryDirectory
      .appendingPathComponent("otgy-ios-document-test-\(UUID().uuidString)", isDirectory: true)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false)
    defer { try? FileManager.default.removeItem(at: directory) }
    let target = directory.appendingPathComponent("export.ogg")
    try Data([9, 9, 9, 9]).write(to: target)

    let writer = try DocumentWriteSession(url: target)
    try writer.write(Data([1, 2]))
    try writer.write(Data([3, 4, 5]))
    try writer.finish()

    XCTAssertEqual(try Data(contentsOf: target), Data([1, 2, 3, 4, 5]))
  }

  func testDocumentWriterResolvesPresenterWhenDocumentPickerIsRequested() {
    var presenter: UIViewController?
    let writer = IOSDocumentWriter(presenterProvider: { presenter })

    XCTAssertNil(writer.resolvePresenter())
    presenter = UIViewController()
    XCTAssertIdentical(writer.resolvePresenter(), presenter)
  }

}
