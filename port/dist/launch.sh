#!/bin/sh
# Maniac Mansion (NES) native port launcher.
#   launch.sh [ROM.nes] [extra maniac-mansion-port args...]
#   launch.sh --check-rom [ROM.nes]     validate only; prints MM_ROM_OK/MM_ROM_ERROR
# The ROM is your own Maniac Mansion (USA) dump, checked by SHA-256. It is
# taken from arg 1, else $MM_ROM, else the last ROM that passed the check,
# else ${XDG_DATA_HOME:-~/.local/share}/maniac-mansion-port/rom.nes.
# Battery saves: ${XDG_DATA_HOME:-~/.local/share}/maniac-mansion-port/saves/
# Logs, screenshots: ${XDG_STATE_HOME:-~/.local/state}/maniac-mansion-port/
# Exit codes: 0 ok, 1 runtime error, 2 no ROM, 3 unreadable, 4 not an NES
# file, 5 not the supported dump (see docs/port-package.md).
set -eu
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

check=""
if [ "${1:-}" = "--check-rom" ]; then
    check="--check-rom"
    shift
fi
if [ -n "${1:-}" ] && [ "${1#-}" = "$1" ]; then
    rom="$1"
    shift
    exec "$HERE/bin/maniac-mansion-port" $check --rom "$rom" "$@"
fi
if [ -n "${MM_ROM:-}" ]; then
    exec "$HERE/bin/maniac-mansion-port" $check --rom "$MM_ROM" "$@"
fi
exec "$HERE/bin/maniac-mansion-port" $check "$@"
