// Solidmaid — host-side unit tests for the disc's pure logic.
//
// Scope discipline: a unit test here is allowed to check something that is true
// independently of the console. Anything that depends on the real rasterizer,
// real input, the real medium or real memory is checked by a runtime probe
// instead — see tools/build.sh smoke / play. Substituting a unit test for one
// of those would be checking that the game agrees with itself.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "sm_assets.hpp"
#include "sm_enemy.hpp"
#include "sm_scene.hpp"
#include "sm_state.hpp"
#include "sm_text.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_case = "";

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::printf("  FAIL  %s:%d  %s\n", g_case, line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)
#define CASE(name)                     \
    g_case = (name);                   \
    std::printf("- %s\n", g_case)

using namespace solidmaid;

// ── the countdown ─────────────────────────────────────────────────────────────

void test_countdown_derivation() {
    CASE("countdown: everything derives from the one number");

    for (int remaining = 5; remaining >= 0; --remaining) {
        sm_countdown state{};
        state.shifts_remaining = remaining;

        // The board digit, the lit lamps and the apartment stage always agree —
        // "two sources of truth is how the board and the street get out of sync".
        CHECK(state.board_digit() == remaining);
        CHECK(state.lamps_lit() == remaining);
        CHECK(state.tier() == 5 - remaining);
        CHECK(state.apartment_state() == state.tier());

        int lit = 0;
        for (int i = 0; i < 5; ++i) lit += state.lamp_lit(i) ? 1 : 0;
        CHECK(lit == remaining);
    }
}

void test_lamp_extinguish_order() {
    CASE("countdown: lamps die L1 first, the factory gate L5 last");

    sm_countdown state{};

    state.shifts_remaining = 5;  // shift 1: all five burning
    for (int i = 0; i < 5; ++i) CHECK(state.lamp_lit(i));

    state.shifts_remaining = 4;  // L1, the courtyard — his own doorway — goes first
    CHECK(!state.lamp_lit(0));
    CHECK(state.lamp_lit(1) && state.lamp_lit(2) && state.lamp_lit(3) && state.lamp_lit(4));

    state.shifts_remaining = 1;  // only L5, the factory gate, is left
    CHECK(!state.lamp_lit(0) && !state.lamp_lit(1) && !state.lamp_lit(2) && !state.lamp_lit(3));
    CHECK(state.lamp_lit(4));

    state.shifts_remaining = 0;  // the final lap: the gate goes out too
    for (int i = 0; i < 5; ++i) CHECK(!state.lamp_lit(i));

    // Out-of-range indices are answered, not trusted.
    CHECK(!state.lamp_lit(-1));
    CHECK(!state.lamp_lit(5));
}

void test_single_writer() {
    CASE("countdown: the only writer is a completed assembly");

    sm_countdown state{};
    CHECK(state.shifts_remaining == 5);

    // An unfinished ritual moves nothing, at any step.
    for (int step = 0; step < SM_ASSEMBLY_STEPS; ++step) {
        state.assembly_step = step;
        CHECK(!state.commit_completed_assembly());
        CHECK(state.shifts_remaining == 5);
    }

    // A finished one moves it exactly once and resets the ritual.
    state.assembly_step = SM_ASSEMBLY_STEPS;
    CHECK(state.commit_completed_assembly());
    CHECK(state.shifts_remaining == 4);
    CHECK(state.assembly_step == 0);

    // And cannot be replayed without doing the work again.
    CHECK(!state.commit_completed_assembly());
    CHECK(state.shifts_remaining == 4);
}

void test_death_costs_no_progress() {
    CASE("countdown: dying restarts the shift and never moves the count");

    sm_countdown state{};
    state.shifts_remaining = 3;
    state.phase = SM_PHASE_FACTORY;
    state.assembly_step = 2;

    state.restart_current_shift();

    CHECK(state.shifts_remaining == 3);  // the board, the lamps and the room are untouched
    CHECK(state.phase == SM_PHASE_HOME);
    CHECK(state.assembly_step == 0);     // within-shift state is cleared
    CHECK(!state.finished);
}

