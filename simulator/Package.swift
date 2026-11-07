// swift-tools-version: 6.0
// ZyperOS Simulator: a GKD Pixel 2 on the Mac's screen, around ZyperOS running
// in QEMU (tools/qemu-run.sh, plastron's qemu_defconfig). docs/simulator.md.
import PackageDescription

let package = Package(
    name: "ZyperSimulator",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "ZyperSimulator", targets: ["ZyperSimulator"]),
    ],
    targets: [
        // The parts with no window: the VNC client, the px2sim link, the
        // state lines and the control socket's commands
        .target(name: "SimCore"),
        .executableTarget(name: "ZyperSimulator", dependencies: ["SimCore"]),
        .testTarget(name: "SimCoreTests", dependencies: ["SimCore"]),
    ],
    swiftLanguageModes: [.v5]
)
