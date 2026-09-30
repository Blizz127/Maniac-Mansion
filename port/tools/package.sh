#!/bin/sh
# Build the Linux x86_64 release tarball (docs/port-package.md).
#   port/tools/package.sh [TAG] [OUTDIR]
# TAG defaults to mm-r0-<sha8 of HEAD> (or mm-r0-dev); OUTDIR to build/release.
# Game data is never packaged; the script refuses if any .nes/.sav/.raw
# file ends up in the staging directory.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
sha=$(git -C "$repo" rev-parse --verify -q --short=8 HEAD 2>/dev/null || echo dev)
commit=${MM_COMMIT:-$(git -C "$repo" rev-parse --verify -q HEAD 2>/dev/null || echo unknown)}
tag=${1:-mm-r0-$sha}
out=$(realpath -m "${2:-$repo/build/release}")
name=maniac-mansion-port-$tag
stage=$out/$name
bld=$repo/build/port-release

cmake -S "$repo/port" -B "$bld" -DCMAKE_BUILD_TYPE=Release -DMM_VERSION="$tag" >/dev/null
cmake --build "$bld" -j"$(nproc)" --target maniac-mansion-port mm-headless test-pads >/dev/null
"$bld/test-pads" >/dev/null

rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/lib"
cp "$bld/maniac-mansion-port" "$stage/bin/"
strip "$stage/bin/maniac-mansion-port"
cp "$repo/port/dist/launch.sh" "$stage/launch.sh"
chmod 755 "$stage/launch.sh" "$stage/bin/maniac-mansion-port"
cp "$repo/port/dist/config.example.ini" "$repo/port/dist/README.txt" "$repo/port/dist/THIRD_PARTY.md" "$stage/"

sdl=$(ldd "$stage/bin/maniac-mansion-port" | awk '/libSDL2-2.0.so.0/ {print $3}')
[ -n "$sdl" ] || { echo "package: libSDL2 not found via ldd" >&2; exit 1; }
cp -L "$sdl" "$stage/lib/libSDL2-2.0.so.0"

glibc=$(objdump -T "$stage/bin/maniac-mansion-port" "$stage/lib/libSDL2-2.0.so.0" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "$tag" >"$stage/version.txt"
cat >"$stage/build-info.json" <<EOF
{
  "name": "maniac-mansion-port",
  "build_id": "$tag",
  "commit": "$commit",
  "built_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "platform": "linux-x86_64",
  "glibc_min": "$glibc",
  "bundled_libs": ["libSDL2-2.0.so.0"],
  "contains_game_data": false,
  "rom": {
    "name": "Maniac Mansion (USA)",
    "size": 262160,
    "sha256": "e59f95a80497779b861daa26e1b890929fd2f9939e78003898d9bff3ea3f6db2",
    "prg_sha256": "84f5377980d2fd44d71faec42f858b1e83540c2f55aba9236c3279d6dde8592a"
  },
  "gate": ${MM_GATE_JSON:-{\}}
}
EOF

if find "$stage" -iname '*.nes' -o -iname '*.sav' -o -iname '*.raw' -o -iname '*.zip' | grep -q .; then
    echo "package: refusing, game or save data in $stage" >&2
    exit 1
fi
(cd "$stage" && find . -type f ! -name MANIFEST | sort | xargs sha256sum >MANIFEST)
tar -C "$out" --owner=0 --group=0 -czf "$out/$name-linux-x86_64.tar.gz" "$name"
(cd "$out" && sha256sum "$name-linux-x86_64.tar.gz" >SHA256SUMS)
echo "$out/$name-linux-x86_64.tar.gz"
cat "$out/SHA256SUMS"