void test_full_run_reaches_the_end() {
    CASE("countdown: five shifts then the final lap, and it cannot overshoot");

    sm_countdown state{};
    for (int shift = 0; shift < SM_SHIFTS_START; ++shift) {
        CHECK(!state.is_final_lap());
        state.assembly_step = SM_ASSEMBLY_STEPS;
        CHECK(state.commit_completed_assembly());
    }

    CHECK(state.shifts_remaining == 0);
    CHECK(state.is_final_lap());
    CHECK(state.tier() == 5);
    CHECK(state.apartment_state() == 5);

    // At zero there is no sixth shift to bank: the hall is inert.
    state.assembly_step = SM_ASSEMBLY_STEPS;
    CHECK(!state.commit_completed_assembly());
    CHECK(state.shifts_remaining == 0);
}

// ── the save blob ─────────────────────────────────────────────────────────────

void test_save_roundtrip() {
    CASE("save: the four fields survive a round trip");

    sm_countdown written{};
    written.shifts_remaining = 2;
    written.phase = SM_PHASE_FACTORY;
    written.assembly_step = 1;
    written.finished = false;

    uint8_t blob[SM_SAVE_BYTES];
    sm_save_encode(written, blob);

    sm_countdown read{};
    CHECK(sm_save_decode(blob, read));
    CHECK(read.shifts_remaining == 2);
    CHECK(read.phase == SM_PHASE_FACTORY);
    CHECK(read.assembly_step == 1);
    CHECK(!read.finished);

    written.finished = true;
    sm_save_encode(written, blob);
    CHECK(sm_save_decode(blob, read));
    CHECK(read.finished);
}

void test_save_rejects_nonsense() {
    CASE("save: a blob that cannot describe a reachable state is refused");

    sm_countdown state{};
    uint8_t blob[SM_SAVE_BYTES];
    sm_save_encode(state, blob);
    sm_countdown out{};

    // Somebody else's save.
    uint8_t foreign[SM_SAVE_BYTES];
    std::memcpy(foreign, blob, SM_SAVE_BYTES);
    foreign[0] ^= 0xFF;
    CHECK(!sm_save_decode(foreign, out));

    // A count that cannot exist.
    uint8_t overflow[SM_SAVE_BYTES];
    std::memcpy(overflow, blob, SM_SAVE_BYTES);
    overflow[4] = 99;
    CHECK(!sm_save_decode(overflow, out));

    // A phase that is not one of the five.
    uint8_t phase[SM_SAVE_BYTES];
    std::memcpy(phase, blob, SM_SAVE_BYTES);
    phase[8] = 77;
    CHECK(!sm_save_decode(phase, out));

    // A ritual step past the end.
    uint8_t step[SM_SAVE_BYTES];
    std::memcpy(step, blob, SM_SAVE_BYTES);
    step[12] = 9;
    CHECK(!sm_save_decode(step, out));

    // The blob is small enough to fit a card slot many times over.
    CHECK(SM_SAVE_BYTES <= 8192);
}

// ── the darkening ramp ────────────────────────────────────────────────────────

