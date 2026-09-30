#!/bin/sh
# Dev menu gate (DEV_MENU_SPEC §10), windowed under Xvfb:
#  1. all-off identity: with the opt-in on and the menu opened, navigated and
#     closed during a trace, every game frame hashes the same as a run with
#     the opt-in off (the overlay is host-only; no guest writes);
#  2. hard disable: MM_CHEATS=0 keeps the menu off even with MM_DEV_MENU=1;
#  3. quick save/load restores the exact machine state;
#  4. menu screenshots (root, options, empty group) for evidence.
#   port/tests/test_devmenu.sh BUILD_DIR ROM OUT_DIR
set -eu
bin=$1/maniac-mansion-port rom=$2 out=$3
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
run() { # name env...
    name=$1; shift
    d=$out/$name
    rm -rf "$d" && mkdir -p "$d"
    env XDG_DATA_HOME="$d/data" XDG_STATE_HOME="$d/state" XDG_CONFIG_HOME="$d/config" SDL_AUDIODRIVER=dummy "$@" \
        xvfb-run -a -s "-screen 0 1280x960x24" "$bin" --rom "$rom" --trace "$here/../traces/${TRACE:-smoke}.mmin" \
        --frames ${FRAMES:-900} --hashes "$d/hashes.txt" $EXTRA >"$d/out.log" 2>&1
}
fail=0
EXTRA="" run off
EXTRA="--window-shot 212:$out/menu-root.png --window-shot 262:$out/menu-options.png --window-shot 322:$out/menu-warp-empty.png" \
    run on MM_DEV_MENU=1 MM_DEV_KEYS="200:F8,230:DOWN,231:DOWN,232:DOWN,250:OK,280:BACK,281:UP,282:UP,283:UP,300:OK,340:BACK,341:BACK,400:F8,401:F1,420:F1,421:F8,450:F8"
if cmp -s "$out/off/hashes.txt" "$out/on/hashes.txt"; then
    echo "PASS all-off identity: $(wc -l <"$out/on/hashes.txt") frames identical with the menu used"
else
    echo "FAIL all-off identity"; fail=1
fi
grep -q '\[DEV_MENU\] open' "$out/on/state/maniac-mansion-port/port.log" && echo "PASS menu opened (scripted F8)" || { echo "FAIL menu did not open"; fail=1; }
EXTRA="" run disabled MM_DEV_MENU=1 MM_CHEATS=0 MM_DEV_KEYS="200:F8"
if grep -q '\[DEV_MENU\] off' "$out/disabled/state/maniac-mansion-port/port.log" && ! grep -q '\[DEV_MENU\] open' "$out/disabled/state/maniac-mansion-port/port.log"; then
    echo "PASS hard disable (MM_CHEATS=0)"
else
    echo "FAIL hard disable"; fail=1
fi
EXTRA="" run default MM_DEV_KEYS="200:F8"
if grep -q '\[DEV_MENU\] off' "$out/default/state/maniac-mansion-port/port.log" && ! grep -q 'scripted key' "$out/default/state/maniac-mansion-port/port.log"; then
    echo "PASS off by default (scripted keys inert)"
else
    echo "FAIL off by default"; fail=1
fi
# quick save near 300, quick load near 600 on the no-input trace: after the
# load, the run must replay the original timeline from the saved frame.
EXTRA="" TRACE=boot run boot-off
EXTRA="" TRACE=boot run quick MM_DEV_MENU=1 MM_DEV_KEYS="300:F11,600:F12"
python3 - "$out/quick/hashes.txt" "$out/boot-off/hashes.txt" <<'PY' || fail=1
import sys
q = [l.split()[1] for l in open(sys.argv[1])]
o = [l.split()[1] for l in open(sys.argv[2])]
# the load lands at a frame boundary near 600; find the frame it resumed at
k = next((k for k in range(598, 606) if q[k] != o[k]), None)
j = None if k is None else next((j for j in range(295, 310) if q[k:k + 200] == o[j:j + 200]), None)
ok = q[:598] == o[:598] and j is not None
print(("PASS" if ok else "FAIL") + f" quick save/load: loaded at frame {k}, replays the original from frame {j} for 200 frames")
sys.exit(0 if ok else 1)
PY
exit $fail
