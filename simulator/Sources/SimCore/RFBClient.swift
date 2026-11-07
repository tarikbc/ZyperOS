// SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
import CoreGraphics
import Darwin
import Foundation

/// The VM's display, from QEMU's VNC server on a Unix socket (-vnc unix:...):
/// RFB 3.8 with no authentication, raw pixels, CopyRect and DesktopSize,
/// which is all a local socket needs. Each finished update is handed over as
/// a CGImage, BGRX in memory.
public final class RFBClient {
    public private(set) var width = 0
    public private(set) var height = 0
    /// Called on the client's thread with every finished update.
    public var onFrame: ((CGImage) -> Void)?

    private let socket: UnixSocket
    private var pixels = [UInt8]()

    public init(path: String) throws {
        socket = try UnixSocket(connectingTo: path)
        // Room for a few whole frames, so QEMU's output buffer empties at once:
        // it sends nothing new while the last frame still sits there
        var size: Int32 = 8 << 20
        setsockopt(socket.fd, SOL_SOCKET, SO_RCVBUF, &size, socklen_t(MemoryLayout<Int32>.size))
    }

    public func close() { socket.close() }

    /// Runs the connection until it ends: returns normally when the VM goes
    /// (QEMU closes the socket), throws on anything else.
    public func run() throws {
        do {
            try handshake()
            try requestUpdate(incremental: false)
            while true { try message() }
        } catch SocketError.closed {
            return
        }
    }

    // ---- the handshake --------------------------------------------------

    private func handshake() throws {
        let version = try socket.read(12)
        guard String(decoding: version, as: UTF8.self).hasPrefix("RFB ") else {
            throw RFBError.protocolError("not a VNC server")
        }
        try socket.write("RFB 003.008\n")
        let count = Int(try socket.read(1)[0])
        if count == 0 { throw RFBError.protocolError(try reason()) }
        let types = try socket.read(count)
        guard types.contains(1) else { throw RFBError.protocolError("the server wants a password") }
        try socket.write([1])                                   // None
        if try u32() != 0 { throw RFBError.protocolError(try reason()) }
        try socket.write([1])                                   // ClientInit: shared
        width = Int(try u16())
        height = Int(try u16())
        _ = try socket.read(16)                                 // its pixel format
        _ = try socket.read(Int(try u32()))                     // its name
        pixels = [UInt8](repeating: 0, count: width * height * 4)

        // 32 bits, true colour, little endian, red at 16, green 8, blue 0:
        // B G R X in memory, a CGImage's 32-bit little-endian skip-first
        try socket.write([0, 0, 0, 0, 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0])
        let encodings: [Int32] = [1, 0, -223]                   // CopyRect, Raw, DesktopSize
        var message: [UInt8] = [2, 0] + be16(encodings.count)
        for e in encodings { message += be32(UInt32(bitPattern: e)) }
        try socket.write(message)
    }

    private func reason() throws -> String {
        String(decoding: try socket.read(Int(try u32())), as: UTF8.self)
    }

    private func requestUpdate(incremental: Bool) throws {
        try socket.write([3, incremental ? 1 : 0, 0, 0, 0, 0] + be16(width) + be16(height))
    }

    // ---- messages from the server -----------------------------------------

    private func message() throws {
        switch try socket.read(1)[0] {
        case 0:
            try update()
        case 1:                                                 // colour map: unused
            _ = try socket.read(3)
            let n = Int(try u16())
            _ = try socket.read(n * 6)
        case 2:                                                 // bell
            break
        case 3:                                                 // cut text
            _ = try socket.read(3)
            _ = try socket.read(Int(try u32()))
        case let type:
            throw RFBError.protocolError("unknown message \(type)")
        }
    }

    private func update() throws {
        _ = try socket.read(1)
        let rects = Int(try u16())
        // The next one asked for now, before this one's pixels are read, so
        // QEMU finds it waiting at its next look (it looks every 30 ms, and
        // waits 50 ms longer after each look that sends nothing)
        try requestUpdate(incremental: true)
        for _ in 0..<rects {
            let x = Int(try u16()), y = Int(try u16())
            let w = Int(try u16()), h = Int(try u16())
            let encoding = Int32(bitPattern: try u32())
            switch encoding {
            case 0:
                try raw(x: x, y: y, w: w, h: h)
            case 1:
                let sx = Int(try u16()), sy = Int(try u16())
                copy(fromX: sx, y: sy, toX: x, y: y, w: w, h: h)
            case -223:
                width = w
                height = h
                pixels = [UInt8](repeating: 0, count: w * h * 4)
            default:
                throw RFBError.protocolError("unasked-for encoding \(encoding)")
            }
        }
        if let image = makeImage() { onFrame?(image) }
    }

    private func raw(x: Int, y: Int, w: Int, h: Int) throws {
        guard x + w <= width, y + h <= height else {
            throw RFBError.protocolError("a rectangle off the screen")
        }
        let row = w * 4
        try pixels.withUnsafeMutableBytes { buf in
            if x == 0 && w == width {                           // whole rows: one read
                try socket.read(into: buf.baseAddress! + y * row, count: row * h)
                return
            }
            for line in 0..<h {
                try socket.read(into: buf.baseAddress! + ((y + line) * width + x) * 4, count: row)
            }
        }
    }

    private func copy(fromX sx: Int, y sy: Int, toX dx: Int, y dy: Int, w: Int, h: Int) {
        guard sx + w <= width, sy + h <= height, dx + w <= width, dy + h <= height else { return }
        let rows = Array(0..<h)
        pixels.withUnsafeMutableBytes { buf in
            // Bottom up when moving down, so a row isn't read after it is written
            for line in (dy > sy ? rows.reversed() : rows) {
                memmove(buf.baseAddress! + ((dy + line) * width + dx) * 4,
                        buf.baseAddress! + ((sy + line) * width + sx) * 4, w * 4)
            }
        }
    }

    private func makeImage() -> CGImage? {
        guard width > 0, height > 0,
              let provider = CGDataProvider(data: Data(pixels) as CFData) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGBitmapInfo.byteOrder32Little.rawValue |
                                                CGImageAlphaInfo.noneSkipFirst.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false,
                       intent: .defaultIntent)
    }

    // ---- numbers, big-endian ---------------------------------------------

    private func u16() throws -> UInt16 {
        let b = try socket.read(2)
        return UInt16(b[0]) << 8 | UInt16(b[1])
    }

    private func u32() throws -> UInt32 {
        let b = try socket.read(4)
        return UInt32(b[0]) << 24 | UInt32(b[1]) << 16 | UInt32(b[2]) << 8 | UInt32(b[3])
    }

    private func be16(_ v: Int) -> [UInt8] { [UInt8(v >> 8 & 0xFF), UInt8(v & 0xFF)] }

    private func be32(_ v: UInt32) -> [UInt8] {
        [UInt8(v >> 24 & 0xFF), UInt8(v >> 16 & 0xFF), UInt8(v >> 8 & 0xFF), UInt8(v & 0xFF)]
    }
}

public enum RFBError: Error, CustomStringConvertible {
    case protocolError(String)

    public var description: String {
        switch self { case let .protocolError(text): return "VNC: \(text)" }
    }
}
