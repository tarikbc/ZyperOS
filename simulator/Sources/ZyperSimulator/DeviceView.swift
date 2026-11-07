// SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
import AppKit
import SimCore
import SwiftUI

/// Where things are on the Pixel 2's front, in millimetres from the middle
/// of its face, up positive, from the dimensioned drawing that
/// tools/build-pixel2-model.py measures (63 x 81 mm). The side keys and the
/// side lights are drawn on the edges where they sit.
struct Geometry {
    static let marginX: CGFloat = 6, marginTop: CGFloat = 4, marginBottom: CGFloat = 2
    static let width: CGFloat = 63 + 2 * marginX
    static let height: CGFloat = 81 + marginTop + marginBottom
    /// Points a millimetre: zoom 1 draws the 48.8 mm screen 320 points wide,
    /// its 640 pixels one to one on a Retina display
    static func scale(zoom: Double) -> CGFloat { CGFloat(zoom) * 320 / 48.8 }

    let s: CGFloat
    func x(_ mm: CGFloat) -> CGFloat { (mm + Geometry.width / 2) * s }
    func y(_ mm: CGFloat) -> CGFloat { (Geometry.marginTop + 40.5 - mm) * s }
    func point(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: self.x(x), y: self.y(y)) }
    func size(_ w: CGFloat, _ h: CGFloat) -> CGSize { CGSize(width: w * s, height: h * s) }
}

private extension Color {
    init(hex: UInt32) {
        self.init(red: Double(hex >> 16 & 0xFF) / 255, green: Double(hex >> 8 & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255)
    }
    static let body = Color(hex: 0x9CB849)
    static let bodyLight = Color(hex: 0xB0C967)
    static let bodyDark = Color(hex: 0x86A23C)
    static let recess = Color(hex: 0x708B32)
    static let cap = Color(hex: 0x343638)
    static let capDown = Color(hex: 0x1F2122)
    static let marking = Color(hex: 0x9A9D9F)
    static let glass = Color(hex: 0x090C0C)
}

/// The Pixel 2, drawn, with its screen showing the VM's
struct DeviceView: View {
    @EnvironmentObject var sim: Simulator
    /// A picture for the screen instead of the live layer: for drawing the
    /// whole device into an image (the control socket's window-shot)
    var still: CGImage?

    var body: some View {
        let g = Geometry(s: Geometry.scale(zoom: sim.zoom))
        TimelineView(.animation(paused: !sim.rumble.isOn)) { time in
            ZStack(alignment: .topLeading) {
                shoulders(g)
                sideKeys(g)
                bodyShell(g)
                screen(g)
                DPad(g: g)
                faceButtons(g)
                sideLights(g)
            }
            .frame(width: Geometry.width * g.s, height: Geometry.height * g.s, alignment: .topLeading)
            .offset(x: shake(time.date, g))
        }
    }

    /// The motor, as the device moving in the hand
    private func shake(_ date: Date, _ g: Geometry) -> CGFloat {
        guard sim.rumble.isOn else { return 0 }
        let t = date.timeIntervalSinceReferenceDate
        return CGFloat(sin(t * 95) * (0.25 + 0.45 * sim.rumble.strength)) * g.s
    }

    // ---- the parts ----------------------------------------------------

    private func bodyShell(_ g: Geometry) -> some View {
        RoundedRectangle(cornerRadius: 3.9 * g.s, style: .continuous)
            .fill(LinearGradient(colors: [.bodyLight, .body, .bodyDark], startPoint: .top, endPoint: .bottom))
            .overlay(RoundedRectangle(cornerRadius: 3.9 * g.s, style: .continuous)
                .inset(by: 0.6 * g.s)
                .stroke(Color.bodyLight.opacity(0.8), lineWidth: 0.5 * g.s))
            .shadow(color: .black.opacity(0.35), radius: 1.5 * g.s, y: 0.8 * g.s)
            .frame(width: 63 * g.s, height: 81 * g.s)
            .position(g.point(0, 0))
            .allowsHitTesting(false)
    }

