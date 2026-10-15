// Autotest scripts for co-op, see autotest.h. Run e.g. with
//
//   AUTOTEST_SCRIPT=coop tools/regress.sh run <binary> <out> "[coop]enabled=1"

#include "game/coop_autotest.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <random>
#include <chrono>
#include <iterator>
#include <string>
#include <vector>

#include "game/actions.h"
#include "game/anim.h"
#include "game/art.h"
#include "game/autotest.h"
#include "game/combat.h"
#include "game/combat_defs.h"
#include "game/coop.h"
#include "game/coop_crash.h"
#include "game/coop_host.h"
#include "game/coop_lobby.h"
#include "game/coop_menu.h"
#include "game/coop_net.h"
#include "game/credits.h"
#include "game/critter.h"
#include "game/game.h"
#include "game/game_vars.h"
#include "game/gconfig.h"
#include "game/endgame.h"
#include "game/gdialog.h"
#include "plib/db/db.h"
#include "game/gmovie.h"
#include "game/gsound.h"
#include "game/scripts.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/loadsave.h"
#include "game/main.h"
#include "game/mainmenu.h"
#include "game/map.h"
#include "game/palette.h"
#include "game/party.h"
#include "game/object.h"
#include "game/protinst.h"
#include "game/perk.h"
#include "game/proto.h"
#include "game/queue.h"
#include "game/roll.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/tile.h"
#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/gnw/inject.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/svga.h"

#include <SDL.h>

namespace fallout {

// Quick save slot used by the co-op scripts (0-based; SLOT08).
#define COOP_AUTOTEST_SLOT 7

// Human combat turns played by a test hook.
static Object* coop_autotest_enemy = NULL;
static int coop_autotest_turns[COOP_MAX_PLAYERS];
static int coop_autotest_turns_total = 0;

static int coop_autotest_players(AutotestNewGameProc* newGame);
static int coop_autotest_travel(AutotestNewGameProc* newGame);
static int coop_autotest_rads(AutotestNewGameProc* newGame);
static int coop_autotest_interact(AutotestNewGameProc* newGame);
static int coop_autotest_map_time(AutotestNewGameProc* newGame);
static int coop_autotest_pack(AutotestNewGameProc* newGame);
static int coop_autotest_story_kill(AutotestNewGameProc* newGame);
static void coop_autotest_log_story(const char* when);
static bool coop_autotest_frames_until(unsigned int ms, bool (*done)());
static void coop_autotest_host_frame();
static bool coop_autotest_players_together(const char* when);
static int coop_autotest_combat(AutotestNewGameProc* newGame);
static bool coop_autotest_attack_turn(Object* player);
static bool coop_autotest_shoot_turn(Object* player);
static bool coop_autotest_video();
static void coop_autotest_video_start();
static bool coop_autotest_pass_turn(Object* player);
static Object* coop_autotest_spawn_enemy(Object* nextTo);
static int coop_autotest_join(AutotestNewGameProc* newGame);
static int coop_autotest_looks(AutotestNewGameProc* newGame);
static int coop_autotest_barter(AutotestNewGameProc* newGame);
static int coop_autotest_give(AutotestNewGameProc* newGame);
static int coop_autotest_talk(AutotestNewGameProc* newGame);
static int coop_autotest_db_sfx(AutotestNewGameProc* newGame);
static void coop_autotest_answer_dialogs();
static int coop_autotest_poison(AutotestNewGameProc* newGame);
static int coop_autotest_character(AutotestNewGameProc* newGame);
static int coop_autotest_gamepad(AutotestNewGameProc* newGame);
static int coop_autotest_net_character_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_character_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_talk_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_talk_client(AutotestNewGameProc* newGame);
static bool coop_autotest_relay_host_setup();
static void coop_autotest_relay_client_setup();
static bool coop_autotest_session_frames(unsigned int ms, bool untilConnected, bool untilDisconnected);
static int coop_autotest_knockout(AutotestNewGameProc* newGame);
static int coop_autotest_net_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_client(AutotestNewGameProc* newGame);
static void coop_autotest_net_client_exit();
static int coop_autotest_net_play_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_play_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_combat_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_combat_client(AutotestNewGameProc* newGame);
static int coop_autotest_main_menu(AutotestNewGameProc* newGame);
static int coop_autotest_menu_flow(AutotestNewGameProc* newGame);
static void coop_autotest_screenshot();
static void coop_autotest_equip(PlayerState* player, const char* const* names, int count, int hand);
static void coop_autotest_scene();
static int coop_autotest_menu_join(AutotestNewGameProc* newGame);
static int coop_autotest_menu_server(AutotestNewGameProc* newGame);
static int coop_autotest_menu_host_code(AutotestNewGameProc* newGame);
static int coop_autotest_menu_crash_report(AutotestNewGameProc* newGame);
static int coop_autotest_net_tour_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_tour_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_session_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_ending_host(AutotestNewGameProc* newGame);
static int coop_autotest_movie(AutotestNewGameProc* newGame);
static int coop_autotest_menu_more(AutotestNewGameProc* newGame);
static int coop_autotest_credits(AutotestNewGameProc* newGame);
static int coop_autotest_net_death_host(AutotestNewGameProc* newGame);
static void coop_autotest_session_shots()
{
    inject_wait(90);
    inject_call(coop_autotest_screenshot);
    inject_call(coop_autotest_session_shots);
}

static int coop_autotest_net_session_client(AutotestNewGameProc* newGame);
static void coop_autotest_compare_lookups(PlayerState* player, const char* when);
static bool coop_autotest_files_equal_except(const char* path1, const char* path2, const char* skipPrefix);

void coop_autotest_register()
{
    autotest_add_script("coop", coop_autotest_players);
    autotest_add_script("coop_travel", coop_autotest_travel);
    autotest_add_script("coop_rads", coop_autotest_rads);
    autotest_add_script("coop_poison", coop_autotest_poison);
    autotest_add_script("coop_character", coop_autotest_character);
    autotest_add_script("gamepad", coop_autotest_gamepad);
    autotest_add_script("coop_interact", coop_autotest_interact);
    autotest_add_script("map_time", coop_autotest_map_time);
    autotest_add_script("pack", coop_autotest_pack);
    autotest_add_script("story_kill", coop_autotest_story_kill);
    autotest_add_script("coop_combat", coop_autotest_combat);
    autotest_add_script("coop_join", coop_autotest_join);
    autotest_add_script("coop_looks", coop_autotest_looks);
    autotest_add_script("coop_barter", coop_autotest_barter);
    autotest_add_script("coop_give", coop_autotest_give);
    autotest_add_script("coop_talk", coop_autotest_talk);
    autotest_add_script("db_sfx", coop_autotest_db_sfx);
    autotest_add_script("coop_knockout", coop_autotest_knockout);
    autotest_add_script("net_host", coop_autotest_net_host);
    autotest_add_script("net_client", coop_autotest_net_client);
    autotest_add_script("net_play_host", coop_autotest_net_play_host);
    autotest_add_script("net_play_client", coop_autotest_net_play_client);
    autotest_add_script("net_combat_host", coop_autotest_net_combat_host);
    autotest_add_script("net_combat_client", coop_autotest_net_combat_client);
    autotest_add_script("main_menu", coop_autotest_main_menu);
    autotest_add_script("menu_flow", coop_autotest_menu_flow);
    autotest_add_script("menu_join", coop_autotest_menu_join);
    autotest_add_script("menu_server", coop_autotest_menu_server);
    autotest_add_script("menu_host_code", coop_autotest_menu_host_code);
    autotest_add_script("menu_crash_report", coop_autotest_menu_crash_report);
    autotest_add_script("net_tour_host", coop_autotest_net_tour_host);
    autotest_add_script("net_tour_client", coop_autotest_net_tour_client);
    autotest_add_script("net_session_host", coop_autotest_net_session_host);
    autotest_add_script("net_session_client", coop_autotest_net_session_client);
    autotest_add_script("net_character_host", coop_autotest_net_character_host);
    autotest_add_script("net_character_client", coop_autotest_net_character_client);
    autotest_add_script("net_talk_host", coop_autotest_net_talk_host);
    autotest_add_script("net_talk_client", coop_autotest_net_talk_client);
    autotest_add_script("net_ending_host", coop_autotest_net_ending_host);
    autotest_add_script("movie", coop_autotest_movie);
    autotest_add_script("menu_more", coop_autotest_menu_more);
    autotest_add_script("credits", coop_autotest_credits);
    autotest_add_script("net_death_host", coop_autotest_net_death_host);
}

// Player 2 is created next to player 1 without touching player 1, keeps
// its own data, and can take local control.
static int coop_autotest_players(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    // Level 4 with room to spare: player 2's XP below must not level up
    // player 1, so only the XP total may differ afterwards.
    stat_pc_add_experience(6500);

    PlayerState* p1 = coop_player(0);
    Object* p1obj = obj_dude;
    int karma = game_get_global_var(GVAR_PLAYER_REPUATION);
    int karmaStat = stat_pc_get(PC_STAT_KARMA);
    int p1Xp = stat_pc_get(PC_STAT_EXPERIENCE);

    autotest_dump_state("p1_before.txt");

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    Object* p2obj = p2->obj;
    autotest_log("p1 '%s' tile %d, p2 '%s' tile %d, distance %d",
        critter_name(p1obj), p1obj->tile, critter_name(p2obj), p2obj->tile, obj_dist(p1obj, p2obj));

    if (coop_player_count() != 2) autotest_fail("player count %d", coop_player_count());
    if (obj_dude != p1obj) autotest_fail("obj_dude changed by adding player 2");
    if (coop_active_player() != p1) autotest_fail("active player changed by adding player 2");
    if (coop_player_of(p2obj) != p2) autotest_fail("player_of(p2)");
    if (coop_player_of(p1obj) != p1) autotest_fail("player_of(p1)");
    if (coop_player_by_pid(p2obj->pid) != p2) autotest_fail("player_by_pid(p2)");
    if (strcmp(critter_name(p1obj), critter_name(p2obj)) == 0) autotest_fail("players share a name");
    if (p2obj->elevation != p1obj->elevation || obj_dist(p1obj, p2obj) > 5) autotest_fail("player 2 not placed next to player 1");
    if (game_get_global_var(GVAR_PLAYER_REPUATION) != karma || stat_pc_get(PC_STAT_KARMA) != karmaStat) {
        autotest_fail("adding player 2 changed shared karma");
    }

    autotest_dump_state("p1_after_add.txt");
    if (!autotest_files_equal("autotest/p1_before.txt", "autotest/p1_after_add.txt")) {
        autotest_fail("adding player 2 changed player 1");
    }

    {
        ActivePlayerScope scope(p2);
        autotest_dump_state("p2.txt");

        // Player 2 only changes.
        int perksAdded = 0;
        for (int perk = PERK_COUNT - 1; perk >= 0 && perksAdded < 2; perk--) {
            if (perk_add(perk) == 0) {
                autotest_log("p2 perk added: %d", perk);
                perksAdded++;
            }
        }
        critter_kill_count_inc(KILL_TYPE_RAT);
        stat_pc_add_experience(1000);
        skill_inc_point(obj_dude, SKILL_STEAL);
        game_set_global_var(GVAR_NUKA_COLA_ADDICT, 1);
        autotest_dump_state("p2_changed.txt");
    }

    if (autotest_files_equal("autotest/p2.txt", "autotest/p2_changed.txt")) {
        autotest_fail("player 2 changes had no effect");
    }

    // XP is earned by every player; nothing else may leak into player 1.
    if (stat_pc_get(PC_STAT_EXPERIENCE) < p1Xp + 1000) {
        autotest_fail("player 1 did not get player 2's XP (%d -> %d)", p1Xp, stat_pc_get(PC_STAT_EXPERIENCE));
    }

    if (game_get_global_var(GVAR_NUKA_COLA_ADDICT) != 0) {
        autotest_fail("player 2's addiction shows for player 1");
    }

    autotest_dump_state("p1_after_p2_changes.txt");
    if (!coop_autotest_files_equal_except("autotest/p1_before.txt", "autotest/p1_after_p2_changes.txt", "pc_stat 2 ")) {
        autotest_fail("player 2 changes leaked into player 1");
    }

    coop_autotest_compare_lookups(p2, "while player 1 active");

    if (!coop_set_controlled(p2)) {
        autotest_fail("coop_set_controlled(p2) refused");
    } else {
        if (obj_dude != p2obj || coop_active_player() != p2 || coop_controlled_player() != p2) {
            autotest_fail("control switch did not activate player 2");
        }

        autotest_dump_state("p2_controlled.txt");
        if (!autotest_files_equal("autotest/p2_changed.txt", "autotest/p2_controlled.txt")) {
            autotest_fail("player 2 state differs when controlled");
        }

        coop_autotest_compare_lookups(p1, "while player 2 controlled");

        tile_refresh_display();
        dump_screen();

        // Only player 1 saves.
        lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
        if (SaveGame(LOAD_SAVE_MODE_QUICK) == 1) {
            autotest_fail("player 2 was allowed to save");
        }

        coop_set_controlled(p1);
        if (obj_dude != p1obj || coop_active_player() != p1) {
            autotest_fail("control did not return to player 1");
        }
    }

    autotest_run_checks("with two players");

    tile_refresh_display();
    dump_screen();

    // Save/load round trip keeps both players.
    {
        ActivePlayerScope scope(p2);
        autotest_dump_state("p2_before_save.txt");
    }
    autotest_dump_state("p1_before_save.txt");

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (SaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save failed");
        return -1;
    }

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (LoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("load failed");
        return -1;
    }

    if (coop_player_count() != 2) {
        autotest_fail("player 2 missing after load (count %d)", coop_player_count());
        return -1;
    }

    p2 = coop_player(1);
    autotest_log("after load: p1 '%s', p2 '%s' tile %d", critter_name(obj_dude), critter_name(p2->obj), p2->obj->tile);

    autotest_dump_state("p1_after_load.txt");
    if (!autotest_files_equal("autotest/p1_before_save.txt", "autotest/p1_after_load.txt")) {
        autotest_fail("player 1 differs after load");
    }

    {
        ActivePlayerScope scope(p2);
        autotest_dump_state("p2_after_load.txt");
    }
    if (!autotest_files_equal("autotest/p2_before_save.txt", "autotest/p2_after_load.txt")) {
        autotest_fail("player 2 differs after load");
    }

    coop_autotest_compare_lookups(p2, "after load");
    autotest_run_checks("after load");

    if (game_get_global_var(GVAR_NUKA_COLA_ADDICT) != 0) {
        autotest_fail("player 1 addicted after load");
    }

    {
        ActivePlayerScope scope(p2);
        if (game_get_global_var(GVAR_NUKA_COLA_ADDICT) != 1) {
            autotest_fail("player 2's addiction lost");
        }
    }

    return 0;
}

// Player 2 follows player 1 through map and elevation changes and cannot
// use exit grids alone.
// How long a map change takes (COOP_AUTOTEST_MAP, default BROHD34.MAP),
// without co-op: to compare screen sizes and builds.
static int coop_autotest_map_time(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    const char* name = getenv("COOP_AUTOTEST_MAP");
    MapTransition transition;
    transition.map = map_match_map_name(name != NULL ? name : "BROHD34.MAP");
    transition.elevation = 0;
    transition.tile = -1;
    transition.rotation = 0;
    unsigned int start = get_time();
    map_leave_map(&transition);
    map_check_state();
    autotest_log("%s loaded in %u ms (screen %dx%d)", map_data.name, elapsed_time(start), screenGetWidth(), screenGetHeight());
    return 0;
}

// Frame packing comes back exactly: random, empty, flat, patterns, and
// the real screen of a map.
static int coop_autotest_pack(AutotestNewGameProc* newGame)
{
    std::vector<unsigned char> data;
    unsigned int seed = 12345;
    auto check = [&](const char* what) {
        int packed = coop_net_pack_selftest(data.data(), data.size());
        autotest_log("pack %s: %zu -> %d bytes", what, data.size(), packed);
        if (packed < 0) {
            autotest_fail("packing %s did not come back the same", what);
        }
    };

    data.clear();
    check("nothing");
    data.assign(1, 7);
    check("one byte");
    data.assign(5, 9);
    check("five bytes");
    data.assign(1000000, 0);
    check("a million zeros");
    data.resize(300000);
    for (unsigned char& byte : data) {
        seed = seed * 1103515245 + 12345;
        byte = (unsigned char)(seed >> 16);
    }
    check("random");
    for (size_t index = 0; index < data.size(); index++) {
        data[index] = (unsigned char)((index % 37) * 3 + (index / 5000));
    }
    check("pattern");
    for (int round = 0; round < 50; round++) {
        size_t size = 1 + (seed >> 8) % 70000;
        data.resize(size);
        for (unsigned char& byte : data) {
            seed = seed * 1103515245 + 12345;
            byte = (unsigned char)((seed >> 24) % 4);
        }
        if (coop_net_pack_selftest(data.data(), data.size()) < 0) {
            autotest_fail("packing a few-values buffer of %zu bytes did not come back the same", size);
        }
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    tile_refresh_display();
    data.assign((const unsigned char*)gSdlSurface->pixels, (const unsigned char*)gSdlSurface->pixels + (size_t)gSdlSurface->pitch * gSdlSurface->h);
    check("the screen");
    return 0;
}

// Single player (or with [coop]enabled=1 and player 2 added: co-op): on
// COOP_AUTOTEST_MAP, the critter named COOP_AUTOTEST_KILL dies as in a fight
// (its death script, player 1 as the killer), then the game runs; the
// story flags before and after, to compare the two.
static int coop_autotest_story_kill(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    if (coop_is_enabled() && coop_add_player("premade\\stealth.gcd") == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    const char* mapName = getenv("COOP_AUTOTEST_MAP");
    const char* killName = getenv("COOP_AUTOTEST_KILL");
    MapTransition transition;
    transition.map = map_match_map_name(mapName != NULL ? mapName : "MSTRLR34.MAP");
    transition.elevation = 0;
    transition.tile = -1;
    transition.rotation = 0;
    map_leave_map(&transition);
    map_check_state();
    coop_autotest_log_story("before");

    Object* victim = NULL;
    for (Object* obj = obj_find_first(); obj != NULL && victim == NULL; obj = obj_find_next()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && !critter_is_dead(obj) && strcmp(critter_name(obj), killName != NULL ? killName : "Master") == 0) {
            victim = obj;
        }
    }
    if (victim == NULL) {
        autotest_fail("nobody to kill");
        return -1;
    }
    // Players' turns in the fight that follows pass by themselves.
    coop_set_turn_hook(coop_autotest_pass_turn);
    autotest_log("killing %s (pid %x, script %d)", critter_name(victim), victim->pid, victim->sid);
    critter_kill(victim, -1, true);
    if (victim->sid != -1) {
        scr_set_objs(victim->sid, obj_dude, NULL);
        exec_script_proc(victim->sid, SCRIPT_PROC_DESTROY);
    }
    coop_autotest_frames_until(15000, NULL);
    coop_set_turn_hook(NULL);
    coop_autotest_log_story("after");
    autotest_log("players alive: %d, quit %d", critter_is_dead(obj_dude) ? 0 : 1, game_user_wants_to_quit);
    return 0;
}

// Runs the game for up to `ms`, until `done` (if given) says so.
static bool coop_autotest_frames_until(unsigned int ms, bool (*done)())
{
    unsigned int start = get_time();
    while (elapsed_time(start) < ms) {
        coop_autotest_host_frame();
        if (done != NULL && done()) {
            return true;
        }
    }
    return done == NULL;
}

static Object* coop_autotest_interact_item = NULL;
static Object* coop_autotest_interact_door = NULL;

static bool coop_autotest_item_picked_up()
{
    return coop_autotest_interact_item->owner == obj_dude;
}

static bool coop_autotest_door_open()
{
    return obj_is_open(coop_autotest_interact_door) != 0;
}

static bool coop_autotest_anims_done()
{
    return !anim_busy(obj_dude);
}

// ONE PC (F9) with player 2 in control, in Vault 13: player 2 picks up an
// item, opens a door and uses a stimpak. (Locks are up to door scripts:
// the engine itself neither keeps unscripted doors shut nor picks them.)
static int coop_autotest_interact(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p1 = coop_player(0);
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    MapTransition transition;
    transition.map = map_match_map_name("VAULT13.MAP");
    transition.elevation = 0;
    transition.tile = -1;
    transition.rotation = 0;
    map_leave_map(&transition);
    map_check_state();
    coop_autotest_frames_until(1000, NULL);
    autotest_log("map %s, p1 tile %d, p2 tile %d", map_data.name, p1->obj->tile, p2->obj->tile);

    // COOP_AUTOTEST_INTERACT_P1: the same with player 1, for comparison.
    bool asPlayer1 = getenv("COOP_AUTOTEST_INTERACT_P1") != NULL;
    if (!asPlayer1) {
        coop_handle_switch_key();
        if (obj_dude != p2->obj) {
            autotest_fail("F9 did not give player 2 control");
            return -1;
        }
    }
    PlayerState* actor = asPlayer1 ? p1 : p2;

    // 1. An item on the ground next to player 2.
    Object* item;
    if (obj_pid_new(&item, PROTO_ID_STIMPACK) == -1) {
        autotest_fail("no stimpak");
        return -1;
    }
    obj_attempt_placement(item, obj_dude->tile, obj_dude->elevation, 1);
    coop_autotest_interact_item = item;
    action_get_an_object(obj_dude, item);
    if (!coop_autotest_frames_until(8000, coop_autotest_item_picked_up)) {
        autotest_fail("player 2 did not pick up the stimpak (owner %p)", (void*)item->owner);
    } else {
        autotest_log("player 2 picked up the stimpak");
    }
    coop_autotest_frames_until(5000, coop_autotest_anims_done);

    // 2. The nearest unscripted door: open it.
    Object* door = NULL;
    int best = 9999;
    for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL; obj = obj_find_next_at()) {
        Proto* proto;
        if (PID_TYPE(obj->pid) != OBJ_TYPE_SCENERY || proto_ptr(obj->pid, &proto) == -1
            || proto->scenery.type != SCENERY_TYPE_DOOR || obj->sid != -1) {
            continue;
        }
        int distance = obj_dist(obj_dude, obj);
        if (distance < best && make_path(obj_dude, obj_dude->tile, obj->tile, NULL, 0) > 0) {
            best = distance;
            door = obj;
        }
    }
    if (door == NULL) {
        autotest_fail("no door to try");
        return -1;
    }
    coop_autotest_interact_door = door;
    autotest_log("door at tile %d, distance %d, open %d", door->tile, best, obj_is_open(door));
    if (obj_is_open(door)) {
        obj_close(door);
        coop_autotest_frames_until(2000, NULL);
    }
    action_use_an_object(obj_dude, door);
    if (!coop_autotest_frames_until(8000, coop_autotest_door_open)) {
        autotest_fail("player 2 did not open the door");
    } else {
        autotest_log("player 2 opened the door");
    }
    coop_autotest_frames_until(5000, coop_autotest_anims_done);

    // 3. The stimpak, used on themself while hurt.
    critter_adjust_hits(obj_dude, -10);
    int hurt = critter_get_hits(obj_dude);
    // As the inventory's USE does.
    if (item_d_take_drug(obj_dude, item) == 1) {
        item_remove_mult(obj_dude, item, 1);
        obj_connect(item, obj_dude->tile, obj_dude->elevation, NULL);
        obj_destroy(item);
    }
    coop_autotest_frames_until(2000, NULL);
    autotest_log("stimpak: hp %d -> %d", hurt, critter_get_hits(obj_dude));
    if (critter_get_hits(obj_dude) <= hurt) {
        autotest_fail("the stimpak did not heal player 2");
    }

    if (obj_dude != actor->obj) {
        autotest_fail("control changed unexpectedly");
    }
    return 0;
}

// Sum of the radiation sickness bonuses on the primary stats.
static int coop_autotest_rad_penalty(Object* obj)
{
    int sum = 0;
    for (int stat = STAT_STRENGTH; stat <= STAT_AGILITY; stat++) {
        sum += stat_get_bonus(obj, stat);
    }
    return sum;
}

static void coop_autotest_pass_hours(int hours)
{
    for (int hour = 0; hour < hours; hour++) {
        inc_game_time(GAME_TIME_TICKS_PER_HOUR);
        queue_process();
    }
}

// Radiation sickness: player 2 with lots of rads gets sick (their stats
// drop) and gets better a week later; player 1 without rads stays well.
static int coop_autotest_rads(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p1 = coop_player(0);
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    int p1Before = coop_autotest_rad_penalty(p1->obj);
    int p2Before = coop_autotest_rad_penalty(p2->obj);
    {
        ActivePlayerScope scope(p2);
        critter_adjust_rads(p2->obj, 450);
    }
    autotest_log("player 2 rads %d, player 1 rads %d", critter_get_rads(p2->obj), critter_get_rads(p1->obj));

    coop_autotest_pass_hours(30);
    int p1Sick = coop_autotest_rad_penalty(p1->obj);
    int p2Sick = coop_autotest_rad_penalty(p2->obj);
    autotest_log("after 30 hours: player 1 stat bonus %d -> %d, player 2 %d -> %d, player 2 hp %d",
        p1Before, p1Sick, p2Before, p2Sick, critter_get_hits(p2->obj));
    if (p2Sick >= p2Before) {
        autotest_fail("player 2 did not get radiation sickness");
    }
    if (p1Sick != p1Before) {
        autotest_fail("player 1 got player 2's radiation sickness");
    }
    if (critter_is_dead(p2->obj) || critter_is_dead(p1->obj)) {
        autotest_fail("a player died");
    }

    // The sickness wears off after a week (the rads stay until treated).
    {
        ActivePlayerScope scope(p2);
        critter_adjust_rads(p2->obj, -critter_get_rads(p2->obj));
    }
    coop_autotest_pass_hours(8 * 24);
    int p2Well = coop_autotest_rad_penalty(p2->obj);
    autotest_log("after 8 more days: player 2 stat bonus %d", p2Well);
    if (p2Well != p2Before) {
        autotest_fail("player 2's radiation sickness did not wear off");
    }

    // Both sick at once: each keeps their own sickness and recovery.
    for (int index = 0; index < 2; index++) {
        ActivePlayerScope scope(coop_player(index));
        critter_adjust_rads(obj_dude, 450);
    }
    coop_autotest_pass_hours(30);
    int p1Both = coop_autotest_rad_penalty(p1->obj);
    int p2Both = coop_autotest_rad_penalty(p2->obj);
    autotest_log("both sick: player 1 stat bonus %d, player 2 %d", p1Both, p2Both);
    if (p1Both >= p1Before || p2Both >= p2Before) {
        autotest_fail("both players should be sick");
    }
    for (int index = 0; index < 2; index++) {
        ActivePlayerScope scope(coop_player(index));
        critter_adjust_rads(obj_dude, -critter_get_rads(obj_dude));
    }
    coop_autotest_pass_hours(8 * 24);
    autotest_log("both well again: player 1 stat bonus %d, player 2 %d",
        coop_autotest_rad_penalty(p1->obj), coop_autotest_rad_penalty(p2->obj));
    if (coop_autotest_rad_penalty(p1->obj) != p1Before || coop_autotest_rad_penalty(p2->obj) != p2Before) {
        autotest_fail("radiation sickness did not wear off for both");
    }

    return 0;
}

static int coop_autotest_travel(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p1 = coop_player(0);
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // A scripted item in player 2's pack must keep its script across map
    // changes and saves, like the party's items do.
    Object* scriptedItem = NULL;
    for (int pid = 1; pid < 1000 && scriptedItem == NULL; pid++) {
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || proto->sid == -1) {
            continue;
        }

        Object* item;
        if (obj_pid_new(&item, pid) == -1) {
            continue;
        }

        if (item->sid == -1 || item_add_force(p2->obj, item, 1) != 0) {
            obj_erase_object(item, NULL);
            continue;
        }

        obj_disconnect(item, NULL);
        scriptedItem = item;
        autotest_log("player 2 carries scripted item %s", proto_name(pid));
    }

