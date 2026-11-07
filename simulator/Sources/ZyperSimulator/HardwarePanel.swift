// SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
import SimCore
import SwiftUI

/// The parts a player can't press: the battery, the cable, the headphones,
/// and what the device shows of them
struct HardwarePanel: View {
    @EnvironmentObject var sim: Simulator
    @State private var battery: Double = 100
    @State private var dragging = false

    var body: some View {
        Form {
            Section("Battery") {
                HStack {
                    Slider(value: $battery, in: 0...100, step: 1) { editing in
                        dragging = editing
                        if !editing { sim.setBattery(Int(battery)) }
                    }
                    Text("\(Int(battery))%").monospacedDigit().frame(width: 40, alignment: .trailing)
                }
                HStack {
                    ForEach([100, 50, 15, 5, 1], id: \.self) { pct in
                        SwiftUI.Button("\(pct)") { battery = Double(pct); sim.setBattery(pct) }
                            .controlSize(.small)
                    }
                }
                Toggle("Charging cable", isOn: Binding(get: { sim.state.cable }, set: { sim.setCable($0) }))
                Picker("Into", selection: Binding(get: { sim.state.kind }, set: { sim.setKind($0) })) {
                    Text("Wall charger").tag(1)
                    Text("Computer").tag(2)
                }
                .pickerStyle(.segmented)
                Text(chargeText).font(.caption).foregroundStyle(.secondary)
            }
            Section("Audio") {
                Toggle("Headphones", isOn: Binding(get: { sim.state.headphones },
                                                  set: { sim.setHeadphones($0) }))
            }
            Section("Shown by the device") {
                LabeledContent("Backlight", value: backlightText)
                LabeledContent("Side lights", value: lightsText)
                LabeledContent("Rumble", value: sim.rumble.isOn
                               ? "\(Int(sim.rumble.strength * 100))%" : "off")
            }
            Section("Keyboard") {
                Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 2) {
                    ForEach(keys, id: \.0) { key, button in
                        GridRow {
                            Text(key).monospaced()
                            Text(button).foregroundStyle(.secondary)
                        }
                    }
                }
                .font(.caption)
            }
            Section("Device") {
                HStack {
                    SwiftUI.Button(sim.power == .off ? "Turn On" : "Power Button") { sim.powerButton() }
                    SwiftUI.Button("Shut Down") { sim.shutdown() }.disabled(sim.power == .off)
                }
                SwiftUI.Button("Save Screenshot") { sim.saveScreenToDesktop() }.disabled(sim.power != .on)
                Text(sim.status).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
            }
        }
        .formStyle(.grouped)
        .frame(width: 260)
        .disabled(sim.power == .starting)
        .onChange(of: sim.state.battery) { _, new in if !dragging { battery = Double(new) } }
        .onAppear { battery = Double(sim.state.battery) }
    }

    private let keys = [
        ("arrows", "d-pad"), ("x z s a", "A B X Y"), ("q w e r", "L1 R1 L2 R2"),
        ("Return", "START"), ("right ⇧, ⌫", "SELECT"),
        ("Esc, m", "MENU"), ("= -", "volume"), ("p", "POWER"), ("h", "headphones"),
    ]

    private var chargeText: String {
        guard sim.state.cable else {
            return sim.power == .off ? "Off; the cable starts it, as a charger does" : "On the battery"
        }
        return sim.state.battery >= 100 ? "Full, on the cable" : "Charging"
    }

    private var backlightText: String {
        sim.state.backlightPower != 0 ? "off" : "\(sim.state.backlight) / 255"
    }

    private var lightsText: String {
        let names = ["red", "green", "2", "3", "4"]
        let on = (0..<5).filter { sim.state.led($0) }.map { names[$0] }
        return on.isEmpty ? "none" : on.joined(separator: " ")
    }
}