    private func screen(_ g: Geometry) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: 0.55 * g.s).fill(Color.glass)
                .frame(width: 55.1 * g.s, height: 43.8 * g.s)
                .position(g.point(0, 14.2))
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
                    Text(sim.power == .starting ? "Starting…" : "Off. Press POWER.")
                        .font(.system(size: 2.4 * g.s, weight: .medium))
                        .foregroundStyle(.gray)
                }
            }
            .frame(width: 48.8 * g.s, height: 36.6 * g.s)
            .position(g.point(0, 15.55))
            Text("ZyperOS")
                .font(.system(size: 1.9 * g.s, weight: .semibold, design: .rounded))
                .foregroundStyle(Color.white.opacity(0.85))
                .position(g.point(0, -5.5))
        }
        .allowsHitTesting(false)
    }

    private func faceButtons(_ g: Geometry) -> some View {
        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 3.8 * g.s).fill(Color.recess)
                .frame(width: 16.4 * g.s, height: 7.6 * g.s)
                .position(g.point(0.1, -29.6))
            ForEach(face, id: \.button) { f in
                Key(button: f.button, label: f.label, shape: Circle(), size: g.size(2 * f.r, 2 * f.r),
                    labelSize: f.label.count == 1 ? 2.8 * g.s : 1.6 * g.s, help: f.help)
                    .position(g.point(f.x, f.y))
            }
        }
    }

    private struct Face { let button: PadButton; let label: String; let x, y, r: CGFloat; let help: String }
    private var face: [Face] {
        [Face(button: .x, label: "X", x: 16.9, y: -14.8, r: 3.05, help: "X (s)"),
         Face(button: .y, label: "Y", x: 10.2, y: -21.5, r: 3.05, help: "Y (a)"),
         Face(button: .a, label: "A", x: 23.6, y: -21.5, r: 3.05, help: "A (x)"),
         Face(button: .b, label: "B", x: 16.9, y: -28.2, r: 3.05, help: "B (z)"),
         Face(button: .select, label: "SE", x: -3.8, y: -29.6, r: 2.85, help: "SELECT (Right Shift, Delete)"),
         Face(button: .start, label: "ST", x: 4, y: -29.6, r: 2.85, help: "START (Return)")]
    }

    /// L1, L2, R2, R1 along the top edge, behind the shell
    private func shoulders(_ g: Geometry) -> some View {
        ZStack(alignment: .topLeading) {
            ForEach(shoulderKeys, id: \.button) { k in
                Key(button: k.button, label: k.label,
                    shape: UnevenRoundedRectangle(topLeadingRadius: 1.2 * g.s, topTrailingRadius: 1.2 * g.s),
                    size: g.size(k.w, 3.6), labelSize: 1.25 * g.s, labelOffset: -0.9 * g.s, help: k.help)
                    .position(g.point(k.x, 40.9))
            }
        }
    }

    private struct Shoulder { let button: PadButton; let label: String; let x, w: CGFloat; let help: String }
    private var shoulderKeys: [Shoulder] {
        [Shoulder(button: .l1, label: "L1", x: -23.9, w: 15.1, help: "L1 (q)"),
         Shoulder(button: .l2, label: "L2", x: -12.6, w: 7.4, help: "L2 (e)"),
         Shoulder(button: .r2, label: "R2", x: 12.6, w: 7.4, help: "R2 (r)"),
         Shoulder(button: .r1, label: "R1", x: 23.9, w: 15.1, help: "R1 (w)")]
    }

    /// The volume rocker on the left side, MENU and POWER on the right
    private func sideKeys(_ g: Geometry) -> some View {
        ZStack(alignment: .topLeading) {
            Key(button: .volup, label: "+", shape: UnevenRoundedRectangle(topLeadingRadius: 1.2 * g.s),
                size: g.size(3, 8), labelSize: 2 * g.s, labelOffset: -0.7 * g.s, labelX: true,
                help: "Volume up (=)")
                .position(g.point(-31.8, 15.75))
            Key(button: .voldown, label: "−", shape: UnevenRoundedRectangle(bottomLeadingRadius: 1.2 * g.s),
                size: g.size(3, 8), labelSize: 2 * g.s, labelOffset: -0.7 * g.s, labelX: true,
                help: "Volume down (-)")
                .position(g.point(-31.8, 7.65))
            Key(button: .menu, label: "", shape: Capsule(), size: g.size(3, 5.3), labelSize: 1,
                help: "MENU (Esc, m)")
                .position(g.point(31.8, 17.1))
            Key(button: .power, label: "", shape: Capsule(), size: g.size(3, 7.2), labelSize: 1,
                help: "POWER (p)")
                .position(g.point(31.8, 6.65))
            sideLabel("VOL", g).position(g.point(-34.6, 11.7))
            sideLabel("MENU", g).position(g.point(34.6, 17.1))
            sideLabel("POWER", g).position(g.point(34.6, 6.65))
        }
    }

    private func sideLabel(_ text: String, _ g: Geometry) -> some View {
        Text(text).font(.system(size: 1.3 * g.s, weight: .semibold))
            .foregroundStyle(.secondary)
            .fixedSize()
            .rotationEffect(.degrees(-90))
            .allowsHitTesting(false)
    }

    /// The four lights under POWER, top to bottom battery4, battery3,
    /// battery2, and the bottom one, red (battery0) or green (battery1)
    private func sideLights(_ g: Geometry) -> some View {
        let lights: [(CGFloat, Color?)] = [
            (1.4, sim.state.led(4) ? .green : nil),
            (-4.03, sim.state.led(3) ? .green : nil),
            (-9.67, sim.state.led(2) ? .green : nil),
            (-15.3, sim.state.led(0) && sim.state.led(1) ? .orange :
                    sim.state.led(0) ? .red : sim.state.led(1) ? .green : nil),
        ]
        return ZStack(alignment: .topLeading) {
            ForEach(0..<lights.count, id: \.self) { i in
                let (y, color) = lights[i]
                Capsule()
                    .fill(color ?? Color(hex: 0x4F6B35))
                    .overlay(Capsule().stroke(.black.opacity(0.4), lineWidth: 0.1 * g.s))
                    .shadow(color: color ?? .clear, radius: color == nil ? 0 : 1.4 * g.s)
                    .frame(width: 1.3 * g.s, height: 3.4 * g.s)
                    .position(g.point(32.0, y))
            }
        }
        .allowsHitTesting(false)
        .help("The side lights (gauge)")
    }
}

