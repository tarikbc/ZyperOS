// SPDX-License-Identifier: GPL-3.0-only
import CoreGraphics
import XCTest
@testable import SimCore

/// RFBClient against a small fake of QEMU's VNC server on a Unix socket
final class RFBClientTests: XCTestCase {
    private func u16(_ v: Int) -> [UInt8] { [UInt8(v >> 8), UInt8(v & 0xFF)] }
    private func u32(_ v: Int) -> [UInt8] { [0, 0, UInt8(v >> 8 & 0xFF), UInt8(v & 0xFF)] }
    /// Parts joined: one call, where a chain of `+` on array literals took an
    /// older Swift (a CI runner's) past its type-checking time limit
    private func packet(_ parts: [UInt8]...) -> [UInt8] { parts.flatMap { $0 } }

    func testHandshakeRawAndCopyRect() throws {
        let path = NSTemporaryDirectory() + "rfb-test-\(getpid()).sock"
        let listener = try UnixSocket.listen(at: path)
        defer { listener.close(); unlink(path) }
        let served = expectation(description: "served")

        Thread.detachNewThread { [self] in
            defer { served.fulfill() }
            guard let c = try? listener.accept() else { return XCTFail("no client") }
            do {
                try c.write("RFB 003.008\n")
                XCTAssertEqual(String(decoding: try c.read(12), as: UTF8.self), "RFB 003.008\n")
                try c.write([1, 1])                          // one type: None
                XCTAssertEqual(try c.read(1), [1])
                try c.write(u32(0))                          // security OK
                XCTAssertEqual(try c.read(1), [1])           // shared
                try c.write(packet(u16(3), u16(1), [UInt8](repeating: 0, count: 16), u32(0)))
                let setFormat = try c.read(20)
                XCTAssertEqual(setFormat[0], 0)
                XCTAssertEqual(setFormat[4], 32)             // bits per pixel
                XCTAssertEqual(Array(setFormat[14...16]), [16, 8, 0])
                let encodings = try c.read(4)
                _ = try c.read(Int(encodings[3]) * 4)
                XCTAssertEqual(try c.read(10)[0], 3)         // the first update request
                // Pixels 0 and 1 raw (red, green), then pixel 2 a copy of pixel 0
                try c.write(packet([0, 0], u16(2),
                                  u16(0), u16(0), u16(2), u16(1), u32(0),
                                  [0, 0, 255, 0, 0, 255, 0, 0],
                                  u16(2), u16(0), u16(1), u16(1), u32(1), u16(0), u16(0)))
                XCTAssertEqual(try c.read(10)[1], 1)         // the next, incremental
                c.close()
            } catch {
                XCTFail("server: \(error)")
            }
        }

        let client = try RFBClient(path: path)
        var frames = [CGImage]()
        client.onFrame = { frames.append($0) }
        try client.run()
        wait(for: [served], timeout: 5)

        XCTAssertEqual(frames.count, 1)
        let image = try XCTUnwrap(frames.first)
        XCTAssertEqual(image.width, 3)
        XCTAssertEqual(image.height, 1)
        let bytes = try XCTUnwrap(image.dataProvider?.data as Data?)
        XCTAssertEqual(Array(bytes), [0, 0, 255, 0, 0, 255, 0, 0, 0, 0, 255, 0])
    }
}
