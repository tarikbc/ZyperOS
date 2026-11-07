// SPDX-License-Identifier: GPL-3.0-only
import Darwin
import Foundation

public enum SocketError: Error, CustomStringConvertible {
    case system(String, Int32)
    case closed

    public var description: String {
        switch self {
        case let .system(what, err): return "\(what): \(String(cString: strerror(err)))"
        case .closed: return "the other end closed the connection"
        }
    }
}

/// A Unix stream socket, blocking: QEMU's VNC and px2sim sockets, and the
/// app's own control socket.
public final class UnixSocket {
    public let fd: Int32
    private var closed = false
    private let lock = NSLock()

    public init(fd: Int32) {
        self.fd = fd
        var one: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
    }

    /// Connects to the socket at path.
    public convenience init(connectingTo path: String) throws {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw SocketError.system("socket", errno) }
        var addr = try UnixSocket.address(path)
        let result = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        if result != 0 {
            let err = errno
            Darwin.close(fd)
            throw SocketError.system("connect \(path)", err)
        }
        self.init(fd: fd)
    }

    /// A listening socket at path, readable and writable by this user only.
    public static func listen(at path: String) throws -> UnixSocket {
        unlink(path)
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw SocketError.system("socket", errno) }
        var addr = try address(path)
        let old = umask(0o077)
        let bound = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        umask(old)
        guard bound == 0, Darwin.listen(fd, 8) == 0 else {
            let err = errno
            Darwin.close(fd)
            throw SocketError.system("listen \(path)", err)
        }
        return UnixSocket(fd: fd)
    }

    static func address(_ path: String) throws -> sockaddr_un {
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(path.utf8)
        guard bytes.count < MemoryLayout.size(ofValue: addr.sun_path) else {
            throw SocketError.system("socket path \(path)", ENAMETOOLONG)
        }
        withUnsafeMutableBytes(of: &addr.sun_path) { buf in
            buf.copyBytes(from: bytes)
            buf[bytes.count] = 0
        }
        return addr
    }

    public func accept() throws -> UnixSocket {
        let client = Darwin.accept(fd, nil, nil)
        guard client >= 0 else { throw SocketError.system("accept", errno) }
        return UnixSocket(fd: client)
    }

    /// Exactly count bytes into buffer, or throws.
    public func read(into buffer: UnsafeMutableRawPointer, count: Int) throws {
        var done = 0
        while done < count {
            let n = recv(fd, buffer + done, count - done, 0)
            if n == 0 { throw SocketError.closed }
            if n < 0 {
                if errno == EINTR { continue }
                throw SocketError.system("recv", errno)
            }
            done += n
        }
    }

    public func read(_ count: Int) throws -> [UInt8] {
        var bytes = [UInt8](repeating: 0, count: count)
        if count > 0 {
            try bytes.withUnsafeMutableBytes { try read(into: $0.baseAddress!, count: count) }
        }
        return bytes
    }

    /// Whatever is there, up to max bytes; empty at the end.
    public func readSome(max: Int = 4096) throws -> [UInt8] {
        var bytes = [UInt8](repeating: 0, count: max)
        while true {
            let n = bytes.withUnsafeMutableBytes { recv(fd, $0.baseAddress!, max, 0) }
            if n >= 0 { return Array(bytes[0..<n]) }
            if errno != EINTR { throw SocketError.system("recv", errno) }
        }
    }

    public func write(_ bytes: [UInt8]) throws {
        lock.lock()
        defer { lock.unlock() }
        var done = 0
        while done < bytes.count {
            let n = bytes.withUnsafeBytes { send(fd, $0.baseAddress! + done, bytes.count - done, 0) }
            if n < 0 {
                if errno == EINTR { continue }
                throw SocketError.system("send", errno)
            }
            done += n
        }
    }

    public func write(_ text: String) throws { try write(Array(text.utf8)) }

    /// Ends the connection; a read blocked on it returns.
    public func close() {
        lock.lock()
        defer { lock.unlock() }
        guard !closed else { return }
        closed = true
        shutdown(fd, SHUT_RDWR)
        Darwin.close(fd)
    }

    deinit { close() }
}
