#!/usr/bin/env bash
# Build a signed arm64 APK from the same pinned SDL sources as the native runtime.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
sdk=${ANDROID_HOME:?Set ANDROID_HOME to the Android SDK}
ndk="$sdk/ndk/27.2.12479018"
cmake="$sdk/cmake/3.31.6/bin/cmake"
tools="$sdk/build-tools/35.0.0"
platform="$sdk/platforms/android-35/android.jar"
work="$root/build/android"
native="$work/native"
version=${NC_VERSION:-dev}
mkdir -p "$work"
stage=$(mktemp -d "$work/package.XXXXXX")
trap 'rm -rf "$stage"' EXIT

"$cmake" -S "$root" -B "$native" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_MAKE_PROGRAM="$sdk/cmake/3.31.6/bin/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DNC_VENDOR_SDL=ON -DNC_INTERPRETER_ONLY=ON \
    -DNC_VERSION="$version"
"$cmake" --build "$native" --target nightmare --parallel

mkdir -p "$stage"/{classes,dex,res/drawable-nodpi,lib/arm64-v8a}
cp "$root/packaging/icon.png" "$stage/res/drawable-nodpi/icon.png"
package=org.nightmarecreatures.port
keystore=${NC_ANDROID_KEYSTORE:-}
if [[ -z "$keystore" ]]; then
    package+=.test
    keystore="$work/debug.keystore"
    export NC_ANDROID_STORE_PASSWORD=android NC_ANDROID_KEY_PASSWORD=android
    alias_name=androiddebugkey
    if [[ ! -f "$keystore" ]]; then
        keytool -genkeypair -keystore "$keystore" -storepass android -keypass android \
            -alias "$alias_name" -dname 'CN=Nightmare Creatures Development' \
            -keyalg RSA -keysize 2048 -validity 10000
    fi
else
    : "${NC_ANDROID_STORE_PASSWORD:?Set the signing store password}"
    export NC_ANDROID_KEY_PASSWORD=${NC_ANDROID_KEY_PASSWORD:-$NC_ANDROID_STORE_PASSWORD}
    alias_name=${NC_ANDROID_KEY_ALIAS:?Set the signing key alias}
fi

"$tools/aapt2" compile --dir "$stage/res" -o "$stage/resources.zip"
"$tools/aapt2" link -o "$stage/unsigned.apk" -I "$platform" \
    --manifest "$root/packaging/android/AndroidManifest.xml" --rename-manifest-package "$package" \
    --min-sdk-version 26 --target-sdk-version 35 --version-code "${NC_VERSION_CODE:-1}" \
    --version-name "$version" "$stage/resources.zip"
sdl_java=$(cat "$native/sdl-java-dir.txt")
javac --release 8 -classpath "$platform" -d "$stage/classes" \
    "$sdl_java"/org/libsdl/app/*.java "$root/packaging/android/NightmareActivity.java"
jar cf "$stage/activity.jar" -C "$stage/classes" .
"$tools/d8" --release --min-api 26 --lib "$platform" --output "$stage/dex" "$stage/activity.jar"

llvm=("$ndk"/toolchains/llvm/prebuilt/*)
lib="$stage/lib/arm64-v8a"
cp "$native/libmain.so" "$native/_deps/sdl3-build/libSDL3.so" \
    "${llvm[0]}/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$lib/"
"${llvm[0]}/bin/llvm-strip" --strip-unneeded "$lib"/*.so
(cd "$stage" && zip -q -0 unsigned.apk lib/arm64-v8a/*.so)
(cd "$stage/dex" && zip -q ../unsigned.apk classes.dex)
"$tools/zipalign" -P 16 -f 4 "$stage/unsigned.apk" "$stage/aligned.apk"
"$tools/apksigner" sign --ks "$keystore" --ks-key-alias "$alias_name" \
    --ks-pass env:NC_ANDROID_STORE_PASSWORD --key-pass env:NC_ANDROID_KEY_PASSWORD \
    --out "$stage/signed.apk" "$stage/aligned.apk"
"$tools/apksigner" verify --verbose "$stage/signed.apk"
"$tools/zipalign" -P 16 -c 4 "$stage/signed.apk"
mv "$stage/signed.apk" "$work/NightmareCreatures-android-arm64.apk"
echo "Built $work/NightmareCreatures-android-arm64.apk ($package)"