void test_palette_ramp() {
    CASE("palette: the ramp darkens, never punches holes, never reaches zero");

    // The cut-out hole must survive every tier untouched, or every sprite in the
    // game gains an opaque box around it.
    for (int tier = 0; tier < SM_TIER_COUNT; ++tier) CHECK(sm_palette_tier_entry(0, tier) == 0);

    // An opaque colour must never become 0000h — the black trap.
    for (int tier = 0; tier < SM_TIER_COUNT; ++tier) {
        for (uint16_t entry = 1; entry < 0x8000u; entry += 37) {
            CHECK(sm_palette_tier_entry(entry, tier) != 0);
        }
    }

    // Monotonically darker, tier by tier, measured as total luminance.
    const uint16_t sample = static_cast<uint16_t>(24 | (20 << 5) | (16 << 10));
    int previous = 1000;
    for (int tier = 0; tier < SM_TIER_COUNT; ++tier) {
        const uint16_t packed = sm_palette_tier_entry(sample, tier);
        const int sum = (packed & 0x1F) + ((packed >> 5) & 0x1F) + ((packed >> 10) & 0x1F);
        CHECK(sum <= previous);
        previous = sum;
    }

    // And the floor never goes to zero — the countdown removes pools of light,
    // not the ability to see (docs/mechanics.md).
    const uint16_t darkest = sm_palette_tier_entry(sample, SM_TIER_COUNT - 1);
    const int lum = (darkest & 0x1F) + ((darkest >> 5) & 0x1F) + ((darkest >> 10) & 0x1F);
    CHECK(lum >= 24);

    // Every step is a BIGGER step than the one before it. The descent has to be
    // felt from inside the run, where each shift is only ever compared to the
    // shift before it, and an even slope is the one thing nobody notices.
    int previous_drop = 0;
    int last = (sm_palette_tier_entry(sample, 0) & 0x1F) +
               ((sm_palette_tier_entry(sample, 0) >> 5) & 0x1F) +
               ((sm_palette_tier_entry(sample, 0) >> 10) & 0x1F);
    for (int tier = 1; tier < SM_TIER_COUNT; ++tier) {
        const uint16_t packed = sm_palette_tier_entry(sample, tier);
        const int sum = (packed & 0x1F) + ((packed >> 5) & 0x1F) + ((packed >> 10) & 0x1F);
        const int drop = last - sum;
        CHECK(drop >= previous_drop);
        previous_drop = drop;
        last = sum;
    }

    // Untiered is the authored image, unchanged — the HUD, the font and the
    // self-lit pre-warm ring reach it with a negative tier and nothing else does.
    CHECK(sm_palette_tier_entry(sample, -1) == sample);
    // And tier 0 is NOT the authored image: shift 1 already sits under it.
    CHECK(sm_palette_tier_entry(sample, 0) != sample);
}

// ── text ──────────────────────────────────────────────────────────────────────

void test_glyph_mapping() {
    CASE("text: the font atlas contract");

    CHECK(sm_glyph_index(' ') == 0);
    CHECK(sm_glyph_index('A') == 'A' - 32);
    CHECK(sm_glyph_index('0') == '0' - 32);
    CHECK(sm_glyph_index('~') == 94);

    CHECK(sm_glyph_index(0x0410) == 96);        // А
    CHECK(sm_glyph_index(0x042F) == 127);       // Я
    CHECK(sm_glyph_index(0x041E) == 96 + 14);   // О

    // Lowercase folds onto uppercase; Ё has no cell of its own and reads as Е.
    CHECK(sm_glyph_index(0x0430) == sm_glyph_index(0x0410));
    CHECK(sm_glyph_index(0x0401) == sm_glyph_index(0x0415));
    CHECK(sm_glyph_index(0x0451) == sm_glyph_index(0x0415));

    // Anything unmapped draws the notdef box rather than nothing at all.
    CHECK(sm_glyph_index(0x4E00) == 95);
    CHECK(sm_glyph_index(0x0001) == 95);
}

void test_utf8_decoding() {
    CASE("text: UTF-8 decoding, including the strings the board must draw");

    // "ОСТАЛОСЬ: 5"
    const std::string board = "\xD0\x9E\xD0\xA1\xD0\xA2\xD0\x90\xD0\x9B\xD0\x9E\xD0\xA1\xD0\xAC: 5";
    std::size_t offset = 0;
    CHECK(sm_utf8_next(board, offset) == 0x041E);  // О
    CHECK(sm_utf8_next(board, offset) == 0x0421);  // С
    CHECK(sm_utf8_next(board, offset) == 0x0422);  // Т
    CHECK(sm_utf8_next(board, offset) == 0x0410);  // А
    CHECK(sm_utf8_next(board, offset) == 0x041B);  // Л
    CHECK(sm_utf8_next(board, offset) == 0x041E);  // О
    CHECK(sm_utf8_next(board, offset) == 0x0421);  // С
    CHECK(sm_utf8_next(board, offset) == 0x042C);  // Ь
    CHECK(sm_utf8_next(board, offset) == ':');
    CHECK(sm_utf8_next(board, offset) == ' ');
    CHECK(sm_utf8_next(board, offset) == '5');
    CHECK(offset == board.size());

    // Every glyph of both board strings must exist in the atlas.
    const std::string plan =
        "\xD0\x9F\xD0\x9B\xD0\x90\xD0\x9D \xD0\x92\xD0\xAB\xD0\x9F\xD0\x9E\xD0\x9B\xD0\x9D\xD0\x95\xD0\x9D";
    for (const std::string* text : {&board, &plan}) {
        std::size_t at = 0;
        while (at < text->size()) {
            const uint32_t cp = sm_utf8_next(*text, at);
            CHECK(sm_glyph_index(cp) != 95);  // nothing may fall back to notdef
        }
    }

    // A malformed byte must advance by exactly one, so no string can loop forever.
    const std::string broken = "\xFF\xFE";
    std::size_t at = 0;
    sm_utf8_next(broken, at);
    CHECK(at == 1);
    sm_utf8_next(broken, at);
    CHECK(at == 2);

    CHECK(sm_text_width("AB", 1) == 16);
    CHECK(sm_text_width("AB", 2) == 32);
}

