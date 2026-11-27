// SPDX-License-Identifier: GPL-3.0-only
import Foundation

/// A handheld as the Simulator draws it: its front straight on, with what
/// stands out of its edges round it (the shoulders along the top, the side
/// keys out of the sides), in millimetres from the body's middle, up
/// positive. The numbers are the device's own: `make handhelds` writes them
/// into Handhelds.swift from src/device/<id>_model.c (td_model.h's
/// td_flat), the same data desk mode draws from (src/device/deskface.c). A
/// new handheld is its model file's; nothing here changes for it.
public struct Handheld: Equatable, Identifiable {
    public struct Size: Equatable { public let w, h: Double }
    /// A box: its middle and its size
    public struct Rect: Equatable { public let x, y, w, h: Double }
    /// Room round the body for what stands out of it
    public struct Margins: Equatable { public let x, top, bottom: Double }

    /// How a key is drawn: round or a pill on the front, the d-pad, a key
    /// out of a side edge, a shoulder above the top edge
    public enum Kind: String { case round, pill, dpad, side, shoulder }
    /// Where a key's keyboard key shows beside it
    public enum Hint: String { case below, right, left, above, on }

    public struct Key: Equatable {
        /// What it presses; nil for the d-pad, whose four ways are its own
        public let button: PadButton?
        public let kind: Kind
        public let x, y, w, h: Double
        /// Printed on its cap ("" none); "■" and "▶" are marks
        public let label: String
        /// Printed on the body under it
        public let under: String?
        public let hint: Hint
        /// Its keyboard key nudged across, points: two keys side by side
        public let hintDX: Double
    }

    /// A word on the body, along an edge (turned) or flat
    public struct Label: Equatable { public let text: String; public let x, y: Double; public let turned: Bool }

    /// A side light: its middle down the edge, and the lights (px2sim's
    /// battery<N>) that light it green and red; both lit, amber
    public struct Light: Equatable { public let y: Double; public let green, red: Int? }

    /// 0xRRGGBB: the body lit from above to below, the well, a key's cap up
    /// and pressed, what is printed, the glass, a light off
    /// A stick, no key of the launcher's: its middle, the radii of the
    /// well it stands in and of its cap
    public struct Stick: Equatable { public let x, y, well, cap: Double }

    public struct Colors: Equatable {
        public let bodyHi, body, bodyLo, recess, cap, capDown, marking, glass, lightOff: UInt32
    }

    /// The profile's id: "pixel2", "brick", --device
    public let id: String
    public let name: String
    /// The screen's picture, pixels, as ZyperOS draws it (turned upright)
    public let panel: Size
    public let body: Size
    public let corner: Double
    public let margin: Margins
    public let glass: Rect
    public let glassRadius: Double
    /// The lit picture
    public let screen: Rect
    /// Printed under the picture
    public let word: String?
    public let wordY: Double
    /// SELECT and START's well
    public let recess: Rect?
    public let recessRadius: Double
    /// Half the width of one of the d-pad's arms
    public let dpadArm: Double
    public let lightX: Double
    public let light: Size
    public let lights: [Light]
    public let colors: Colors
    /// The sticks, drawn and never pressed (the R36S's)
    public let sticks: [Stick]
    public let keys: [Key]
    public let labels: [Label]

    /// The whole drawing's size, the margins round the body included
    public var width: Double { body.w + 2 * margin.x }
    public var height: Double { body.h + margin.top + margin.bottom }

    public var dpad: Key? { keys.first { $0.kind == .dpad } }

    /// The key that presses `button`: the d-pad for its four ways
    public func key(for button: PadButton) -> Key? {
        if [.up, .down, .left, .right].contains(button) { return dpad }
        return keys.first { $0.button == button }
    }

    /// Whether this handheld has `button` at all
    public func has(_ button: PadButton) -> Bool { key(for: button) != nil }

    public static func named(_ id: String) -> Handheld? { all.first { $0.id == id } }

    /// The handheld the VM is: the GKD Pixel 2 until QEMU stands in for
    /// another (docs/simulator.md, "Another handheld")
    public static var standard: Handheld { named("pixel2") ?? all[0] }
}
