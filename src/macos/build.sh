#!/bin/bash
set -euo pipefail

source_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$(cd "$source_dir/../.." && pwd)"
output_dir="${1:-$project_dir/dist/DouNi-Calendar-macOS}"
version="${DOUNI_VERSION:-1.0.0}"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/douni-calendar-build.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT

app="$build_dir/DouNi Calendar.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$source_dir/Info.plist" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $version" "$app/Contents/Info.plist"
cp "$source_dir/AppIcon.icns" "$app/Contents/Resources/AppIcon.icns"
sdk="$(xcrun --sdk macosx --show-sdk-path)"
for architecture in arm64 x86_64; do
    xcrun swiftc -swift-version 5 -O -whole-module-optimization \
        -target "$architecture-apple-macosx13.0" -sdk "$sdk" \
        -framework AppKit -framework ServiceManagement \
        "$source_dir/main.swift" -o "$build_dir/DouNiCalendar-$architecture"
done
xcrun lipo -create "$build_dir/DouNiCalendar-arm64" "$build_dir/DouNiCalendar-x86_64" \
    -output "$app/Contents/MacOS/DouNiCalendar"
xcrun lipo -verify_arch arm64 x86_64 "$app/Contents/MacOS/DouNiCalendar"
codesign --force --sign - "$app"
codesign --verify --strict "$app"
"$app/Contents/MacOS/DouNiCalendar" --self-test
if [[ -e "$output_dir/DouNi Calendar.app" ]]; then
    echo "Output already exists: $output_dir/DouNi Calendar.app" >&2
    exit 1
fi
ditto "$app" "$output_dir/DouNi Calendar.app"
echo "$output_dir/DouNi Calendar.app"