/// A key: pressed while the mouse holds it, shown pressed while the VM holds it
private struct Key<S: Shape>: View {
    @EnvironmentObject var sim: Simulator
    let button: PadButton
    let label: String
    let shape: S
    let size: CGSize
    let labelSize: CGFloat
    var labelOffset: CGFloat = 0
    var labelX = false
    let help: String

    var body: some View {
        let down = sim.held.contains(button)
        shape.fill(down ? Color.capDown : Color.cap)
            .overlay(shape.stroke(.black.opacity(0.4), lineWidth: 0.7))
            .overlay(
                Text(label).font(.system(size: labelSize, weight: .bold, design: .rounded))
                    .foregroundStyle(Color.marking)
                    .offset(x: labelX ? labelOffset : 0, y: labelX ? 0 : labelOffset))
            .frame(width: size.width, height: size.height)
            .shadow(color: .black.opacity(down ? 0 : 0.45), radius: down ? 0 : 1.2, y: down ? 0 : 1)
            .scaleEffect(down ? 0.96 : 1)
            .contentShape(shape)
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in sim.press(button) }
                .onEnded { _ in sim.release(button) })
            .help(help)
    }
}

/// The d-pad: one direction at a time, the one the pointer is furthest
/// along, rolling to the next as it moves, as a thumb does
private struct DPad: View {
    @EnvironmentObject var sim: Simulator
    let g: Geometry
    @State private var current: PadButton?

    var body: some View {
        let size = 18 * g.s
        ZStack {
            Cross(arm: 3.2 / 9).fill(Color.black.opacity(0.85)).frame(width: size * 1.08, height: size * 1.08)
            Cross(arm: 3.2 / 9).fill(Color.cap)
                .overlay(arrows(size))
                .frame(width: size, height: size)
                .shadow(color: .black.opacity(0.45), radius: 1.2, y: 1)
        }
        .contentShape(Cross(arm: 3.2 / 9))
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { v in roll(to: direction(v.location, size * 1.08)) }
            .onEnded { _ in roll(to: nil) })
        .help("D-pad (arrow keys)")
        .position(g.point(-16.9, -21.0))
    }

    private func arrows(_ size: CGFloat) -> some View {
        ZStack {
            ForEach([PadButton.up, .right, .down, .left], id: \.self) { b in
                Image(systemName: "arrowtriangle.up.fill")
                    .font(.system(size: size * 0.11))
                    .foregroundStyle(sim.held.contains(b) ? Color.white.opacity(0.8) : Color.marking.opacity(0.7))
                    .offset(y: -size * 0.34)
                    .rotationEffect(.degrees(b == .up ? 0 : b == .right ? 90 : b == .down ? 180 : 270))
            }
        }
    }

    private func direction(_ p: CGPoint, _ size: CGFloat) -> PadButton {
        let dx = p.x - size / 2, dy = p.y - size / 2
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
                         cornerSize: CGSize(width: a * 0.2, height: a * 0.2))
        p.addRoundedRect(in: CGRect(x: r.minX, y: c.y - a, width: r.width, height: 2 * a),
                         cornerSize: CGSize(width: a * 0.2, height: a * 0.2))
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
