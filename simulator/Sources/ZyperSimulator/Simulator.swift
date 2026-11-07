// SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
import AppKit
import CoreGraphics
import ImageIO
import SimCore
import UniformTypeIdentifiers

/// One simulated Pixel 2: the QEMU VM behind it (tools/qemu-run.sh, by name),
/// its screen over VNC, and its parts over px2sim's link.
@MainActor
final class Simulator: ObservableObject {
    enum Power: String { case off, starting, on }

    @Published private(set) var power: Power = .off
    @Published private(set) var state = DeviceState()
    @Published private(set) var rumble = Rumble()
    @Published private(set) var held: Set<PadButton> = []
    @Published private(set) var status = ""
    @Published var showPanel = true
    @Published var zoom: Double = 0.75

    let name: String
    let qemuRun: String
    let image: String?
    let vmDir: String

    /// The newest frame, and the screen view's way of hearing of each one
    private(set) var frame: CGImage?
    private(set) var frameCount = 0
    var frameSink: ((CGImage) -> Void)?

    private var rfb: RFBClient?
    private var link: SimLink?
    /// What the window last set, given to the VM again when it comes back:
    /// a battery keeps its charge, a cable stays in, across a power cycle
    private var wantBattery: Int?
    private var wantCable: Bool?
    private var wantKind: Int?
    private var wantHeadphones: Bool?
    private var startedHere = false
    private var attaching = false
    private var rumbleOff: DispatchWorkItem?

    init(name: String, qemuRun: String, image: String?) {
        self.name = name
        self.qemuRun = qemuRun
        self.image = image
        let home = ProcessInfo.processInfo.environment["ZYPEROS_QEMU_HOME"] ??
            (NSHomeDirectory() + "/Library/Caches/zyperos-qemu")
        vmDir = home + "/" + name
    }

    nonisolated var vmRunning: Bool {
        guard let text = try? String(contentsOfFile: vmDir + "/qemu.pid", encoding: .utf8),
              let pid = Int32(text.trimmingCharacters(in: .whitespacesAndNewlines)) else { return false }
        return kill(pid, 0) == 0
    }

