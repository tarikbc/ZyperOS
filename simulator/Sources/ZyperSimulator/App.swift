// SPDX-License-Identifier: GPL-3.0-only
import AppKit
import SimCore
import SwiftUI

/// ZyperOS Simulator: a GKD Pixel 2 on the Mac's screen, around ZyperOS
/// running in QEMU, as Apple's Simulator does an iPhone. docs/simulator.md.
///
///   ZyperSimulator [--name NAME] [--qemu-run PATH] [--image DIR]
///
/// NAME is the VM's (tools/qemu-run.sh -n), "simulator" unless told; a VM by
/// that name already running is shown as it is, otherwise one is started.
@main
struct ZyperSimulatorApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var sim = AppDelegate.makeSimulator()

    var body: some Scene {
        Window("ZyperOS Simulator", id: "device") {
            ContentView()
                .environmentObject(sim)
                .onAppear { delegate.attach(sim) }
        }
        .windowResizability(.contentSize)
        .commands { SimulatorCommands(sim: sim) }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var sim: Simulator?
    private var control: ControlServer?
    private var keys: Any?

    @MainActor
    static func makeSimulator() -> Simulator {
        let args = CommandLine.arguments
        func value(_ flag: String) -> String? {
            guard let i = args.firstIndex(of: flag), i + 1 < args.count else { return nil }
            return args[i + 1]
        }
        // The repository the app was built from (scripts/build-app.sh writes
        // it into Info.plist), for its tools/qemu-run.sh
        let repo = Bundle.main.object(forInfoDictionaryKey: "ZyperOSRepo") as? String
        let qemuRun = value("--qemu-run") ?? repo.map { $0 + "/tools/qemu-run.sh" } ?? "tools/qemu-run.sh"
        return Simulator(name: value("--name") ?? "simulator", qemuRun: qemuRun,
                         image: value("--image") ?? ProcessInfo.processInfo.environment["ZYPEROS_QEMU_IMAGE"])
    }

    @MainActor
    func attach(_ sim: Simulator) {
        guard self.sim == nil else { return }
        self.sim = sim
        control = ControlServer(path: sim.vmDir + "/app.sock", sim: sim)
        control?.start()
        keys = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp, .flagsChanged]) { event in
            Keyboard.handle(event, sim: sim) ? nil : event
        }
        sim.boot()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ app: NSApplication) -> Bool { true }

    func applicationWillTerminate(_ notification: Notification) {
        control?.stop()
        MainActor.assumeIsolated { sim?.quitting() }
    }
}

struct ContentView: View {
    @EnvironmentObject var sim: Simulator

    var body: some View {
        HStack(spacing: 0) {
            DeviceView()
                .padding(12)
            if sim.showPanel {
                Divider()
                HardwarePanel()
            }
        }
        .fixedSize()
        .background(.windowBackground)
    }
}

/// The keyboard as the Pixel 2's buttons, the same keys px2sim takes from
/// QEMU's own window: arrows, x z s a (A B X Y), q w e r (L1 R1 L2 R2),
/// Return (START), Right Shift or Delete (SELECT), Esc or m (MENU), = and -
/// (volume), p (power), h (headphones). Keys with Command go to the menus.
enum Keyboard {
    private static let map: [UInt16: PadButton] = [
        126: .up, 125: .down, 123: .left, 124: .right,
        7: .a, 6: .b, 1: .x, 0: .y,
        12: .l1, 13: .r1, 14: .l2, 15: .r2,
        36: .start, 76: .start, 51: .select, 53: .menu, 46: .menu,
        24: .volup, 27: .voldown, 35: .power,
    ]
    private static let rightShift: UInt16 = 60, h: UInt16 = 4

    @MainActor
    static func handle(_ event: NSEvent, sim: Simulator) -> Bool {
        if event.modifierFlags.contains(.command) { return false }
        // Typing in the panel's fields stays typing
        if NSApp.keyWindow?.firstResponder is NSText { return false }
        switch event.type {
        case .flagsChanged where event.keyCode == rightShift:
            if event.modifierFlags.contains(.shift) { sim.press(.select) } else { sim.release(.select) }
            return true
        case .keyDown where event.keyCode == h:
            if !event.isARepeat { sim.setHeadphones(!sim.state.headphones) }
            return true
        case .keyDown, .keyUp:
            guard let button = map[event.keyCode] else { return false }
            if event.type == .keyUp {
                sim.release(button)
            } else if !event.isARepeat {
                if button == .power && sim.power == .off { sim.start() } else { sim.press(button) }
            }
            return true
        default:
            return false
        }
    }
}

struct SimulatorCommands: Commands {
    @ObservedObject var sim: Simulator

    var body: some Commands {
        CommandGroup(replacing: .newItem) {}
        CommandMenu("Device") {
            SwiftUI.Button(sim.power == .off ? "Turn On" : "Power Button") { sim.powerButton() }
                .keyboardShortcut("p", modifiers: [.command, .shift])
            SwiftUI.Button("Shut Down") { sim.shutdown() }
                .disabled(sim.power == .off)
            Divider()
            Toggle("Charging Cable", isOn: Binding(get: { sim.state.cable }, set: { sim.setCable($0) }))
                .keyboardShortcut("u", modifiers: [.command, .shift])
            Picker("Cable Into", selection: Binding(get: { sim.state.kind }, set: { sim.setKind($0) })) {
                Text("Wall Charger").tag(1)
                Text("Computer").tag(2)
            }
            Menu("Battery") {
                ForEach([100, 80, 50, 20, 15, 10, 5, 1], id: \.self) { pct in
                    SwiftUI.Button("\(pct)%") { sim.setBattery(pct) }
                }
            }
            Toggle("Headphones", isOn: Binding(get: { sim.state.headphones }, set: { sim.setHeadphones($0) }))
                .keyboardShortcut("h", modifiers: [.command, .shift])
            Divider()
            SwiftUI.Button("Volume Up") { sim.tap([.volup], milliseconds: 80) }
                .keyboardShortcut(.upArrow, modifiers: [.command])
            SwiftUI.Button("Volume Down") { sim.tap([.voldown], milliseconds: 80) }
                .keyboardShortcut(.downArrow, modifiers: [.command])
            Divider()
            SwiftUI.Button("Save Screenshot") { sim.saveScreenToDesktop() }
                .keyboardShortcut("s", modifiers: [.command])
            SwiftUI.Button("Copy Screen") { sim.copyScreen() }
                .keyboardShortcut("c", modifiers: [.command, .control])
        }
        CommandGroup(after: .toolbar) {
            Toggle("Hardware Panel", isOn: $sim.showPanel)
                .keyboardShortcut("i", modifiers: [.command])
            Divider()
            ForEach([(1.0, "1"), (1.25, "2"), (1.5, "3"), (2.0, "4")], id: \.0) { zoom, key in
                SwiftUI.Button("Zoom \(Int(zoom * 100))%") { sim.zoom = zoom }
                    .keyboardShortcut(KeyEquivalent(Character(key)), modifiers: [.command])
            }
        }
    }
}
