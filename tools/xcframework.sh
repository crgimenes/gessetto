#!/bin/sh
# Builds swift/Frameworks/CGessetto.xcframework: the C core for macOS, iOS
# and the iOS simulator, so one Swift package serves all three. Swift sees
# gessetto.h only.
# usage: tools/xcframework.sh <cc> <c-flags> -- <sources...>
set -eu

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "usage: tools/xcframework.sh <cc> <c-flags> -- <sources...>"
    echo "Writes swift/Frameworks/CGessetto.xcframework (macOS, iOS, iOS simulator)."
    exit 0
fi

cc=$1
flags=$2
shift 3

out=build/xc
rm -rf "$out" swift/Frameworks/CGessetto.xcframework
mkdir -p "$out/include" swift/Frameworks
cp gessetto.h "$out/include/"
cat >"$out/include/module.modulemap" <<'MAP'
module CGessetto {
    header "gessetto.h"
    export *
}
MAP

# The Mac's slice is universal: macos (arm64) and macos-x86 are built apart
# and joined by lipo below.
args=""
for slice in macos:macosx:arm64-apple-macos14.0 macos-x86:macosx:x86_64-apple-macos14.0 \
    ios:iphoneos:arm64-apple-ios17.0 ios-sim:iphonesimulator:arm64-apple-ios17.0-simulator; do
    name=${slice%%:*}
    rest=${slice#*:}
    sdk=${rest%%:*}
    target=${rest#*:}
    mkdir -p "$out/$name/obj"
    sysroot=$(xcrun --sdk "$sdk" --show-sdk-path)
    for src in "$@"; do
        obj="$out/$name/obj/$(basename "$src" .c).o"
        # shellcheck disable=SC2086 # flags is a list on purpose
        $cc $flags -target "$target" -isysroot "$sysroot" -c "$src" -o "$obj"
    done
    ar rcs "$out/$name/libgessetto.a" "$out/$name"/obj/*.o
    case "$name" in
    macos) ;;
    macos-x86)
        mkdir -p "$out/mac"
        lipo -create -output "$out/mac/libgessetto.a" "$out/macos/libgessetto.a" \
            "$out/macos-x86/libgessetto.a"
        args="$args -library $out/mac/libgessetto.a -headers $out/include"
        ;;
    *) args="$args -library $out/$name/libgessetto.a -headers $out/include" ;;
    esac
done
# shellcheck disable=SC2086 # args is a list on purpose
xcodebuild -create-xcframework $args -output swift/Frameworks/CGessetto.xcframework >/dev/null
echo "swift/Frameworks/CGessetto.xcframework"