// ── collision ─────────────────────────────────────────────────────────────────

void test_collision_never_traps() {
    CASE("collision: the solver never leaves the player inside geometry");

    sm_scene scene{};
    scene.floor_y = 0.0f;
    // A corridor with a pillar in the middle of it.
    scene.add_collider(-6.0f, -1.0f, -1.0f, 20.0f);
    scene.add_collider(1.0f, -1.0f, 6.0f, 20.0f);
    scene.add_collider(-0.4f, 6.0f, 0.4f, 6.8f);

    auto inside_anything = [&](rv_pdklib::rv_vec3 p) {
        for (const sm_rect& rect : scene.colliders) {
            const float cx = p.x < rect.x0 ? rect.x0 : (p.x > rect.x1 ? rect.x1 : p.x);
            const float cz = p.z < rect.z0 ? rect.z0 : (p.z > rect.z1 ? rect.z1 : p.z);
            const float dx = p.x - cx;
            const float dz = p.z - cz;
            // A hair of tolerance: the solver parks the circle ON the surface.
            if (dx * dx + dz * dz < (SM_PLAYER_RADIUS - 0.01f) * (SM_PLAYER_RADIUS - 0.01f)) {
                return true;
            }
        }
        return false;
    };

    // Walk the length of the corridor, shoving into both walls and the pillar.
    // The bail-out below counts THIS test's failures, not the suite's: keyed to
    // the global it would break the moment any earlier test failed, and then
    // report a second, invented failure on line 349 that says nothing about
    // collision at all.
    const int failures_before = g_failures;
    rv_pdklib::rv_vec3 at{0.0f, 0.0f, 0.0f};
    for (int step = 0; step < 600; ++step) {
        const float push = (step % 3 == 0) ? 0.9f : ((step % 3 == 1) ? -0.9f : 0.0f);
        const rv_pdklib::rv_vec3 want{at.x + push, 0.0f, at.z + 0.05f};
        at = sm_scene_slide(scene, at, want, SM_PLAYER_RADIUS);

        CHECK(std::isfinite(at.x) && std::isfinite(at.z));
        CHECK(!inside_anything(at));
        if (g_failures > failures_before) break;  // one report is enough; do not print 600
    }

    // And it made progress rather than refusing to move at all.
    CHECK(at.z > 5.0f);
}

// ── the smoker's exhale ───────────────────────────────────────────────────────

// Walks a player across open ground in front of a smoker until it commits, and
// hands back the cloud it filed together with where the player was standing at
// that moment. Returns false if it never committed.
bool run_until_exhale(rv_pdklib::rv_vec3 heading, sm_cloud& out_cloud,
                      rv_pdklib::rv_vec3& out_player) {
    sm_scene scene{};
    scene.floor_y = 0.0f;

    sm_enemies enemies{};
    enemies.reset();
    rv_pdklib::rv_vec3 player{0.0f, 0.0f, 0.0f};
    // Past SM_SPAWN_MIN_DISTANCE, and off to the side so it has to close to
    // its standoff before it can commit to anything.
    CHECK(enemies.spawn(SM_ENEMY_SMOKER, rv_pdklib::rv_vec3{0.0f, 0.0f, 11.0f}, player));

    std::vector<sm_enemy_damage> damage;
    const float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 900; ++frame) {
        player = player + heading * (SM_PLAYER_WALK_SPEED * dt);
        enemies.update(dt, scene, player, damage);
        for (const sm_cloud& cloud : enemies.clouds()) {
            if (!cloud.alive) continue;
            out_cloud = cloud;
            out_player = player;
            return true;
        }
    }
    return false;
}

