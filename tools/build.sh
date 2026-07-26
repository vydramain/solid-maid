#!/usr/bin/env bash
# Burn this directory into a .mppcdisc and, optionally, run it.
#
# The console and its tools are a separate product in a separate repository;
# this script only drives them. It never builds or modifies the console — if the
# tools are missing it says so and stops, rather than reaching into somebody
# else's tree to fix it.
#
#   tools/build.sh                 burn only
#   tools/build.sh run             burn, then run windowed
#   tools/build.sh smoke [FRAMES]  burn, then run headless for FRAMES and dump
#                                  the last frame to build/frame.ppm
#   tools/build.sh play            burn, then run the scripted playthrough
#
# Environment:
#   MPPC_ROOT   the console checkout (default: the parent of the shelf this
#               disc is symlinked onto)
set -euo pipefail

DISC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MPPC_ROOT="${MPPC_ROOT:-/home/vydra/Repositories/vydramain/3dmppc-polymer}"

CONSOLE="$MPPC_ROOT/build/3dmppc"
BURNER="$MPPC_ROOT/pdk/tools/build/mppcburner/mppcburner"
BAKER="$MPPC_ROOT/pdk/tools/build/mppcbaker/mppcbaker"
OUT_DIR="$DISC_DIR/build"
DISC="$OUT_DIR/solid-maid.mppcdisc"

for tool in "$CONSOLE" "$BURNER" "$BAKER"; do
    if [[ ! -x "$tool" ]]; then
        echo "missing: $tool" >&2
        echo "build the console and its tools first, from $MPPC_ROOT:" >&2
        echo "  cmake -S . -B build -G Ninja && cmake --build build" >&2
        echo "  cmake -S pdk/tools -B pdk/tools/build -G Ninja && cmake --build pdk/tools/build" >&2
        exit 1
    fi
done

mkdir -p "$OUT_DIR"

echo "== burning =="
"$BURNER" build "$DISC_DIR" -o "$DISC" --baker "$BAKER" --keep-build="$OUT_DIR/burn"
echo
"$BURNER" inspect "$DISC"

case "${1:-}" in
    run)
        echo "== running =="
        exec "$CONSOLE" --scale 3 --memcard "$OUT_DIR/memcard.mppccard" "$DISC"
        ;;
    smoke)
        frames="${2:-240}"
        echo "== headless smoke test, $frames frames =="
        exec "$CONSOLE" --headless --fixed-step --frames "$frames" \
            --memcard "$OUT_DIR/harness.mppccard" \
            --dump-frame "$OUT_DIR/frame.ppm" "$DISC"
        ;;
    play)
        frames="${2:-60000}"
        echo "== scripted playthrough =="
        # A harness NEVER writes the card the player plays from: a test run that
        # leaves a half-finished shift behind is a save the next real session
        # silently resumes into.
        rm -f "$OUT_DIR/harness.mppccard"
        SOLIDMAID_AUTOPILOT=1 exec "$CONSOLE" --headless --fixed-step --frames "$frames" \
            --memcard "$OUT_DIR/harness.mppccard" \
            --dump-frame "$OUT_DIR/frame.ppm" "$DISC"
        ;;
    "")
        ;;
    *)
        echo "unknown mode: $1" >&2
        exit 1
        ;;
esac
