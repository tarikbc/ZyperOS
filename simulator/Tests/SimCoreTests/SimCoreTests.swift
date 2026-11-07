// SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
import XCTest
@testable import SimCore

final class SimCoreTests: XCTestCase {
    func testStateLine() {
        var s = DeviceState()
        XCTAssertTrue(s.apply(line: "state leds=30 backlight=95 bl_power=0 battery=100 cable=1 headphones=0"))
        XCTAssertEqual(s.leds, 30)
        XCTAssertFalse(s.led(0))
        XCTAssertTrue(s.led(4))
        XCTAssertEqual(s.backlight, 95)
        XCTAssertTrue(s.cable)
        XCTAssertEqual(s.screenLight, 95.0 / 255, accuracy: 0.001)
        XCTAssertEqual(s.kind, 1)
        XCTAssertTrue(s.apply(line: "state cable=1 kind=2"))
        XCTAssertEqual(s.kind, 2)
        XCTAssertTrue(s.apply(line: "state kind=7"))
        XCTAssertEqual(s.kind, 1)
        XCTAssertTrue(s.apply(line: "state bl_power=4 cable=0 headphones=1"))
        XCTAssertEqual(s.screenLight, 0)
        XCTAssertFalse(s.cable)
        XCTAssertTrue(s.headphones)
        XCTAssertEqual(s.leds, 30, "fields a line leaves out stay")
        XCTAssertFalse(s.apply(line: "rumble 1 2 3"))
    }

    func testRumbleLine() {
        XCTAssertEqual(Rumble(line: "rumble 65535 0 200"), Rumble(strong: 65535, weak: 0, milliseconds: 200))
        XCTAssertEqual(Rumble(line: "rumble 0 0 0")?.isOn, false)
        XCTAssertNil(Rumble(line: "rumble 1 2"))
        XCTAssertNil(Rumble(line: "state leds=1"))
    }

    func testCommands() {
        XCTAssertEqual(ControlCommand(line: "state"), .state)
        XCTAssertEqual(ControlCommand(line: "press a+right"), .press([.a, .right]))
        XCTAssertEqual(ControlCommand(line: "tap start 300"), .tap([.start], milliseconds: 300))
        XCTAssertEqual(ControlCommand(line: "tap menu"), .tap([.menu], milliseconds: 80))
        XCTAssertEqual(ControlCommand(line: "battery 15"), .battery(15))
        XCTAssertEqual(ControlCommand(line: "cable 0"), .cable(false))
        XCTAssertEqual(ControlCommand(line: "kind wall"), .kind(1))
        XCTAssertEqual(ControlCommand(line: "kind computer"), .kind(2))
        XCTAssertNil(ControlCommand(line: "kind usb"))
        XCTAssertEqual(ControlCommand(line: "headphones 1"), .headphones(true))
        XCTAssertEqual(ControlCommand(line: "screenshot /tmp/x.png"), .screenshot("/tmp/x.png"))
        XCTAssertEqual(ControlCommand(line: "wait-frames 5"), .waitFrames(5))
        XCTAssertNil(ControlCommand(line: "press z"))
        XCTAssertNil(ControlCommand(line: "battery 101"))
        XCTAssertNil(ControlCommand(line: "cable yes"))
        XCTAssertNil(ControlCommand(line: "tap a 0"))
        XCTAssertNil(ControlCommand(line: ""))
    }
}