    // Exit grids: find one leading to another map (not the world map,
    // whose screen would wait for input).
    Object* exitGrid = NULL;
    for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
        if (obj->pid >= 0x5000010 && obj->pid <= 0x5000017 && obj->data.misc.map > 0 && obj->elevation == obj_dude->elevation) {
            exitGrid = obj;
            break;
        }
    }

    char mapName[16];
    strcpy(mapName, map_data.name);

    if (exitGrid == NULL) {
        autotest_log("no exit grid to another map on %s; exit grid checks skipped", mapName);
    } else {
        autotest_log("exit grid at tile %d -> map %d", exitGrid->tile, exitGrid->data.misc.map);

        coop_set_controlled(p2);
        obj_move_to_tile(p2->obj, exitGrid->tile, exitGrid->elevation, NULL);
        coop_set_controlled(p1);
        map_check_state();
        if (strcmp(map_data.name, mapName) != 0) {
            autotest_fail("player 2 left the map through an exit grid");
            return -1;
        }
        autotest_log("player 2 on exit grid: stayed on %s", map_data.name);

        obj_move_to_tile(p1->obj, exitGrid->tile, exitGrid->elevation, NULL);
        map_check_state();
        if (strcmp(map_data.name, mapName) == 0) {
            autotest_fail("player 1 did not leave the map through an exit grid");
        } else {
            autotest_log("player 1 on exit grid: moved to %s", map_data.name);
        }

        coop_autotest_players_together("after exit grid");
    }

    // Script-style map change.
    MapTransition transition;
    transition.map = map_match_map_name("V13Ent.map");
    transition.elevation = 0;
    transition.tile = -1;
    transition.rotation = 0;
    map_leave_map(&transition);
    map_check_state();
    autotest_log("after load_map: %s", map_data.name);
    coop_autotest_players_together("after map change");

    // Elevation change: player 2 follows.
    int otherElevation = obj_dude->elevation == 0 ? 1 : 0;
    obj_move_to_tile(obj_dude, obj_dude->tile, otherElevation, NULL);
    map_set_elevation(otherElevation);
    coop_autotest_players_together("after elevation change");

    tile_refresh_display();
    dump_screen();

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (SaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save after travel failed");
        return -1;
    }

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (LoadGame(LOAD_SAVE_MODE_QUICK) != 1 || coop_player_count() != 2) {
        autotest_fail("load after travel failed");
        return -1;
    }

    coop_autotest_players_together("after load");

    if (scriptedItem != NULL) {
        int scripted = 0;
        Inventory* inventory = &(coop_player(1)->obj->data.inventory);
        for (int index = 0; index < inventory->length; index++) {
            Object* item = inventory->items[index].item;
            Script* script;
            if (item->sid != -1) {
                if (scr_ptr(item->sid, &script) == -1) {
                    autotest_fail("player 2's %s lost its script", object_name(item));
                } else {
                    scripted++;
                }
            }
        }
        autotest_log("player 2 has %d scripted items after travel and load", scripted);
        if (scripted == 0) {
            autotest_fail("player 2's scripted item is gone");
        }
    }

    return 0;
}

// Points of a 640x480 layout that the game centers on bigger screens
// (character editor, the view around the player) are queued with this
// flag and placed when used: a client only knows player 1's frame size
// once frames arrive, and its frames can be scaled to fit.
#define COOP_AUTOTEST_CENTERED 0x4000

static void coop_autotest_mouse_transform(int* x, int* y)
{
    bool client = coop_net_mode() == COOP_NET_CLIENT;
    // The layout is centered on the host's screen; a client's frames may
    // be that screen scaled down.
    int hostWidth = screenGetWidth();
    int hostHeight = screenGetHeight();
    int frameWidth = 0;
    int frameHeight = 0;
    bool frames = client && coop_net_client_frame_size(&frameWidth, &frameHeight);
    if (client && !coop_net_client_host_size(&hostWidth, &hostHeight) && frames) {
        hostWidth = frameWidth;
        hostHeight = frameHeight;
    }
    if ((*x & COOP_AUTOTEST_CENTERED) != 0) {
        *x = (*x & ~COOP_AUTOTEST_CENTERED) + (hostWidth - 640) / 2;
        *y += (hostHeight - 480) / 2;
    }
    if (frames) {
        // Host screen -> frame (centre of the pixel) -> this window.
        *x = (2 * *x + 1) * frameWidth / (2 * hostWidth);
        *y = (2 * *y + 1) * frameHeight / (2 * hostHeight);
        coop_net_client_frame_to_window(x, y);
    }
}

static void coop_autotest_click_centered(int x, int y)
{
    inject_set_mouse_transform(coop_autotest_mouse_transform);
    inject_click(x | COOP_AUTOTEST_CENTERED, y);
}

// The F9 debug key: player 2 joins through the character creation screen
// (driven by injected key presses), then control switches back and forth.
static int coop_autotest_join(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    Object* p1obj = obj_dude;

    // Cancelling character creation leaves the game as it was.
    inject_key(KEY_ESCAPE);
    coop_handle_switch_key();
    if (coop_player_count() != 1 || obj_dude != p1obj) {
        autotest_fail("cancelled creation still added player 2");
        return -1;
    }
    autotest_log("creation cancelled: still one player");

    // Character creation by mouse (640x480 editor layout): tag three
    // skills, spend the 5 character points on Strength, screenshot, Done.
    inject_wait(10);
    coop_autotest_click_centered(355, 31);
    inject_wait(20);
    coop_autotest_click_centered(355, 42);
    inject_wait(20);
    coop_autotest_click_centered(355, 53);
    inject_wait(20);
    // The stat slider runs its own repeat loop; give it time to settle.
    for (int index = 0; index < 5; index++) {
        coop_autotest_click_centered(157, 42);
        inject_wait(20);
    }
    inject_key(KEY_F12);
    inject_wait(3);
    // Done, then pick the second look (leather jacket).
    inject_key(KEY_RETURN);
    inject_wait(30);
    inject_key(KEY_ARROW_DOWN);
    inject_wait(3);
    inject_key(KEY_RETURN);
    coop_handle_switch_key();

    // Drop whatever the editor did not consume.
    while (inject_pending()) {
        inject_update();
    }

    if (coop_player_count() != 2) {
        autotest_fail("player 2 was not created");
        return -1;
    }

    PlayerState* p2 = coop_player(1);
    autotest_log("player 2 '%s' joined: level %d, ST %d, %d/%d hp", p2->name,
        p2->pcStats[PC_STAT_LEVEL], stat_level(p2->obj, STAT_STRENGTH), critter_get_hits(p2->obj), stat_level(p2->obj, STAT_MAXIMUM_HIT_POINTS));

    {
        ActivePlayerScope scope(p2);
        autotest_dump_state("p2_created.txt");
    }

    if (critter_get_hits(p2->obj) != stat_level(p2->obj, STAT_MAXIMUM_HIT_POINTS)) {
        autotest_fail("player 2 does not start at full health");
    }

    int jacket = art_critter_index(stat_level(p2->obj, STAT_GENDER) == GENDER_MALE ? "hmmaxx" : "hfmaxx");
    autotest_log("player 2 look %d, art %d (leather jacket art %d)", p2->look, p2->obj->fid & 0xFFF, jacket);
    if (p2->look != COOP_LOOK_LEATHER_JACKET || (p2->obj->fid & 0xFFF) != jacket) {
        autotest_fail("player 2 does not wear the chosen look");
    }

    tile_refresh_display();
    dump_screen();

    coop_handle_switch_key();
    if (obj_dude != p2->obj) autotest_fail("F9 did not switch to player 2");

    tile_refresh_display();
    dump_screen();

    coop_handle_switch_key();
    if (obj_dude != p1obj) autotest_fail("F9 did not switch back to player 1");

    return 0;
}

