#!/bin/bash
# Builds fixture .app bundles with known architectures, runs fubcc on them,
# and checks which list each one lands in.
#
# Usage: run_tests.sh <path-to-fubcc>

set -euo pipefail

fubcc="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

failures=0

fail() {
    echo "FAIL: $1"
    failures=$((failures + 1))
}

# make_bundle <bundle-dir> <executable-name> <xml|binary>
# Creates the bundle skeleton; the caller supplies the executable.
make_bundle() {
    mkdir -p "$1/Contents/MacOS"
    cat > "$1/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>$2</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
</dict>
</plist>
EOF
    if [ "$3" = binary ]; then
        plutil -convert binary1 "$1/Contents/Info.plist"
    fi
}

# Thin and fat executables to copy into bundles.
echo 'int main(void) { return 0; }' > "$work/main.c"
xcrun clang -arch x86_64 "$work/main.c" -o "$work/x86_64"
xcrun clang -arch arm64 "$work/main.c" -o "$work/arm64"
lipo -create "$work/x86_64" "$work/arm64" -output "$work/fat"
lipo -create -fat64 "$work/x86_64" "$work/arm64" -output "$work/fat64"
lipo -create "$work/x86_64" -output "$work/fat_x86_64_only"

apps="$work/apps"

# app <parent-dir> <name> <xml|binary> <source-executable>
app() {
    local bundle="$1/$2.app"
    make_bundle "$bundle" "$2" "$3"
    cp "$4" "$bundle/Contents/MacOS/$2"
    chmod +x "$bundle/Contents/MacOS/$2"
}

app "$apps" Intel xml "$work/x86_64"
app "$apps" Arm binary "$work/arm64"
app "$apps" Fat xml "$work/fat"
app "$apps" Fat64 binary "$work/fat64"
app "$apps" FatIntelOnly xml "$work/fat_x86_64_only"

# Nested in a plain folder: should be found by the recursive scan.
app "$apps/Sub" Nested xml "$work/arm64"

# A helper app inside another app's bundle: should not be listed.
app "$apps" Outer xml "$work/arm64"
app "$apps/Outer.app/Contents/Helpers" Inner xml "$work/x86_64"

# Bundles that can't be classified.
make_bundle "$apps/Broken.app" Broken xml
printf 'ab' > "$work/short"
app "$apps" Short xml "$work/short"
echo 'not a binary' > "$work/text"
app "$apps" Text xml "$work/text"

expected="
Intel (x86_64) Binaries:
FatIntelOnly
Intel

Apple Silicon (arm64) Binaries:
Arm
Nested
Outer

Universal Binaries:
Fat
Fat64

Unknown:
Broken
Short
Text"

# Classification, recursion, and skipping nested helper apps.
if actual="$("$fubcc" "$apps" 2>/dev/null)"; then
    [ "$actual" = "$expected" ] ||
        fail "classification: unexpected output"$'\n'"$(diff <(echo "$expected") <(echo "$actual"))"
else
    fail "classification: exited with status $?"
fi

# Overlapping paths list each bundle once.
actual="$("$fubcc" "$apps" "$apps/Arm.app" "$apps/" 2>/dev/null)" || true
[ "$actual" = "$expected" ] || fail "overlapping paths: unexpected output (duplicates?)"

# A single .app bundle can be passed directly.
actual="$("$fubcc" "$apps/Fat.app" 2>/dev/null)" || true
grep -qx Fat <<< "$actual" || fail "single bundle: Fat.app not listed"

# A missing path is reported, the other paths are still scanned, and the
# exit status is 1.
status=0
actual="$("$fubcc" "$work/does-not-exist" "$apps" 2>"$work/stderr")" || status=$?
[ "$status" -eq 1 ] || fail "missing path: expected exit status 1, got $status"
grep -q "does-not-exist" "$work/stderr" || fail "missing path: no error message"
[ "$actual" = "$expected" ] || fail "missing path: unexpected output for the other paths"

if [ "$failures" -gt 0 ]; then
    echo "$failures test(s) failed"
    exit 1
fi
echo "All tests passed"
