// SPDX-License-Identifier: GPL-3.0-only
import Foundation

/// px2sim's line protocol, on the socket QEMU keeps for the VM's
/// virtio-serial port (tools/qemu-run.sh: sim.sock). The window sends
/// commands ("press a", "battery 15", "cable 0") and hears every change of
/// state and of the motor. QEMU lets one client on at a time.
public final class SimLink {
    /// Called on the link's thread with each line from the VM
    public var onLine: ((String) -> Void)?

    private let socket: UnixSocket

    public init(path: String) throws {
        socket = try UnixSocket(connectingTo: path)
    }

    public func send(_ command: String) {
        try? socket.write(command.hasSuffix("\n") ? command : command + "\n")
    }

    public func close() { socket.close() }

    /// Says hello (px2sim answers with the state) and reads lines until the
    /// socket closes
    public func run() {
        send("hello")
        var pending = [UInt8]()
        while let bytes = try? socket.readSome(), !bytes.isEmpty {
            pending += bytes
            while let end = pending.firstIndex(of: 10) {
                let line = String(decoding: pending[..<end], as: UTF8.self)
                pending.removeSubrange(...end)
                if !line.isEmpty { onLine?(line) }
            }
        }
    }
}
