#!/bin/sh
# package_macos.sh <build dir> <version> <output.dmg>: wraps the executable in a .app and a .dmg.
set -eu
BUILD=$1; VERSION=$2; OUT=$3
APP="$BUILD/dmg/Nightmare Creatures.app"
rm -rf "$BUILD/dmg" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BUILD/nightmare" "$APP/Contents/MacOS/nightmare"
sed "s/@VERSION@/$VERSION/g" packaging/macos/Info.plist > "$APP/Contents/Info.plist"
ICONSET="$BUILD/icon.iconset"; rm -rf "$ICONSET" && mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    sips -z $s $s packaging/icon.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    d=$((s * 2)); [ $d -le 512 ] && sips -z $d $d packaging/icon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/icon.icns"
codesign --force --deep --sign - "$APP" # ad-hoc signature (required on Apple Silicon)
ln -s /Applications "$BUILD/dmg/Applications"
hdiutil create -volname "Nightmare Creatures" -srcfolder "$BUILD/dmg" -ov -format UDZO "$OUT"
