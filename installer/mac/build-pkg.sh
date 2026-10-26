#!/usr/bin/env bash
# Assembles the macOS .pkg (installs the plugins + app + both engines) and
# wraps it in a .dmg. Expects the CI artifacts unpacked under $STAGE:
#   $STAGE/mac-plugin/mac-plugin.tgz
#   $STAGE/mac-engine-arm64/mac-engine-arm64.tgz
#   $STAGE/mac-engine-x86_64/mac-engine-x86_64.tgz
# Usage: build-pkg.sh [version]
set -euo pipefail

VERSION="${1:-1.0.1}"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+ ]] || VERSION="1.0.1"
STAGE="${STAGE:-stage}"
IDENT="com.pathfinderpressings.transform6"

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="$REPO/macpkg"
ROOT="$WORK/root"
rm -rf "$WORK"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" \
         "$ROOT/Library/Audio/Plug-Ins/Components" \
         "$ROOT/Applications/TRANSFORM-6"

echo "Unpacking plugin bundles..."
tar -xzf "$STAGE/mac-plugin/mac-plugin.tgz" -C "$WORK"
cp -R "$WORK/VST3/TRANSFORM-6.vst3"           "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R "$WORK/AU/TRANSFORM-6.component"         "$ROOT/Library/Audio/Plug-Ins/Components/"
cp -R "$WORK/Standalone/TRANSFORM-6.app"       "$ROOT/Applications/TRANSFORM-6/"

echo "Unpacking engines..."
tar -xzf "$STAGE/mac-engine-arm64/mac-engine-arm64.tgz" -C "$WORK"
rm -rf "$ROOT/Applications/TRANSFORM-6/engine-arm64"
mv "$WORK/transform6-engine" "$ROOT/Applications/TRANSFORM-6/engine-arm64"
tar -xzf "$STAGE/mac-engine-x86_64/mac-engine-x86_64.tgz" -C "$WORK"
rm -rf "$ROOT/Applications/TRANSFORM-6/engine-x86_64"
mv "$WORK/transform6-engine" "$ROOT/Applications/TRANSFORM-6/engine-x86_64"

chmod +x "$ROOT/Applications/TRANSFORM-6/engine-arm64/transform6-engine"  || true
chmod +x "$ROOT/Applications/TRANSFORM-6/engine-x86_64/transform6-engine" || true

echo "Building component package..."
pkgbuild --root "$ROOT" \
         --identifier "$IDENT" \
         --version "$VERSION" \
         --install-location / \
         "$WORK/TRANSFORM-6-component.pkg"

productbuild --package "$WORK/TRANSFORM-6-component.pkg" "$WORK/TRANSFORM-6.pkg"

echo "Building disk image..."
DMGROOT="$WORK/dmg"
mkdir -p "$DMGROOT"
cp "$WORK/TRANSFORM-6.pkg" "$DMGROOT/TRANSFORM-6.pkg"
[ -f "$HERE/READ-ME-FIRST.txt" ] && cp "$HERE/READ-ME-FIRST.txt" "$DMGROOT/"

mkdir -p "$REPO/installer/Output"
hdiutil create -volname "TRANSFORM-6 $VERSION" \
               -srcfolder "$DMGROOT" -ov -format UDZO \
               "$REPO/installer/Output/TRANSFORM-6-macOS.dmg"

echo "Done: installer/Output/TRANSFORM-6-macOS.dmg"
