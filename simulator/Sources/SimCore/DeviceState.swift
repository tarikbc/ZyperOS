// SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
import Foundation

/// The Pixel 2's parts as px2sim reports them (plastron's package/px2sim):
/// "state leds=3 backlight=128 bl_power=0 battery=80 cable=1 headphones=0".
public struct DeviceState: Equatable, Codable {
    /// The side lights, bit 0 battery0 (the bottom light's red) to bit 4
    /// battery4 (the top green)
    public var leds = 0
    /// 0-255, and bl_power: 0 on, 4 off (Muse's pocket lock)
    public var backlight = 128
    public var backlightPower = 0
    public var battery = 100
    public var cable = true
    /// What the cable is in: 1 a wall charger (the USB PHY's DCP), 2 a
    /// computer (SDP), which ZyperOS's charging screen names
    public var kind = 1
    public var headphones = false

    public init() {}

    /// The screen's light, 0 (dark) to 1
    public var screenLight: Double {
        backlightPower != 0 ? 0 : Double(max(0, min(255, backlight))) / 255
    }

    public func led(_ index: Int) -> Bool { leds & (1 << index) != 0 }

    /// A state line's fields over this state; false when it isn't one
    public mutating func apply(line: String) -> Bool {
        let words = line.split(separator: " ")
        guard words.first == "state" else { return false }
        for word in words.dropFirst() {
            let pair = word.split(separator: "=", maxSplits: 1)
            guard pair.count == 2, let value = Int(pair[1]) else { continue }
            switch pair[0] {
            case "leds": leds = value
            case "backlight": backlight = value
            case "bl_power": backlightPower = value
            case "battery": battery = value
            case "cable": cable = value != 0
            case "kind": kind = value == 2 ? 2 : 1
            case "headphones": headphones = value != 0
            default: break
            }
        }
        return true
    }
}

/// The motor, from a "rumble STRONG WEAK MS" line: 0-65535 each, MS 0 for
/// until told to stop
public struct Rumble: Equatable {
    public var strong = 0
    public var weak = 0
    public var milliseconds = 0

    public init(strong: Int = 0, weak: Int = 0, milliseconds: Int = 0) {
        self.strong = strong
        self.weak = weak
        self.milliseconds = milliseconds
    }

    public var isOn: Bool { strong > 0 || weak > 0 }
    /// How hard, 0 to 1
    public var strength: Double { Double(max(strong, weak)) / 65535 }

    public init?(line: String) {
        let words = line.split(separator: " ")
        guard words.count == 4, words[0] == "rumble",
              let s = Int(words[1]), let w = Int(words[2]), let ms = Int(words[3]) else { return nil }
        self.init(strong: s, weak: w, milliseconds: ms)
    }
}

/// The Pixel 2's buttons, by px2-input's and px2sim's names
public enum PadButton: String, CaseIterable, Codable {
    case up, down, left, right, a, b, x, y, l1, r1, l2, r2, select, start, menu
    case volup, voldown, power
}