    nonisolated var sshPort: String {
        (try? String(contentsOfFile: vmDir + "/ssh.port", encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? "?"
    }

    // ---- the VM ---------------------------------------------------------

    /// At launch: the VM if it runs already, else a new start
    func boot() {
        if vmRunning { attach() } else { start() }
    }

    /// With `charger`, as a charger plugged into the Pixel 2 while it is off
    /// starts it: the RK817's plug-in reason, which U-Boot turns into
    /// zycharge=1 and ZyperOS into its charging screen (qemu-run.sh start
    /// --charger, DESIGN.md 11.25)
    func start(charger: Bool = false) {
        guard power == .off else { return }
        power = .starting
        status = charger ? "\(name): the charger starts it…" : "Starting \(name)…"
        startedHere = true
        var args = [qemuRun, "start", "-n", name, "--sound"]
        if charger { args.append("--charger") }
        if let image { args += ["--image", image] }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = args
        let errors = Pipe()
        process.standardError = errors
        process.standardOutput = errors
        process.terminationHandler = { [weak self] p in
            let text = String(decoding: errors.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
            Task { @MainActor in
                guard let self else { return }
                if p.terminationStatus != 0 {
                    self.status = text.split(separator: "\n").last.map(String.init) ?? "qemu-run.sh failed"
                    if !self.vmRunning { self.power = .off }
                } else if self.power == .on {
                    self.status = "\(self.name): SSH on 127.0.0.1:\(self.sshPort)"
                }
            }
        }
        do {
            try process.run()
        } catch {
            power = .off
            status = "Can't run \(qemuRun): \(error)"
            return
        }
        attach()
    }

    /// A clean power-off, as ZyperOS's own Power Off does
    func shutdown() {
        guard power != .off else { return }
        status = "Shutting down…"
        DispatchQueue.global().async { [qemuRun, name] in
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/bin/sh")
            p.arguments = [qemuRun, "stop", "-n", name]
            try? p.run()
            p.waitUntilExit()
        }
    }

    /// When the app quits: a VM this window started goes with it
    func quitting() {
        guard startedHere, vmRunning else { return }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/bin/sh")
        p.arguments = [qemuRun, "stop", "-n", name]
        try? p.run()
        p.waitUntilExit()
    }

    /// The screen and the link, as soon as QEMU's sockets answer
    private func attach() {
        guard !attaching else { return }
        attaching = true
        if power == .off { power = .starting }
        let vnc = vmDir + "/vnc.sock", sim = vmDir + "/sim.sock"
        let started = Date()
        DispatchQueue.global().async { [weak self] in
            var client: RFBClient?
            // Two minutes for QEMU to open its socket; fifteen seconds if
            // it isn't even running by then (qemu-run.sh refused to start)
            while Date().timeIntervalSince(started) < 120 {
                client = try? RFBClient(path: vnc)
                if client != nil { break }
                if !(self?.vmRunning ?? false), Date().timeIntervalSince(started) > 15 { break }
                Thread.sleep(forTimeInterval: 0.2)
            }
            DispatchQueue.main.async { self?.attaching = false }
            guard let client else {
                DispatchQueue.main.async { self?.lost() }
                return
            }
            client.onFrame = { image in
                DispatchQueue.main.async { self?.show(image) }
            }
            DispatchQueue.main.async {
                guard let self else { return }
                self.rfb = client
                self.power = .on
                self.status = "\(self.name): SSH on 127.0.0.1:\(self.sshPort)"
                self.connectLink(sim)
            }
            do { try client.run() } catch {
                DispatchQueue.main.async { self?.status = "\(error)" }
            }
            DispatchQueue.main.async { self?.lost() }
        }
    }

    private func connectLink(_ path: String) {
        DispatchQueue.global().async { [weak self] in
            for _ in 0..<50 {
                if let link = try? SimLink(path: path) {
                    link.onLine = { line in DispatchQueue.main.async { self?.heard(line) } }
                    DispatchQueue.main.async {
                        guard let self else { return }
                        self.link = link
                        if let b = self.wantBattery { link.send("battery \(b)") }
                        if let c = self.wantCable { link.send("cable \(c ? 1 : 0)") }
                        if let k = self.wantKind { link.send("kind \(k == 2 ? "computer" : "wall")") }
                        if let h = self.wantHeadphones { link.send("headphones \(h ? 1 : 0)") }
                    }
                    link.run()
                    DispatchQueue.main.async { if self?.link === link { self?.link = nil } }
                    return
                }
                Thread.sleep(forTimeInterval: 0.2)
            }
        }
    }

    /// The screen's connection ended: the VM has gone, or will come back
    private func lost() {
        rfb = nil
        link?.close()
        link = nil
        held.removeAll()
        rumble = Rumble()
        if vmRunning {
            attach()
        } else {
            power = .off
            frame = nil
            // Nothing lit with the system off: gauge went with it
            state.leds = 0
            status = "\(name) is off. Press POWER to turn it on."
        }
    }

    private func show(_ image: CGImage) {
        frame = image
        frameCount += 1
        frameSink?(image)
    }

    private func heard(_ line: String) {
        var next = state
        if next.apply(line: line) {
            if next != state { state = next }
        } else if let r = Rumble(line: line) {
            rumble = r
            rumbleOff?.cancel()
            if r.isOn, r.milliseconds > 0 {
                let off = DispatchWorkItem { [weak self] in self?.rumble = Rumble() }
                rumbleOff = off
                DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(r.milliseconds), execute: off)
            }
        } else if line.hasPrefix("error") {
            status = "px2sim: \(line)"
        }
    }

    // ---- the buttons and the parts ----------------------------------------

    func press(_ button: PadButton) {
        if button == .power && power == .off {
            start()
            return
        }
        guard !held.contains(button) else { return }
        held.insert(button)
        link?.send("press \(button.rawValue)")
    }

    func release(_ button: PadButton) {
        guard held.contains(button) else { return }
        held.remove(button)
        link?.send("release \(button.rawValue)")
    }

    func tap(_ buttons: [PadButton], milliseconds: Int) {
        buttons.forEach(press)
        DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(milliseconds)) { [weak self] in
            buttons.forEach { self?.release($0) }
        }
    }

    func setBattery(_ percent: Int) {
        wantBattery = max(0, min(100, percent))
        link?.send("battery \(wantBattery!)")
    }

    /// The cable. Plugged into a handheld that is off, it starts it, as the
    /// Pixel 2's RK817 does with a charger (measured 2026-11-06): ZyperOS's
    /// charging screen, not the home
    func setCable(_ on: Bool) {
        let was = state.cable
        wantCable = on
        if power == .off {
            state.cable = on
            if on && !was { start(charger: true) }
            return
        }
        link?.send("cable \(on ? 1 : 0)")
    }

    /// What the cable is in: 1 a wall charger, 2 a computer's port
    func setKind(_ kind: Int) {
        let k = kind == 2 ? 2 : 1
        wantKind = k
        state.kind = k
        link?.send("kind \(k == 2 ? "computer" : "wall")")
    }

    func setHeadphones(_ on: Bool) {
        wantHeadphones = on
        link?.send("headphones \(on ? 1 : 0)")
    }

    /// The power button, held a moment as a finger would
    func powerButton() {
        if power == .off { start() } else { tap([.power], milliseconds: 150) }
    }

    // ---- pictures ---------------------------------------------------------

    func saveScreen(to path: String) -> Bool {
        guard let frame else { return false }
        return Simulator.writePNG(frame, to: path)
    }

    /// The screen to the Desktop, as the iPhone simulator does
    func saveScreenToDesktop() {
        let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: ".")
        let path = NSHomeDirectory() + "/Desktop/ZyperOS \(stamp).png"
        status = saveScreen(to: path) ? "Saved \(path)" : "No screen to save"
    }

    func copyScreen() {
        guard let frame else { return }
        let image = NSImage(cgImage: frame, size: NSSize(width: frame.width, height: frame.height))
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects([image])
    }

    static func writePNG(_ image: CGImage, to path: String) -> Bool {
        let url = URL(fileURLWithPath: path)
        guard let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)
        else { return false }
        CGImageDestinationAddImage(dest, image, nil)
        return CGImageDestinationFinalize(dest)
    }
}