// Not a test: lists the critter models that have every animation a player
// character needs (so none falls back to another model), and shows them
// standing in a row next to player 1, as candidates for player 2's look.
static int coop_autotest_looks(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    // Unarmed and every weapon type (WEAPON_ANIMATION_KNIFE .. LAUNCHER).
    static const int baseAnims[] = { ANIM_STAND, ANIM_WALK, ANIM_RUNNING, ANIM_THROW_PUNCH, ANIM_KICK_LEG, ANIM_DODGE_ANIM, ANIM_HIT_FROM_FRONT, ANIM_FALL_BACK };

    int candidates[32];
    int candidatesCount = 0;

    for (int index = 0; index < 1000; index++) {
        int standFid = art_id(OBJ_TYPE_CRITTER, index, ANIM_STAND, 0, 0);
        char* path = art_get_name(standFid);
        if (path == NULL || !art_exists(standFid)) {
            continue;
        }

        char name[16];
        const char* fileName = strrchr(path, '\\');
        strncpy(name, fileName != NULL ? fileName + 1 : path, 6);
        name[6] = '\0';

        int present = 0;
        int total = 0;
        for (int anim : baseAnims) {
            total++;
            present += art_exists((OBJ_TYPE_CRITTER << 24) | (anim << 16) | index) ? 1 : 0;
        }

        for (int weapon = WEAPON_ANIMATION_KNIFE; weapon <= WEAPON_ANIMATION_LAUNCHER; weapon++) {
            total++;
            present += art_exists((OBJ_TYPE_CRITTER << 24) | (ANIM_TAKE_OUT << 16) | (weapon << 12) | index) ? 1 : 0;
        }

        if (present * 100 / total >= 80) {
            autotest_log("look %d: %s %d/%d animations", index, name, present, total);
            // Even the vault suit lacks one, so allow that.
            if (present >= total - 1 && candidatesCount < 32) {
                candidates[candidatesCount++] = index;
            }
        }
    }

    // Show the complete ones in rows of 6.
    int shown = 0;
    while (shown < candidatesCount) {
        Object* row[6];
        int rowCount = 0;
        int tile = tile_num_in_direction(obj_dude->tile, 1, 2);
        for (; rowCount < 6 && shown < candidatesCount; rowCount++, shown++) {
            Object* obj;
            obj_new(&obj, art_id(OBJ_TYPE_CRITTER, candidates[shown], ANIM_STAND, 0, 2), 0x1000001);
            obj_move_to_tile(obj, tile, obj_dude->elevation, NULL);
            obj_set_rotation(obj, 2, NULL);
            obj_set_light(obj, 2, 0x10000, NULL);
            row[rowCount] = obj;
            autotest_log("screenshot %d position %d: look %d", shown / 6, rowCount + 1, candidates[shown]);
            tile = tile_num_in_direction(tile, 2, 2);
        }

        tile_set_center(row[rowCount / 2]->tile, TILE_SET_CENTER_REFRESH_WINDOW);
        tile_refresh_display();
        dump_screen();

        for (int index = 0; index < rowCount; index++) {
            obj_erase_object(row[index], NULL);
        }
    }

    return 0;
}

// Player 2 reaching a merchant trades directly (no conversation).
static int coop_autotest_barter(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_autotest_scene();

    // A merchant of the map by name (COOP_AUTOTEST_MERCHANT), else a
    // generic one.
    Object* localMerchant = NULL;
    const char* merchantName = getenv("COOP_AUTOTEST_MERCHANT");
    if (merchantName != NULL) {
        for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
            if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critter_name(obj), merchantName) != NULL) {
                localMerchant = obj;
                break;
            }
        }
        if (localMerchant == NULL) {
            autotest_log("no %s on this map", merchantName);
        }
    }

    int merchantPid = -1;
    for (int index = 1; index < 400 && merchantPid == -1; index++) {
        Proto* proto;
        if (proto_ptr(0x1000000 | index, &proto) != -1 && compat_stricmp(proto_name(0x1000000 | index), "Merchant") == 0) {
            merchantPid = 0x1000000 | index;
        }
    }

    if (getenv("COOP_AUTOTEST_HEADS") != NULL) {
        for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
            Proto* proto;
            if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && proto_ptr(obj->pid, &proto) != -1) {
                autotest_log("head %s pid %x headFid %x barter %d", critter_name(obj), obj->pid, proto->critter.headFid, (proto->critter.data.flags & CRITTER_BARTER) != 0);
            }
        }
    }

    Object* merchant = localMerchant;
    if (merchant != NULL) {
        obj_attempt_placement(p2->obj, merchant->tile, merchant->elevation, 1);
        tile_set_center(p2->obj->tile, TILE_SET_CENTER_REFRESH_WINDOW);
    } else if (merchantPid == -1 || obj_pid_new(&merchant, merchantPid) == -1) {
        autotest_fail("could not create a merchant");
        return -1;
    } else {
        obj_attempt_placement(merchant, p2->obj->tile, p2->obj->elevation, 1);
    }

    Object* stimpak;
    if (obj_pid_new(&stimpak, PROTO_ID_STIMPACK) == 0 && item_add_force(merchant, stimpak, 2) == 0) {
        obj_disconnect(stimpak, NULL);
    }

    if (!coop_can_barter_with(merchant)) {
        autotest_fail("merchant does not barter");
        return -1;
    }

    if (coop_can_barter_with(obj_dude)) {
        autotest_fail("player 1 counts as a merchant");
    }

    // Screenshot of the barter screen, then leave it.
    inject_wait(20);
    inject_call(dump_screen);
    inject_wait(3);
    inject_key(KEY_ESCAPE);

    coop_request_barter(p2, merchant);
    coop_process_requests();

    while (inject_pending()) {
        inject_update();
    }

    if (obj_dude != coop_player(0)->obj || coop_active_player() != coop_player(0)) {
        autotest_fail("barter left player 2 active");
    }

    autotest_log("barter with %s done", critter_name(merchant));

    tile_refresh_display();
    dump_screen();

    return 0;
}

// Seen from inside coop_autotest_talk's conversation.
static Object* coop_autotest_talk_dude = NULL;
static PlayerState* coop_autotest_talk_viewer_check = NULL;
static bool coop_autotest_talk_seen = false;

static void coop_autotest_talk_check()
{
    coop_autotest_talk_seen = dialog_active();
    coop_autotest_talk_dude = obj_dude;
    autotest_log("in the conversation: dialog %d, dude %s, active player %d", dialog_active() ? 1 : 0,
        obj_dude != NULL ? critter_name(obj_dude) : "-", coop_active_player() == coop_player(1) ? 2 : 1);
    dump_screen();
}

// Player 2 starts a conversation (COOP_AUTOTEST_TALK, on COOP_AUTOTEST_MAP;
// default Aradesh in Shady Sands): it runs as player 2's character, and
// afterwards player 1 is back in charge.
static int coop_autotest_talk(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    const char* mapName = getenv("COOP_AUTOTEST_MAP") != NULL ? getenv("COOP_AUTOTEST_MAP") : "SHADYW.MAP";
    const char* name = getenv("COOP_AUTOTEST_TALK") != NULL ? getenv("COOP_AUTOTEST_TALK") : "Aradesh";
    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = map_match_map_name(mapName);
    transition.tile = -1;
    if (transition.map == -1) {
        autotest_fail("no map %s", mapName);
        return -1;
    }
    map_leave_map(&transition);
    map_check_state();

    Object* npc = NULL;
    for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critter_name(obj), name) != NULL) {
            npc = obj;
            break;
        }
    }
    if (npc == NULL) {
        for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
            if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER) {
                autotest_log("%s: critter '%s' elev %d", map_data.name, critter_name(obj), obj->elevation);
            }
        }
        autotest_fail("no %s on %s", name, map_data.name);
        return -1;
    }

    // Player 2 next to them, player 1 where they are.
    if (npc->elevation != obj_dude->elevation) {
        obj_move_to_tile(obj_dude, npc->tile, npc->elevation, NULL);
        map_set_elevation(npc->elevation);
    }
    for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
        int distance = tile_dist(tile, npc->tile);
        if (distance >= 1 && distance <= 2 && obj_blocking_at(NULL, tile, npc->elevation) == NULL
            && make_path_func(npc, npc->tile, tile, NULL, 0, obj_sight_blocking_at) != 0) {
            obj_move_to_tile(p2->obj, tile, npc->elevation, NULL);
            break;
        }
    }
    tile_set_center(p2->obj->tile, TILE_SET_CENTER_REFRESH_WINDOW);

    if (!coop_talk_setting()) {
        autotest_fail("[coop]talk is off");
    }

    // Look around once inside, then answer (first option, Escape now and
    // then) until it ends.
    inject_wait(30);
    inject_call(coop_autotest_talk_check);
    add_bk_process(coop_autotest_answer_dialogs);

    scripts_request_dialog_by(p2->obj, npc);
    scripts_check_state();
    remove_bk_process(coop_autotest_answer_dialogs);
    while (inject_pending()) {
        inject_update();
    }

    autotest_log("talked to %s", critter_name(npc));
    if (!coop_autotest_talk_seen) {
        autotest_fail("the conversation did not start");
    } else if (coop_autotest_talk_dude != p2->obj) {
        autotest_fail("the conversation was not player 2's");
    }
    if (obj_dude != coop_player(0)->obj || coop_active_player() != coop_player(0)) {
        autotest_fail("the conversation left player 2 active");
    }
    if (dialog_active()) {
        autotest_fail("the conversation did not end");
    }

    tile_refresh_display();
    dump_screen();
    return 0;
}

// Every sound effect (COOP_AUTOTEST_DB_PATTERN, default the SFX folder) read
// from the game's data into a buffer of its listed size: nothing may be
// written past it (soak runs found a sound cache block damaged on load).
static int coop_autotest_db_sfx(AutotestNewGameProc* newGame)
{
    const char* pattern = getenv("COOP_AUTOTEST_DB_PATTERN") != NULL ? getenv("COOP_AUTOTEST_DB_PATTERN") : "sound\\sfx\\*.acm";
    char** names;
    int count = db_get_file_list(pattern, &names, NULL, 0);
    const int guard = 4096;
    int bad = 0;
    for (int index = 0; index < count; index++) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%.*s%s", (int)(strrchr(pattern, '\\') - pattern + 1), pattern, names[index]);
        dir_entry de;
        if (db_dir_entry(path, &de) != 0) {
            autotest_log("%s: no entry", path);
            continue;
        }
        std::vector<unsigned char> buffer(de.length + guard, 0xA5);
        db_read_to_buf(path, buffer.data());
        int over = 0;
        for (int at = guard - 1; at >= 0; at--) {
            if (buffer[de.length + at] != 0xA5) {
                over = at + 1;
                break;
            }
        }
        if (over != 0) {
            bad++;
            autotest_log("%s: %d bytes (flags %x, packed %d) and %d bytes written past them", path, de.length, de.flags, de.field_C, over);
        }
    }
    autotest_log("%d files read, %d wrote past their size", count, bad);
    if (bad != 0) {
        autotest_fail("files are written past their size");
    }
    db_free_file_list(&names, NULL);
    return 0;
}

// How many stimpaks `critter` carries.
static int coop_autotest_stimpaks(Object* critter)
{
    int count = 0;
    for (int index = 0; index < critter->data.inventory.length; index++) {
        if (critter->data.inventory.items[index].item->pid == PROTO_ID_STIMPACK) {
            count += critter->data.inventory.items[index].quantity;
        }
    }
    return count;
}

// The giver and the receiver of coop_autotest_give's item move.
static Object* coop_autotest_give_from = NULL;
static Object* coop_autotest_give_to = NULL;

// While the give screen is open: moves a stimpak across, as dragging it
// would, and checks the screen belongs to the giver.
static void coop_autotest_give_move()
{
    if (obj_dude != coop_autotest_give_from) {
        autotest_fail("give screen open for the wrong player");
    }
    Object* stimpak = NULL;
    for (int index = 0; index < coop_autotest_give_from->data.inventory.length; index++) {
        Object* item = coop_autotest_give_from->data.inventory.items[index].item;
        if (item->pid == PROTO_ID_STIMPACK) {
            stimpak = item;
        }
    }
    if (stimpak == NULL || item_move(coop_autotest_give_from, coop_autotest_give_to, stimpak, 1) != 0) {
        autotest_fail("could not move the stimpak");
    }
    dump_screen();
}

// Talking to the other player opens the loot screen between the two to
// give each other items, both ways; equipped things stay where they are,
// and it is not stealing (no experience, nobody gets caught).
static int coop_autotest_give(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    Object* p1obj = coop_player(0)->obj;
    Object* p2obj = p2->obj;
    const char* armor[] = { "Leather Jacket" };
    coop_autotest_equip(p2, armor, 1, 0);
    Object* p2armor = inven_worn(p2obj);

    for (int round = 0; round < 2; round++) {
        Object* from = round == 0 ? p1obj : p2obj;
        Object* to = round == 0 ? p2obj : p1obj;
        Object* stimpak;
        if (obj_pid_new(&stimpak, PROTO_ID_STIMPACK) != 0 || item_add_force(from, stimpak, 1) != 0) {
            autotest_fail("no stimpak");
            return -1;
        }
        obj_disconnect(stimpak, NULL);
        int before = coop_autotest_stimpaks(to);
        int xp = stat_pc_get(PC_STAT_EXPERIENCE);

        coop_autotest_give_from = from;
        coop_autotest_give_to = to;
        inject_wait(20);
        inject_call(coop_autotest_give_move);
        inject_wait(3);
        inject_key(KEY_ESCAPE);

        scripts_request_give_container(from, to);
        scripts_check_state();

        while (inject_pending()) {
            inject_update();
        }

        autotest_log("give %d: %s gave a stimpak to %s (%d -> %d)", round, critter_name(from), critter_name(to), before, coop_autotest_stimpaks(to));
        if (coop_autotest_stimpaks(to) != before + 1) {
            autotest_fail("the stimpak did not arrive");
        }
        if (stat_pc_get(PC_STAT_EXPERIENCE) != xp) {
            autotest_fail("giving gave experience");
        }
        if (obj_dude != p1obj || coop_active_player() != coop_player(0)) {
            autotest_fail("giving left the wrong player active");
        }
    }

    if (p2armor == NULL || inven_worn(p2obj) != p2armor) {
        autotest_fail("player 2's armor came off");
    }

    return 0;
}

