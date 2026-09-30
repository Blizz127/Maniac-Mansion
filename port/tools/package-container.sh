#!/bin/sh
# Build the release tarball inside the Ubuntu 22.04 container
# (port/dist/Dockerfile.build) for a low glibc requirement.
#   port/tools/package-container.sh [TAG] [OUTDIR]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=$(realpath -m "${2:-$repo/build/release}")
mkdir -p "$out"
sha=$(git -C "$repo" rev-parse --verify -q --short=8 HEAD 2>/dev/null || echo dev)
commit=$(git -C "$repo" rev-parse --verify -q HEAD 2>/dev/null || echo unknown)
tag=${1:-mm-r0-$sha}
docker build -q -t mm-port-build -f "$repo/port/dist/Dockerfile.build" "$repo/port/dist" >/dev/null
# Only port/ is mounted (read-only); no ROM or other repo content enters the container.
docker run --rm -u "$(id -u):$(id -g)" \
    -v "$repo/port:/src/port:ro" -v "$out:/out" \
    -e MM_COMMIT="$commit" -e MM_GATE_JSON="${MM_GATE_JSON:-{\}}" \
    mm-port-build sh -c "cp -r /src/port /tmp/repo-port && mkdir -p /tmp/repo && mv /tmp/repo-port /tmp/repo/port && sh /tmp/repo/port/tools/package.sh '$tag' /out"
