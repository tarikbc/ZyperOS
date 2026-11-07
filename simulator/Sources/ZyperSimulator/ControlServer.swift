// SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
import AppKit
import SimCore
import SwiftUI

/// The window driven from a shell (simulator/simctl): a Unix socket in the
/// VM's folder, app.sock, one command a line (ControlCommand) and one JSON
/// line back. For checking the simulator, and ZyperOS in it, with no clicks.
final class ControlServer {
    private let path: String
    private weak var sim: Simulator?
    private var listener: UnixSocket?

    init(path: String, sim: Simulator) {
        self.path = path
        self.sim = sim
    }

    func start() {
        do {
            try FileManager.default.createDirectory(atPath: (path as NSString).deletingLastPathComponent,
                                                    withIntermediateDirectories: true)
            listener = try UnixSocket.listen(at: path)
        } catch {
            NSLog("ZyperOS Simulator: no control socket: \(error)")
            return
        }
        Thread.detachNewThread { [weak self] in
            while let self, let listener = self.listener, let client = try? listener.accept() {
                Thread.detachNewThread { self.serve(client) }
            }
        }
    }

    func stop() {
        listener?.close()
        unlink(path)
    }

    private func serve(_ client: UnixSocket) {
        var pending = [UInt8]()
        while let bytes = try? client.readSome(), !bytes.isEmpty {
            pending += bytes
            while let end = pending.firstIndex(of: 10) {
                let line = String(decoding: pending[..<end], as: UTF8.self)
                pending.removeSubrange(...end)
                try? client.write(answer(line) + "\n")
            }
        }
        client.close()
    }

    private func answer(_ line: String) -> String {
        guard let command = ControlCommand(line: line) else {
            return json(["ok": false, "error": "usage: \(ControlCommand.usage)"])
        }
        if case let .waitFrames(n) = command { return waitFrames(n) }
        if case let .tap(buttons, ms) = command {
            onMain { $0.tap(buttons, milliseconds: ms) }
            Thread.sleep(forTimeInterval: Double(ms) / 1000 + 0.05)
            return json(["ok": true])
        }
        return DispatchQueue.main.sync { MainActor.assumeIsolated { run(command) } }
    }

    @MainActor
    private func run(_ command: ControlCommand) -> String {
        guard let sim else { return json(["ok": false, "error": "no simulator"]) }
        switch command {
        case .state:
            let s = sim.state
            return json(["ok": true, "name": sim.name, "power": sim.power.rawValue,
                         "ssh_port": sim.sshPort, "frames": sim.frameCount,
                         "screen": [sim.frame?.width ?? 0, sim.frame?.height ?? 0],
                         "leds": s.leds, "backlight": s.backlight, "bl_power": s.backlightPower,
                         "battery": s.battery, "cable": s.cable, "kind": s.kind == 2 ? "computer" : "wall",
                         "headphones": s.headphones,
                         "rumble": [sim.rumble.strong, sim.rumble.weak],
                         "held": sim.held.map(\.rawValue).sorted()])
        case let .press(buttons): buttons.forEach(sim.press)
        case let .release(buttons): buttons.forEach(sim.release)
        case let .battery(pct): sim.setBattery(pct)
        case let .cable(on): sim.setCable(on)
        case let .kind(k): sim.setKind(k)
        case let .headphones(on): sim.setHeadphones(on)
        case .power: sim.powerButton()
        case .shutdown: sim.shutdown()
        case let .screenshot(path):
            guard sim.saveScreen(to: path) else { return json(["ok": false, "error": "no frame yet"]) }
            return json(["ok": true, "path": path])
        case let .windowShot(path):
            let renderer = ImageRenderer(content: DeviceView(still: sim.frame).environmentObject(sim))
            renderer.scale = 2
            guard let image = renderer.cgImage, Simulator.writePNG(image, to: path) else {
                return json(["ok": false, "error": "could not draw the window"])
            }
            return json(["ok": true, "path": path])
        case .quit:
            DispatchQueue.main.async { NSApp.terminate(nil) }
        case .tap, .waitFrames:
            break
        }
        return json(["ok": true])
    }

    private func waitFrames(_ n: Int) -> String {
        let start = DispatchQueue.main.sync { MainActor.assumeIsolated { sim?.frameCount ?? 0 } }
        let deadline = Date().addingTimeInterval(10)
        while Date() < deadline {
            let now = DispatchQueue.main.sync { MainActor.assumeIsolated { sim?.frameCount ?? 0 } }
            if now - start >= n { return json(["ok": true, "frames": now - start]) }
            Thread.sleep(forTimeInterval: 0.02)
        }
        return json(["ok": false, "error": "fewer than \(n) frames in 10 s"])
    }

    private func onMain(_ body: @escaping @MainActor (Simulator) -> Void) {
        DispatchQueue.main.async { MainActor.assumeIsolated { if let sim = self.sim { body(sim) } } }
    }

    private func json(_ object: [String: Any]) -> String {
        guard let data = try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys]) else {
            return "{\"ok\":false}"
        }
        return String(decoding: data, as: UTF8.self)
    }
}
