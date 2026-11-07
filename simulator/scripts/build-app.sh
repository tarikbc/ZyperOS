#!/bin/sh
# Build "simulator/build/ZyperOS Simulator.app" and sign it (ad-hoc unless
# CODESIGN_IDENTITY names an identity). The app remembers this repository
# (Info.plist key ZyperOSRepo) for its tools/qemu-run.sh.
set -eu

cd "$(dirname "$0")/.."
SIM="$(pwd)"
REPO="$(cd .. && pwd)"
APP="$SIM/build/ZyperOS Simulator.app"

swift build -c release --product ZyperSimulator
BIN="$(swift build -c release --show-bin-path)/ZyperSimulator"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/ZyperSimulator"

# SwiftPM records the deployment target (14.0) as the SDK version too, and
# AppKit reads that field to pick its look: record the real SDK
xcrun vtool -set-build-version macos 14.0 "$(xcrun --show-sdk-version)" -replace \
	-output "$APP/Contents/MacOS/ZyperSimulator" "$APP/Contents/MacOS/ZyperSimulator"

PLIST="$APP/Contents/Info.plist"
cat > "$PLIST" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>ZyperSimulator</string>
  <key>CFBundleIdentifier</key><string>com.tarikbc.zyperos.simulator</string>
  <key>CFBundleName</key><string>ZyperOS Simulator</string>
  <key>CFBundleDisplayName</key><string>ZyperOS Simulator</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.developer-tools</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
plutil -insert ZyperOSRepo -string "$REPO" "$PLIST"

codesign --force --timestamp=none --sign "${CODESIGN_IDENTITY:--}" "$APP"
echo "Built $APP"
