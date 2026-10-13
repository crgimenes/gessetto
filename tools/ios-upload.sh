#!/bin/sh
# Sends the build tools/ios-store.sh made to App Store Connect (TestFlight):
# an .xcarchive put together by hand around the signed app, then
# xcodebuild -exportArchive with destination upload, which authenticates as
# the account signed in to Xcode. The recipe is fosforo's.
set -eu

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "usage: PROFILE=f tools/ios-upload.sh"
    echo "Uploads build/store/Payload/gessetto.app (from make ios-store) to App Store Connect."
    echo "  PROFILE=f   the App Store provisioning profile the app was signed with (required)"
    echo "example: PROFILE=~/.appstoreconnect/profiles/gessetto.mobileprovision tools/ios-upload.sh"
    exit 0
fi

profile=${PROFILE:?PROFILE: the App Store provisioning profile (see -h)}
app=build/store/Payload/gessetto.app
[ -d "$app" ] || {
    echo "ios-upload: no $app; run make ios-store first" >&2
    exit 1
}
work=build/upload
archive=$work/gessetto.xcarchive
rm -rf "$work"
mkdir -p "$archive/Products/Applications" "$archive/dSYMs"
ditto "$app" "$archive/Products/Applications/gessetto.app"
ditto -x -k build/gessetto.dSYM.zip "$archive/dSYMs"

pb() { /usr/libexec/PlistBuddy -c "Print :$1" "$app/Info.plist"; }
bundle=$(pb CFBundleIdentifier)
version=$(pb CFBundleShortVersionString)
build=$(pb CFBundleVersion)
security cms -D -i "$profile" >"$work/profile.plist"
uuid=$(/usr/libexec/PlistBuddy -c "Print :UUID" "$work/profile.plist")
name=$(/usr/libexec/PlistBuddy -c "Print :Name" "$work/profile.plist")
team=$(/usr/libexec/PlistBuddy -c "Print :TeamIdentifier:0" "$work/profile.plist")
# exportArchive finds profiles only where Xcode keeps them.
installed="$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/$uuid.mobileprovision"
[ -f "$installed" ] || cp "$profile" "$installed"

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
    <key>CFBundleIdentifier</key><string>$bundle</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$build</string>
    <key>SigningIdentity</key><string>Apple Distribution</string>
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
  <key>signingStyle</key><string>manual</string>
  <key>teamID</key><string>$team</string>
  <key>provisioningProfiles</key>
  <dict><key>$bundle</key><string>$name</string></dict>
  <key>manageAppVersionAndBuildNumber</key><false/>
  <key>uploadSymbols</key><true/>
</dict>
</plist>
EOF
xcodebuild -exportArchive -archivePath "$archive" -exportOptionsPlist "$work/export.plist" \
    -exportPath "$work/out"
echo "ios-upload: $bundle $version ($build) sent"
