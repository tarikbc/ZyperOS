// SPDX-License-Identifier: GPL-3.0-only
import Foundation

/// A line on the simulator's control socket (simulator/simctl), for driving
/// and checking the window from a shell with no clicks. One command a line,
/// one JSON line back.
public enum ControlCommand: Equatable {
    case state
    case press([PadButton])
    case release([PadButton])
    case tap([PadButton], milliseconds: Int)
    case battery(Int)
    case cable(Bool)
    /// What the cable is in: 1 a wall charger, 2 a computer
    case kind(Int)
    case headphones(Bool)
    /// The power button: pressed while the VM runs, the VM started when it doesn't
    case power
    /// A clean power-off, as ZyperOS's own Power Off does
    case shutdown
    /// The VM's screen as a PNG
    case screenshot(String)
    /// The whole window's device, drawn, as a PNG
    case windowShot(String)
    /// Returns once count more frames have come from the VM (10 s at most)
    case waitFrames(Int)
    case quit

    public static let usage = """
        state | press|release BUTTON[+BUTTON] | tap BUTTON[+BUTTON] [MS] | battery 0-100 | \
        cable 0|1 | kind wall|computer | headphones 0|1 | power | shutdown | screenshot FILE.png | \
        window-shot FILE.png | wait-frames N | quit
        """

    public init?(line: String) {
        let words = line.split(whereSeparator: { $0 == " " || $0 == "\t" || $0 == "\r" }).map(String.init)
        guard let verb = words.first else { return nil }
        let arg = words.count > 1 ? words[1] : nil
        switch (verb, words.count) {
        case ("state", 1): self = .state
        case ("power", 1): self = .power
        case ("shutdown", 1): self = .shutdown
        case ("quit", 1): self = .quit
        case ("press", 2), ("release", 2), ("tap", 2), ("tap", 3):
            guard let buttons = ControlCommand.chord(arg!) else { return nil }
            if verb == "press" { self = .press(buttons) }
            else if verb == "release" { self = .release(buttons) }
            else {
                guard let ms = words.count == 3 ? Int(words[2]) : 80, (1...5000).contains(ms) else { return nil }
                self = .tap(buttons, milliseconds: ms)
            }
        case ("battery", 2):
            guard let pct = Int(arg!), (0...100).contains(pct) else { return nil }
            self = .battery(pct)
        case ("cable", 2), ("headphones", 2):
            guard arg == "0" || arg == "1" else { return nil }
            self = verb == "cable" ? .cable(arg == "1") : .headphones(arg == "1")
        case ("kind", 2):
            guard arg == "wall" || arg == "computer" else { return nil }
            self = .kind(arg == "wall" ? 1 : 2)
        case ("screenshot", 2): self = .screenshot(arg!)
        case ("window-shot", 2): self = .windowShot(arg!)
        case ("wait-frames", 2):
            guard let n = Int(arg!), (1...600).contains(n) else { return nil }
            self = .waitFrames(n)
        default: return nil
        }
    }

    /// "a+right" as buttons; nil if one isn't a button
    static func chord(_ text: String) -> [PadButton]? {
        let buttons = text.split(separator: "+").map { PadButton(rawValue: String($0)) }
        guard !buttons.isEmpty, !buttons.contains(where: { $0 == nil }) else { return nil }
        return buttons.compactMap { $0 }
    }
}
