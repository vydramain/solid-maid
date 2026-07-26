#!/usr/bin/env bash
# Unit tests for the disc's pure logic.
#
# These compile the game's own translation units against the PDK headers and run
# them on the host. They cover the parts that are provably right or wrong without
# a console: the countdown's rules, the save blob, the palette ramp, the text
# encoding, and the collision solver. Everything that depends on the real
# renderer, real input or the real medium is checked by a runtime probe instead
# (tools/build.sh smoke / play) — a unit test is not allowed to stand in for
# those.
set -euo pipefail

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DISC_DIR="$(cd "$TEST_DIR/.." && pwd)"
MPPC_ROOT="${MPPC_ROOT:-/home/vydra/Repositories/vydramain/3dmppc-polymer}"

OUT="$DISC_DIR/build/tests"
mkdir -p "$OUT"

g++ -std=c++23 -Wall -Wextra -Werror -g -O1 \
    -I"$DISC_DIR/src" \
    -I"$MPPC_ROOT/pdk/include" \
    -I"$MPPC_ROOT/pdk/lib/include" \
    -o "$OUT/sm_tests" \
    "$TEST_DIR/sm_tests.cpp" \
    "$DISC_DIR/src/sm_state.cpp" \
    "$DISC_DIR/src/sm_assets.cpp" \
    "$DISC_DIR/src/sm_scene.cpp" \
    "$DISC_DIR/src/sm_level_home.cpp" \
    "$DISC_DIR/src/sm_level_street.cpp" \
    "$DISC_DIR/src/sm_level_factory.cpp" \
    "$DISC_DIR/src/sm_gfx.cpp" \
    "$DISC_DIR/src/sm_text.cpp" \
    "$DISC_DIR/src/sm_input.cpp"

"$OUT/sm_tests"
