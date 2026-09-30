#!/bin/sh
# Produce reference frame hashes for a trace with a reference emulator.
#   port/tools/ref-run.sh fceux|mesen TRACE.mmin OUT.txt [ROM]
# Emulator homes/configs live under build/port-ref/ (never the user's own).
# Env: FCEUX (default .tools/fceux/usr/games/fceux), MESEN (default
# ~/.local/opt/mesen2/Mesen-src, then ~/.local/opt/mesen2/Mesen).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
emu=$1 trace=$(realpath "$2") out=$3 rom=$(realpath "${4:-$repo/rom/original.nes}")
work=$repo/build/port-ref
mkdir -p "$work"
out=$(realpath -m "$out")

case $emu in
fceux)
    fceux=${FCEUX:-$repo/.tools/fceux/usr/games/fceux}
    libs=$(dirname "$fceux")/../lib/x86_64-linux-gnu
    raw=$work/fceux-$$.raw
    mkdir -p "$work/fceux-home"
    HOME=$work/fceux-home LD_LIBRARY_PATH=$libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} \
        QT_PLUGIN_PATH=$libs/qt5/plugins MM_RAW=$raw MM_INPUT=$trace \
        xvfb-run -a "$fceux" --no-config 1 --sound 0 --loadlua "$here/ref-fceux.lua" "$rom" \
        >"$work/fceux.log" 2>&1 || true
    python3 "$here/framehash.py" raw "$raw" --index-only >"$out"
    rm -f "$raw"
    ;;
mesen)
    mesen=${MESEN:-}
    [ -n "$mesen" ] || for m in "$HOME/.local/opt/mesen2/Mesen-src" "$HOME/.local/opt/mesen2/Mesen"; do
        [ -x "$m" ] && mesen=$m && break
    done
    home=$work/mesen-home
    mkdir -p "$home/.config/Mesen2"
    [ -f "$home/.config/Mesen2/settings.json" ] || cp "$here/mesen-settings.json" "$home/.config/Mesen2/settings.json"
    raw=$work/mesen-$$.raw
    HOME=$home XDG_CONFIG_HOME=$home/.config SDL_AUDIODRIVER=dummy \
        MM_RAW=$raw MM_INPUT=$trace MM_RAMDUMP="${MM_RAMDUMP:-}" \
        xvfb-run -a "$mesen" --testRunner "$here/ref-mesen.lua" "$rom" --timeout=36000 >"$work/mesen.log" 2>&1 || true
    python3 "$here/framehash.py" raw "$raw" --bpp 2 >"$out"
    rm -f "$raw"
    ;;
*)
    echo "usage: $0 fceux|mesen TRACE.mmin OUT.txt [ROM]" >&2
    exit 2
    ;;
esac
echo "$(grep -vc '^#' "$out") frames -> $out"
