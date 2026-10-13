#!/bin/sh
# Builds gessetto for the App Store: the iPad app signed with the Apple
# Distribution certificate and an App Store provisioning profile, packed as
# build/gessetto.ipa. Nothing is sent; uploading is a separate step.
# Beside it: the dSYM zipped and a manifest of the commits and hashes it
# came from. Adapted from fosforo's tools/ios-store.sh.
set -eu

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "usage: PROFILE=f tools/ios-store.sh"
    echo "Writes build/gessetto.ipa for App Store Connect (TestFlight, review), with"
    echo "build/gessetto.dSYM.zip and build/gessetto.ipa.manifest beside it."
    echo "  PROFILE=f   the App Store provisioning profile (required)"
    echo "  BUILD=n     CFBundleVersion (default: commits on HEAD; must grow per upload)"
    echo "example: PROFILE=~/.appstoreconnect/profiles/gessetto.mobileprovision tools/ios-store.sh"
    exit 0
fi

profile=${PROFILE:?PROFILE: the App Store provisioning profile (see -h)}
out=build/store
app=$out/Payload/gessetto.app
ipa=build/gessetto.ipa
dsym=build/gessetto.dSYM.zip
manifest=$ipa.manifest
last=$(sed -n 's/^build //p' "$manifest" 2>/dev/null || true)
rm -rf "$out" "$ipa" "$dsym"
mkdir -p "$out"

fail() {
    echo "ios-store: $1" >&2
    exit 1
}

security cms -D -i "$profile" >"$out/profile.plist"
pb() { /usr/libexec/PlistBuddy -c "Print :$1" "$out/profile.plist" 2>/dev/null; }
if pb ProvisionedDevices >/dev/null || [ "$(pb Entitlements:get-task-allow)" != "false" ]; then
    fail "$profile is not an App Store profile (it lists devices or allows debugging)"
fi
bundle=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" assets/Info-iOS.plist)
team=$(pb TeamIdentifier:0)
[ "$(pb Entitlements:application-identifier)" = "$team.$bundle" ] || fail "the profile is not for $bundle"
cat >"$out/entitlements.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>application-identifier</key><string>$team.$bundle</string>
  <key>com.apple.developer.team-identifier</key><string>$team</string>
  <key>get-task-allow</key><false/>
  <key>beta-reports-active</key><true/>
</dict>
</plist>
EOF

# Coverage off: the package scheme's Release build otherwise carries the
# profiler's counters.
(cd swift && xcodebuild -scheme Gessetto -destination 'generic/platform=iOS' ARCHS=arm64 \
    -configuration Release -derivedDataPath ../build/dd-store CODE_SIGNING_ALLOWED=NO \
    PRODUCT_BUNDLE_IDENTIFIER="$bundle" CLANG_ENABLE_CODE_COVERAGE=NO ENABLE_CODE_COVERAGE=NO \
    -quiet build)
mkdir -p "$app"
products=build/dd-store/Build/Products/Release-iphoneos
sdk=$(xcrun --sdk iphoneos --show-sdk-version)
minos=$(/usr/libexec/PlistBuddy -c "Print :MinimumOSVersion" assets/Info-iOS.plist)
# The link of the package's executable records the deployment target as its
# SDK, and App Store Connect refuses an old SDK: only that record is
# rewritten, after checking the objects were compiled against this SDK.
for o in "$products"/GessettoKit*.o; do
    built=$(xcrun vtool -show-build "$o" | awk '$1 == "sdk" { print $2; exit }')
    [ "$built" = "$sdk" ] || fail "$o was built against SDK $built, not $sdk"
done
xcrun vtool -set-build-version ios "$minos" "$sdk" -replace \
    -output "$app/gessetto" "$products/gessetto"
[ "$(lipo -archs "$app/gessetto")" = arm64 ] || fail "not arm64 only"
shown=$(xcrun vtool -show-build "$app/gessetto")
echo "$shown" | grep -q "minos $minos" || fail "minos is not $minos"
echo "$shown" | grep -q "sdk $sdk" || fail "sdk is not $sdk"
if otool -l "$app/gessetto" | grep -q __llvm_prf; then fail "coverage counters in the binary"; fi
uuid=$(dwarfdump --uuid "$app/gessetto" | awk '{ print $2 }')
[ "$(dwarfdump --uuid "$products/gessetto.dSYM" | awk '{ print $2 }')" = "$uuid" ] ||
    fail "the dSYM is not this binary's"
ditto -c -k --keepParent "$products/gessetto.dSYM" "$dsym"

plist=$app/Info.plist
cp assets/Info-iOS.plist "$plist"
set_key() { /usr/libexec/PlistBuddy -c "Set :$1 $2" "$plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :$1 string $2" "$plist"; }
/usr/libexec/PlistBuddy -c "Set :CFBundleSupportedPlatforms:0 iPhoneOS" "$plist"
set_key CFBundleVersion "${BUILD:-$(git rev-list --count HEAD)}"

# What Xcode writes into every bundle it builds; App Store Connect reads it
# to know the toolchain and SDK.
xcode=$(dirname "$(xcode-select -p)")
sdk_build=$(xcrun --sdk iphoneos --show-sdk-build-version)
set_key DTPlatformName iphoneos
set_key DTPlatformVersion "$sdk"
set_key DTPlatformBuild "$sdk_build"
set_key DTSDKName "iphoneos$sdk"
set_key DTSDKBuild "$sdk_build"
set_key DTXcode "$(plutil -extract DTXcode raw "$xcode/Info.plist")"
set_key DTXcodeBuild "$(plutil -extract ProductBuildVersion raw "$xcode/version.plist")"
set_key DTCompiler com.apple.compilers.llvm.clang.1_0
set_key BuildMachineOSBuild "$(sw_vers -buildVersion)"

xcrun actool assets/gessetto.icon --compile "$app" --platform iphoneos \
    --minimum-deployment-target "$minos" --target-device ipad --app-icon gessetto \
    --output-partial-info-plist "$out/icon.plist" --output-format human-readable-text >"$out/actool.txt"
/usr/libexec/PlistBuddy -c "Merge $out/icon.plist" "$plist"
plutil -lint "$plist" >/dev/null

cp assets/PrivacyInfo.xcprivacy "$app/"
cp "$profile" "$app/embedded.mobileprovision"
codesign --force --sign "Apple Distribution: $(pb TeamName) ($team)" \
    --entitlements "$out/entitlements.plist" --generate-entitlement-der --timestamp=none "$app"
codesign --verify --strict --deep "$app"
codesign -d --entitlements - --xml "$app" 2>/dev/null | plutil -extract get-task-allow raw - |
    grep -qx false || fail "debuggable (get-task-allow)"
(cd "$out" && zip -qr -X ../gessetto.ipa Payload)
version=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$plist")
build=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$plist")
if [ -n "$last" ] && [ "$build" -le "$last" ]; then
    echo "ios-store: build $build is not above the last one made here ($last): set BUILD to upload it" >&2
fi
{
    echo "version $version"
    echo "build $build"
    echo "sdk $sdk"
    echo "uuid $uuid"
    tools/manifest.sh "$ipa" "$dsym"
} >"$manifest"
echo "$ipa: $bundle $version ($build); $dsym, $manifest"
