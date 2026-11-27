// SPDX-License-Identifier: GPL-3.0-only
import AppKit
import SimCore
import SwiftUI

/// Where things are on a handheld's face, in millimetres from the middle of
/// its body, up positive, as its device files have them (Handhelds.swift,
/// written by `make handhelds` from src/device/<id>_model.c). Desk mode draws
/// the same handheld from the same numbers (src/device/deskface.c).
struct Geometry {
    let h: Handheld
    /// Points a millimetre: zoom 1 draws the screen's pixels one to one on a
    /// Retina display (the Pixel 2's 640 in 320 points)
    let s: CGFloat

    init(_ h: Handheld, zoom: Double) {
        self.h = h
        s = CGFloat(zoom * h.panel.w / 2 / h.screen.w)
    }

    var width: CGFloat { CGFloat(h.width) * s }
    var height: CGFloat { CGFloat(h.height) * s }
    func x(_ mm: Double) -> CGFloat { CGFloat(mm + h.width / 2) * s }
    func y(_ mm: Double) -> CGFloat { CGFloat(h.margin.top + h.body.h / 2 - mm) * s }
    func point(_ x: Double, _ y: Double) -> CGPoint { CGPoint(x: self.x(x), y: self.y(y)) }
    func size(_ w: Double, _ h: Double) -> CGSize { CGSize(width: CGFloat(w) * s, height: CGFloat(h) * s) }
    func len(_ mm: Double) -> CGFloat { CGFloat(mm) * s }
}

extension Color {
    init(hex: UInt32, opacity: Double = 1) {
        self.init(red: Double(hex >> 16 & 0xFF) / 255, green: Double(hex >> 8 & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255, opacity: opacity)
    }

    /// `hex` lighter (d above 0) or darker, each channel by d
    init(hex: UInt32, shade d: Int) {
        func ch(_ v: UInt32) -> UInt32 { UInt32(max(0, min(255, Int(v & 0xFF) + d))) }
        self.init(hex: ch(hex >> 16) << 16 | ch(hex >> 8) << 8 | ch(hex))
    }
}

/// A handheld's colours, as its device file has them, and the shades the
/// drawing lights them with
struct Palette {
    let bodyHi, body, bodyLo, recessTop, recess, wellTop, wellBottom: Color
    let capLight, cap, capDark, capDownTop, capDown, marking, glassTop, glass, lightOffTop, lightOff: Color

    init(_ c: Handheld.Colors) {
        bodyHi = Color(hex: c.bodyHi)
        body = Color(hex: c.body)
        bodyLo = Color(hex: c.bodyLo)
        recessTop = Color(hex: c.recess, shade: -10)
        recess = Color(hex: c.recess)
        wellTop = Color(hex: c.recess, shade: -18)
        wellBottom = Color(hex: c.recess, shade: -2)
        capLight = Color(hex: c.cap, shade: 21)
        cap = Color(hex: c.cap)
        capDark = Color(hex: c.cap, shade: -15)
        capDownTop = Color(hex: c.capDown, shade: -10)
        capDown = Color(hex: c.capDown)
        marking = Color(hex: c.marking)
        glassTop = Color(hex: c.glass, shade: 15)
        glass = Color(hex: c.glass)
        lightOffTop = Color(hex: c.lightOff, shade: -18)
        lightOff = Color(hex: c.lightOff)
    }
}

/// Where a key's keyboard key shows beside it
enum HintSide { case above, below, leading, trailing, on }

/// A key's keyboard key, as a small keycap: over the key under the pointer,
/// over every key while Option is held
struct KeyCap: View {
    let text: String