// One hit of `damage` from `attacker` on `defender`, through the game's own
// damage code.
static void coop_autotest_hit(Object* attacker, Object* defender, int damage)
{
    Attack attack;
    combat_ctd_init(&attack, attacker, defender, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
    attack.attackerFlags = DAM_HIT;
    attack.defenderDamage = damage;
    death_checks(&attack);
    apply_damage(&attack, false);
}

// [coop]knockout=1: a player who would die is knocked out while the other
// is up, can't be finished off, and wakes up when healed; when nobody is
// left up, death is death. With knockout off the hit just kills.
static int coop_autotest_knockout(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    Object* p1obj = coop_player(0)->obj;
    Object* p2obj = p2->obj;
    Object* enemy = coop_autotest_spawn_enemy(p1obj);
    if (enemy == NULL) {
        return -1;
    }

    bool knockout = coop_knockout_enabled();
    autotest_log("knockout setting: %d", knockout ? 1 : 0);

    coop_autotest_hit(enemy, p2obj, 500);
    int results = p2obj->data.critter.combat.results;
    autotest_log("player 2 after a lethal hit: %d hp, dead %d, knocked out %d", critter_get_hits(p2obj),
        (results & DAM_DEAD) != 0 ? 1 : 0, (results & DAM_KNOCKED_OUT) != 0 ? 1 : 0);

    if (!knockout) {
        if ((results & DAM_DEAD) == 0) {
            autotest_fail("without knockout the hit did not kill");
        }
        return 0;
    }

    if ((results & DAM_DEAD) != 0 || (results & DAM_KNOCKED_OUT) == 0 || critter_get_hits(p2obj) != 1) {
        autotest_fail("player 2 was not knocked out");
        return 0;
    }

    coop_autotest_hit(enemy, p2obj, 500);
    if ((p2obj->data.critter.combat.results & DAM_DEAD) != 0 || critter_get_hits(p2obj) != 1) {
        autotest_fail("a knocked out player was hurt");
    }

    critter_adjust_hits(p2obj, 10);
    results = p2obj->data.critter.combat.results;
    autotest_log("player 2 healed: %d hp, knocked out %d", critter_get_hits(p2obj), (results & DAM_KNOCKED_OUT) != 0 ? 1 : 0);
    if ((results & DAM_KNOCKED_OUT) != 0) {
        autotest_fail("healing did not wake player 2");
    }

    // Player 1 down, then player 2 with nobody left up: dead.
    coop_autotest_hit(enemy, p1obj, 500);
    coop_autotest_hit(enemy, p2obj, 500);
    autotest_log("both down: player 1 knocked out %d, player 2 dead %d",
        (p1obj->data.critter.combat.results & DAM_KNOCKED_OUT) != 0 ? 1 : 0,
        (p2obj->data.critter.combat.results & DAM_DEAD) != 0 ? 1 : 0);
    if ((p1obj->data.critter.combat.results & DAM_KNOCKED_OUT) == 0
        || (p2obj->data.critter.combat.results & DAM_DEAD) == 0) {
        autotest_fail("the last player up did not die");
    }

    return 0;
}

// Player 2 is poisoned and gets rads while player 1 is the active player
// (an enemy's poison, a script's rads): it lands on player 2, the poison
// ticks and wears off for them, and player 1's own poison is left alone.
static int coop_autotest_poison(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    Object* p1obj = coop_player(0)->obj;
    Object* p2obj = p2->obj;

    critter_adjust_poison(p1obj, 6);
    critter_adjust_poison(p2obj, 12);
    critter_adjust_rads(p2obj, 40);
    int p1Poison = critter_get_poison(p1obj);
    int p2Poison = critter_get_poison(p2obj);
    int p2Hp = critter_get_hits(p2obj);
    autotest_log("after: player 1 poison %d, player 2 poison %d, rads %d", p1Poison, p2Poison, critter_get_rads(p2obj));
    if (p2Poison <= 0 || critter_get_rads(p2obj) <= 0) {
        autotest_fail("player 2 got no poison or rads");
    }
    if (obj_dude != p1obj) {
        autotest_fail("player 1 is no longer active");
    }

    // A few game hours: the poison does damage and wears off, for each.
    for (int step = 0; step < 60; step++) {
        inc_game_time_in_seconds(60);
        queue_process();
    }
    autotest_log("later: player 1 poison %d, player 2 poison %d (hp %d -> %d)", critter_get_poison(p1obj),
        critter_get_poison(p2obj), p2Hp, critter_get_hits(p2obj));
    if (critter_get_poison(p2obj) >= p2Poison || critter_get_hits(p2obj) >= p2Hp) {
        autotest_fail("player 2's poison did not work");
    }
    if (critter_get_poison(p1obj) >= p1Poison) {
        autotest_fail("player 1's poison stopped (the other player's cleared it)");
    }
    if (obj_dude != p1obj || coop_active_player() != coop_player(0)) {
        autotest_fail("the wrong player is active afterwards");
    }

    return 0;
}

// What coop_autotest_character compares before and after the round trip.
struct CoopAutotestCharacter {
    std::string name;
    int level;
    int hp;
    int maxHp;
    int strength;
    int smallGuns;
    int stimpaks;
    int armorPid;
};

static CoopAutotestCharacter coop_autotest_character_now(PlayerState* player)
{
    CoopAutotestCharacter c;
    ActivePlayerScope scope(player);
    Object* obj = player->obj;
    c.name = critter_name(obj);
    c.level = stat_pc_get(PC_STAT_LEVEL);
    c.hp = critter_get_hits(obj);
    c.maxHp = stat_level(obj, STAT_MAXIMUM_HIT_POINTS);
    c.strength = stat_level(obj, STAT_STRENGTH);
    c.smallGuns = skill_level(obj, SKILL_SMALL_GUNS);
    c.stimpaks = coop_autotest_stimpaks(obj);
    Object* armor = inven_worn(obj);
    c.armorPid = armor != NULL ? armor->pid : -1;
    return c;
}

// Player 2's own copy of their character: exported, player 2 gone, then
// imported again (as when joining another game with it). Everything that
// makes the character theirs comes back; player 1 is untouched.
static int coop_autotest_character(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    const char* armor[] = { "Leather Jacket" };
    coop_autotest_equip(p2, armor, 1, 0);
    Object* stimpak;
    if (obj_pid_new(&stimpak, PROTO_ID_STIMPACK) == 0 && item_add_force(p2->obj, stimpak, 3) == 0) {
        // (Created on the map; carried now.)
        obj_disconnect(stimpak, NULL);
    }
    {
        ActivePlayerScope scope(p2);
        stat_pc_add_experience(3000);
    }
    critter_adjust_hits(p2->obj, -5);

    Object* p1obj = coop_player(0)->obj;
    int p1Hp = critter_get_hits(p1obj);
    CoopAutotestCharacter before = coop_autotest_character_now(p2);

    std::vector<unsigned char> data;
    if (!coop_export_player2(&data)) {
        autotest_fail("export failed");
        return -1;
    }
    std::string name;
    int level = 0;
    coop_character_info(data, &name, &level);
    autotest_log("exported %d bytes: %s, level %d", (int)data.size(), name.c_str(), level);
    // For net_character_client (COOP_AUTOTEST_CHARACTER_FILE).
    FILE* copy = fopen("character-copy.dat", "wb");
    if (copy != NULL) {
        fwrite(data.data(), 1, data.size(), copy);
        fclose(copy);
    }
    if (name != before.name || level != before.level) {
        autotest_fail("the copy's name or level is wrong");
    }

    coop_reset();
    if (coop_player_count() != 1) {
        autotest_fail("player 2 is still there");
        return -1;
    }

    std::string error;
    PlayerState* back = coop_import_player2(data, &error);
    if (back == NULL) {
        autotest_fail("import failed: %s", error.c_str());
        return -1;
    }

    CoopAutotestCharacter after = coop_autotest_character_now(back);
    autotest_log("before: %s level %d hp %d/%d ST %d small guns %d stimpaks %d armor %x", before.name.c_str(), before.level,
        before.hp, before.maxHp, before.strength, before.smallGuns, before.stimpaks, before.armorPid);
    autotest_log("after:  %s level %d hp %d/%d ST %d small guns %d stimpaks %d armor %x", after.name.c_str(), after.level,
        after.hp, after.maxHp, after.strength, after.smallGuns, after.stimpaks, after.armorPid);
    if (after.name != before.name || after.level != before.level || after.hp != before.hp || after.maxHp != before.maxHp
        || after.strength != before.strength || after.smallGuns != before.smallGuns || after.stimpaks != before.stimpaks
        || after.armorPid != before.armorPid) {
        autotest_fail("the character changed on the way");
    }
    if (back->obj->tile == -1 || back->obj->elevation != p1obj->elevation || obj_dist(back->obj, p1obj) > 5) {
        autotest_fail("player 2 is not next to player 1");
    }
    if (obj_dude != p1obj || critter_get_hits(p1obj) != p1Hp) {
        autotest_fail("player 1 changed");
    }

    // The game still saves and loads with the imported player 2.
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (SaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save with the imported player 2 failed");
    }
    if (LoadGame(LOAD_SAVE_MODE_QUICK) != 1 || coop_player_count() != 2) {
        autotest_fail("load with the imported player 2 failed");
    } else {
        CoopAutotestCharacter loaded = coop_autotest_character_now(coop_player(1));
        autotest_log("after load: %s level %d stimpaks %d", loaded.name.c_str(), loaded.level, loaded.stimpaks);
        if (loaded.name != before.name || loaded.level != before.level || loaded.stimpaks != before.stimpaks) {
            autotest_fail("the imported player 2 changed through save and load");
        }
    }

    return 0;
}

// Player 2 joins a game without a player 2 and brings their own character
// (COOP_AUTOTEST_CHARACTER_FILE on the client, e.g. coop_character's
// character-copy.dat): the host uses it instead of character creation and
// sends a fresh copy back. Paired with net_character_client.
static int coop_autotest_net_character_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    coop_host_init();
    if (!coop_host_active() || !coop_autotest_relay_host_setup()) {
        autotest_fail("cannot host");
        return -1;
    }

    if (!coop_autotest_session_frames(30000, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }

    unsigned int start = get_time();
    while (coop_player_count() < 2 && elapsed_time(start) < 20000) {
        coop_autotest_session_frames(200, false, false);
    }
    if (coop_player_count() < 2) {
        autotest_fail("player 2 never came");
        return -1;
    }

    PlayerState* p2 = coop_player(1);
    int level;
    {
        ActivePlayerScope scope(p2);
        level = stat_pc_get(PC_STAT_LEVEL);
    }
    autotest_log("player 2: %s, level %d", critter_name(p2->obj), level);
    const char* expected = getenv("COOP_AUTOTEST_CHARACTER_NAME");
    if (expected != NULL && strcmp(critter_name(p2->obj), expected) != 0) {
        autotest_fail("player 2 is not the character they brought");
    }

    // Time for the copy to reach player 2, and a few steps.
    coop_autotest_session_frames(4000, false, false);
    dump_screen();
    coop_host_exit();
    return 0;
}

static int coop_autotest_net_character_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();

    if (const char* path = getenv("COOP_AUTOTEST_CHARACTER_FILE")) {
        std::vector<unsigned char> data;
        FILE* stream = fopen(path, "rb");
        if (stream != NULL) {
            unsigned char buffer[4096];
            size_t read;
            while ((read = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
                data.insert(data.end(), buffer, buffer + read);
            }
            fclose(stream);
        }
        std::string name;
        int level = 0;
        if (!coop_character_info(data, &name, &level)) {
            autotest_fail("no character in %s", path);
        }
        autotest_log("bringing %s, level %d", name.c_str(), level);
        coop_net_set_bring_character(data);
    }
    remove(COOP_NET_CHARACTER_FILE);

    inject_wait(200);
    inject_call(coop_autotest_screenshot);
    int rc = coop_net_client_run();
    autotest_log("client finished with %d", rc);

    // The host sent back a copy of the character to keep.
    std::vector<unsigned char> copy;
    FILE* stream = fopen(COOP_NET_CHARACTER_FILE, "rb");
    if (stream != NULL) {
        unsigned char buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
            copy.insert(copy.end(), buffer, buffer + read);
        }
        fclose(stream);
    }
    std::string name;
    int level = 0;
    if (!coop_character_info(copy, &name, &level)) {
        autotest_fail("no copy of the character was kept");
    } else {
        autotest_log("kept a copy: %s, level %d", name.c_str(), level);
    }
    return 0;
}

// While net_talk_host's conversation runs: whose it is.
static void coop_autotest_net_talk_watch()
{
    if (dialog_active() && !coop_autotest_talk_seen) {
        coop_autotest_talk_seen = true;
        coop_autotest_talk_dude = obj_dude;
        autotest_log("conversation on: dude %s", critter_name(obj_dude));
    }
}

// Over the network, player 2 talks to Aradesh in Shady Sands and ends the
// conversation with their own keys (net_talk_client presses 6, "I'll just
// be going now", then 1, [Done]); the host never answers for them.
static int coop_autotest_net_talk_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    // Player 2's character is ready (no character creation over the
    // network).
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_host_active() || !coop_autotest_relay_host_setup()) {
        autotest_fail("cannot host");
        return -1;
    }

    if (!coop_autotest_session_frames(30000, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }

    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = map_match_map_name("SHADYW.MAP");
    transition.tile = -1;
    map_leave_map(&transition);
    map_check_state();

    Object* npc = NULL;
    for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critter_name(obj), "Aradesh") != NULL) {
            npc = obj;
            break;
        }
    }
    if (npc == NULL) {
        autotest_fail("no Aradesh");
        return -1;
    }
    for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
        int distance = tile_dist(tile, npc->tile);
        if (distance >= 1 && distance <= 2 && obj_blocking_at(NULL, tile, npc->elevation) == NULL
            && make_path_func(npc, npc->tile, tile, NULL, 0, obj_sight_blocking_at) != 0) {
            obj_move_to_tile(p2->obj, tile, npc->elevation, NULL);
            break;
        }
    }
    coop_autotest_session_frames(2000, false, false);

    add_bk_process(coop_autotest_net_talk_watch);
    unsigned int start = get_time();
    scripts_request_dialog_by(p2->obj, npc);
    scripts_check_state();
    remove_bk_process(coop_autotest_net_talk_watch);
    autotest_log("player 2's conversation took %u ms", elapsed_time(start));

    if (!coop_autotest_talk_seen) {
        autotest_fail("the conversation did not start");
    } else if (coop_autotest_talk_dude != p2->obj) {
        autotest_fail("the conversation was not player 2's");
    }
    if (obj_dude != coop_player(0)->obj || coop_active_player() != coop_player(0)) {
        autotest_fail("the conversation left player 2 active");
    }

    // (No more frames: Aradesh has Seth show them out, to the world map.)
    dump_screen();
    coop_host_exit();
    return 0;
}

static void coop_autotest_net_talk_keys()
{
    static int presses = 0;
    inject_wait(90);
    inject_key(++presses % 2 == 1 ? KEY_6 : KEY_1); // "I'll just be going now", then [Done]
    inject_call(coop_autotest_screenshot);
    inject_call(coop_autotest_net_talk_keys);
}

static int coop_autotest_net_talk_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();
    inject_wait(300);
    inject_call(coop_autotest_net_talk_keys);
    int rc = coop_net_client_run();
    autotest_log("client finished with %d", rc);
    return 0;
}

// Runs the input loop for `ms`, collecting the keys it returns.
static std::vector<int> coop_autotest_input_for(unsigned int ms)
{
    std::vector<int> keys;
    unsigned int start = get_time();
    while (elapsed_time(start) < ms) {
        int key = get_input();
        if (key != -1) {
            keys.push_back(key);
        }
        renderPresent();
        SDL_Delay(10);
    }
    return keys;
}

// A game controller (a virtual one, made by SDL): the left stick moves the
// cursor, A clicks, X is the inventory key and RT ends the turn (Space).
static int coop_autotest_gamepad(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    SDL_VirtualJoystickDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "Autotest pad";
    int index = SDL_JoystickAttachVirtualEx(&desc);
    SDL_Joystick* joystick = index >= 0 ? SDL_JoystickOpen(index) : NULL;
    if (joystick == NULL) {
        autotest_fail("no virtual controller: %s", SDL_GetError());
        return -1;
    }
    coop_autotest_input_for(200);

    mouse_set_position(100, 100);
    int x0;
    int y0;
    mouse_get_position(&x0, &y0);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 32767);
    coop_autotest_input_for(300);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 0);
    coop_autotest_input_for(100);
    int x1;
    int y1;
    mouse_get_position(&x1, &y1);
    autotest_log("left stick right for 0.3 s: cursor %d,%d -> %d,%d", x0, y0, x1, y1);
    if (x1 - x0 < 100 || y1 != y0) {
        autotest_fail("the left stick did not move the cursor right");
    }

    SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 1);
    coop_autotest_input_for(100);
    bool clicked = (mouse_get_buttons() & MOUSE_EVENT_LEFT_BUTTON_DOWN_REPEAT) != 0;
    SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 0);
    coop_autotest_input_for(100);
    autotest_log("A held: left button %d", clicked ? 1 : 0);
    if (!clicked) {
        autotest_fail("A did not press the left mouse button");
    }

    SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_X, 1);
    std::vector<int> keys = coop_autotest_input_for(100);
    SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_X, 0);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    std::vector<int> more = coop_autotest_input_for(100);
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
    coop_autotest_input_for(100);
    keys.insert(keys.end(), more.begin(), more.end());
    bool inventory = std::find(keys.begin(), keys.end(), KEY_LOWERCASE_I) != keys.end();
    bool space = std::find(keys.begin(), keys.end(), KEY_SPACE) != keys.end();
    autotest_log("X gave the inventory key %d, RT gave Space %d", inventory ? 1 : 0, space ? 1 : 0);
    if (!inventory || !space) {
        autotest_fail("controller buttons did not give their keys");
    }

    SDL_JoystickClose(joystick);
    SDL_JoystickDetachVirtual(index);
    return 0;
}

// COOP_AUTOTEST_RELAY=host:port: the host opens a relay room and writes
// its code to COOP_AUTOTEST_RELAY_FILE; the client joins that code.
static bool coop_autotest_relay_host_setup()
{
    const char* relay = getenv("COOP_AUTOTEST_RELAY");
    const char* file = getenv("COOP_AUTOTEST_RELAY_FILE");
    if (relay == NULL || file == NULL) {
        return true;
    }

    coop_net_set_relay_server(relay);
    std::string error;
    const char* wanted = getenv("COOP_AUTOTEST_RELAY_CODE");
    if (!coop_net_relay_host_start(&error, wanted != NULL ? wanted : "")) {
        autotest_fail("relay: %s", error.c_str());
        return false;
    }

    FILE* stream = fopen(file, "w");
    if (stream != NULL) {
        fprintf(stream, "%s\n", coop_net_relay_code());
        fclose(stream);
    }
    autotest_log("relay code %s", coop_net_relay_code());
    return true;
}

static void coop_autotest_relay_client_setup()
{
    const char* relay = getenv("COOP_AUTOTEST_RELAY");
    const char* file = getenv("COOP_AUTOTEST_RELAY_FILE");
    if (relay == NULL || file == NULL) {
        return;
    }

    char code[32] = "";
    unsigned int start = get_time();
    while (elapsed_time(start) < 30000) {
        FILE* stream = fopen(file, "r");
        if (stream != NULL) {
            bool read = fgets(code, sizeof(code), stream) != NULL;
            fclose(stream);
            if (read && strlen(code) >= 6) {
                break;
            }
        }
        SDL_Delay(100);
    }
    code[strcspn(code, "\r\n")] = '\0';

    // Typed by hand, with a dash, as a player might.
    std::string typed = std::string(code).substr(0, 3) + "-" + std::string(code).substr(3);
    std::string parsed;
    if (!coop_net_parse_code(typed.c_str(), &parsed)) {
        autotest_fail("relay code '%s' not understood", typed.c_str());
    }
    coop_net_set_relay_server(relay);
    coop_net_set_relay_join(parsed);
    autotest_log("joining relay code %s", parsed.c_str());
}

// Network: host a game and wait (up to 30 s) for a client to join, then
// for it to leave. Paired with "net_client" by tools/nettest.sh.
static int coop_autotest_net_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    // Found by LAN searches (the server browser), like a real host; with
    // COOP_AUTOTEST_LOBBY_SERVER also listed online there.
    const char* lobbyServer = getenv("COOP_AUTOTEST_LOBBY_SERVER");

    if (!coop_net_host_start()) {
        autotest_fail("cannot host");
        return -1;
    }

    // With COOP_AUTOTEST_RELAY the listing carries the relay code.
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }
    coop_lobby_host_start(lobbyServer != NULL ? lobbyServer : "", lobbyServer != NULL);

    autotest_log("hosting, checksum %016llx", (unsigned long long)coop_net_data_checksum());

    unsigned int start = get_time();
    while (elapsed_time(start) < 30000 && !coop_net_client_connected()) {
        coop_net_host_pump();
        coop_lobby_host_pump(critter_name(obj_dude), coop_net_client_connected() ? 2 : 1);
        SDL_Delay(10);
    }

    if (!coop_net_client_connected()) {
        autotest_fail("no client joined");
        return -1;
    }

    autotest_log("client joined");

    // Stream a few seconds of frames (each present sends one), with a
    // status line, then take a screenshot and leave; the client takes its
    // own when we are gone, and the two must show the same picture.
    coop_net_send_status("Player 1 is testing");

    // Speech reaches the client too (the hook is set by coop_host_init();
    // here it is set by hand).
    gsound_event_hook = coop_net_send_sound;
    gsound_speech_play("narrator\\options", 12, 13, 15);

    start = get_time();
    while (elapsed_time(start) < 3000) {
        tile_refresh_display();
        renderPresent();
        SDL_Delay(30);
    }

    dump_screen();
    coop_lobby_host_stop();
    coop_net_exit();

    autotest_log("left");
    return 0;
}

static void coop_autotest_net_client_exit()
{
    dump_screen();
}

