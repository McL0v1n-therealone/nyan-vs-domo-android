#!/usr/bin/env bash
# Builds build/NyanVsDomo.apk without Gradle: the app is pure native code
# (NativeActivity), so it's just clang + aapt2 + zipalign + apksigner.
#
#   ./build.sh            build the APK
#   ./build.sh install    build, install on the connected device (adb) and launch
#
# Needs: Android SDK with build-tools 34, platform android-33 and NDK r25c
# (the last NDK that can still target Android 4.4 / API 19).
set -euo pipefail
cd "$(dirname "$0")"

SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}
NDK=${ANDROID_NDK_HOME:-$SDK/ndk/25.2.9519653}
BT=$SDK/build-tools/34.0.0
PLATFORM=$SDK/platforms/android-33/android.jar
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
GLUE=$NDK/sources/android/native_app_glue
OUT=build
APK=$OUT/NyanVsDomo.apk
KEYSTORE=${KEYSTORE:-$HOME/.android/debug.keystore}

for f in "$BT/aapt2" "$PLATFORM" "$TC/clang" "$GLUE/android_native_app_glue.c"; do
    [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

rm -rf "$OUT/apk" "$OUT/lib"
mkdir -p "$OUT/apk"

# ---- native code: 32-bit ARM for old phones (API 19), 64-bit ARM for new ones ----
build_abi() {   # abi  clang-target
    local abi=$1 target=$2 dir=$OUT/lib/$1
    mkdir -p "$dir" "$OUT/apk/lib/$abi"
    echo "[cc] $abi"
    "$TC/clang" --target="$target" -O2 -Wall -fPIC -ffunction-sections -fdata-sections \
        -I"$GLUE" -Isrc ${CFLAGS:-} \
        -c "$GLUE/android_native_app_glue.c" -o "$dir/glue.o"
    "$TC/clang" --target="$target" -O2 -Wall -fPIC -ffunction-sections -fdata-sections \
        -I"$GLUE" -Isrc ${CFLAGS:-} \
        -shared -o "$OUT/apk/lib/$abi/libnyanvsdomo.so" \
        src/*.c "$dir/glue.o" \
        -u ANativeActivity_onCreate -Wl,--gc-sections -Wl,-z,max-page-size=16384 \
        -landroid -llog -lEGL -lGLESv2 -lOpenSLES -lm
    "$TC/llvm-strip" --strip-unneeded "$OUT/apk/lib/$abi/libnyanvsdomo.so"
}
build_abi armeabi-v7a armv7a-linux-androideabi19
build_abi arm64-v8a   aarch64-linux-android21

# ---- resources + manifest ----
echo "[aapt2] resources"
"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/unsigned.apk" -I "$PLATFORM" \
    --manifest AndroidManifest.xml --min-sdk-version 19 --target-sdk-version 33 \
    "$OUT/res.zip"

# ---- add native libs, align, sign ----
(cd "$OUT/apk" && zip -q -r ../unsigned.apk lib)
"$BT/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

if [ ! -f "$KEYSTORE" ]; then
    echo "[keytool] creating debug keystore $KEYSTORE"
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android \
    --min-sdk-version 19 --out "$APK" "$OUT/aligned.apk"
rm -f "$OUT/unsigned.apk" "$OUT/aligned.apk" "$OUT/aligned.apk.idsig" "$APK.idsig"
echo "[ok] $APK ($(du -h "$APK" | cut -f1))"

if [ "${1:-}" = install ]; then
    adb install -r "$APK"
    adb shell am start -n com.mcl0v1n.nyanvsdomo/android.app.NativeActivity
fi
