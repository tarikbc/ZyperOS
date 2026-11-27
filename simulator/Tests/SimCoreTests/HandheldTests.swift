// SPDX-License-Identifier: GPL-3.0-only
@testable import SimCore
import XCTest

/// The handhelds `make handhelds` writes from the device files: each one
/// whole enough for the window to draw and press
final class HandheldTests: XCTestCase {
    func testEveryHandheld() {
        XCTAssertFalse(Handheld.all.isEmpty)
        XCTAssertEqual(Set(Handheld.all.map(\.id)).count, Handheld.all.count, "one handheld per id")
        XCTAssertEqual(Handheld.standard.id, "pixel2", "the VM is a Pixel 2")
        for h in Handheld.all {
            func inside(_ r: Handheld.Rect, _ w: Double, _ hgt: Double, _ cy: Double = 0) -> Bool {
                abs(r.x) + r.w / 2 <= w / 2 + 0.01 && abs(r.y - cy) + r.h / 2 <= hgt / 2 + 0.01
            }
            XCTAssertTrue(inside(h.glass, h.body.w, h.body.h), "\(h.id): the glass on the body")
            XCTAssertTrue(h.screen.w <= h.glass.w && h.screen.h <= h.glass.h, "\(h.id): the screen in its glass")
            // The picture's pixels and the lit area are the same shape
            XCTAssertEqual(h.panel.w / h.panel.h, h.screen.w / h.screen.h, accuracy: 0.01, h.id)
            XCTAssertNotNil(h.dpad, "\(h.id): a d-pad")
            let faceY = (h.margin.top - h.margin.bottom) / 2
            for k in h.keys {
                XCTAssertTrue(k.kind == .dpad || k.button != nil, "\(h.id): every key presses something")
                XCTAssertTrue(inside(Handheld.Rect(x: k.x, y: k.y, w: k.w, h: k.h), h.width, h.height, faceY),
                              "\(h.id): \(k.button?.rawValue ?? "d-pad") in the drawing")
            }
            for b in [PadButton.a, .b, .start, .select, .up, .power] {
                XCTAssertTrue(h.has(b), "\(h.id): \(b.rawValue)")
            }
        }
    }

    func testButtonsAHandheldHasNot() {
        let pixel2 = Handheld.named("pixel2")!
        XCTAssertFalse(pixel2.has(.brightup), "the Pixel 2 has no brightness keys")
        XCTAssertEqual(pixel2.key(for: .left), pixel2.dpad)
        if let brick = Handheld.named("brick") {
            XCTAssertTrue(brick.has(.brightdown) && brick.has(.brightup), "the Brick's F1 and F2")
            XCTAssertEqual(brick.panel.w, 1024)
        }
        if let r36s = Handheld.named("r36s") {
            XCTAssertFalse(r36s.has(.brightup), "the R36S has no brightness keys: FN with a volume key")
            XCTAssertEqual(r36s.panel.w, 640)
            XCTAssertEqual(r36s.sticks.count, 2, "its two sticks, drawn")
        }
        XCTAssertTrue(pixel2.sticks.isEmpty, "the Pixel 2 has no sticks")
    }
}