static int coop_autotest_net_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();
    autotest_log("connecting, checksum %016llx", (unsigned long long)coop_net_data_checksum());
    coop_net_set_client_exit_hook(coop_autotest_net_client_exit);
    int rc = coop_net_client_run();
    autotest_log("client finished with %d", rc);
    if (rc != 0) {
        autotest_fail("client failed");
    }

    return 0;
}

// Network play, host side: runs the real main loop pieces while the client
// creates player 2 and walks them somewhere. Paired with net_play_client.
static int coop_autotest_net_play_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    coop_host_init();
    if (!coop_host_active()) {
        autotest_fail("cannot host");
        return -1;
    }

    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }

    int startTile = -1;
    bool moved = false;
    const int chatKeys[] = { KEY_LOWERCASE_T, 'W', 'a', 'r', ' ', 'n', 'e', 'v', 'e', 'r', ' ', 'c', 'h', 'a', 'n', 'g', 'e', 's', KEY_RETURN };
    int chatKey = 0;
    unsigned int start = get_time();
    while (elapsed_time(start) < 90000) {
        sharedFpsLimiter.mark();

        coop_host_begin_main_input();
        int keyCode = get_input();
        coop_host_end_main_input();
        game_handle_input(keyCode, false);

        // Player 1 chats once player 2 is there: T, a message, Enter (one
        // key a frame, through player 1's own input handling).
        if (startTile != -1 && chatKey < (int)(sizeof(chatKeys) / sizeof(chatKeys[0]))) {
            game_handle_input(chatKeys[chatKey++], false);
        }

        scripts_check_state();
        coop_process_requests();
        map_check_state();
        coop_host_frame();
        renderPresent();

        sharedFpsLimiter.throttle();

        if (coop_player_count() > 1) {
            Object* p2 = coop_player(1)->obj;
            if (startTile == -1) {
                startTile = p2->tile;
                autotest_log("player 2 '%s' joined at tile %d, look %d", coop_player(1)->name, startTile, coop_player(1)->look);
            } else if (p2->tile != startTile && !moved) {
                moved = true;
                autotest_log("player 2 moving (tile %d)", p2->tile);
            }
        }

        if (!coop_net_client_connected() && startTile != -1) {
            break;
        }
    }

    if (startTile == -1) {
        autotest_fail("player 2 never joined");
    } else if (!moved) {
        autotest_fail("player 2 never moved");
    } else {
        autotest_log("player 2 ended at tile %d (distance %d from start)", coop_player(1)->obj->tile,
            tile_dist(startTile, coop_player(1)->obj->tile));
    }

    dump_screen();
    coop_host_exit();
    return 0;
}

static void coop_autotest_screenshot()
{
    dump_screen();
}

// Network play, client side: drives character creation through the host,
// then clicks the map to walk player 2.
static int coop_autotest_net_play_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();

    // Character creation (same layout as coop_join): tag three skills,
    // Strength +5, Done, then the look list: leather jacket.
    inject_wait(240);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_centered(355, 31);
    inject_wait(20);
    coop_autotest_click_centered(355, 42);
    inject_wait(20);
    coop_autotest_click_centered(355, 53);
    inject_wait(20);
    for (int index = 0; index < 5; index++) {
        coop_autotest_click_centered(157, 42);
        inject_wait(20);
    }
    inject_key(KEY_RETURN);
    inject_wait(120);
    inject_call(coop_autotest_screenshot); // the look list
    inject_key(KEY_ARROW_DOWN);
    inject_wait(10);
    inject_key(KEY_RETURN);

    // Free roam in player 2's own view: walk to a spot right of center.
    inject_wait(180);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_centered(420, 250);
    inject_wait(240);
    inject_call(coop_autotest_screenshot);

    // Chat: T, a message (the I must not open the inventory), Enter.
    inject_key(KEY_LOWERCASE_T);
    inject_wait(5);
    const char* chat = "Hi from the Vault!";
    for (const char* c = chat; *c != '\0'; c++) {
        inject_key(*c);
        inject_wait(2);
    }
    inject_wait(30);
    inject_call(coop_autotest_screenshot); // typing
    inject_key(KEY_RETURN);
    inject_wait(30);
    inject_call(coop_autotest_screenshot); // sent

    // Player 2's inventory: open, look, close.
    inject_key(KEY_LOWERCASE_I);
    inject_wait(120);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_ESCAPE);
    inject_wait(120);
    inject_call(coop_net_client_request_exit);

    coop_net_set_client_exit_hook(coop_autotest_net_client_exit);
    int rc = coop_net_client_run();
    autotest_log("client finished with %d", rc);
    if (rc != 0) {
        autotest_fail("client failed");
    }

    return 0;
}

// Network combat, host side: player 2 already exists (as from a save), the
// client joins, then player 1 (hooked) and the enemy fight while player 2's
// turns are ended by the client's Space presses over the network.
static int coop_autotest_net_combat_player2_turns = 0;

static bool coop_autotest_net_combat_turn(Object* player)
{
    // For the video both players shoot, player 2's turns played here.
    if (coop_autotest_video()) {
        if (player != coop_player(0)->obj) {
            coop_autotest_net_combat_player2_turns++;
        }
        return coop_autotest_shoot_turn(player);
    }

    if (player == coop_player(0)->obj) {
        // COOP_AUTOTEST_COMBAT_CHAT: player 1 chats in their first turn
        // (the space must not end it), then ends it with Space.
        static bool chatted = false;
        if (getenv("COOP_AUTOTEST_COMBAT_CHAT") != NULL && !chatted) {
            chatted = true;
            inject_key(KEY_LOWERCASE_T);
            for (const char* c = "on my way"; *c != '\0'; c++) {
                inject_key(*c);
            }
            inject_key(KEY_RETURN);
            inject_wait(5);
            inject_key(KEY_SPACE);
            return false;
        }
        return coop_autotest_attack_turn(player);
    }

    // Player 2: played by the client.
    coop_autotest_net_combat_player2_turns++;
    autotest_log("player 2's turn %d", coop_autotest_net_combat_player2_turns);
    return false;
}

static int coop_autotest_net_combat_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_host_active()) {
        autotest_fail("cannot host");
        return -1;
    }
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }

    unsigned int start = get_time();
    while (elapsed_time(start) < 30000 && !coop_net_client_connected()) {
        coop_net_host_pump();
        renderPresent();
        SDL_Delay(10);
    }

    if (!coop_net_client_connected()) {
        autotest_fail("no client joined");
        return -1;
    }

    // A few free roam frames: player 2 resumes without creating a character.
    for (int frame = 0; frame < 30; frame++) {
        coop_host_frame();
        renderPresent();
        SDL_Delay(15);
    }

    if (coop_player_count() != 2 || coop_player(1) != p2) {
        autotest_fail("player 2 was not kept for the joining client");
    }

    coop_autotest_scene();
    coop_autotest_video_start();

    // COOP_AUTOTEST_P2_HP: player 2 starts the fight hurt (to get knocked
    // out with [coop]knockout=1).
    if (const char* hp = getenv("COOP_AUTOTEST_P2_HP")) {
        critter_adjust_hits(p2->obj, atoi(hp) - critter_get_hits(p2->obj));
    }

    // (Next to the hurt player 2, so they get hit.)
    Object* enemy = coop_autotest_spawn_enemy(getenv("COOP_AUTOTEST_P2_HP") != NULL ? p2->obj : coop_player(0)->obj);
    if (enemy == NULL) {
        return -1;
    }

    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_set_turn_hook(coop_autotest_net_combat_turn);

    STRUCT_664980 attack;
    memset(&attack, 0, sizeof(attack));
    // (The hurt player 2 starts it, so the enemy goes for them.)
    attack.attacker = getenv("COOP_AUTOTEST_P2_HP") != NULL ? p2->obj : coop_player(0)->obj;
    attack.defender = enemy;
    combat(&attack);

    coop_set_turn_hook(NULL);

    autotest_log("combat over: enemy dead %d, player 2 turns %d, players dead %d, player 2 knocked out %d, hp %d",
        critter_is_dead(enemy) ? 1 : 0, coop_autotest_net_combat_player2_turns, coop_any_player_has(DAM_DEAD) ? 1 : 0,
        (p2->obj->data.critter.combat.results & DAM_KNOCKED_OUT) != 0 ? 1 : 0, critter_get_hits(p2->obj));

    if (coop_autotest_net_combat_player2_turns == 0) {
        autotest_fail("player 2 never had a turn");
    }

    if (!critter_is_dead(enemy) && !coop_any_player_has(DAM_DEAD)) {
        autotest_fail("combat ended without a result");
    }

    coop_host_exit();
    return 0;
}

static void coop_autotest_screenshot_forever()
{
    inject_wait(130);
    inject_call(coop_autotest_screenshot);
    inject_call(coop_autotest_screenshot_forever);
}

static void coop_autotest_press_space_forever()
{
    // Re-queues itself: Space every second until the host leaves.
    inject_wait(60);
    inject_key(KEY_SPACE);
    inject_call(coop_autotest_press_space_forever);
}

static int coop_autotest_net_combat_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();

    inject_wait(400);
    inject_call(coop_autotest_screenshot);
    // COOP_AUTOTEST_COMBAT_CHAT: chat in a turn first; the space in the
    // message must not end the turn.
    if (getenv("COOP_AUTOTEST_COMBAT_CHAT") != NULL) {
        inject_key(KEY_LOWERCASE_T);
        for (const char* c = "hello there"; *c != '\0'; c++) {
            inject_key(*c);
            inject_wait(2);
        }
        inject_wait(10);
        inject_call(coop_autotest_screenshot);
        inject_key(KEY_RETURN);
        inject_wait(30);
    }
    if (coop_autotest_video()) {
        // The host plays player 2's turns for the video.
        coop_autotest_video_start();
    } else {
        inject_call(coop_autotest_press_space_forever);
        if (getenv("COOP_AUTOTEST_MAP") != NULL) {
            inject_call(coop_autotest_screenshot_forever);
        }
    }

    int rc = coop_net_client_run();
    autotest_log("client finished with %d", rc);
    if (rc != 0) {
        autotest_fail("client failed");
    }

    return 0;
}

// Human combat turns played by a test hook.

static int coop_autotest_combat(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p1 = coop_player(0);
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // Fight 1: both players punch a weak enemy until it dies.
    Object* enemy = coop_autotest_spawn_enemy(p2->obj);
    if (enemy == NULL) {
        return -1;
    }

    int killType = critter_kill_count_type(enemy);
    int killXp = critter_kill_exps(enemy);
    int p1Kills;
    int p2Kills;
    int p1Xp;
    int p2Xp;
    {
        ActivePlayerScope scope(p1);
        p1Kills = critter_kill_count(killType);
        p1Xp = stat_pc_get(PC_STAT_EXPERIENCE);
    }
    {
        ActivePlayerScope scope(p2);
        p2Kills = critter_kill_count(killType);
        p2Xp = stat_pc_get(PC_STAT_EXPERIENCE);
    }

    coop_autotest_enemy = enemy;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_autotest_turns_total = 0;
    coop_set_turn_hook(coop_autotest_attack_turn);

    STRUCT_664980 attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = p2->obj;
    attack.defender = enemy;
    combat(&attack);

    coop_set_turn_hook(NULL);

    autotest_log("fight 1: turns p1 %d, p2 %d; enemy dead %d",
        coop_autotest_turns[0], coop_autotest_turns[1], critter_is_dead(enemy) ? 1 : 0);

    if (isInCombat()) autotest_fail("fight 1 did not end");
    if (!critter_is_dead(enemy)) autotest_fail("enemy survived fight 1");
    if (coop_autotest_turns[1] == 0) autotest_fail("player 2 never got a human turn");
    if (coop_any_player_has(DAM_DEAD)) autotest_fail("a player died in fight 1");

    // Exactly one player gets the kill; both get the XP.
    {
        ActivePlayerScope scope(p1);
        int p1KillsAfter = critter_kill_count(killType);
        int p1XpAfter = stat_pc_get(PC_STAT_EXPERIENCE);
        ActivePlayerScope scope2(p2);
        int p2KillsAfter = critter_kill_count(killType);
        int p2XpAfter = stat_pc_get(PC_STAT_EXPERIENCE);

        autotest_log("kills p1 %d->%d, p2 %d->%d; xp p1 %d->%d, p2 %d->%d (kill xp %d)",
            p1Kills, p1KillsAfter, p2Kills, p2KillsAfter, p1Xp, p1XpAfter, p2Xp, p2XpAfter, killXp);

        if ((p1KillsAfter - p1Kills) + (p2KillsAfter - p2Kills) != 1) autotest_fail("kill not credited to exactly one player");
        if (p1XpAfter < p1Xp + killXp || p2XpAfter < p2Xp + killXp) autotest_fail("kill XP not given to both players");
    }

    tile_refresh_display();
    dump_screen();

    // Fight 2: players pass (by running out of [coop] turn_time when set,
    // else via the hook); the enemy kills a 1 HP player 2 -> game over.
    enemy = coop_autotest_spawn_enemy(p2->obj);
    if (enemy == NULL) {
        return -1;
    }

    critter_adjust_hits(p2->obj, 1 - critter_get_hits(p2->obj));

    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    if (coop_turn_time_limit() == 0) {
        coop_set_turn_hook(coop_autotest_pass_turn);
    }

    unsigned int fightStart = get_time();
    memset(&attack, 0, sizeof(attack));
    attack.attacker = enemy;
    attack.defender = p2->obj;
    combat(&attack);

    coop_set_turn_hook(NULL);

    autotest_log("fight 2: %d hooked turns, %u ms; p2 dead %d", coop_autotest_turns_total, elapsed_time(fightStart), critter_is_dead(p2->obj) ? 1 : 0);

    if (isInCombat()) autotest_fail("fight 2 did not end");
    if (!critter_is_dead(p2->obj)) autotest_fail("player 2 survived fight 2");
    if (!coop_any_player_has(DAM_DEAD)) autotest_fail("game over not detected");

    tile_refresh_display();
    dump_screen();

    return 0;
}

static void coop_autotest_count_turn(Object* player)
{
    PlayerState* state = coop_player_of(player);
    for (int index = 0; index < coop_player_count(); index++) {
        if (coop_player(index) == state) {
            coop_autotest_turns[index]++;
        }
    }

    // Safety net against fights that never end.
    if (++coop_autotest_turns_total > 200) {
        autotest_fail("combat did not finish in 200 player turns");
        combat_end();
    }
}

static bool coop_autotest_attack_turn(Object* player)
{
    coop_autotest_count_turn(player);

    Object* enemy = coop_autotest_enemy;
    if (enemy != NULL && !critter_is_dead(enemy) && obj_dist(player, enemy) <= 1) {
        autotest_log("%s punches %s", critter_name(player), critter_name(enemy));
        combat_attack(player, enemy, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
    }

    // Enemy gone: END COMBAT, as a player would (bystanders that joined
    // keep a fight going otherwise).
    if (enemy != NULL && critter_is_dead(enemy)) {
        combat_end();
    }

    return true;
}

static bool coop_autotest_pass_turn(Object* player)
{
    coop_autotest_count_turn(player);
    return true;
}

// Fires the equipped weapon at the enemy, for the video.
static bool coop_autotest_shoot_turn(Object* player)
{
    coop_autotest_count_turn(player);

    Object* enemy = coop_autotest_enemy;
    if (enemy != NULL && !critter_is_dead(enemy)) {
        // Let the last move finish first, and this shot after.
        combat_turn_run();
        autotest_log("%s shoots %s", critter_name(player), critter_name(enemy));
        combat_attack(player, enemy, HIT_MODE_RIGHT_WEAPON_PRIMARY, HIT_LOCATION_UNCALLED);
        combat_turn_run();
    }

    if (enemy != NULL && critter_is_dead(enemy)) {
        combat_end();
    }

    return true;
}

// Spawns a "Cave Rat" next to `nextTo`, with few hit
// points.
static Object* coop_autotest_spawn_enemy(Object* nextTo)
{
    int pid = -1;
    for (int index = 1; index < 400 && pid == -1; index++) {
        Proto* proto;
        if (proto_ptr(0x1000000 | index, &proto) != -1) {
            const char* enemyName = getenv("COOP_AUTOTEST_ENEMY");
            if (compat_stricmp(proto_name(0x1000000 | index), enemyName != NULL ? enemyName : "Cave Rat") == 0) {
                pid = 0x1000000 | index;
            }
        }
    }

    if (pid == -1) {
        autotest_fail("no Cave Rat proto found");
        return NULL;
    }

    Object* enemy;
    if (obj_pid_new(&enemy, pid) == -1) {
        autotest_fail("could not create enemy");
        return NULL;
    }

    int tile = nextTo->tile;
    if (coop_autotest_video()) {
        // Some distance, so the shots show.
        tile = tile_num_in_direction(tile, 1, 5);
    }
    obj_attempt_placement(enemy, tile, nextTo->elevation, 1);
    const char* enemyHits = getenv("COOP_AUTOTEST_ENEMY_HP");
    int hits = enemyHits != NULL ? atoi(enemyHits) : 3;
    critter_adjust_hits(enemy, hits - critter_get_hits(enemy));

    autotest_log("spawned %s (pid %x) at tile %d, %d hp, team %d", critter_name(enemy), pid, enemy->tile,
        critter_get_hits(enemy), enemy->data.critter.combat.team);

    return enemy;
}

// For screenshots: with COOP_AUTOTEST_MAP set, both players (equipped)
// move to that map first.
static void coop_autotest_scene()
{
    const char* mapName = getenv("COOP_AUTOTEST_MAP");
    if (mapName == NULL || coop_player_count() < 2) {
        return;
    }

    const char* armor1[] = { "Metal Armor", "Leather Armor" };
    const char* weapon1[] = { "Hunting Rifle", "10mm Pistol" };
    const char* armor2[] = { "Leather Jacket", "Leather Armor" };
    const char* weapon2[] = { "10mm SMG", "10mm Pistol" };
    coop_autotest_equip(coop_player(0), armor1, 2, 0);
    coop_autotest_equip(coop_player(0), weapon1, 2, HAND_RIGHT);
    coop_autotest_equip(coop_player(1), armor2, 2, 0);
    coop_autotest_equip(coop_player(1), weapon2, 2, HAND_RIGHT);

    register_screendump(KEY_F12, NULL);

    MapTransition transition;
    transition.map = map_match_map_name(mapName);
    transition.elevation = 0;
    transition.tile = -1;
    transition.rotation = 0;
    if (transition.map == -1) {
        autotest_log("no map %s", mapName);
        return;
    }

    map_leave_map(&transition);
    map_check_state();
    autotest_log("scene: %s", map_data.name);

    if (coop_autotest_video()) {
        // Noon, loaded guns in the active hand.
        set_game_time(game_time() / 864000 * 864000 + 12 * 36000);
        for (int index = 0; index < 2; index++) {
            PlayerState* player = coop_player(index);
            Object* weapon = inven_right_hand(player->obj);
            if (weapon != NULL) {
                item_w_set_curr_ammo(weapon, item_w_max_ammo(weapon));
                // The gun in their hands (as switching hands does).
                Object* obj = player->obj;
                int fid = art_id(OBJ_TYPE_CRITTER, obj->fid & 0xFFF, ANIM_STAND, item_w_anim_code(weapon), obj->rotation + 1);
                obj_change_fid(obj, fid, NULL);
            }
            player->currentHand = HAND_RIGHT;
        }
        intface_update_items(false);
    }
}

static bool coop_autotest_players_together(const char* when)
{
    Object* p1obj = coop_player(0)->obj;
    Object* p2obj = coop_player(1)->obj;

    autotest_log("%s: p1 tile %d elev %d, p2 tile %d elev %d, map elev %d",
        when, p1obj->tile, p1obj->elevation, p2obj->tile, p2obj->elevation, map_elevation);

    if (p2obj->elevation != p1obj->elevation || p1obj->elevation != map_elevation || p2obj->tile == -1 || obj_dist(p1obj, p2obj) > 5) {
        autotest_fail("%s: players are not together", when);
        return false;
    }

    return true;
}

// Stats and skills of `player` must be the same whether looked up from
// another player's scope or from its own.
static void coop_autotest_compare_lookups(PlayerState* player, const char* when)
{
    Object* obj = player->obj;
    int outside[STAT_COUNT + SKILL_COUNT];

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        outside[stat] = stat_level(obj, stat);
    }

    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        outside[STAT_COUNT + skill] = skill_level(obj, skill);
    }

    ActivePlayerScope scope(player);

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        if (stat_level(obj, stat) != outside[stat]) {
            autotest_fail("%s: stat %d of %s is %d from outside, %d inside", when, stat, critter_name(obj), outside[stat], stat_level(obj, stat));
        }
    }

    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        if (skill_level(obj, skill) != outside[STAT_COUNT + skill]) {
            autotest_fail("%s: skill %d of %s is %d from outside, %d inside", when, skill, critter_name(obj), outside[STAT_COUNT + skill], skill_level(obj, skill));
        }
    }
}

