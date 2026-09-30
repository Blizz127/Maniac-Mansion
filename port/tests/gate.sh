#!/bin/sh
# Port gate: every check a release must pass. Prints a summary and writes
# build/port-gate.json (used as build-info.json "gate" by package.sh).
#   port/tests/gate.sh [ROM]
# Reference hashes (build/port-ref/*.mesen.txt) and test ROMs
# (build/port-tests/) are local and optional; missing ones are reported as
# "skipped", never as passed.
set -u
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
rom=${1:-$repo/rom/original.nes}
b=$repo/build/port
ref=$repo/build/port-ref
tests=$repo/build/port-tests
out=$repo/build/port-gate
mkdir -p "$out"
cmake -S "$repo/port" -B "$b" >/dev/null && cmake --build "$b" -j"$(nproc)" >/dev/null || { echo "gate: build FAILED"; exit 1; }
H=$b/mm-headless
json="{"
sep=""
add() { json="$json$sep\"$1\":\"$2\""; sep=","; echo "$1: $2"; }

if [ -f "$tests/nestest.nes" ]; then
    $H --rom "$tests/nestest.nes" --allow-unknown --nestest-log "$tests/nestest.log" >"$out/nestest.txt" 2>&1 &&
        add nestest "pass $(grep -o '[0-9]* instructions' "$out/nestest.txt")" || add nestest FAIL
    for t in all_instrs official_only instr_misc instr_timing cpu_interrupts ppu_vbl_nmi oam_read apu_test cpu_dummy_writes_oam; do
        [ -f "$tests/$t.nes" ] || continue
        $H --rom "$tests/$t.nes" --allow-unknown --blargg --frames 4000 >"$out/$t.txt" 2>&1 && add "$t" pass || add "$t" FAIL
    done
else
    add cpu_tests skipped
fi
"$b/test-pads" >"$out/pads.txt" 2>&1 && add pads_4a pass || add pads_4a FAIL
for t in boot smoke newgame; do
    [ -f "$repo/port/traces/$t.mmin" ] || continue
    $H --rom "$rom" --input "$repo/port/traces/$t.mmin" --hashes "$out/$t.txt" >/dev/null 2>&1
    if [ -s "$ref/$t.mesen.txt" ]; then
        r=$(python3 "$repo/port/tools/framehash.py" compare "$out/$t.txt" "$ref/$t.mesen.txt" | head -1)
        case $r in *" 0 differ"*) add "mesen_$t" "pass ${r#compared }" ;; *) add "mesen_$t" "FAIL ${r#compared }" ;; esac
    else
        add "mesen_$t" skipped
    fi
done
if command -v xvfb-run >/dev/null; then
    "$here/test_devmenu.sh" "$b" "$rom" "$out/devmenu" >"$out/devmenu.txt" 2>&1 && add devmenu "pass $(grep -c PASS "$out/devmenu.txt") checks" || add devmenu FAIL
else
    add devmenu skipped
fi
json="$json}"
echo "$json" >"$repo/build/port-gate.json"
case $json in *FAIL*) echo "gate: FAILED"; exit 1 ;; esac
echo "gate: passed (see build/port-gate.json)"
