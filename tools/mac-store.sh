#!/bin/sh
# Sends the Mac app to App Store Connect (TestFlight, the Mac App Store):
# the universal, sandboxed bundle make app builds, its recorded SDK fixed,
# put in an .xcarchive and exported with automatic signing, so Xcode signs
# it with the account's cloud-managed distribution certificates and makes
# the Mac App Store profile itself.
set -eu

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "usage: tools/mac-store.sh"
    echo "Uploads build/gessetto.app (from make app) to App Store Connect."
    echo "  BUILD=n   CFBundleVersion (default: commits on HEAD; must grow per upload)"
    echo "example: make app && BUILD=15 tools/mac-store.sh"
    exit 0
fi

fail() {
    echo "mac-store: $1" >&2
    exit 1
}

src=build/gessetto.app
[ -d "$src" ] || fail "no $src; run make app first"
work=build/mac-store
archive=$work/gessetto.xcarchive
app=$archive/Products/Applications/gessetto.app
rm -rf "$work"
mkdir -p "$archive/Products/Applications" "$archive/dSYMs"
ditto "$src" "$app"
products=$(cd swift && swift build -c release --arch arm64 --arch x86_64 --show-bin-path)
ditto "$products/gessetto.dSYM" "$archive/dSYMs/gessetto.dSYM"

# The package's link records the deployment target as its SDK, and App
# Store Connect refuses an old SDK: only that record is rewritten, after
# checking the objects were compiled against this SDK.
sdk=$(xcrun --sdk macosx --show-sdk-version)
minos=$(/usr/libexec/PlistBuddy -c "Print :LSMinimumSystemVersion" assets/Info.plist)
for arch in arm64 x86_64; do
    built=$(xcrun vtool -arch "$arch" -show-build "$products/GessettoKit.o" |
        awk '$1 == "sdk" { print $2; exit }')
    [ "$built" = "$sdk" ] || fail "GessettoKit.o ($arch) was built against SDK $built, not $sdk"
done
bin=$app/Contents/MacOS/gessetto
xcrun vtool -set-build-version macos "$minos" "$sdk" -replace -output "$bin.fixed" "$bin"
mv "$bin.fixed" "$bin"
[ "$(lipo -archs "$bin")" = "x86_64 arm64" ] || fail "not universal"
if otool -l "$bin" | grep -q __llvm_prf; then fail "coverage counters in the binary"; fi

plist=$app/Contents/Info.plist
set_key() { /usr/libexec/PlistBuddy -c "Set :$1 $2" "$plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :$1 string $2" "$plist"; }
set_key CFBundleVersion "${BUILD:-$(git rev-list --count HEAD)}"
xcode=$(dirname "$(xcode-select -p)")
sdk_build=$(xcrun --sdk macosx --show-sdk-build-version)
set_key DTPlatformName macosx
set_key DTPlatformVersion "$sdk"
set_key DTPlatformBuild "$sdk_build"
set_key DTSDKName "macosx$sdk"
set_key DTSDKBuild "$sdk_build"
set_key DTXcode "$(plutil -extract DTXcode raw "$xcode/Info.plist")"
set_key DTXcodeBuild "$(plutil -extract ProductBuildVersion raw "$xcode/version.plist")"
set_key DTCompiler com.apple.compilers.llvm.clang.1_0
set_key BuildMachineOSBuild "$(sw_vers -buildVersion)"
plutil -lint "$plist" >/dev/null
codesign --force --sign - --entitlements assets/gessetto.entitlements "$app"

pb() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist"; }
bundle=$(pb CFBundleIdentifier)
version=$(pb CFBundleShortVersionString)
build=$(pb CFBundleVersion)
team=LLGRBEQ6QP
cat >"$archive/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>ArchiveVersion</key><integer>2</integer>
  <key>Name</key><string>gessetto</string>
  <key>SchemeName</key><string>gessetto</string>
  <key>CreationDate</key><date>$(date -u +%Y-%m-%dT%H:%M:%SZ)</date>
  <key>ApplicationProperties</key>
  <dict>
    <key>ApplicationPath</key><string>Applications/gessetto.app</string>
    <key>Architectures</key><array><string>arm64</string><string>x86_64</string></array>
    <key>CFBundleIdentifier</key><string>$bundle</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$build</string>
    <key>Team</key><string>$team</string>
  </dict>
</dict>
</plist>
EOF
cat >"$work/export.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>method</key><string>app-store-connect</string>
  <key>destination</key><string>upload</string>
  <key>signingStyle</key><string>automatic</string>
  <key>teamID</key><string>$team</string>
  <key>manageAppVersionAndBuildNumber</key><false/>
  <key>uploadSymbols</key><true/>
</dict>
</plist>
EOF
xcodebuild -exportArchive -archivePath "$archive" -exportOptionsPlist "$work/export.plist" \
    -exportPath "$work/out" -allowProvisioningUpdates
echo "mac-store: $bundle $version ($build) sent"