// Compares two text files line by line, ignoring lines starting with
// `skipPrefix`.
static bool coop_autotest_files_equal_except(const char* path1, const char* path2, const char* skipPrefix)
{
    FILE* stream1 = fopen(path1, "rt");
    FILE* stream2 = fopen(path2, "rt");
    bool equal = stream1 != NULL && stream2 != NULL;
    size_t prefixLength = strlen(skipPrefix);

    char line1[256];
    char line2[256];
    while (equal) {
        char* read1;
        do {
            read1 = fgets(line1, sizeof(line1), stream1);
        } while (read1 != NULL && strncmp(line1, skipPrefix, prefixLength) == 0);

        char* read2;
        do {
            read2 = fgets(line2, sizeof(line2), stream2);
        } while (read2 != NULL && strncmp(line2, skipPrefix, prefixLength) == 0);

        if (read1 == NULL || read2 == NULL) {
            equal = read1 == read2;
            break;
        }

        equal = strcmp(line1, line2) == 0;
    }

    if (stream1 != NULL) {
        fclose(stream1);
    }

    if (stream2 != NULL) {
        fclose(stream2);
    }

    return equal;
}

// Screenshot of the main menu.
static int coop_autotest_main_menu(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_create();
    main_menu_show(false);
    renderPresent();
    dump_screen();
    main_menu_hide(false);
    return 0;
}

// Main menu button `slot` (0-5), as the mouse clicks it.
static void coop_autotest_click_menu_slot(int slot)
{
    // The main menu is centered on bigger screens.
    coop_autotest_click_centered(438, slot * 41 + 58);
    inject_wait(15);
}

// The multiplayer menu, driven like a player would: main menu MULTIPLAYER,
// JOIN GAME (type an address, give up), then HOST GAME and NEW GAME.
static int coop_autotest_menu_flow(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_create();
    main_menu_show(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    int rc = main_menu_loop();
    if (rc != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen (%d)", rc);
        return -1;
    }

    inject_wait(10);
    inject_call(coop_autotest_screenshot);
    // JOIN GAME, type an address, screenshot, Escape.
    coop_autotest_click_menu_slot(1);
    inject_wait(10);
    const char* address = "192.168.1.10";
    for (const char* p = address; *p != '\0'; p++) {
        inject_key(*p);
        inject_wait(2);
    }
    inject_wait(10);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_ESCAPE);
    inject_wait(15);
    // HOST GAME, screenshot, NEW GAME, screenshot of the hosting message,
    // close it.
    coop_autotest_click_menu_slot(0);
    inject_wait(10);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(0);
    inject_wait(90);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);

    rc = coop_menu_run();
    autotest_log("multiplayer menu returned %d, hosting %d", rc, coop_host_active() ? 1 : 0);

    if (rc != MAIN_MENU_NEW_GAME) {
        autotest_fail("HOST GAME, NEW GAME did not start a new game");
    }

    if (!coop_host_active() || !coop_is_enabled()) {
        autotest_fail("not hosting");
    }

    coop_menu_end_session();
    if (coop_host_active() || coop_is_enabled()) {
        autotest_fail("session did not end");
    }

    return 0;
}

// Client side of a nettest, joining through the menu (the address field
// comes filled in from [coop] host); the host leaves after a few seconds
// and the menu reports it. Paired with net_host.
static int coop_autotest_menu_join(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_create();
    main_menu_show(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (main_menu_loop() != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen");
        return -1;
    }

    // JOIN GAME: the server browser finds the host (LAN search) and lists
    // it first; COOP_AUTOTEST_JOIN_ADDRESS takes ENTER ADDRESS instead.
    coop_autotest_click_menu_slot(1);
    inject_wait(30);
    inject_call(coop_autotest_screenshot);
    if (getenv("COOP_AUTOTEST_JOIN_ADDRESS") != NULL) {
        coop_autotest_click_menu_slot(2);
        inject_wait(10);
        inject_key(KEY_RETURN);
    } else {
        coop_autotest_click_menu_slot(0);
    }
    // Playing until the host leaves, then the message.
    inject_wait(900);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);
    inject_wait(15);
    // BACK.
    coop_autotest_click_menu_slot(5);

    int rc = coop_menu_run();
    autotest_log("menu returned %d, last error '%s'", rc, coop_net_last_error());

    if (rc != -1) {
        autotest_fail("joining should come back to the main menu");
    }

    if (strstr(coop_net_last_error(), "left") == NULL) {
        autotest_fail("the host leaving was not reported");
    }

    if (coop_is_enabled() || coop_net_mode() != COOP_NET_NONE) {
        autotest_fail("co-op still on after leaving");
    }

    return 0;
}

// HOST GAME, NEW GAME, PRIVATE CODE: the game gets a relay code (needs
// [coop] relay), shown in the hosting message.
static int coop_autotest_menu_host_code(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_create();
    main_menu_show(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (main_menu_loop() != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen");
        return -1;
    }

    coop_autotest_click_menu_slot(0); // HOST GAME
    coop_autotest_click_menu_slot(0); // NEW GAME
    inject_wait(10);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(2); // PRIVATE CODE
    inject_wait(90);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);

    int rc = coop_menu_run();
    autotest_log("menu returned %d, relay code '%s'", rc, coop_net_relay_code());
    if (rc != MAIN_MENU_NEW_GAME || coop_net_relay_code()[0] == '\0') {
        autotest_fail("PRIVATE CODE did not host with a code");
    }
    coop_host_exit();
    return 0;
}

// A crash-report.txt from last time: opening MULTIPLAYER offers it, SEND
// REPORT sends it (to [coop] server) and it becomes crash-report-old.txt.
static int coop_autotest_menu_crash_report(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_create();
    main_menu_show(false);

    std::string report;
    if (!coop_crash_pending(&report)) {
        autotest_fail("no crash-report.txt to offer");
        return -1;
    }

    inject_wait(10);
    coop_autotest_click_menu_slot(3); // MULTIPLAYER
    if (main_menu_loop() != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen");
        return -1;
    }

    inject_wait(20);
    inject_call(coop_autotest_screenshot); // "The game crashed last time."
    inject_key(KEY_RETURN);
    inject_wait(20);
    inject_call(coop_autotest_screenshot); // SEND REPORT / DON'T SEND
    coop_autotest_click_menu_slot(0);
    inject_wait(60);
    inject_call(coop_autotest_screenshot); // "Thank you!"
    inject_key(KEY_RETURN);
    inject_wait(20);
    coop_autotest_click_menu_slot(5); // BACK

    coop_menu_run();

    std::string again;
    autotest_log("after sending: pending %d", coop_crash_pending(&again) ? 1 : 0);
    if (coop_crash_pending(&again)) {
        autotest_fail("the crash report is still pending");
    }
    return 0;
}

// UPDATE (a newer version is offered and downloaded) and REPORT BUG (sent)
// against tools/coop_server.py, set as [coop] server.
static int coop_autotest_menu_server(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_create();
    main_menu_show(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (main_menu_loop() != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen");
        return -1;
    }

    // UPDATE: "version available" message, DOWNLOAD, "saved" message.
    coop_autotest_click_menu_slot(3);
    inject_wait(60);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);
    inject_wait(15);
    coop_autotest_click_menu_slot(0);
    inject_wait(60);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);
    inject_wait(15);

    // REPORT BUG: describe, send, "thank you".
    coop_autotest_click_menu_slot(4);
    inject_wait(10);
    for (const char* p = "trade crash"; *p != '\0'; p++) {
        inject_key(*p);
        inject_wait(2);
    }
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);
    inject_wait(60);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);
    inject_wait(15);
    coop_autotest_click_menu_slot(5);

    int rc = coop_menu_run();
    autotest_log("menu returned %d", rc);
    return 0;
}

// The first item proto named `name`, or -1.
static int coop_autotest_item_pid(const char* name)
{
    for (int index = 1; index < 1000; index++) {
        Proto* proto;
        if (proto_ptr(index, &proto) != -1 && compat_stricmp(proto_name(index), name) == 0) {
            return index;
        }
    }
    return -1;
}

// Gives `critter` (a player) the first of `names` that exists and wields or
// wears it.
static void coop_autotest_equip(PlayerState* player, const char* const* names, int count, int hand)
{
    for (int index = 0; index < count; index++) {
        int pid = coop_autotest_item_pid(names[index]);
        Object* item;
        if (pid == -1 || obj_pid_new(&item, pid) == -1) {
            continue;
        }

        ActivePlayerScope scope(player);
        if (item_add_force(obj_dude, item, 1) == 0) {
            obj_disconnect(item, NULL);
            inven_wield(obj_dude, item, hand);
            autotest_log("%s gets %s", critter_name(obj_dude), names[index]);
        }
        return;
    }
}

// COOP_AUTOTEST_VIDEO=1: screenshots become video frames,
// vid-<wall clock ms>.raw (width, height, palette, pixels), so the frames
// of the host and the client can be matched up afterwards.
static int coop_autotest_video_dump(int width, int height, unsigned char* buffer, unsigned char* palette)
{
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                       .count();
    char name[64];
    snprintf(name, sizeof(name), "vid-%lld.raw", ms);
    FILE* stream = fopen(name, "wb");
    if (stream == NULL) {
        return -1;
    }
    fwrite(&width, sizeof(width), 1, stream);
    fwrite(&height, sizeof(height), 1, stream);
    fwrite(palette, 1, 768, stream);
    fwrite(buffer, 1, width * height, stream);
    fclose(stream);
    return 0;
}

static bool coop_autotest_video()
{
    return getenv("COOP_AUTOTEST_VIDEO") != NULL;
}

// For scripts without their own frame pacing (the fight): a video frame
// every 100 ms from the screen updates, so animations are recorded too.
// Chained after the present hook already set (the host's frame sender).
static void (*coop_autotest_video_next_hook)() = NULL;

static void coop_autotest_video_frame()
{
    if (coop_autotest_video_next_hook != NULL) {
        coop_autotest_video_next_hook();
    }

    static unsigned int last = 0;
    unsigned int now = SDL_GetTicks();
    if (now - last >= 100) {
        last = now;
        dump_screen();
    }
}

static void coop_autotest_video_start()
{
    if (coop_autotest_video() && svga_present_hook != coop_autotest_video_frame) {
        register_screendump(KEY_F12, coop_autotest_video_dump);
        coop_autotest_video_next_hook = svga_present_hook;
        svga_present_hook = coop_autotest_video_frame;
    }
}

// Player 1 strolls to a free tile a few hexes away.
static void coop_autotest_stroll(Object* critter)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        int tile = tile_num_in_direction(critter->tile, roll_random(0, 5), roll_random(3, 7));
        if (tile != critter->tile && obj_blocking_at(NULL, tile, critter->elevation) == NULL
            && make_path_func(critter, critter->tile, tile, NULL, 0, obj_blocking_at) != 0) {
            register_begin(ANIMATION_REQUEST_RESERVED);
            register_object_move_to_tile(critter, tile, critter->elevation, -1, 0);
            register_end();
            return;
        }
    }
}

// COOP_AUTOTEST_TOUR=ALL: every map. The players are healed all the time,
// player 1 answers dialogs (first option) and in fights clears the
// opposing side and ends combat, so the tour gets through hostile places;
// it looks for crashes and hangs, not at the story.
static bool coop_autotest_rough = false;

static void coop_autotest_log_story(const char* when)
{
    if (const char* itemName = getenv("COOP_AUTOTEST_P2_ITEM")) {
        int pid = coop_autotest_item_pid(itemName);
        autotest_log("story %s: %s carried by p1 %d, p2 %d", when, itemName,
            inven_pid_quantity_carried(coop_player(0)->obj, pid),
            coop_player_count() > 1 ? inven_pid_quantity_carried(coop_player(1)->obj, pid) : 0);
    }
    autotest_log("story %s: vault_water %d find_chip %d chip_taken %d vats_blown %d master_blown %d master_dead %d vats_status %d",
        when, game_get_global_var(GVAR_VAULT_WATER), game_get_global_var(GVAR_FIND_WATER_CHIP),
        game_get_global_var(GVAR_NECROP_WATER_CHIP_TAKEN), game_get_global_var(GVAR_VATS_BLOWN),
        game_get_global_var(GVAR_MASTER_BLOWN), game_get_global_var(GVAR_MASTER_DEAD), game_get_global_var(GVAR_VATS_STATUS));
}

static void coop_autotest_heal_players()
{
    static bool tough = false;
    if (!tough) {
        // Hard to kill in one round, so the tour gets everywhere.
        tough = true;
        for (int index = 0; index < coop_player_count(); index++) {
            stat_set_bonus(coop_player(index)->obj, STAT_MAXIMUM_HIT_POINTS, 800); // (the HUD shows 3 digits)
        }
    }

    for (int index = 0; index < coop_player_count(); index++) {
        Object* obj = coop_player(index)->obj;
        if (obj != NULL && !critter_is_dead(obj)) {
            int missing = stat_level(obj, STAT_MAXIMUM_HIT_POINTS) - critter_get_hits(obj);
            if (missing > 0) {
                critter_adjust_hits(obj, missing);
            }
            // And no radiation sickness (the Glow otherwise kills a
            // player a few maps later): no rads, and no sickness already
            // on its way.
            int rads = critter_get_rads(obj);
            if (rads > 0) {
                ActivePlayerScope scope(coop_player(index));
                critter_adjust_rads(obj, -rads);
                queue_remove_this(obj, EVENT_TYPE_RADIATION);
            }
        }
    }
}

static bool coop_autotest_rough_turn(Object* player)
{
    coop_autotest_heal_players();
    if (player != coop_player(0)->obj) {
        // Player 2 plays (or the turn timer ends the turn).
        return false;
    }

    // Collect first: killing moves objects around in the object list.
    int team = player->data.critter.combat.team;
    std::vector<Object*> targets;
    for (Object* obj = obj_find_first_at(player->elevation); obj != NULL; obj = obj_find_next_at()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && coop_player_of(obj) == NULL && !critter_is_dead(obj)
            && obj->data.critter.combat.team != team && obj_dist(obj, player) <= 20) {
            targets.push_back(obj);
        }
    }
    for (Object* obj : targets) {
        critter_kill(obj, -1, true);
    }
    if (!targets.empty()) {
        autotest_log("%s: cleared %d hostile critters", map_data.name, (int)targets.size());
    }
    combat_end();
    return true;
}

// A background process (runs in every input loop, dialogs too); an
// injected chain would never let player 2's part of a frame finish.
static void coop_autotest_answer_dialogs()
{
    static int presses = 0;
    static unsigned int last = 0;
    if (dialog_active() && elapsed_time(last) > 1000 && !inject_pending()) {
        // First option a few times; then Escape (a barter screen, say).
        inject_key(++presses % 4 == 0 ? KEY_ESCAPE : '1');
        last = get_time();
    }
}

// One frame of the main loop, as the game runs it while hosting.
static void coop_autotest_host_frame()
{
    sharedFpsLimiter.mark();
    coop_host_begin_main_input();
    int keyCode = get_input();
    coop_host_end_main_input();
    game_handle_input(keyCode, false);
    scripts_check_state();
    coop_process_requests();
    map_check_state();
    coop_host_frame();
    renderPresent();
    sharedFpsLimiter.throttle();
}