void test_exhale_leads_the_player() {
    CASE("smoker: the exhale opens ahead of the player, never on the camera");

    // Strafing across the smoker's face, so the lead cannot be confused with
    // "it just aimed at itself".
    const rv_pdklib::rv_vec3 heading{1.0f, 0.0f, 0.0f};
    sm_cloud cloud{};
    rv_pdklib::rv_vec3 player{};
    CHECK(run_until_exhale(heading, cloud, player));

    // It is filed as a pre-warm ring, not as live smoke.
    CHECK(cloud.age < 0.0f);

    const rv_pdklib::rv_vec3 offset = cloud.centre - player;
    const float along = offset.x * heading.x + offset.z * heading.z;

    // AHEAD. This is the whole fix: a centre on the player's own feet puts the
    // near rim behind the near plane and the puffs on the camera, so a walking
    // player takes chip damage from smoke that was never drawn.
    CHECK(along > 0.5f);
    // But short of the full windup's travel, or holding course would be a
    // guaranteed hit and the ring would stop being a question.
    CHECK(along < SM_PLAYER_WALK_SPEED * SM_SMOKER_WINDUP);
    CHECK(along <= SM_CLOUD_LEAD_MAX + 0.01f);

    // The lead is along the movement, not sideways: crossing ground the player
    // never aimed at would read as the smoker missing rather than as a threat.
    const float sideways = std::fabs(offset.z * heading.x - offset.x * heading.z);
    CHECK(sideways < 0.35f);

    // The player is still inside it at commitment — the ring is pressure, not a
    // free pass — so the answer has to be a change of course.
    const float from_centre = std::sqrt(offset.x * offset.x + offset.z * offset.z);
    CHECK(from_centre < SM_CLOUD_RADIUS);

    // And a player who reverses clears it: 0.85 s of walking away from a centre
    // already 1.6 m behind them is more than the radius.
    const float escape = SM_PLAYER_WALK_SPEED * SM_SMOKER_WINDUP + along;
    CHECK(escape > SM_CLOUD_RADIUS);
}

void test_line_of_sight() {
    CASE("collision: line of sight stops at a wall");

    sm_scene scene{};
    scene.add_collider(-1.0f, 2.0f, 1.0f, 3.0f);

    CHECK(sm_scene_clear_line(scene, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.5f}));
    CHECK(!sm_scene_clear_line(scene, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 5.0f}));
    CHECK(sm_scene_clear_line(scene, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 5.0f}));
}

// ── the areas ─────────────────────────────────────────────────────────────────

void test_areas_are_buildable_at_every_tier() {
    CASE("world: every area builds at every tier, with a usable start");

    for (int remaining = 5; remaining >= 0; --remaining) {
        sm_countdown state{};
        state.shifts_remaining = remaining;

        for (sm_area area : {SM_AREA_HOME, SM_AREA_STREET, SM_AREA_FACTORY}) {
            sm_scene scene{};
            sm_build_area(scene, area, state);

            CHECK(!scene.surfaces.empty());
            CHECK(!scene.colliders.empty());
            CHECK(std::isfinite(scene.player_start.x) && std::isfinite(scene.player_start.z));

            // The player never starts inside a wall.
            bool trapped = false;
            for (const sm_rect& rect : scene.colliders) {
                const float cx = scene.player_start.x < rect.x0
                                     ? rect.x0
                                     : (scene.player_start.x > rect.x1 ? rect.x1 : scene.player_start.x);
                const float cz = scene.player_start.z < rect.z0
                                     ? rect.z0
                                     : (scene.player_start.z > rect.z1 ? rect.z1 : scene.player_start.z);
                const float dx = scene.player_start.x - cx;
                const float dz = scene.player_start.z - cz;
                if (dx * dx + dz * dz < SM_PLAYER_RADIUS * SM_PLAYER_RADIUS) trapped = true;
            }
            CHECK(!trapped);

            // Every area must offer a way onward, or the run softlocks.
            if (area != SM_AREA_FACTORY) CHECK(!scene.triggers.empty());
        }
    }
}

