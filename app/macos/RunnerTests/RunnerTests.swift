import Cocoa
import FlutterMacOS
import XCTest
@testable import OneTwoGoYeah

class RunnerTests: XCTestCase {

  func testStoppedBufferCannotCompleteReplacementPlayback() {
    let state = PlaybackSessionState()
    let firstGeneration = state.start(totalFrames: 100)
    state.updatePlayedFrames(25)
    state.pause()

    let secondGeneration = state.start(totalFrames: 80)
    XCTAssertFalse(state.complete(generation: firstGeneration, frameCount: 100))
    XCTAssertTrue(state.playing)
    XCTAssertEqual(state.playedFrames, 0)

    XCTAssertTrue(state.complete(generation: secondGeneration, frameCount: 80))
    XCTAssertFalse(state.playing)
    XCTAssertEqual(state.playedFrames, 80)
  }

}
