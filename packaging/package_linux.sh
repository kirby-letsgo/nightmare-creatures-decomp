#!/bin/sh
# package_linux.sh <build dir> <output.AppImage>: builds an AppImage with linuxdeploy.
set -eu
BUILD=$1; OUT=$2
APPDIR="$BUILD/AppDir"; rm -rf "$APPDIR"
TOOL="$BUILD/linuxdeploy-x86_64.AppImage"
[ -f "$TOOL" ] || curl -fsSL -o "$TOOL" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x "$TOOL"
cp packaging/icon.png "$BUILD/nightmare.png"
APPIMAGE_EXTRACT_AND_RUN=1 OUTPUT="$OUT" "$TOOL" --appdir "$APPDIR" \
    --executable "$BUILD/nightmare" --desktop-file packaging/linux/nightmare.desktop \
    --icon-file "$BUILD/nightmare.png" --output appimage