// Network tour, host side: both players (equipped) travel through towns;
// each stop runs a few seconds of the real game with the client walking
// around, with screenshots on both sides. Paired with net_tour_client.
static int coop_autotest_net_tour_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    // Screenshots without "Saved screenshot." in the (shared) message box.
    register_screendump(KEY_F12, coop_autotest_video() ? coop_autotest_video_dump : NULL);

    PlayerState* p1 = coop_player(0);
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    const char* armor1[] = { "Metal Armor", "Leather Armor" };
    const char* weapon1[] = { "Hunting Rifle", "10mm Pistol" };
    const char* armor2[] = { "Leather Jacket", "Leather Armor" };
    const char* weapon2[] = { "10mm SMG", "10mm Pistol" };
    coop_autotest_equip(p1, armor1, 2, 0);
    coop_autotest_equip(p1, weapon1, 2, HAND_RIGHT);
    coop_autotest_equip(p2, armor2, 2, 0);
    coop_autotest_equip(p2, weapon2, 2, HAND_RIGHT);

    // COOP_AUTOTEST_P2_ITEM: a (quest) item in player 2's pack.
    if (const char* itemName = getenv("COOP_AUTOTEST_P2_ITEM")) {
        int pid = coop_autotest_item_pid(itemName);
        Object* item;
        if (pid != -1 && obj_pid_new(&item, pid) == 0 && item_add_force(p2->obj, item, 1) == 0) {
            obj_disconnect(item, NULL);
            autotest_log("player 2 carries %s (pid %d)", itemName, pid);
        } else {
            autotest_fail("no item %s", itemName);
        }
    }
    // COOP_AUTOTEST_STORY=VATS,MASTER: the story as if they were destroyed.
    if (const char* story = getenv("COOP_AUTOTEST_STORY")) {
        if (strstr(story, "VATS") != NULL) {
            game_set_global_var(GVAR_VATS_BLOWN, 1);
            game_set_global_var(GVAR_VATS_STATUS, 1);
        }
        if (strstr(story, "MASTER") != NULL) {
            game_set_global_var(GVAR_MASTER_BLOWN, 1);
            game_set_global_var(GVAR_MASTER_DEAD, 1);
        }
        if (strstr(story, "CHIP") != NULL) {
            game_set_global_var(GVAR_FIND_WATER_CHIP, 2);
        }
    }
    coop_autotest_log_story("start");

    coop_host_init();
    if (!coop_host_active()) {
        autotest_fail("cannot host");
        return -1;
    }
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }

    unsigned int start = get_time();
    while (elapsed_time(start) < 30000 && !coop_net_client_connected()) {
        coop_autotest_host_frame();
    }

    if (!coop_net_client_connected()) {
        autotest_fail("no client joined");
        return -1;
    }

    const char* stops[] = {
        "SHADYE.MAP",
        "SHADYW.MAP",
        "JUNKENT.MAP",
        "JUNKCSNO.MAP",
        "HUBENT.MAP",
        "HUBDWNTN.MAP",
        "HUBOLDTN.MAP",
        "BROHDENT.MAP",
        // (Not LAADYTUM: its guard's greeting waits for player 1.)
        "LAFOLLWR.MAP",
        "HALLDED.MAP",
        "CHILDRN1.MAP",
    };

    // COOP_AUTOTEST_TOUR=A.MAP,B.MAP replaces the list (for screenshots),
    // ALL tours every map (see coop_autotest_rough).
    std::vector<std::string> tour;
    const char* tourList = getenv("COOP_AUTOTEST_TOUR");
    if (tourList != NULL && strcmp(tourList, "ALL") == 0) {
        coop_autotest_rough = true;
        // (Map 0 in a transition means the world map.)
        for (int index = 1; index < 500; index++) {
            char name[16];
            if (map_get_name_idx(name, index) != 0) {
                break;
            }
            tour.push_back(name);
        }
        // COOP_AUTOTEST_TOUR_AFTER=X.MAP: continue a tour after X.
        if (const char* after = getenv("COOP_AUTOTEST_TOUR_AFTER")) {
            auto it = std::find(tour.begin(), tour.end(), std::string(after));
            if (it != tour.end()) {
                tour.erase(tour.begin(), it + 1);
            }
        }
        coop_set_turn_hook(coop_autotest_rough_turn);
        add_bk_process(coop_autotest_answer_dialogs);
    } else if (const char* list = tourList) {
        if (getenv("COOP_AUTOTEST_ROUGH") != NULL) {
            coop_autotest_rough = true;
            coop_set_turn_hook(coop_autotest_rough_turn);
            add_bk_process(coop_autotest_answer_dialogs);
        }
        std::string rest = list;
        size_t comma;
        while ((comma = rest.find(',')) != std::string::npos) {
            tour.push_back(rest.substr(0, comma));
            rest = rest.substr(comma + 1);
        }
        tour.push_back(rest);
    } else {
        tour.assign(std::begin(stops), std::end(stops));
    }

    // COOP_AUTOTEST_SEED=n: the maps in a shuffled order (the same n gives
    // the same order, to repeat a run).
    if (const char* seed = getenv("COOP_AUTOTEST_SEED")) {
        std::mt19937 random((unsigned int)strtoul(seed, NULL, 10));
        std::shuffle(tour.begin(), tour.end(), random);
        autotest_log("tour order from seed %s, first %s", seed, tour.empty() ? "-" : tour.front().c_str());
    }

    int visited = 0;
    for (const std::string& stopName : tour) {
        const char* stop = stopName.c_str();
        int index = map_match_map_name(stop);
        if (index == -1) {
            autotest_log("no map %s", stop);
            continue;
        }

        if (coop_autotest_video()) {
            // Daylight for the video: noon (game time is in 1/10 s).
            set_game_time(game_time() / 864000 * 864000 + 12 * 36000);
        }

        MapTransition transition;
        transition.map = index;
        transition.elevation = 0;
        transition.tile = -1;
        transition.rotation = 0;
        map_leave_map(&transition);

        start = get_time();
        map_check_state();
        autotest_log("%s loaded in %u ms", stop, elapsed_time(start));

        // COOP_AUTOTEST_TALK=<name>: player 1 talks to them (for
        // screenshots; the dialog waits for player 1, so the tour ends by
        // timeout there).
        if (const char* talkTo = getenv("COOP_AUTOTEST_TALK")) {
            for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
                if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critter_name(obj), talkTo) != NULL) {
                    if (obj->elevation != obj_dude->elevation) {
                        // Up or down to them first (player 2 follows).
                        obj_move_to_tile(obj_dude, obj->tile, obj->elevation, NULL);
                        map_set_elevation(obj->elevation);
                    }
                    // Somewhere they can be talked to from (not behind a
                    // counter).
                    for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
                        int distance = tile_dist(tile, obj->tile);
                        if (distance >= 1 && distance <= 3 && obj_blocking_at(NULL, tile, obj->elevation) == NULL
                            && make_path_func(obj_dude, tile, obj->tile, NULL, 0, obj_sight_blocking_at) != 0) {
                            obj_move_to_tile(obj_dude, tile, obj->elevation, NULL);
                            break;
                        }
                    }
                    obj_attempt_placement(coop_player(1)->obj, obj_dude->tile, obj->elevation, 2);
                    tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
                    for (int frame = 0; frame < 60; frame++) {
                        coop_autotest_host_frame();
                    }
                    gdialog_enter(obj, 1);
                    break;
                }
            }
        }

        // COOP_AUTOTEST_KILL=<name>: someone dies (the Master, say), with
        // the players on their level, to run what the story does then.
        if (getenv("COOP_AUTOTEST_LIST") != NULL) {
            for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
                if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER) {
                    autotest_log("%s: critter '%s' pid %x elev %d", map_data.name, critter_name(obj), obj->pid, obj->elevation);
                }
            }
        }
        if (const char* killName = getenv("COOP_AUTOTEST_KILL")) {
            // The exact name first ("Master", not "Master's Pet").
            Object* victim = NULL;
            for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
                if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && !critter_is_dead(obj) && strcmp(critter_name(obj), killName) == 0) {
                    victim = obj;
                    break;
                }
            }
            for (Object* obj = obj_find_first(); obj != NULL && victim == NULL; obj = obj_find_next()) {
                if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && !critter_is_dead(obj) && strstr(critter_name(obj), killName) != NULL) {
                    victim = obj;
                }
            }
            for (Object* obj = victim; obj != NULL; obj = NULL) {
                {
                    if (obj->elevation != obj_dude->elevation) {
                        obj_move_to_tile(obj_dude, obj->tile, obj->elevation, NULL);
                        map_set_elevation(obj->elevation);
                    }
                    autotest_log("%s: killing %s", map_data.name, critter_name(obj));
                    critter_kill(obj, -1, true);
                    // As a fight would: their death script, killed by player 1.
                    if (obj->sid != -1) {
                        scr_set_objs(obj->sid, obj_dude, NULL);
                        exec_script_proc(obj->sid, SCRIPT_PROC_DESTROY);
                    }
                    break;
                }
            }
        }

        // COOP_AUTOTEST_RECRUIT=<name>: they join the party (as after their
        // dialog), then travel along.
        if (const char* recruitName = getenv("COOP_AUTOTEST_RECRUIT")) {
            for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
                if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && !critter_is_dead(obj) && strcmp(critter_name(obj), recruitName) == 0 && !isPartyMember(obj)) {
                    obj_attempt_placement(obj, obj_dude->tile, obj_dude->elevation, 2);
                    partyMemberAdd(obj);
                    autotest_log("%s: %s joins the party", map_data.name, recruitName);
                    break;
                }
            }
        }

        // COOP_AUTOTEST_STAY_MS: longer on each map (countdowns...).
        const char* stayText = getenv("COOP_AUTOTEST_STAY_MS");
        unsigned int stay = stayText != NULL ? (unsigned int)atoi(stayText) : (coop_autotest_rough ? 6000u : 9000u);

        start = get_time();
        bool shot = false;
        unsigned int lastFrame = 0;
        unsigned int lastStroll = 0;
        while (elapsed_time(start) < stay && coop_net_client_connected()) {
            if (coop_autotest_rough) {
                coop_autotest_heal_players();
            }
            coop_autotest_host_frame();
            if (coop_autotest_video()) {
                if (elapsed_time(lastFrame) >= 100) {
                    lastFrame = get_time();
                    dump_screen();
                }
                if (elapsed_time(lastStroll) >= 2500) {
                    lastStroll = get_time();
                    coop_autotest_stroll(obj_dude);
                }
            } else if (!shot && elapsed_time(start) > 6000) {
                dump_screen();
                shot = true;
            }
        }

        // Rough tour: the other elevations too.
        for (int elevation = 1; coop_autotest_rough && elevation < ELEVATION_COUNT && coop_net_client_connected(); elevation++) {
            if (map_is_elevation_empty(elevation) || obj_dude->elevation == elevation) {
                continue;
            }
            obj_move_to_tile(obj_dude, obj_dude->tile, elevation, NULL);
            map_set_elevation(elevation);
            unsigned int elevationStart = get_time();
            while (elapsed_time(elevationStart) < 4000 && coop_net_client_connected()) {
                coop_autotest_heal_players();
                coop_autotest_host_frame();
            }
            autotest_log("%s elevation %d: p2 elevation %d", map_data.name, elevation, coop_player(1)->obj->elevation);
        }

        if (!coop_net_client_connected()) {
            autotest_fail("client lost at %s", stop);
            break;
        }

        coop_autotest_log_story(map_data.name);

        if (const char* recruitName = getenv("COOP_AUTOTEST_RECRUIT")) {
            bool along = false;
            for (Object* obj = obj_find_first(); obj != NULL; obj = obj_find_next()) {
                if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strcmp(critter_name(obj), recruitName) == 0 && isPartyMember(obj)) {
                    along = true;
                    autotest_log("%s: %s here, elev %d, %d from player 1", map_data.name, recruitName, obj->elevation, obj_dist(obj, obj_dude));
                }
            }
            if (!along) {
                autotest_log("%s: %s is not with the party", map_data.name, recruitName);
            }
        }

        // COOP_AUTOTEST_SAVELOAD: save and load on every map, player 2
        // connected.
        if (getenv("COOP_AUTOTEST_SAVELOAD") != NULL) {
            lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
            bool saved = SaveGame(LOAD_SAVE_MODE_QUICK) == 1;
            lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
            bool loaded = saved && LoadGame(LOAD_SAVE_MODE_QUICK) == 1;
            autotest_log("%s: save %d load %d, %d players, connected %d", map_data.name, saved ? 1 : 0, loaded ? 1 : 0,
                coop_player_count(), coop_net_client_connected() ? 1 : 0);
            if (!loaded || coop_player_count() != 2) {
                autotest_fail("%s: save/load failed", map_data.name);
            }
            for (int frame = 0; frame < 120; frame++) {
                coop_autotest_host_frame();
            }
        }
        // (Fetched only now: loading a save replaces the objects.)
        Object* p1obj = p1->obj;
        Object* p2obj = coop_player(1)->obj;
        autotest_log("%s: p1 tile %d elev %d hp %d rads %d, p2 tile %d elev %d hp %d rads %d, distance %d",
            map_data.name, p1obj->tile, p1obj->elevation, critter_get_hits(p1obj), critter_get_rads(p1obj),
            p2obj->tile, p2obj->elevation, critter_get_hits(p2obj), critter_get_rads(p2obj), obj_dist(p1obj, p2obj));

        if (coop_player_count() != 2 || p2obj->elevation != p1obj->elevation || p2obj->tile == -1) {
            autotest_fail("%s: players separated", stop);
        }
        if (coop_any_player_has(DAM_DEAD)) {
            autotest_fail("%s: a player died", stop);
            break;
        }
        visited++;
    }

    autotest_log("visited %d maps", visited);
    coop_host_exit();
    return 0;
}

static void coop_autotest_tour_walk()
{
    // Walk somewhere around player 2, look, again - forever (the host
    // ends the tour).
    static int step = 0;
    static const int offsets[][2] = { { 90, 40 }, { -80, 50 }, { 60, -60 }, { -70, -30 } };
    int offset[2] = { offsets[step % 4][0], offsets[step % 4][1] };
    step++;
    // With COOP_AUTOTEST_SEED: somewhere random around player 2 instead.
    if (const char* seed = getenv("COOP_AUTOTEST_SEED")) {
        static std::mt19937 random((unsigned int)strtoul(seed, NULL, 10) + 1);
        offset[0] = (int)(random() % 241) - 120;
        offset[1] = (int)(random() % 161) - 80;
    }
    coop_autotest_click_centered(320 + offset[0], 190 + offset[1]);
    if (coop_autotest_video()) {
        // A video frame about every 100 ms.
        for (int frame = 0; frame < 30; frame++) {
            inject_wait(6);
            inject_call(coop_autotest_screenshot);
        }
    } else {
        inject_wait(150);
        inject_call(coop_autotest_screenshot);
        inject_wait(90);
    }
    inject_call(coop_autotest_tour_walk);
}

static int coop_autotest_net_tour_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();

    if (coop_autotest_video()) {
        register_screendump(KEY_F12, coop_autotest_video_dump);
    }

    inject_wait(300);
    inject_call(coop_autotest_tour_walk);

    int rc = coop_net_client_run();
    autotest_log("client finished with %d, %s", rc, coop_net_last_error());
    return 0;
}

// A rough session, host side: player 2 joins and walks, player 1 saves,
// player 2 drops out and comes back (keeping their character), player 1
// loads the save with player 2 connected, then a fight where player 2 does
// nothing and the turn timer ([coop]turn_time, HOST_EXTRA) ends their turns.
// Paired with net_session_client.

static int coop_autotest_level(PlayerState* player)
{
    ActivePlayerScope scope(player);
    return stat_pc_get(PC_STAT_LEVEL);
}

static int coop_autotest_session_p2_turns = 0;
static unsigned int coop_autotest_session_turn_start = 0;
static unsigned int coop_autotest_session_longest = 0;

static bool coop_autotest_session_turn(Object* player)
{
    if (coop_autotest_session_turn_start != 0) {
        coop_autotest_session_longest = std::max(coop_autotest_session_longest, elapsed_time(coop_autotest_session_turn_start));
        coop_autotest_session_turn_start = 0;
    }

    if (player == coop_player(0)->obj) {
        return coop_autotest_attack_turn(player);
    }

    coop_autotest_session_p2_turns++;
    coop_autotest_session_turn_start = get_time();
    return false;
}

static bool coop_autotest_session_frames(unsigned int ms, bool untilConnected, bool untilDisconnected)
{
    unsigned int start = get_time();
    while (elapsed_time(start) < ms) {
        coop_autotest_host_frame();
        if (untilConnected && coop_net_client_connected()) {
            return true;
        }
        if (untilDisconnected && !coop_net_client_connected()) {
            return true;
        }
    }
    return false;
}