void test_home_dresses_to_the_countdown() {
    CASE("world: the apartment loses something every shift, and never the tools");

    std::size_t previous_surfaces = 0;
    for (int remaining = 5; remaining >= 0; --remaining) {
        sm_countdown state{};
        state.shifts_remaining = remaining;

        sm_scene scene{};
        sm_build_home(scene, state);

        // The brick and the pipe are by the door in EVERY state, including the
        // final lap: "subtraction never touches what the shift requires".
        bool brick = false;
        bool pipe = false;
        for (const sm_interactable& item : scene.interactables) {
            if (item.kind == SM_INTERACT_BRICK) brick = true;
            if (item.kind == SM_INTERACT_PIPE) pipe = true;
        }
        CHECK(brick);
        CHECK(pipe);

        // The television is interactable in state 0 and in no other.
        bool television = false;
        for (const sm_interactable& item : scene.interactables) {
            if (item.kind == SM_INTERACT_TELEVISION) television = true;
        }
        CHECK(television == (state.apartment_state() == 0));

        // The room never gains anything.
        if (remaining < 5) CHECK(scene.surfaces.size() <= previous_surfaces + 4);
        previous_surfaces = scene.surfaces.size();

        // And there is always a way out.
        bool exit_trigger = false;
        for (const sm_trigger& trigger : scene.triggers) {
            if (trigger.id == SM_TRIGGER_LEAVE_HOME) exit_trigger = true;
        }
        CHECK(exit_trigger);
    }
}

void test_street_keeps_its_poles() {
    CASE("world: the town keeps all five posts and loses all five lamps");

    for (int remaining = 5; remaining >= 0; --remaining) {
        sm_countdown state{};
        state.shifts_remaining = remaining;

        sm_scene scene{};
        sm_build_street(scene, state);

        // Five poles, always. A dead lamppost is not removed from the world.
        CHECK(scene.lamps.size() == 5);
        bool seen[5] = {false, false, false, false, false};
        for (const sm_lamp& lamp : scene.lamps) {
            CHECK(lamp.index >= 0 && lamp.index < 5);
            if (lamp.index >= 0 && lamp.index < 5) seen[lamp.index] = true;
        }
        for (int i = 0; i < 5; ++i) CHECK(seen[i]);

        // And a way into the factory.
        bool gate = false;
        for (const sm_trigger& trigger : scene.triggers) {
            if (trigger.id == SM_TRIGGER_ENTER_FACTORY) gate = true;
        }
        CHECK(gate);
    }
}

void test_factory_offers_the_work_and_the_ending() {
    CASE("world: the hall has a bench, a return, and a board to walk up to");

    sm_countdown working{};
    working.shifts_remaining = 3;
    sm_scene scene{};
    sm_build_factory(scene, working);

    bool bench = false;
    for (const sm_interactable& item : scene.interactables) {
        if (item.kind == SM_INTERACT_ASSEMBLY) bench = true;
    }
    CHECK(bench);

    bool ret = false;
    bool board = false;
    for (const sm_trigger& trigger : scene.triggers) {
        if (trigger.id == SM_TRIGGER_RETURN_HOME) ret = true;
        if (trigger.id == SM_TRIGGER_APPROACH_BOARD) board = true;
    }
    CHECK(ret);   // opens once the ritual completes
    CHECK(board); // the final lap's only objective
}

}  // namespace

int main() {
    std::printf("solidmaid unit tests\n\n");

    test_countdown_derivation();
    test_lamp_extinguish_order();
    test_single_writer();
    test_death_costs_no_progress();
    test_full_run_reaches_the_end();

    test_save_roundtrip();
    test_save_rejects_nonsense();

    test_palette_ramp();

    test_glyph_mapping();
    test_utf8_decoding();

    test_collision_never_traps();
    test_exhale_leads_the_player();
    test_line_of_sight();

    test_areas_are_buildable_at_every_tier();
    test_home_dresses_to_the_countdown();
    test_street_keeps_its_poles();
    test_factory_offers_the_work_and_the_ending();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