    var body: some View {
        Text(text)
            .font(.system(size: 10, weight: .bold, design: .rounded))
            .foregroundStyle(.white)
            .padding(.horizontal, 5)
            .frame(minWidth: 18, minHeight: 17)
            .background(
                RoundedRectangle(cornerRadius: 4, style: .continuous)
                    .fill(LinearGradient(colors: [Color(white: 0.17), Color(white: 0.09)],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(RoundedRectangle(cornerRadius: 4, style: .continuous)
                        .strokeBorder(LinearGradient(colors: [.white.opacity(0.28), .white.opacity(0.06)],
                                                     startPoint: .top, endPoint: .bottom), lineWidth: 0.6)))
            .shadow(color: .black.opacity(0.4), radius: 2.5, y: 1)
            .fixedSize()
            .allowsHitTesting(false)
    }
}

/// A keycap's width, near enough to set it beside a key: its padding and a
/// letter's width each
private func capWidth(_ text: String) -> CGFloat { max(18, 10 + 6.6 * CGFloat(text.count)) }

private extension View {
    /// The keycap beside a key of `size`, on `side`, nudged `dx` across, when
    /// `shown`
    func keyHint(_ text: String, side: HintSide, size: CGSize, dx: CGFloat = 0, shown: Bool) -> some View {
        let gap: CGFloat = 5, half: CGFloat = 8.5
        let off: CGSize
        switch side {
        case .above: off = CGSize(width: dx, height: -(size.height / 2 + gap + half))
        case .below: off = CGSize(width: dx, height: size.height / 2 + gap + half)
        case .leading: off = CGSize(width: -(size.width / 2 + gap + capWidth(text) / 2), height: 0)
        case .trailing: off = CGSize(width: size.width / 2 + gap + capWidth(text) / 2, height: 0)
        case .on: off = .zero
        }
        return overlay {
            if shown && !text.isEmpty {
                KeyCap(text: text).offset(off).transition(.opacity)
            }
        }
    }
}

extension HintSide {
    init(_ h: Handheld.Hint) {
        switch h {
        case .above: self = .above
        case .below: self = .below
        case .left: self = .leading
        case .right: self = .trailing
        case .on: self = .on
        }
    }
}

/// The handheld, drawn from its device files, with its screen showing the
/// VM's
struct DeviceView: View {
    @EnvironmentObject var sim: Simulator
    /// A picture for the screen instead of the live layer: for drawing the
    /// whole device into an image (the control socket's window-shot)
    var still: CGImage?

    var body: some View {
        let g = Geometry(sim.handheld, zoom: sim.zoom)
        let p = Palette(sim.handheld.colors)
        TimelineView(.animation(paused: !sim.rumble.isOn)) { time in
            ZStack(alignment: .topLeading) {
                shoulders(g, p)
                sideKeys(g, p)
                bodyShell(g, p)
                screen(g, p)
                faceButtons(g, p)
                if let pad = sim.handheld.dpad { DPad(g: g, p: p, key: pad, arm: sim.handheld.dpadArm) }
                sideLights(g, p)
                // Over the glass too, where a handheld's glass reaches near
                // its edges (the Brick's)
                edgeHints(g)
            }
            .frame(width: g.width, height: g.height, alignment: .topLeading)
            .offset(x: shake(time.date, g))
        }
        .animation(.easeOut(duration: 0.12), value: sim.showKeys)
    }

    private var keys: [Handheld.Key] { sim.handheld.keys }

    /// The motor, as the device moving in the hand
    private func shake(_ date: Date, _ g: Geometry) -> CGFloat {
        guard sim.rumble.isOn else { return 0 }
        let t = date.timeIntervalSinceReferenceDate
        return CGFloat(sin(t * 95) * (0.25 + 0.45 * sim.rumble.strength)) * g.s
    }

    // ---- the parts ----------------------------------------------------

    /// The shell: lit from above, a bright rim along its top, a dark one
    /// along its bottom, a contact shadow and a soft one under it
    private func bodyShell(_ g: Geometry, _ p: Palette) -> some View {
        let h = sim.handheld
        let shape = RoundedRectangle(cornerRadius: g.len(h.corner), style: .continuous)
        return shape
            .fill(LinearGradient(colors: [p.bodyHi, p.body, p.bodyLo], startPoint: .top, endPoint: .bottom))
            .overlay(shape.inset(by: 0.35 * g.s)
                .stroke(LinearGradient(colors: [.white.opacity(0.5), .white.opacity(0.08), .clear],
                                       startPoint: .top, endPoint: .center), lineWidth: 0.6 * g.s))
            .overlay(shape.stroke(LinearGradient(colors: [.clear, .black.opacity(0.28)],
                                                 startPoint: .center, endPoint: .bottom), lineWidth: 0.4 * g.s))
            .shadow(color: .black.opacity(0.3), radius: 0.5 * g.s, y: 0.3 * g.s)
            .shadow(color: .black.opacity(0.4), radius: 3.2 * g.s, y: 1.8 * g.s)
            .frame(width: g.len(h.body.w), height: g.len(h.body.h))
            .position(g.point(0, 0))
            .allowsHitTesting(false)
    }

    private func screen(_ g: Geometry, _ p: Palette) -> some View {
        let h = sim.handheld
        let glass = RoundedRectangle(cornerRadius: g.len(h.glassRadius), style: .continuous)
        return ZStack {
            glass
                .fill(LinearGradient(colors: [p.glassTop, p.glass], startPoint: .top, endPoint: .bottom))
                .overlay(glass.stroke(LinearGradient(colors: [.white.opacity(0.16), .clear],
                                                     startPoint: .top, endPoint: .center), lineWidth: 0.5))
                .shadow(color: .black.opacity(0.35), radius: 0.3 * g.s, y: -0.1 * g.s)
                .frame(width: g.len(h.glass.w), height: g.len(h.glass.h))
                .position(g.point(h.glass.x, h.glass.y))
            ZStack {
                if let still {
                    Image(decorative: still, scale: 1).resizable().interpolation(.none)
                } else {
                    ScreenLayer(sim: sim)
                }
                // The backlight as a shade over the picture: dark when off,
                // and only a hint of the level otherwise, so a dim setting
                // still leaves the picture readable on the Mac
                Rectangle().fill(.black)
                    .opacity(sim.power != .on || sim.state.screenLight == 0
                             ? 1 : 0.55 * (1 - sim.state.screenLight))
                    .animation(.easeOut(duration: 0.15), value: sim.state.screenLight)
                if sim.power != .on {
                    VStack(spacing: 0.9 * g.s) {
                        if sim.power == .starting {
                            ProgressView().controlSize(.small).tint(.gray)
                        } else {
                            Image(systemName: "power").font(.system(size: 3.2 * g.s, weight: .semibold))
                        }
                        Text(sim.power == .starting ? "Starting…" : "Off. Press POWER.")
                            .font(.system(size: 2.1 * g.s, weight: .semibold, design: .rounded))
                    }
                    .foregroundStyle(Color.white.opacity(0.45))
                }
            }
            .frame(width: g.len(h.screen.w), height: g.len(h.screen.h))
            .position(g.point(h.screen.x, h.screen.y))
            if let word = h.word {
                Text(word)
                    .font(.system(size: 1.9 * g.s, weight: .semibold, design: .rounded))
                    .foregroundStyle(Color.white.opacity(0.82))
                    .position(g.point(0, h.wordY))
            }
        }
        .allowsHitTesting(false)
    }

    /// The keys on the front, and SELECT and START's well where there is one
    private func faceButtons(_ g: Geometry, _ p: Palette) -> some View {
        let h = sim.handheld
        let well = RoundedRectangle(cornerRadius: g.len(h.recessRadius), style: .continuous)
        return ZStack(alignment: .topLeading) {
            if let r = h.recess {
                // Sunk into the shell
                well.fill(LinearGradient(colors: [p.recessTop, p.recess], startPoint: .top, endPoint: .bottom))
                    .overlay(well.stroke(LinearGradient(colors: [.black.opacity(0.35), .clear],
                                                        startPoint: .top, endPoint: .center), lineWidth: 0.5 * g.s))
                    .overlay(well.stroke(LinearGradient(colors: [.clear, .white.opacity(0.25)],
                                                        startPoint: .center, endPoint: .bottom), lineWidth: 0.25 * g.s))
                    .frame(width: g.len(r.w), height: g.len(r.h))
                    .position(g.point(r.x, r.y))
                    .allowsHitTesting(false)
            }
            ForEach(0..<h.sticks.count, id: \.self) { i in
                // A stick, sunk in its well, never pressed
                let k = h.sticks[i]
                Circle().fill(LinearGradient(colors: [p.recessTop, p.recess], startPoint: .top, endPoint: .bottom))
                    .overlay(Circle().fill(LinearGradient(colors: [p.capLight, p.cap], startPoint: .top,
                                                          endPoint: .bottom))
                        .frame(width: g.len(2 * k.cap), height: g.len(2 * k.cap)))
                    .frame(width: g.len(2 * k.well), height: g.len(2 * k.well))
                    .position(g.point(k.x, k.y))
                    .allowsHitTesting(false)
            }
            ForEach(keys.filter { $0.kind == .round || $0.kind == .pill }, id: \.button) { k in
                let n = k.label.count
                Key(button: k.button!, label: k.label,
                    shape: k.kind == .pill ? AnyShape(Capsule()) : AnyShape(Circle()),
                    size: g.size(k.w, k.h), g: g, p: p,
                    labelSize: g.len(k.w * (n == 1 ? 0.46 : n == 2 ? 0.28 : 0.2)),
                    hint: HintSide(k.hint), hintDX: k.hintDX)
                    .position(g.point(k.x, k.y))
                if let under = k.under {
                    Text(under)
                        .font(.system(size: 1.25 * g.s, weight: .bold, design: .rounded))
                        .foregroundStyle(p.marking.opacity(0.85))
                        .fixedSize()
                        .position(g.point(k.x, k.y - k.h / 2 - 1.6))
                        .allowsHitTesting(false)
                }
            }
        }
    }

    /// The shoulders along the top edge, behind the shell
    private func shoulders(_ g: Geometry, _ p: Palette) -> some View {
        ZStack(alignment: .topLeading) {
            ForEach(keys.filter { $0.kind == .shoulder }, id: \.button) { k in
                Key(button: k.button!, label: k.label,
                    shape: AnyShape(UnevenRoundedRectangle(topLeadingRadius: 1.2 * g.s, topTrailingRadius: 1.2 * g.s)),
                    size: g.size(k.w, k.h), g: g, p: p, labelSize: 1.25 * g.s, labelOffset: -0.25 * g.len(k.h),
                    hint: nil)
                    .position(g.point(k.x, k.y))
            }
        }
    }

    /// The keys of what stands out of the shell's edges, over the shell:
    /// the shoulders and the side keys themselves are behind it
    private func edgeHints(_ g: Geometry) -> some View {
        let places: [(PadButton, Double, Double)] = keys.compactMap { k in
            guard let b = k.button else { return nil }
            switch k.kind {
            case .shoulder: return (b, k.x, k.y + 0.3)
            case .side: return (b, k.x < 0 ? k.x + k.w / 2 + 1.0 : k.x - k.w / 2 - 1.3, k.y)
            default: return nil
            }
        }
        return ZStack(alignment: .topLeading) {
            ForEach(places, id: \.0) { button, x, y in
                if sim.showKeys || sim.hovered == button {
                    KeyCap(text: Keyboard.name(for: button))
                        .position(g.point(x, y))
                        .transition(.opacity)
                }
            }
        }
        .allowsHitTesting(false)
    }

    /// The keys out of the side edges (a rocker's halves, where two meet,
    /// square where they join), and the words printed beside them
    private func sideKeys(_ g: Geometry, _ p: Palette) -> some View {
        let side = keys.filter { $0.kind == .side }
        return ZStack(alignment: .topLeading) {
            ForEach(side, id: \.button) { k in
                let left = k.x < 0, r = 1.2 * g.s
                let joined = side.filter { $0.button != k.button && $0.x == k.x && abs($0.y - k.y) <= ($0.h + k.h) / 2 + 0.05 }
                let below = joined.contains { $0.y < k.y }, above = joined.contains { $0.y > k.y }
                let shape: AnyShape = !below && !above ? AnyShape(Capsule())
                    : AnyShape(UnevenRoundedRectangle(topLeadingRadius: left && !above ? r : 0,
                                                      bottomLeadingRadius: left && !below ? r : 0,
                                                      bottomTrailingRadius: !left && !below ? r : 0,
                                                      topTrailingRadius: !left && !above ? r : 0))
                Key(button: k.button!, label: k.label == "-" ? "\u{2212}" : k.label, shape: shape,
                    size: g.size(k.w, k.h), g: g, p: p, labelSize: 2 * g.s,
                    labelOffset: (left ? -0.7 : 0.7) * g.s, labelX: true, hint: nil)
                    .position(g.point(k.x, k.y))
            }
            ForEach(sim.handheld.labels, id: \.text) { l in
                Text(l.text).font(.system(size: 1.3 * g.s, weight: .bold, design: .rounded))
                    .foregroundStyle(Color(white: 0.5))
                    .fixedSize()
                    .rotationEffect(.degrees(l.turned ? -90 : 0))
                    .position(g.point(l.x, l.y))
                    .allowsHitTesting(false)
            }
        }
    }

    /// The side lights, top to bottom, each lit green, red or both (amber)
    /// by its lights in px2sim's state: lit, each glows
    private func sideLights(_ g: Geometry, _ p: Palette) -> some View {
        let h = sim.handheld
        let green = Color(hex: 0x5BE36A), red = Color(hex: 0xFF4A3D), amber = Color(hex: 0xFFB13D)
        let lights: [(Double, Color?)] = h.lights.map { l in
            let gOn = l.green.map { sim.state.led($0) } ?? false, rOn = l.red.map { sim.state.led($0) } ?? false
            return (l.y, gOn && rOn ? amber : rOn ? red : gOn ? green : nil)
        }
        return ZStack(alignment: .topLeading) {
            ForEach(0..<lights.count, id: \.self) { i in
                let (y, color) = lights[i]
                Capsule()
                    .fill(LinearGradient(colors: color.map { [$0.opacity(0.85), $0] } ?? [p.lightOffTop, p.lightOff],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(Capsule().stroke(.black.opacity(0.35), lineWidth: 0.1 * g.s))
                    .overlay(Capsule().fill(.white.opacity(color == nil ? 0.08 : 0.45))
                        .frame(width: 0.4 * g.s, height: 1.1 * g.s).offset(y: -0.7 * g.s))
                    .shadow(color: color ?? .clear, radius: color == nil ? 0 : 1.6 * g.s)
                    .shadow(color: color?.opacity(0.6) ?? .clear, radius: color == nil ? 0 : 0.5 * g.s)
                    .frame(width: g.len(h.light.w), height: g.len(h.light.h))
                    .position(g.point(h.lightX, y))
                    .animation(.easeOut(duration: 0.12), value: color)
            }
        }
        .allowsHitTesting(false)
        .help("The side lights (gauge)")
    }
}

/// A key: pressed while the mouse holds it, shown pressed while the VM holds
/// it; it stands up from the shell, lit from above, and sinks when pressed
private struct Key: View {
    @EnvironmentObject var sim: Simulator
    let button: PadButton
    let label: String
    let shape: AnyShape
    let size: CGSize
    let g: Geometry
    let p: Palette
    let labelSize: CGFloat
    var labelOffset: CGFloat = 0
    var labelX = false
    /// nil: drawn by the device over the shell (the shoulders' and the side
    /// keys')
    var hint: HintSide? = .above
    var hintDX: Double = 0
    @State private var hovering = false

    var body: some View {
        let down = sim.held.contains(button)
        shape
            .fill(down
                  ? LinearGradient(colors: [p.capDownTop, p.capDown], startPoint: .top, endPoint: .bottom)
                  : LinearGradient(colors: [p.capLight, p.cap, p.capDark], startPoint: .top, endPoint: .bottom))
            .overlay(shape.stroke(LinearGradient(colors: down ? [.black.opacity(0.6), .clear]
                                                              : [.white.opacity(0.26), .clear],
                                                 startPoint: .top, endPoint: .center), lineWidth: 0.9))
            .overlay(shape.stroke(.black.opacity(0.45), lineWidth: 0.5))
            .overlay(mark(down).offset(x: labelX ? labelOffset : 0, y: labelX ? 0 : labelOffset))
            .frame(width: size.width, height: size.height)
            .shadow(color: .black.opacity(down ? 0.2 : 0.42), radius: down ? 0.3 * g.s : 0.7 * g.s,
                    y: down ? 0.1 * g.s : 0.4 * g.s)
            .offset(y: down ? 0.16 * g.s : 0)
            .animation(.easeOut(duration: 0.06), value: down)
            .keyHint(Keyboard.name(for: button), side: hint ?? .on, size: size, dx: CGFloat(hintDX),
                     shown: hint != nil && (sim.showKeys || hovering))
            .contentShape(shape)
            .onHover { inside in
                hovering = inside
                if inside { sim.hovered = button } else if sim.hovered == button { sim.hovered = nil }
            }
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in sim.press(button) }
                .onEnded { _ in sim.release(button) })
            .help(Keyboard.help(for: button))
    }

    /// What is printed on the cap: its word, or SELECT's square and START's
    /// triangle as marks
    @ViewBuilder private func mark(_ down: Bool) -> some View {
        let color = p.marking.opacity(down ? 0.75 : 1)
        switch label {
        case "\u{25A0}":
            Image(systemName: "square.fill").font(.system(size: size.width * 0.3)).foregroundStyle(color)
        case "\u{25B6}":
            Image(systemName: "play.fill").font(.system(size: size.width * 0.34)).foregroundStyle(color)
        default:
            Text(label).font(.system(size: labelSize, weight: .bold, design: .rounded))
                .foregroundStyle(color)
                .shadow(color: .black.opacity(0.5), radius: 0, y: -0.5)
        }
    }
}

/// The d-pad: one direction at a time, the one the pointer is furthest
/// along, rolling to the next as it moves, as a thumb does. It stands in a
/// well in the shell, its held arm tipped down.
private struct DPad: View {
    @EnvironmentObject var sim: Simulator
    let g: Geometry
    let p: Palette
    let key: Handheld.Key
    /// Half an arm's width, millimetres
    let arm: Double
    @State private var current: PadButton?
    @State private var hovering: PadButton?

    var body: some View {
        let size = g.len(key.w)
        let arm = CGFloat(self.arm / (key.w / 2))
        ZStack {
            Cross(arm: arm * 1.12)
                .fill(LinearGradient(colors: [p.wellTop, p.wellBottom], startPoint: .top, endPoint: .bottom))
                .overlay(Cross(arm: arm * 1.12).stroke(.black.opacity(0.25), lineWidth: 0.4 * g.s))
                .frame(width: size * 1.1, height: size * 1.1)
            Cross(arm: arm)
                .fill(LinearGradient(colors: [p.capLight, p.cap, p.capDark], startPoint: .top, endPoint: .bottom))
                .overlay(Cross(arm: arm).stroke(LinearGradient(colors: [.white.opacity(0.24), .clear],
                                                               startPoint: .top, endPoint: .center), lineWidth: 0.9))
                .overlay(Cross(arm: arm).stroke(.black.opacity(0.45), lineWidth: 0.5))
                .overlay(heldArm(size, arm))
                .overlay(Circle()
                    .fill(LinearGradient(colors: [p.capDark, p.capLight.opacity(0.9)], startPoint: .top, endPoint: .bottom))
                    .frame(width: size * 0.2, height: size * 0.2))
                .overlay(arrows(size))
                .frame(width: size, height: size)
                .shadow(color: .black.opacity(0.42), radius: 0.7 * g.s, y: 0.4 * g.s)
            ForEach([PadButton.up, .down, .left, .right], id: \.self) { b in
                if sim.showKeys || hovering == b {
                    KeyCap(text: Keyboard.name(for: b))
                        .offset(hintOffset(b, size))
                        .transition(.opacity)
                }
            }
        }
        .frame(width: size * 1.1, height: size * 1.1)
        .contentShape(Cross(arm: arm))
        .onContinuousHover { phase in
            if case let .active(pt) = phase { hovering = direction(pt, size * 1.1) } else { hovering = nil }
        }
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { v in roll(to: direction(v.location, size * 1.1)) }
            .onEnded { _ in roll(to: nil) })
        .help(Keyboard.help(for: .up))
        .position(g.point(key.x, key.y))
    }

    private func hintOffset(_ b: PadButton, _ size: CGFloat) -> CGSize {
        let d = size * 0.5 + 12
        switch b {
        case .up: return CGSize(width: 0, height: -d)
        case .down: return CGSize(width: 0, height: d)
        case .left: return CGSize(width: -d - 8, height: 0)
        default: return CGSize(width: d + 8, height: 0)
        }
    }

    private func heldArm(_ size: CGFloat, _ arm: CGFloat) -> some View {
        let a = size / 2 * arm
        return ZStack {
            if let b = [PadButton.up, .down, .left, .right].first(where: { sim.held.contains($0) }) {
                let vertical = b == .up || b == .down
                RoundedRectangle(cornerRadius: a * 0.25)
                    .fill(.black.opacity(0.38))
                    .frame(width: vertical ? 2 * a : size / 2 - a, height: vertical ? size / 2 - a : 2 * a)
                    .offset(x: b == .left ? -(size / 4 + a / 2) : b == .right ? size / 4 + a / 2 : 0,
                            y: b == .up ? -(size / 4 + a / 2) : b == .down ? size / 4 + a / 2 : 0)
            }
        }
    }

    private func arrows(_ size: CGFloat) -> some View {
        ZStack {
            ForEach([PadButton.up, .right, .down, .left], id: \.self) { b in
                Image(systemName: "arrowtriangle.up.fill")
                    .font(.system(size: size * 0.1))
                    .foregroundStyle(sim.held.contains(b) ? Color.white.opacity(0.85) : p.marking.opacity(0.75))
                    .offset(y: -size * 0.34)
                    .rotationEffect(.degrees(b == .up ? 0 : b == .right ? 90 : b == .down ? 180 : 270))
            }
        }
    }

    private func direction(_ pt: CGPoint, _ size: CGFloat) -> PadButton {
        let dx = pt.x - size / 2, dy = pt.y - size / 2
        return abs(dx) > abs(dy) ? (dx > 0 ? .right : .left) : (dy > 0 ? .down : .up)
    }

    private func roll(to next: PadButton?) {
        guard next != current else { return }
        if let current { sim.release(current) }
        if let next { sim.press(next) }
        current = next
    }
}

private struct Cross: Shape {
    /// Half an arm's width, as a share of half the cross
    let arm: CGFloat

    func path(in r: CGRect) -> Path {
        let c = CGPoint(x: r.midX, y: r.midY), h = r.width / 2, a = h * arm
        var p = Path()
        p.addRoundedRect(in: CGRect(x: c.x - a, y: r.minY, width: 2 * a, height: r.height),
                         cornerSize: CGSize(width: a * 0.25, height: a * 0.25))
        p.addRoundedRect(in: CGRect(x: r.minX, y: c.y - a, width: r.width, height: 2 * a),
                         cornerSize: CGSize(width: a * 0.25, height: a * 0.25))
        return p
    }
}

/// The VM's screen as a layer, its contents swapped for each frame without
/// SwiftUI drawing anything
struct ScreenLayer: NSViewRepresentable {
    let sim: Simulator

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        view.wantsLayer = true
        let layer = view.layer!
        layer.backgroundColor = NSColor.black.cgColor
        layer.contentsGravity = .resize
        layer.magnificationFilter = .nearest
        layer.minificationFilter = .trilinear
        layer.contents = sim.frame
        sim.frameSink = { [weak layer] image in
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            layer?.contents = image
            CATransaction.commit()
        }
        return view
    }

    func updateNSView(_ view: NSView, context: Context) {}
}