static int coop_autotest_net_session_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }
    std::string p2Name = critter_name(p2->obj);

    coop_host_init();
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }
    if (!coop_autotest_session_frames(30000, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }
    autotest_log("client joined");
    unsigned int joinsBefore = coop_net_client_joins();

    // COOP_AUTOTEST_XP: both players level up while player 2 is connected
    // (they then open their character screen).
    if (const char* xp = getenv("COOP_AUTOTEST_XP")) {
        stat_pc_add_experience(atoi(xp));
        autotest_log("after %s XP: player 1 level %d, player 2 level %d", xp, coop_autotest_level(coop_player(0)), coop_autotest_level(coop_player(1)));
    }

    int p2Level = coop_autotest_level(p2);
    int startTile = p2->obj->tile;
    // (Up to 20 s: big screens make both games slower.)
    for (int wait = 0; wait < 40 && coop_player(1)->obj->tile == startTile; wait++) {
        coop_autotest_session_frames(500, false, false);
    }
    coop_autotest_session_frames(2000, false, false);
    autotest_log("player 2 walked from %d to %d", startTile, coop_player(1)->obj->tile);
    if (coop_player(1)->obj->tile == startTile) {
        autotest_fail("player 2 did not move");
    }

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (SaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save with player 2 connected failed");
    }
    int savedTile = coop_player(1)->obj->tile;

    // Player 2 drops out (F10), the game goes on, player 2 comes back.
    // (On a slow machine they may have left and come back already: they
    // joined again since the first join.)
    unsigned int leaveStart = get_time();
    bool left = false;
    while (!left && elapsed_time(leaveStart) < 60000) {
        left = coop_autotest_session_frames(200, false, true) || coop_net_client_joins() != joinsBefore;
    }
    if (!left) {
        autotest_fail("client did not leave");
        return -1;
    }
    autotest_log("client left");
    // (The test client comes back after two seconds.)
    coop_autotest_session_frames(300, false, false);
    if (coop_player_count() != 2) {
        autotest_fail("player 2 vanished when the client left");
    }
    // (Only while they are still away: on a slow machine they are back.)
    if (!coop_net_client_connected() && (coop_player(1)->obj->flags & OBJECT_HIDDEN) == 0) {
        autotest_fail("player 2's character is still out while they are away");
    }

    if (!coop_autotest_session_frames(30000, true, false)) {
        autotest_fail("client did not come back");
        return -1;
    }
    coop_autotest_session_frames(2000, false, false);
    if ((coop_player(1)->obj->flags & OBJECT_HIDDEN) != 0 || obj_dist(coop_player(1)->obj, coop_player(0)->obj) > 5) {
        autotest_fail("player 2 did not come back next to player 1");
    }
    autotest_log("client back: %d players, player 2 '%s' level %d", coop_player_count(),
        critter_name(coop_player(1)->obj), coop_autotest_level(coop_player(1)));
    if (coop_player_count() != 2 || p2Name != critter_name(coop_player(1)->obj) || coop_autotest_level(coop_player(1)) != p2Level) {
        autotest_fail("player 2 changed after rejoining");
    }

    // Load with the client connected.
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (LoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("load with player 2 connected failed");
        return -1;
    }
    coop_autotest_session_frames(3000, false, false);
    autotest_log("after load: %d players, connected %d, player 2 tile %d (saved %d)", coop_player_count(),
        coop_net_client_connected() ? 1 : 0, coop_player(1)->obj->tile, savedTile);
    if (coop_player_count() != 2 || !coop_net_client_connected() || p2Name != critter_name(coop_player(1)->obj)) {
        autotest_fail("load broke the session");
    }

    // Player 2 trades (COOP_AUTOTEST_MAP/MERCHANT for a real merchant):
    // the game must stand still meanwhile.
    coop_autotest_scene();
    coop_autotest_session_frames(1500, false, false);
    {
        Object* merchant = NULL;
        const char* merchantName = getenv("COOP_AUTOTEST_MERCHANT");
        for (Object* obj = obj_find_first(); obj != NULL && merchantName != NULL; obj = obj_find_next()) {
            if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critter_name(obj), merchantName) != NULL) {
                merchant = obj;
                break;
            }
        }
        if (merchant == NULL) {
            for (int index = 1; index < 400 && merchant == NULL; index++) {
                if (compat_stricmp(proto_name(0x1000000 | index), "Merchant") == 0 && obj_pid_new(&merchant, 0x1000000 | index) == 0) {
                    obj_attempt_placement(merchant, coop_player(1)->obj->tile, coop_player(1)->obj->elevation, 1);
                }
            }
        }

        if (merchant == NULL) {
            autotest_fail("no merchant");
        } else {
            obj_attempt_placement(coop_player(1)->obj, merchant->tile, merchant->elevation, 1);
            int gameTime = game_time();
            unsigned int barterStart = get_time();
            // Close it after a while, as player 2 would (unless the test
            // has player 2 leave in the middle of it).
            if (getenv("COOP_AUTOTEST_LEAVE_IN_BARTER") == NULL) {
                inject_wait(240);
                inject_key(KEY_ESCAPE);
            }
            coop_request_barter(coop_player(1), merchant);
            coop_autotest_host_frame();
            autotest_log("player 2 traded with %s for %u ms; game time moved %d ticks",
                critter_name(merchant), elapsed_time(barterStart), game_time() - gameTime);
            if (elapsed_time(barterStart) < 2000) {
                autotest_fail("the barter screen did not stay open");
            }
            if (game_time() - gameTime > 5) {
                autotest_fail("the game did not pause while player 2 traded");
            }
        }
    }
    coop_autotest_session_frames(1500, false, false);

    // A fight where player 2 never acts: the turn timer has to move on.
    Object* enemy = coop_autotest_spawn_enemy(coop_player(0)->obj);
    if (enemy == NULL) {
        return -1;
    }
    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_set_turn_hook(coop_autotest_session_turn);

    STRUCT_664980 attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = coop_player(0)->obj;
    attack.defender = enemy;
    unsigned int fightStart = get_time();
    combat(&attack);
    coop_set_turn_hook(NULL);

    // Player 2's own turns (the time from their turn's start to the next
    // player's also counts the enemy's turn in between).
    coop_autotest_session_longest = coop_longest_turn(1);
    autotest_log("fight over after %u ms: enemy dead %d, player 2 turns %d, longest player 2 turn %u ms (limit %u)",
        elapsed_time(fightStart), critter_is_dead(enemy) ? 1 : 0, coop_autotest_session_p2_turns,
        coop_autotest_session_longest, coop_turn_time_limit());
    if (coop_autotest_session_p2_turns == 0) {
        autotest_fail("player 2 never had a turn");
    }
    if (coop_turn_time_limit() != 0 && coop_autotest_session_longest > coop_turn_time_limit() + 6000) {
        autotest_fail("the turn timer did not end player 2's turn");
    }

    coop_autotest_session_frames(2000, false, false);
    dump_screen();

    // At the end, so the steps above keep their timing with the client.
    // Player 1's mouse stays where it is while player 2 plays. It slid
    // away when the two had cursors with different hotspots (here player
    // 1's scroll arrow at the left edge).
    mouse_set_position(0, screenGetHeight() / 3);
    coop_autotest_session_frames(50, false, false);
    int mouseX;
    int mouseY;
    mouse_get_position(&mouseX, &mouseY);
    coop_autotest_session_frames(2000, false, false);
    int laterX;
    int laterY;
    mouse_get_position(&laterX, &laterY);
    autotest_log("player 1's mouse %d,%d then %d,%d", mouseX, mouseY, laterX, laterY);
    if (laterX != mouseX || laterY != mouseY) {
        autotest_fail("player 1's mouse moved by itself");
    }

    // Player 1 points at a spot (G): the marker shows on both screens.
    mouse_set_position(screenGetWidth() / 2, screenGetHeight() / 3);
    coop_autotest_session_frames(50, false, false);
    if (!coop_host_chat_key(KEY_LOWERCASE_G)) {
        autotest_fail("G did not place a marker");
    }
    renderPresent();
    {
        // Yellow on the screen around the spot, and the game's own screen
        // buffer clean again (the marker is only drawn for showing).
        int yellow = 0;
        int cx = screenGetWidth() / 2;
        int cy = screenGetHeight() / 3;
        for (int y = std::max(cy - 60, 0); y < std::min(cy + 40, gSdlTextureSurface->h); y++) {
            for (int x = std::max(cx - 40, 0); x < std::min(cx + 40, gSdlTextureSurface->w); x++) {
                Uint32 pixel = ((Uint32*)gSdlTextureSurface->pixels)[y * gSdlTextureSurface->pitch / 4 + x];
                Uint8 r;
                Uint8 g;
                Uint8 b;
                SDL_GetRGB(pixel, gSdlTextureSurface->format, &r, &g, &b);
                if (r > 200 && g > 200 && b < 90) {
                    yellow++;
                }
            }
        }
        autotest_log("marker: %d yellow pixels shown", yellow);
        SDL_SaveBMP(gSdlTextureSurface, "marker.bmp");
        if (yellow < 20) {
            autotest_fail("the marker is not on screen");
        }
    }
    coop_autotest_session_frames(1500, false, false);

    coop_host_exit();
    return 0;
}

static void coop_autotest_session_walk()
{
    static int step = 0;
    static const int offsets[][2] = { { 90, 40 }, { -80, 50 }, { 60, -60 }, { -70, -30 } };
    if (step >= 4 && getenv("COOP_AUTOTEST_LEAVE_IN_MENU") != NULL) {
        // Leave (F10) with a screen still open: the host must cope.
        inject_key(getenv("COOP_AUTOTEST_LEAVE_IN_MENU")[0]);
        inject_wait(90);
        inject_call(coop_autotest_screenshot);
        inject_key(KEY_F10);
        return;
    }
    if (step >= 4) {
        // Player 2's own screens, each closed again, then leave (F10).
        const int keys[] = { 'i', 'c', 'p', 's' };
        for (int key : keys) {
            inject_key(key);
            inject_wait(90);
            inject_call(coop_autotest_screenshot);
            inject_key(KEY_ESCAPE);
            inject_wait(60);
        }
        inject_call(coop_autotest_screenshot);
        inject_key(KEY_F10);
        return;
    }
    const int* offset = offsets[step++ % 4];
    coop_autotest_click_centered(320 + offset[0], 190 + offset[1]);
    inject_wait(90);
    inject_call(coop_autotest_session_walk);
}

static int coop_autotest_net_session_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();

    inject_wait(240);
    inject_call(coop_autotest_session_walk);
    int rc = coop_net_client_run();
    autotest_log("first visit finished with %d: %s", rc, coop_net_last_error());

    SDL_Delay(2000);

    // Back in; then just watch (no input, a screenshot now and then)
    // until the host ends.
    if (const char* leaveAfter = getenv("COOP_AUTOTEST_LEAVE_IN_BARTER")) {
        inject_wait(atoi(leaveAfter));
        inject_call(coop_autotest_screenshot);
        inject_key(KEY_F10);
    }
    inject_call(coop_autotest_session_shots);
    rc = coop_net_client_run();
    autotest_log("second visit finished with %d: %s", rc, coop_net_last_error());
    return 0;
}

// The end of the game with player 2 connected: the ending slideshow and
// movie (which ends the game, like main_game_loop would). Paired with
// net_tour_client, whose screenshots show what player 2 sees.
static int coop_autotest_net_ending_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    if (coop_add_player("premade\\stealth.gcd") == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }
    if (!coop_autotest_session_frames(30000, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }
    coop_autotest_session_frames(3000, false, false);

    // The good ending's choices.
    game_set_global_var(GVAR_VATS_STATUS, 1);
    game_set_global_var(GVAR_NECROP_WATER_CHIP_TAKEN, 1);

    unsigned int start = get_time();
    endgame_slideshow();
    autotest_log("slideshow took %u ms, client connected %d", elapsed_time(start), coop_net_client_connected() ? 1 : 0);

    start = get_time();
    endgame_movie();
    autotest_log("movie and credits took %u ms, game ends %d, client connected %d", elapsed_time(start),
        game_user_wants_to_quit, coop_net_client_connected() ? 1 : 0);
    if (game_user_wants_to_quit != 2) {
        autotest_fail("the ending did not end the game");
    }
    if (!coop_net_client_connected()) {
        autotest_fail("player 2 was dropped during the ending");
    }

    coop_host_exit();
    return 0;
}

static void coop_autotest_save_surface()
{
    // The movie player draws straight to the screen surface, which
    // dump_screen() does not see.
    static int count = 0;
    char name[32];
    snprintf(name, sizeof(name), "surface%02d.bmp", count++);
    SDL_SaveBMP(gSdlSurface, name);
}

// The ending movie on its own, with screenshots while it plays (to tell
// the movie player's own picture from what a client gets).
static int coop_autotest_movie(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    for (int shot = 0; shot < 6; shot++) {
        inject_wait(90);
        inject_call(coop_autotest_save_surface);
    }
    gmovie_play(MOVIE_WALKM, 0);
    autotest_log("movie done");
    return 0;
}

// MULTIPLAYER, MORE: COLORBLIND toggles and is saved.
static int coop_autotest_menu_more(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_create();
    main_menu_show(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (main_menu_loop() != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen");
        return -1;
    }

    inject_wait(10);
    inject_call(coop_autotest_screenshot); // the MULTIPLAYER menu
    coop_autotest_click_menu_slot(2); // MORE
    inject_wait(20);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(1); // OUTLINES
    inject_wait(20);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(0); // PLAYER 1: GREEN -> WHITE (skips yellow)
    inject_wait(20);
    coop_autotest_click_menu_slot(1); // PLAYER 2: YELLOW -> ORANGE
    inject_wait(20);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(2); // BACK
    inject_wait(10);
    coop_autotest_click_menu_slot(2); // RESOLUTION
    inject_wait(20);
    coop_autotest_click_menu_slot(0); // 640x480 -> 800x600
    inject_wait(20);
    coop_autotest_click_menu_slot(0); // -> 1024x768
    inject_wait(20);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(1); // BACK
    inject_wait(20);
    inject_call(coop_autotest_screenshot); // "Restart the game"
    inject_key(KEY_RETURN);
    inject_wait(20);
    coop_autotest_click_menu_slot(3); // KNOCKOUT: OFF -> ON
    inject_wait(20);
    inject_call(coop_autotest_screenshot); // what it does
    inject_key(KEY_RETURN);
    inject_wait(20);
    inject_call(coop_autotest_screenshot); // KNOCKOUT: ON
    coop_autotest_click_menu_slot(4); // TALK: BOTH -> PLAYER 1
    inject_wait(20);
    inject_call(coop_autotest_screenshot); // what it does
    inject_key(KEY_RETURN);
    inject_wait(20);
    coop_autotest_click_menu_slot(5); // BACK
    inject_wait(10);
    coop_autotest_click_menu_slot(5); // BACK

    bool knockoutBefore = coop_knockout_setting();
    bool talkBefore = coop_talk_setting();
    coop_menu_run();

    int talkSaved = 1;
    config_get_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_TALK_KEY, &talkSaved);
    autotest_log("TALK: %d before, %d after, saved %d", talkBefore ? 1 : 0, coop_talk_setting() ? 1 : 0, talkSaved);
    if (coop_talk_setting() == talkBefore || talkSaved != (coop_talk_setting() ? 1 : 0)) {
        autotest_fail("TALK did not change and save the setting");
    }

    int knockoutSaved = 0;
    config_get_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_KNOCKOUT_KEY, &knockoutSaved);
    autotest_log("KNOCKOUT: %d before, %d after, saved %d", knockoutBefore ? 1 : 0, coop_knockout_setting() ? 1 : 0, knockoutSaved);
    if (coop_knockout_setting() == knockoutBefore || knockoutSaved != (coop_knockout_setting() ? 1 : 0)) {
        autotest_fail("KNOCKOUT did not change and save the setting");
    }

    Config resolutionConfig;
    int width = 0;
    int height = 0;
    if (config_init(&resolutionConfig)) {
        config_load(&resolutionConfig, "f1_res.ini", false);
        config_get_value(&resolutionConfig, "MAIN", "SCR_WIDTH", &width);
        config_get_value(&resolutionConfig, "MAIN", "SCR_HEIGHT", &height);
        config_exit(&resolutionConfig);
    }
    // Two steps from 640x480 (or whatever the test started with).
    autotest_log("f1_res.ini after RESOLUTION: %dx%d (screen %dx%d)", width, height, screenGetWidth(), screenGetHeight());
    if (screenGetWidth() == 640 ? (width != 1024 || height != 768) : (width == screenGetWidth() && height == screenGetHeight())) {
        autotest_fail("RESOLUTION did not save a new size");
    }

    int colors[2];
    coop_outline_colors_from_config(colors);
    int expected[2] = { 2, 3 };
    coop_set_outline_colors(0, expected);
    int white = coop_viewer_outline_color(0);
    autotest_log("outline colors after menu: %s, %s", coop_outline_color_name(colors[0]), coop_outline_color_name(colors[1]));
    if (colors[0] != 2 || colors[1] != 3 || white != colorTable[(31 << 10) | (31 << 5) | 31]) {
        autotest_fail("OUTLINES did not change the colors");
    }
    return 0;
}

static void coop_autotest_screenshot_every_2s()
{
    inject_wait(120);
    inject_call(coop_autotest_screenshot);
    inject_call(coop_autotest_screenshot_every_2s);
}

// The credits from the main menu, with a screenshot every two seconds
// (the co-op lines come at the end).
static int coop_autotest_credits(AutotestNewGameProc* newGame)
{
    inject_call(coop_autotest_screenshot_every_2s);
    credits("credits.txt", -1, false);
    autotest_log("credits done");
    return 0;
}

static Object* coop_autotest_death_victim = NULL;

static void coop_autotest_kill_victim()
{
    if (coop_autotest_death_victim != NULL) {
        autotest_log("%s dies", critter_name(coop_autotest_death_victim));
        critter_kill(coop_autotest_death_victim, -1, true);
    }
}

// A player dies while player 2 is connected (COOP_AUTOTEST_VICTIM=1 or 2):
// the game's own main loop must end the game in death. Paired with
// net_tour_client.
static int coop_autotest_net_death_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    if (coop_add_player("premade\\stealth.gcd") == NULL) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_autotest_session_frames(30000, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }

    const char* victim = getenv("COOP_AUTOTEST_VICTIM");
    int index = victim != NULL && atoi(victim) == 1 ? 0 : 1;
    coop_autotest_death_victim = coop_player(index)->obj;

    // In the real loop: a few seconds of play, then the death.
    inject_wait(240);
    inject_call(coop_autotest_kill_victim);

    autotest_log("before the loop: quit %d", game_user_wants_to_quit);
    unsigned int start = get_time();
    bool death = main_game_loop_for_test();
    autotest_log("after the loop: quit %d, victim dead %d", game_user_wants_to_quit, critter_is_dead(coop_autotest_death_victim) ? 1 : 0);
    autotest_log("main loop ended after %u ms, death %d, client connected %d", elapsed_time(start), death ? 1 : 0, coop_net_client_connected() ? 1 : 0);
    if (!death) {
        autotest_fail("the game did not end in death");
    }

    coop_host_exit();
    return 0;
}

} // namespace fallout
