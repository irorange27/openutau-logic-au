#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=${OPENUTAU_SOURCE:?Set OPENUTAU_SOURCE to a checkout of OpenUtau 0.1.572.1-alpha.}
official_app=${OPENUTAU_OFFICIAL_APP:-/Applications/OpenUtau.app}
output_app=${OPENUTAU_PINCH_APP:?Set OPENUTAU_PINCH_APP to a new .app path.}
dotnet=${DOTNET:-dotnet}
patch="$repo/patches/openutau-piano-roll-pinch-0.1.572.1-alpha.patch"

test "$(uname -m)" = arm64 || { echo 'Build on Apple Silicon.' >&2; exit 1; }
test "$(git -C "$source_dir" rev-parse HEAD)" = ca49f0640a5eea783a34adafdfc440ca73222084 || {
    echo 'Expected official OpenUtau 0.1.572.1-alpha source.' >&2
    exit 1
}
test ! -e "$output_app" || { echo "Output already exists: $output_app" >&2; exit 1; }
test "$(shasum -a 256 "$official_app/Contents/MacOS/OpenUtau.dll" | awk '{print $1}')" = \
    a6738f156684a3715c0eaff5d9d5e6a72ab3198f63efab24dfdded3df57bee96 || {
    echo 'Expected unmodified official OpenUtau 0.1.572.1-alpha app.' >&2
    exit 1
}

if git -C "$source_dir" apply --check "$patch" 2>/dev/null; then
    git -C "$source_dir" apply "$patch"
elif ! git -C "$source_dir" apply --reverse --check "$patch" 2>/dev/null; then
    echo 'Source differs from the expected patch state.' >&2
    exit 1
fi

guide_patch="$repo/patches/openutau-logic-guide-0.1.572.1-alpha.patch"
if git -C "$source_dir" apply --check "$guide_patch" 2>/dev/null; then
    git -C "$source_dir" apply "$guide_patch"
elif ! git -C "$source_dir" apply --reverse --check "$guide_patch" 2>/dev/null; then
    echo 'Source differs from the expected Logic guide patch state.' >&2
    exit 1
fi

timing_patch="$repo/patches/openutau-diffsinger-phrase-lead-0.1.572.1-alpha.patch"
if git -C "$source_dir" apply --check "$timing_patch" 2>/dev/null; then
    git -C "$source_dir" apply "$timing_patch"
elif ! git -C "$source_dir" apply --reverse --check "$timing_patch" 2>/dev/null; then
    echo 'Source differs from the expected phrase timing patch state.' >&2
    exit 1
fi

(
    cd "$source_dir"
    "$dotnet" build OpenUtau -c Release -r osx-arm64 \
        -p:Version=0.1.572.1 -p:AssemblyVersion=0.1.572.1 -p:FileVersion=0.1.572.1 \
        -v:quiet
)

ditto "$official_app" "$output_app"
ditto "$source_dir/OpenUtau/bin/Release/net10.0/osx-arm64/OpenUtau.dll" \
    "$output_app/Contents/MacOS/OpenUtau.dll"
"$dotnet" run --project "$repo/tools/patch-phrase-timing" -c Release -- \
    "$official_app/Contents/MacOS/OpenUtau.Core.dll" \
    "$source_dir/OpenUtau.Core/bin/Release/net10.0/osx-arm64/OpenUtau.Core.dll" \
    "$output_app/Contents/MacOS/OpenUtau.Core.dll"
clang -dynamiclib -fobjc-arc -fblocks -framework AppKit -arch arm64 \
    ${OPENUTAU_PINCH_DEBUG:+-DOPENUTAU_PINCH_DEBUG} \
    "$repo/native/openutau_pinch_axis.m" \
    -o "$output_app/Contents/MacOS/libOpenUtauPinchAxis.dylib"
codesign --force --deep --sign - "$output_app"
codesign --verify --deep --strict "$output_app"
echo "Built $output_app"
