#!/bin/sh
# build-apk.sh
# Zero-Gradle standalone builder for Googlebook Slot B Restore Android APK.
# Optionally bundles ../slotb-backup/*.img directly into APK assets if --bundle-images is passed.

set -eu

APP_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
SDK_CACHE="${ANDROID_HOME:-/tmp/android-sdk-cache}"
BUILD_TOOLS_JAR_DIR="$SDK_CACHE/android-11"
BUILD_TOOLS_BIN_DIR="$SDK_CACHE/android-15"
PLATFORM_JAR="$SDK_CACHE/android-12/android.jar"
WORK_DIR="/tmp/googlebook-apk-build"
OUT_APK="$APP_DIR/bin/googlebook-slotb-restore.apk"
BUNDLE_IMAGES=0

if [ "${1:-}" = "--bundle-images" ]; then
  BUNDLE_IMAGES=1
fi

mkdir -p "$SDK_CACHE" "$APP_DIR/bin"
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/classes" "$WORK_DIR/assets/slotb-backup"

if [ ! -x "$BUILD_TOOLS_JAR_DIR/d8" ]; then
  echo "[build-apk] Downloading Android build-tools 30.0.3 (Java 8 d8/apksigner)..."
  curl -sSL https://dl.google.com/android/repository/f6d24b187cc6bd534c6c37604205171784ac5621.build-tools_r30.0.3-macosx.zip -o "$SDK_CACHE/bt30.zip"
  unzip -q -o "$SDK_CACHE/bt30.zip" -d "$SDK_CACHE"
  rm -f "$SDK_CACHE/bt30.zip"
fi

if [ ! -x "$BUILD_TOOLS_BIN_DIR/aapt2" ]; then
  echo "[build-apk] Downloading Android build-tools 35.0.1 (arm64 aapt2/zipalign)..."
  curl -sSL https://dl.google.com/android/repository/build-tools_r35.0.1_macosx.zip -o "$SDK_CACHE/bt35.zip"
  unzip -q -o "$SDK_CACHE/bt35.zip" -d "$SDK_CACHE"
  rm -f "$SDK_CACHE/bt35.zip"
fi

if [ ! -f "$PLATFORM_JAR" ]; then
  echo "[build-apk] Downloading Android platform-32 android.jar..."
  curl -sSL https://dl.google.com/android/repository/platform-32_r01.zip -o "$SDK_CACHE/pf.zip"
  unzip -q -o "$SDK_CACHE/pf.zip" "android-12/android.jar" -d "$SDK_CACHE"
  rm -f "$SDK_CACHE/pf.zip"
fi

if [ ! -f "$SDK_CACHE/ecj.jar" ]; then
  echo "[build-apk] Downloading Eclipse Java Compiler (ecj.jar)..."
  curl -sSL https://repo1.maven.org/maven2/org/eclipse/jdt/ecj/3.21.0/ecj-3.21.0.jar -o "$SDK_CACHE/ecj.jar"
fi

AAPT2="$BUILD_TOOLS_BIN_DIR/aapt2"
D8="$BUILD_TOOLS_JAR_DIR/d8"
ZIPALIGN="$BUILD_TOOLS_BIN_DIR/zipalign"
APKSIGNER="$BUILD_TOOLS_JAR_DIR/apksigner"
ECJ_JAR="$SDK_CACHE/ecj.jar"

if [ "$BUNDLE_IMAGES" = "1" ] && [ -d "$APP_DIR/../slotb-backup" ]; then
  for f in boot_b.img init_boot_b.img vendor_boot_b.img vbmeta_b.img; do
    if [ -f "$APP_DIR/../slotb-backup/$f" ]; then
      echo "[build-apk] Bundling $f into APK assets..."
      cp -f "$APP_DIR/../slotb-backup/$f" "$WORK_DIR/assets/slotb-backup/$f"
    fi
  done
fi

echo "[build-apk] Compiling resources with aapt2..."
"$AAPT2" compile --dir "$APP_DIR/res" -o "$WORK_DIR/res.zip"
"$AAPT2" link \
  -I "$PLATFORM_JAR" \
  --manifest "$APP_DIR/AndroidManifest.xml" \
  -A "$WORK_DIR/assets" \
  -0 img \
  --java "$WORK_DIR/src_gen" \
  -o "$WORK_DIR/unaligned.apk" \
  "$WORK_DIR/res.zip"

echo "[build-apk] Compiling Java sources with ecj..."
java -jar "$ECJ_JAR" -1.8 -nowarn \
  -bootclasspath "$PLATFORM_JAR" \
  -classpath "$PLATFORM_JAR" \
  -d "$WORK_DIR/classes" \
  "$WORK_DIR/src_gen/com/fixitfoundry/googlebookrestore/R.java" \
  "$APP_DIR/src/com/fixitfoundry/googlebookrestore/BootImages.java" \
  "$APP_DIR/src/com/fixitfoundry/googlebookrestore/MainActivity.java"

echo "[build-apk] Converting bytecode to DEX with d8..."
CLASS_FILES=$(find "$WORK_DIR/classes" -name "*.class")
"$D8" --lib "$PLATFORM_JAR" --output "$WORK_DIR" $CLASS_FILES

(cd "$WORK_DIR" && zip -q -u unaligned.apk classes.dex)

echo "[build-apk] Aligning and signing APK..."
"$ZIPALIGN" -f -p 4 "$WORK_DIR/unaligned.apk" "$WORK_DIR/aligned.apk"

KEYSTORE="$SDK_CACHE/debug.keystore"
if [ ! -f "$KEYSTORE" ]; then
  keytool -genkeypair -v \
    -keystore "$KEYSTORE" \
    -storepass android \
    -alias androiddebugkey \
    -keypass android \
    -keyalg RSA \
    -keysize 2048 \
    -validity 10000 \
    -dname "CN=FixItFoundry,O=Android,C=US" >/dev/null 2>&1
fi

"$APKSIGNER" sign \
  --ks "$KEYSTORE" \
  --ks-pass pass:android \
  --key-pass pass:android \
  --out "$OUT_APK" \
  "$WORK_DIR/aligned.apk"

rm -rf "$WORK_DIR"
ls -lh "$OUT_APK"
echo "[build-apk] Built signed APK: $OUT_APK"
