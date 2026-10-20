#include "coop_autotest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <unistd.h>
#endif

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "autotest.h"
#include "combat.h"
#include "config.h"
#include "coop.h"
#include "coop_compat.h"
#include "coop_host.h"
#include "coop_crash.h"
#include "coop_lobby.h"
#include "coop_menu.h"
#include "coop_net.h"
#include "credits.h"
#include "critter.h"
#include "db.h"
#include "display_monitor.h"
#include "endgame.h"
#include "main.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_dialog.h"
#include "game_vars.h"
#include "inject.h"
#include "input.h"
#include "interface.h"
#include "kb.h"
#include "item.h"
#include "loadsave.h"
#include "mainmenu.h"
#include "map.h"
#include "object.h"
#include "palette.h"
#include "perk.h"
#include "platform_compat.h"
#include "party_member.h"
#include "actions.h"
#include "animation.h"
#include "art.h"
#include "game_movie.h"
#include "proto.h"
#include "random.h"
#include "proto_instance.h"
#include "memory.h"
#include "sound_effects_list.h"
#include "queue.h"
#include "scripts.h"
#include "sfall_config.h"
#include "skill.h"
#include "sound.h"
#include "sound_decoder.h"
#include "svga.h"
#include "stat.h"
#include "tile.h"
#include "window_manager.h"
#include "worldmap.h"

namespace fallout {

// Quick save slot used by the co-op tests (0-based; SLOT08).
#define COOP_AUTOTEST_SLOT 7

static int coop_autotest_players(AutotestNewGameProc* newGame);
static int coop_autotest_rads(AutotestNewGameProc* newGame);
static int coop_autotest_movie(AutotestNewGameProc* newGame);
static int coop_autotest_sound(AutotestNewGameProc* newGame);
static int coop_autotest_far_tile(Object* player);
static void coop_autotest_check_far_cameras(const char* mapName);
static void coop_autotest_heal_players();
static int coop_autotest_travel(AutotestNewGameProc* newGame);
static int coop_autotest_combat(AutotestNewGameProc* newGame);
static bool coop_autotest_attack_turn(Object* player);
static bool coop_autotest_pass_turn(Object* player);
static Object* coop_autotest_spawn_enemy(Object* nextTo);
static void coop_autotest_count_turn(Object* player);
static void coop_autotest_clear_critters();
static int coop_autotest_join(AutotestNewGameProc* newGame);
static int coop_autotest_main_menu(AutotestNewGameProc* newGame);
static int coop_autotest_gamepad(AutotestNewGameProc* newGame);
static int coop_autotest_give(AutotestNewGameProc* newGame);
static int coop_autotest_knockout(AutotestNewGameProc* newGame);
static int coop_autotest_character(AutotestNewGameProc* newGame);
static int coop_autotest_barter(AutotestNewGameProc* newGame);
static int coop_autotest_looks(AutotestNewGameProc* newGame);
static int coop_autotest_roofs(AutotestNewGameProc* newGame);
static int coop_autotest_four(AutotestNewGameProc* newGame);
static int coop_autotest_net_four_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_four_client(AutotestNewGameProc* newGame);
static void coop_autotest_press_space_forever();
static int coop_autotest_net_talk_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_talk_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_combat_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_combat_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_character_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_character_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_session_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_tour_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_tour_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_session_client(AutotestNewGameProc* newGame);
static void coop_autotest_session_walk();
static void coop_autotest_session_shots();
static int coop_autotest_talk(AutotestNewGameProc* newGame);
static int coop_autotest_menu_flow(AutotestNewGameProc* newGame);
static void coop_autotest_click_menu_slot(int slot);
static bool coop_autotest_relay_host_setup();
static void coop_autotest_relay_client_setup();
static void coop_autotest_host_frame();
static bool coop_autotest_session_frames(unsigned int ms, bool untilConnected, bool untilDisconnected);
static int coop_autotest_net_host(AutotestNewGameProc* newGame);
static void coop_autotest_net_client_exit();
static int coop_autotest_net_client(AutotestNewGameProc* newGame);
static int coop_autotest_net_play_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_play_client(AutotestNewGameProc* newGame);
static void coop_autotest_click_centered(int x, int y);
// How long a test host waits for a client to join (slow under the address
// sanitizer, on a busy machine).
#define COOP_AUTOTEST_JOIN_WAIT 90000

static void coop_autotest_screenshot();

// The enemy of the combat tests, and the player turns counted there.
static Object* coop_autotest_enemy = nullptr;
static int coop_autotest_turns[COOP_MAX_PLAYERS];
static int coop_autotest_turns_total = 0;
static bool coop_autotest_players_together(const char* when);
static int coop_autotest_poison(AutotestNewGameProc* newGame);
static int coop_autotest_lobby_find(AutotestNewGameProc* newGame);
static int coop_autotest_explode_exit(AutotestNewGameProc* newGame);
static int coop_autotest_map_entry(AutotestNewGameProc* newGame);
static int coop_autotest_map_edges(AutotestNewGameProc* newGame);
static int coop_autotest_egg(AutotestNewGameProc* newGame);
static int coop_autotest_monitor(AutotestNewGameProc* newGame);
static int coop_autotest_game_info(AutotestNewGameProc* newGame);
static int coop_autotest_net_death_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_worldmap_host(AutotestNewGameProc* newGame);
static int coop_autotest_net_ending_host(AutotestNewGameProc* newGame);
static int coop_autotest_menu_join(AutotestNewGameProc* newGame);
static int coop_autotest_menu_server(AutotestNewGameProc* newGame);
static int coop_autotest_menu_host_code(AutotestNewGameProc* newGame);
static int coop_autotest_menu_crash_report(AutotestNewGameProc* newGame);
static int coop_autotest_menu_more(AutotestNewGameProc* newGame);
static int coop_autotest_menu_update_prompt(AutotestNewGameProc* newGame);
static int coop_autotest_interact(AutotestNewGameProc* newGame);
static int coop_autotest_p2_moves(AutotestNewGameProc* newGame);
static int coop_autotest_combat_far(AutotestNewGameProc* newGame);
static void coop_autotest_compare_lookups(PlayerState* player, const char* when);
static bool coop_autotest_files_equal_except(const char* path1, const char* path2, const char* skipPrefix);
static int coop_autotest_map_time(AutotestNewGameProc* newGame);
static int coop_autotest_pack(AutotestNewGameProc* newGame);
static int coop_autotest_story_kill(AutotestNewGameProc* newGame);
static void coop_autotest_log_story(const char* when);
static bool coop_autotest_frames_until(unsigned int ms, bool (*done)());
static int coop_autotest_object_find(AutotestNewGameProc* newGame);
static int coop_autotest_config_brackets(AutotestNewGameProc* newGame);
static int coop_autotest_acm_fmt31(AutotestNewGameProc* newGame);
static int coop_autotest_sfx_sizes(AutotestNewGameProc* newGame);
static int coop_autotest_db_sfx(AutotestNewGameProc* newGame);
static int coop_autotest_draw_bench(AutotestNewGameProc* newGame);
static int coop_autotest_credits(AutotestNewGameProc* newGame);

void coop_autotest_register()
{
    autotest_add_script("coop", coop_autotest_players);
    autotest_add_script("coop_rads", coop_autotest_rads);
    autotest_add_script("movie", coop_autotest_movie);
    autotest_add_script("coop_sound", coop_autotest_sound);
    autotest_add_script("coop_travel", coop_autotest_travel);
    autotest_add_script("coop_combat", coop_autotest_combat);
    autotest_add_script("coop_join", coop_autotest_join);
    autotest_add_script("main_menu", coop_autotest_main_menu);
    autotest_add_script("gamepad", coop_autotest_gamepad);
    autotest_add_script("coop_give", coop_autotest_give);
    autotest_add_script("coop_knockout", coop_autotest_knockout);
    autotest_add_script("coop_character", coop_autotest_character);
    autotest_add_script("coop_barter", coop_autotest_barter);
    autotest_add_script("coop_looks", coop_autotest_looks);
    autotest_add_script("coop_roofs", coop_autotest_roofs);
    autotest_add_script("coop_four", coop_autotest_four);
    autotest_add_script("net_four_host", coop_autotest_net_four_host);
    autotest_add_script("net_four_client", coop_autotest_net_four_client);
    autotest_add_script("net_talk_host", coop_autotest_net_talk_host);
    autotest_add_script("net_talk_client", coop_autotest_net_talk_client);
    autotest_add_script("net_combat_host", coop_autotest_net_combat_host);
    autotest_add_script("net_combat_client", coop_autotest_net_combat_client);
    autotest_add_script("net_character_host", coop_autotest_net_character_host);
    autotest_add_script("net_character_client", coop_autotest_net_character_client);
    autotest_add_script("net_session_host", coop_autotest_net_session_host);
    autotest_add_script("net_tour_host", coop_autotest_net_tour_host);
    autotest_add_script("net_tour_client", coop_autotest_net_tour_client);
    autotest_add_script("net_session_client", coop_autotest_net_session_client);
    autotest_add_script("coop_talk", coop_autotest_talk);
    autotest_add_script("menu_flow", coop_autotest_menu_flow);
    autotest_add_script("net_host", coop_autotest_net_host);
    autotest_add_script("net_client", coop_autotest_net_client);
    autotest_add_script("net_play_host", coop_autotest_net_play_host);
    autotest_add_script("net_play_client", coop_autotest_net_play_client);
    autotest_add_script("coop_poison", coop_autotest_poison);
    autotest_add_script("lobby_find", coop_autotest_lobby_find);
    autotest_add_script("explode_exit", coop_autotest_explode_exit);
    autotest_add_script("map_entry", coop_autotest_map_entry);
    autotest_add_script("map_edges", coop_autotest_map_edges);
    autotest_add_script("coop_egg", coop_autotest_egg);
    autotest_add_script("coop_monitor", coop_autotest_monitor);
    autotest_add_script("game_info", coop_autotest_game_info);
    autotest_add_script("net_death_host", coop_autotest_net_death_host);
    autotest_add_script("net_worldmap_host", coop_autotest_net_worldmap_host);
    autotest_add_script("net_ending_host", coop_autotest_net_ending_host);
    autotest_add_script("menu_join", coop_autotest_menu_join);
    autotest_add_script("menu_server", coop_autotest_menu_server);
    autotest_add_script("menu_host_code", coop_autotest_menu_host_code);
    autotest_add_script("menu_crash_report", coop_autotest_menu_crash_report);
    autotest_add_script("menu_more", coop_autotest_menu_more);
    autotest_add_script("menu_update_prompt", coop_autotest_menu_update_prompt);
    autotest_add_script("coop_interact", coop_autotest_interact);
    autotest_add_script("coop_p2_moves", coop_autotest_p2_moves);
    autotest_add_script("coop_combat_far", coop_autotest_combat_far);
    autotest_add_script("map_time", coop_autotest_map_time);
    autotest_add_script("pack", coop_autotest_pack);
    autotest_add_script("story_kill", coop_autotest_story_kill);
    autotest_add_script("object_find", coop_autotest_object_find);
    autotest_add_script("config_brackets", coop_autotest_config_brackets);
    autotest_add_script("acm_fmt31", coop_autotest_acm_fmt31);
    autotest_add_script("sfx_sizes", coop_autotest_sfx_sizes);
    autotest_add_script("db_sfx", coop_autotest_db_sfx);
    autotest_add_script("draw_bench", coop_autotest_draw_bench);
    autotest_add_script("credits", coop_autotest_credits);
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

    pcAddExperience(6500);

    PlayerState* p1 = coop_player(0);
    Object* p1obj = gDude;
    int karma = gameGetGlobalVar(GVAR_PLAYER_REPUTATION);
    int karmaStat = pcGetStat(PC_STAT_KARMA);
    int p1Xp = pcGetStat(PC_STAT_EXPERIENCE);

    autotest_dump_state("p1_before.txt");

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // Player 2 takes no companion's place (scripts count the party against
    // Charisma).
    autotest_log("party: %d members, %d for scripts", _getPartyMemberCount(), partyMemberCountForScripts());
    if (partyMemberCountForScripts() != 1 || _getPartyMemberCount() != 2) {
        autotest_fail("player 2 counts as a companion");
    }

    Object* p2obj = p2->obj;
    autotest_log("p1 '%s' tile %d, p2 '%s' tile %d, distance %d",
        critterGetName(p1obj), p1obj->tile, critterGetName(p2obj), p2obj->tile, objectGetDistanceBetween(p1obj, p2obj));

    if (coop_player_count() != 2) autotest_fail("player count %d", coop_player_count());
    if (gDude != p1obj) autotest_fail("gDude changed by adding player 2");
    if (coop_active_player() != p1) autotest_fail("active player changed by adding player 2");
    if (coop_player_of(p2obj) != p2) autotest_fail("player_of(p2)");
    if (coop_player_of(p1obj) != p1) autotest_fail("player_of(p1)");
    if (coop_player_by_pid(p2obj->pid) != p2) autotest_fail("player_by_pid(p2)");
    if (strcmp(critterGetName(p1obj), critterGetName(p2obj)) == 0) autotest_fail("players share a name");
    if (p2obj->elevation != p1obj->elevation || objectGetDistanceBetween(p1obj, p2obj) > 5) autotest_fail("player 2 not placed next to player 1");
    if (gameGetGlobalVar(GVAR_PLAYER_REPUTATION) != karma || pcGetStat(PC_STAT_KARMA) != karmaStat) {
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
            if (perkAdd(gDude, perk) == 0) {
                autotest_log("p2 perk added: %d", perk);
                perksAdded++;
            }
        }
        killsIncByType(KILL_TYPE_RAT);
        pcAddExperience(1000);
        skillAdd(gDude, SKILL_STEAL);
        gameSetGlobalVar(GVAR_NUKA_COLA_ADDICT, 1);
        autotest_dump_state("p2_changed.txt");
    }

    if (autotest_files_equal("autotest/p2.txt", "autotest/p2_changed.txt")) {
        autotest_fail("player 2 changes had no effect");
    }

    // XP is earned by every player; nothing else may leak into player 1.
    if (pcGetStat(PC_STAT_EXPERIENCE) < p1Xp + 1000) {
        autotest_fail("player 1 did not get player 2's XP (%d -> %d)", p1Xp, pcGetStat(PC_STAT_EXPERIENCE));
    }

    if (gameGetGlobalVar(GVAR_NUKA_COLA_ADDICT) != 0) {
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
        if (gDude != p2obj || coop_active_player() != p2 || coop_controlled_player() != p2) {
            autotest_fail("control switch did not activate player 2");
        }

        autotest_dump_state("p2_controlled.txt");
        if (!autotest_files_equal("autotest/p2_changed.txt", "autotest/p2_controlled.txt")) {
            autotest_fail("player 2 state differs when controlled");
        }

        coop_autotest_compare_lookups(p1, "while player 2 controlled");

        tileWindowRefresh();
        takeScreenshot();

        coop_set_controlled(p1);
        if (gDude != p1obj || coop_active_player() != p1) {
            autotest_fail("control did not return to player 1");
        }
    }

    autotest_run_checks("with two players");

    tileWindowRefresh();
    takeScreenshot();

    // Save/load round trip keeps both players.
    {
        ActivePlayerScope scope(p2);
        autotest_dump_state("p2_before_save.txt");
    }
    autotest_dump_state("p1_before_save.txt");

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save failed");
        return -1;
    }

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("load failed");
        return -1;
    }

    if (coop_player_count() != 2) {
        autotest_fail("player 2 missing after load (count %d)", coop_player_count());
        return -1;
    }

    p2 = coop_player(1);
    autotest_log("after load: p1 '%s', p2 '%s' tile %d", critterGetName(gDude), critterGetName(p2->obj), p2->obj->tile);

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

    if (gameGetGlobalVar(GVAR_NUKA_COLA_ADDICT) != 0) {
        autotest_fail("player 1 addicted after load");
    }

    {
        ActivePlayerScope scope(p2);
        if (gameGetGlobalVar(GVAR_NUKA_COLA_ADDICT) != 1) {
            autotest_fail("player 2's addiction lost");
        }
    }

    return 0;
}

// Sum of the radiation sickness bonuses on the primary stats.
static int coop_autotest_rad_penalty(Object* obj)
{
    int sum = 0;
    for (int stat = STAT_STRENGTH; stat <= STAT_AGILITY; stat++) {
        sum += critterGetBonusStat(obj, stat);
    }
    return sum;
}

static void coop_autotest_pass_hours(int hours)
{
    for (int hour = 0; hour < hours; hour++) {
        gameTimeAddTicks(GAME_TIME_TICKS_PER_HOUR);
        queueProcessEvents();
    }
}

// Radiation sickness: player 2 with lots of rads gets sick (their stats
// drop) and gets better a week later; player 1 without rads stays well.
static void coop_autotest_save_surface()
{
    // The movie player draws straight to the screen surface, which
    // screenshots do not see.
    static int count = 0;
    char name[32];
    snprintf(name, sizeof(name), "surface%02d.bmp", count++);
    SDL_SaveBMP(gSdlSurface, name);
}

// A movie on its own, with pictures of the screen while it plays (a test
// of the movie decoder: compare the pictures between builds).
// The last effect the host passed on (the hook the co-op host sets).
static std::string coop_autotest_reported_effect;

static void coop_autotest_capture_effect(int event, const char* name, int a, int b, int c)
{
    if (event == GSOUND_EVENT_SFX) {
        coop_autotest_reported_effect = name;
    }
}

// Sound effects as loud as they are for each player (what the host sends
// each client): player 2 put some 24 hexes from player 1, off each other's
// screens at the tests' 640x480.
static int coop_autotest_sound(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* player2 = coop_add_player("premade\\stealth.gcd");
    if (player2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    int distant = coop_autotest_far_tile(player2->obj);
    if (distant == -1) {
        autotest_fail("no free tile far from player 1");
        return -1;
    }
    Rect rect;
    objectSetLocation(player2->obj, distant, obj_dude->elevation, &rect);

    int here = obj_dude->tile;
    int elevation = obj_dude->elevation;
    struct Check {
        const char* what;
        int index;
        int tile;
        int elevation;
        int expected;
    };
    const Check checks[] = {
        { "next to player 1, for player 1", 0, here, elevation, 0x7FFF },
        { "next to player 1, for player 2", 1, here, elevation, 0x2AAA },
        { "next to player 2, for player 2", 1, distant, elevation, 0x7FFF },
        { "next to player 2, for player 1", 0, distant, elevation, 0x2AAA },
        { "another elevation, for player 2", 1, distant, (elevation + 1) % 3, 0x2AAA },
        { "from nowhere, for player 2", 1, -1, elevation, 1234 },
    };
    for (const Check& check : checks) {
        int volume = coop_effect_volume_for(check.index, check.tile, check.elevation, 1234);
        if (volume != check.expected) {
            autotest_fail("effect %s: volume %d, expected %d", check.what, volume, check.expected);
        } else {
            autotest_log("effect %s: volume %d", check.what, volume);
        }
    }

    // The players' hit and death sounds (most only exist as aliases, by
    // gender) as the host passes them on: a client, without the critter,
    // must be able to load each name it gets.
    GsoundEventHook* savedHook = gsound_event_hook;
    gsound_event_hook = coop_autotest_capture_effect;
    int passed = 0;
    int heard = 0;
    Object* critters[] = { obj_dude, player2->obj };
    const int anims[] = { ANIM_HIT_FROM_FRONT, ANIM_HIT_FROM_BACK };
    for (Object* critter : critters) {
        for (int anim : anims) {
            for (int extra = 0; extra < 2; extra++) {
                char* name = sfxBuildCharName(critter, anim, extra == 0 ? CHARACTER_SOUND_EFFECT_UNUSED : CHARACTER_SOUND_EFFECT_DIE);
                std::string requested = name;
                Sound* sound = soundEffectLoad(requested.c_str(), critter);
                if (sound == nullptr) {
                    continue;
                }
                heard++;
                coop_autotest_reported_effect.clear();
                soundEffectPlay(sound);
                std::string reported = coop_autotest_reported_effect;
                // (Stopped, so it doesn't hold one of the few effect slots.)
                soundStop(sound);
                soundEffectDelete(sound);
                Sound* onClient = reported.empty() ? nullptr : soundEffectLoad(reported.c_str(), nullptr);
                if (onClient == nullptr) {
                    autotest_fail("effect %s passed on as '%s', which a client can't load", requested.c_str(), reported.c_str());
                } else {
                    passed++;
                    if (reported != requested) {
                        autotest_log("effect %s passed on as %s", requested.c_str(), reported.c_str());
                    }
                    soundEffectDelete(onClient);
                }
            }
        }
    }
    gsound_event_hook = savedHook;
    autotest_log("%d of %d critter sounds loadable on a client", passed, heard);
    return 0;
}

static int coop_autotest_movie(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    for (int shot = 0; shot < 8; shot++) {
        inject_wait(60);
        inject_call(coop_autotest_save_surface);
    }
    gameMoviePlay(MOVIE_VSUIT, 0);
    autotest_log("movie done");
    return 0;
}

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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    int p1Before = coop_autotest_rad_penalty(p1->obj);
    int p2Before = coop_autotest_rad_penalty(p2->obj);
    {
        ActivePlayerScope scope(p2);
        critterAdjustRadiation(p2->obj, 450);
    }
    autotest_log("player 2 rads %d, player 1 rads %d", critterGetRadiation(p2->obj), critterGetRadiation(p1->obj));

    coop_autotest_pass_hours(30);
    int p1Sick = coop_autotest_rad_penalty(p1->obj);
    int p2Sick = coop_autotest_rad_penalty(p2->obj);
    autotest_log("after 30 hours: player 1 stat bonus %d -> %d, player 2 %d -> %d, player 2 hp %d",
        p1Before, p1Sick, p2Before, p2Sick, critterGetHitPoints(p2->obj));
    if (p2Sick >= p2Before) {
        autotest_fail("player 2 did not get radiation sickness");
    }
    if (p1Sick != p1Before) {
        autotest_fail("player 1 got player 2's radiation sickness");
    }
    if (critterIsDead(p2->obj) || critterIsDead(p1->obj)) {
        autotest_fail("a player died");
    }

    // The sickness wears off after a week (the rads stay until treated).
    {
        ActivePlayerScope scope(p2);
        critterAdjustRadiation(p2->obj, -critterGetRadiation(p2->obj));
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
        critterAdjustRadiation(gDude, 450);
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
        critterAdjustRadiation(gDude, -critterGetRadiation(gDude));
    }
    coop_autotest_pass_hours(8 * 24);
    autotest_log("both well again: player 1 stat bonus %d, player 2 %d",
        coop_autotest_rad_penalty(p1->obj), coop_autotest_rad_penalty(p2->obj));
    if (coop_autotest_rad_penalty(p1->obj) != p1Before || coop_autotest_rad_penalty(p2->obj) != p2Before) {
        autotest_fail("radiation sickness did not wear off for both");
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    Object* p1obj = coop_player(0)->obj;
    Object* p2obj = p2->obj;

    critterAdjustPoison(p1obj, 6);
    critterAdjustPoison(p2obj, 12);
    critterAdjustRadiation(p2obj, 40);
    int p1Poison = critterGetPoison(p1obj);
    int p2Poison = critterGetPoison(p2obj);
    int p2Hp = critterGetHitPoints(p2obj);
    autotest_log("after: player 1 poison %d, player 2 poison %d, rads %d", p1Poison, p2Poison, critterGetRadiation(p2obj));
    if (p2Poison <= 0 || critterGetRadiation(p2obj) <= 0) {
        autotest_fail("player 2 got no poison or rads");
    }
    if (gDude != p1obj) {
        autotest_fail("player 1 is no longer active");
    }

    // A few game hours: the poison does damage and wears off, for each.
    for (int step = 0; step < 60; step++) {
        gameTimeAddSeconds(60);
        queueProcessEvents();
    }
    autotest_log("later: player 1 poison %d, player 2 poison %d (hp %d -> %d)", critterGetPoison(p1obj),
        critterGetPoison(p2obj), p2Hp, critterGetHitPoints(p2obj));
    if (critterGetPoison(p2obj) >= p2Poison || critterGetHitPoints(p2obj) >= p2Hp) {
        autotest_fail("player 2's poison did not work");
    }
    if (critterGetPoison(p1obj) >= p1Poison) {
        autotest_fail("player 1's poison stopped (the other player's cleared it)");
    }
    if (gDude != p1obj || coop_active_player() != coop_player(0)) {
        autotest_fail("the wrong player is active afterwards");
    }

    return 0;
}

// The index of map `fileName` (e.g. "arvillag.map"), or -1.
static int coop_autotest_map_index(const char* fileName)
{
    char name[32];
    snprintf(name, sizeof(name), "%s", fileName);
    return wmMapMatchNameToIdx(name);
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // A scripted item in player 2's pack must keep its script across map
    // changes and saves, like the party's items do.
    Object* scriptedItem = nullptr;
    for (int pid = 1; pid < 1000 && scriptedItem == nullptr; pid++) {
        Proto* proto;
        if (protoGetProto(pid, &proto) == -1 || proto->sid == -1) {
            continue;
        }

        Object* item;
        if (objectCreateWithPid(&item, pid) == -1) {
            continue;
        }

        if (item->sid == -1 || itemAdd(p2->obj, item, 1) != 0) {
            objectDestroy(item, nullptr);
            continue;
        }

        _obj_disconnect(item, nullptr);
        scriptedItem = item;
        autotest_log("player 2 carries scripted item %s", protoGetName(pid));
    }

    // Exit grids: find one leading to another map (not the world map,
    // whose screen would wait for input).
    Object* exitGrid = nullptr;
    for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
        if (obj->pid >= 0x5000010 && obj->pid <= 0x5000017 && obj->data.misc.map > 0 && obj->elevation == gDude->elevation) {
            exitGrid = obj;
            break;
        }
    }

    char mapName[16];
    strcpy(mapName, gMapHeader.name);

    if (exitGrid == nullptr) {
        autotest_log("no exit grid to another map on %s; exit grid checks skipped", mapName);
    } else {
        autotest_log("exit grid at tile %d -> map %d", exitGrid->tile, exitGrid->data.misc.map);

        coop_set_controlled(p2);
        objectSetLocation(p2->obj, exitGrid->tile, exitGrid->elevation, nullptr);
        coop_set_controlled(p1);
        mapHandleTransition();
        if (strcmp(gMapHeader.name, mapName) != 0) {
            autotest_fail("player 2 left the map through an exit grid");
            return -1;
        }
        autotest_log("player 2 on exit grid: stayed on %s", gMapHeader.name);

        objectSetLocation(p1->obj, exitGrid->tile, exitGrid->elevation, nullptr);
        mapHandleTransition();
        if (strcmp(gMapHeader.name, mapName) == 0) {
            autotest_fail("player 1 did not leave the map through an exit grid");
        } else {
            autotest_log("player 1 on exit grid: moved to %s", gMapHeader.name);
        }

        coop_autotest_players_together("after exit grid");
    }

    // Script-style map change.
    MapTransition transition;
    transition.map = coop_autotest_map_index("arvillag.map");
    transition.elevation = 0;
    transition.tile = -1;
    transition.rotation = 0;
    mapSetTransition(&transition);
    mapHandleTransition();
    autotest_log("after load_map: %s", gMapHeader.name);
    coop_autotest_players_together("after map change");

    // Elevation change: player 2 follows.
    int otherElevation = gDude->elevation == 0 ? 1 : 0;
    objectSetLocation(gDude, gDude->tile, otherElevation, nullptr);
    mapSetElevation(otherElevation);
    coop_autotest_players_together("after elevation change");

    tileWindowRefresh();
    takeScreenshot();

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save after travel failed");
        return -1;
    }

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1 || coop_player_count() != 2) {
        autotest_fail("load after travel failed");
        return -1;
    }

    coop_autotest_players_together("after load");

    if (scriptedItem != nullptr) {
        int scripted = 0;
        Inventory* inventory = &(coop_player(1)->obj->data.inventory);
        for (int index = 0; index < inventory->length; index++) {
            Object* item = inventory->items[index].item;
            Script* script;
            if (item->sid != -1) {
                if (scriptGetScript(item->sid, &script) == -1) {
                    autotest_fail("player 2's %s lost its script", objectGetName(item));
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

static bool coop_autotest_players_together(const char* when)
{
    Object* p1obj = coop_player(0)->obj;
    Object* p2obj = coop_player(1)->obj;

    autotest_log("%s: p1 tile %d elev %d, p2 tile %d elev %d, map elev %d",
        when, p1obj->tile, p1obj->elevation, p2obj->tile, p2obj->elevation, gElevation);

    if (p2obj->elevation != p1obj->elevation || p1obj->elevation != gElevation || p2obj->tile == -1 || objectGetDistanceBetween(p1obj, p2obj) > 5) {
        autotest_fail("%s: players are not together", when);
        return false;
    }

    return true;
}

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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // No bystanders: at the temple Klint attacks anyone who fights there
    // (single player too).
    coop_autotest_clear_critters();

    // Fight 1: both players punch a weak enemy until it dies.
    Object* enemy = coop_autotest_spawn_enemy(p2->obj);
    if (enemy == nullptr) {
        return -1;
    }

    int killType = critterGetKillType(enemy);
    int killXp = critterGetExp(enemy);
    int p1Kills;
    int p2Kills;
    int p1Xp;
    int p2Xp;
    {
        ActivePlayerScope scope(p1);
        p1Kills = killsGetByType(killType);
        p1Xp = pcGetStat(PC_STAT_EXPERIENCE);
    }
    {
        ActivePlayerScope scope(p2);
        p2Kills = killsGetByType(killType);
        p2Xp = pcGetStat(PC_STAT_EXPERIENCE);
    }

    coop_autotest_enemy = enemy;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_autotest_turns_total = 0;
    coop_set_turn_hook(coop_autotest_attack_turn);

    CombatStartData attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = p2->obj;
    attack.defender = enemy;
    _combat(&attack);

    coop_set_turn_hook(nullptr);

    autotest_log("fight 1: turns p1 %d, p2 %d; enemy dead %d",
        coop_autotest_turns[0], coop_autotest_turns[1], critterIsDead(enemy) ? 1 : 0);

    if (isInCombat()) autotest_fail("fight 1 did not end");
    if (!critterIsDead(enemy)) autotest_fail("enemy survived fight 1");
    if (coop_autotest_turns[1] == 0) autotest_fail("player 2 never got a human turn");
    if (coop_any_player_has(DAM_DEAD)) autotest_fail("a player died in fight 1");

    // Exactly one player gets the kill; both get the XP.
    {
        ActivePlayerScope scope(p1);
        int p1KillsAfter = killsGetByType(killType);
        int p1XpAfter = pcGetStat(PC_STAT_EXPERIENCE);
        ActivePlayerScope scope2(p2);
        int p2KillsAfter = killsGetByType(killType);
        int p2XpAfter = pcGetStat(PC_STAT_EXPERIENCE);

        autotest_log("kills p1 %d->%d, p2 %d->%d; xp p1 %d->%d, p2 %d->%d (kill xp %d)",
            p1Kills, p1KillsAfter, p2Kills, p2KillsAfter, p1Xp, p1XpAfter, p2Xp, p2XpAfter, killXp);

        if ((p1KillsAfter - p1Kills) + (p2KillsAfter - p2Kills) != 1) autotest_fail("kill not credited to exactly one player");
        if (p1XpAfter < p1Xp + killXp || p2XpAfter < p2Xp + killXp) autotest_fail("kill XP not given to both players");
    }

    tileWindowRefresh();
    takeScreenshot();

    // Fight 2: players pass (by running out of [coop] turn_time when set,
    // else via the hook); the enemy kills a 1 HP player 2 -> game over.
    enemy = coop_autotest_spawn_enemy(p2->obj);
    if (enemy == nullptr) {
        return -1;
    }

    critterAdjustHitPoints(p2->obj, 1 - critterGetHitPoints(p2->obj));

    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    if (coop_turn_time_limit() == 0) {
        coop_set_turn_hook(coop_autotest_pass_turn);
    }

    unsigned int fightStart = getTicks();
    memset(&attack, 0, sizeof(attack));
    attack.attacker = enemy;
    attack.defender = p2->obj;
    _combat(&attack);

    coop_set_turn_hook(nullptr);

    autotest_log("fight 2: %d hooked turns, %u ms; p2 dead %d", coop_autotest_turns_total, getTicksSince(fightStart), critterIsDead(p2->obj) ? 1 : 0);

    if (isInCombat()) autotest_fail("fight 2 did not end");
    if (!critterIsDead(p2->obj)) autotest_fail("player 2 survived fight 2");
    if (!coop_any_player_has(DAM_DEAD)) autotest_fail("game over not detected");

    tileWindowRefresh();
    takeScreenshot();

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
        combatAttemptEnd();
    }
}

static bool coop_autotest_attack_turn(Object* player)
{
    coop_autotest_count_turn(player);

    Object* enemy = coop_autotest_enemy;
    if (enemy != nullptr && !critterIsDead(enemy) && objectGetDistanceBetween(player, enemy) <= 1) {
        autotest_log("%s punches %s", critterGetName(player), critterGetName(enemy));
        _combat_attack(player, enemy, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
    }

    // Enemy gone: END COMBAT, as a player would (bystanders that joined
    // keep a fight going otherwise).
    if (enemy != nullptr && critterIsDead(enemy)) {
        combatAttemptEnd();
    }

    return true;
}

static bool coop_autotest_pass_turn(Object* player)
{
    coop_autotest_count_turn(player);
    return true;
}

// Spawns a "Rat" next to `nextTo`, with few hit
// points.
static Object* coop_autotest_spawn_enemy(Object* nextTo)
{
    int pid = -1;
    for (int index = 1; index < 400 && pid == -1; index++) {
        Proto* proto;
        if (protoGetProto(0x1000000 | index, &proto) != -1) {
            const char* enemyName = getenv("COOP_AUTOTEST_ENEMY");
            if (compat_stricmp(protoGetName(0x1000000 | index), enemyName != nullptr ? enemyName : "Rat") == 0) {
                pid = 0x1000000 | index;
            }
        }
    }

    if (pid == -1) {
        autotest_fail("no Rat proto found");
        return nullptr;
    }

    Object* enemy;
    if (objectCreateWithPid(&enemy, pid) == -1) {
        autotest_fail("could not create enemy");
        return nullptr;
    }

    int tile = nextTo->tile;
    _obj_attempt_placement(enemy, tile, nextTo->elevation, 1);
    const char* enemyHits = getenv("COOP_AUTOTEST_ENEMY_HP");
    int hits = enemyHits != nullptr ? atoi(enemyHits) : 3;
    critterAdjustHitPoints(enemy, hits - critterGetHitPoints(enemy));

    autotest_log("spawned %s (pid %x) at tile %d, %d hp, team %d", critterGetName(enemy), pid, enemy->tile,
        critterGetHitPoints(enemy), enemy->data.critter.combat.team);

    return enemy;
}

// Removes every critter from the map except the players (drawn gone at
// once).
static void coop_autotest_clear_critters()
{
    std::vector<Object*> critters;
    for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && coop_player_of(obj) == nullptr) {
            critters.push_back(obj);
        }
    }

    for (Object* obj : critters) {
        objectDestroy(obj, nullptr);
    }
    tileWindowRefresh();

    autotest_log("removed %d critters from %s", (int)critters.size(), gMapHeader.name);
}

// Mouse positions in a 640x480 layout centered on the screen.
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
    // (Before any frames came, e.g. in the menus, this screen.)
    if (frames && (!coop_net_client_host_size(&hostWidth, &hostHeight) || hostWidth <= 0 || hostHeight <= 0)) {
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

static void coop_autotest_screenshot()
{
    takeScreenshot();
}

// The F9 key: player 2 joins through the character creation screen (driven
// by injected input), then control switches back and forth.
static int coop_autotest_join(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    Object* p1obj = gDude;

    // Cancelling character creation leaves the game as it was.
    inject_wait(10);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_ESCAPE);
    coop_handle_switch_key();
    while (inject_pending()) {
        inject_update();
    }
    if (coop_player_count() != 1 || gDude != p1obj) {
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
    inject_call(coop_autotest_screenshot);
    inject_wait(3);
    inject_key(KEY_RETURN);
    inject_wait(30);
    inject_call(coop_autotest_screenshot);
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
        p2->pcStats[PC_STAT_LEVEL], critterGetStat(p2->obj, STAT_STRENGTH), critterGetHitPoints(p2->obj), critterGetStat(p2->obj, STAT_MAXIMUM_HIT_POINTS));

    if (critterGetHitPoints(p2->obj) != critterGetStat(p2->obj, STAT_MAXIMUM_HIT_POINTS)) {
        autotest_fail("player 2 does not start at full health");
    }

    tileWindowRefresh();
    takeScreenshot();

    coop_handle_switch_key();
    if (gDude != p2->obj) autotest_fail("F9 did not switch to player 2");

    coop_handle_switch_key();
    if (gDude != p1obj) autotest_fail("F9 did not switch back to player 1");

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
    while (elapsed_time(start) < COOP_AUTOTEST_JOIN_WAIT && !coop_net_client_connected()) {
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

// A free tile some 24 hexes left or right of player 1 where a camera may
// center (as single player's would with the player standing there), or -1.
static int coop_autotest_far_tile(Object* player)
{
    int start = obj_dude->tile;
    int savedCenter = tile_center_tile;
    bool limiting = tileScrollLimitingIsEnabled();
    const int offsets[] = { 24, -24, 26, -26, 22, -22 };
    int found = -1;
    for (int offset : offsets) {
        int tile = start + offset;
        if (tile <= 0 || tile >= 40000 || tile / 200 != start / 200) {
            continue;
        }
        if (_obj_blocking_at(nullptr, tile, player->elevation) != nullptr) {
            continue;
        }
        // With the player there, the scroll limit (counted from them) lets
        // the camera center; the map's edges and scroll blockers decide.
        tileScrollLimitingDisable();
        bool centers = tile_set_center(tile, 0) == 0;
        if (limiting) {
            tileScrollLimitingEnable();
        }
        tile_set_center(savedCenter, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
        if (centers) {
            found = tile;
            break;
        }
    }
    return found;
}

// The tour's camera check: each other player put some 24 hexes from player
// 1 in turn; a little later their view must be centered on them. Then back
// next to player 1. Not in a fight (whose turns show one view).
static void coop_autotest_check_far_cameras(const char* mapName)
{
    for (int index = 1; index < coop_player_count() && coop_net_clients_connected() > 0; index++) {
        if (isInCombat()) {
            return;
        }

        Object* player = coop_player(index)->obj;
        if (player == nullptr || player->elevation != obj_dude->elevation || critter_is_dead(player) || (player->flags & OBJECT_HIDDEN) != 0) {
            continue;
        }

        int distant = coop_autotest_far_tile(player);
        if (distant == -1) {
            autotest_log("%s: no tile far from player 1 for player %d's camera", mapName, index + 1);
            continue;
        }

        reg_anim_clear(player);
        Rect rect;
        objectSetLocation(player, distant, player->elevation, &rect);
        tile_refresh_rect(&rect, player->elevation);
        // Two parts of theirs at least (over a slow line a part waits for
        // their client).
        unsigned int parts = coop_host_view_parts(index);
        unsigned int start = getTicks();
        while (coop_host_view_parts(index) < parts + 2 && getTicksSince(start) < 15000 && coop_net_clients_connected() > 0) {
            coop_autotest_heal_players();
            coop_autotest_host_frame();
        }

        // (Unless they walked on meanwhile, or a fight began.)
        int center = coop_host_view_center(index);
        if (coop_host_view_parts(index) < parts + 2) {
            autotest_log("%s: player %d had no part for 15 s, camera not checked", mapName, index + 1);
        } else if (!isInCombat() && player->tile == distant && center != distant) {
            autotest_fail("%s: player %d's camera stayed at tile %d, %d hexes from them", mapName, index + 1, center, tile_dist(center, distant));
        } else {
            autotest_log("%s: player %d's camera follows them %d hexes from player 1", mapName, index + 1, tile_dist(obj_dude->tile, distant));
        }

        // Placing doesn't redraw (the host's views keep what they showed
        // where nothing redrew).
        obj_attempt_placement(player, obj_dude->tile, obj_dude->elevation, 2);
        tile_refresh_display();
    }
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
    // The camera check (below).
    int lastTile = -1;
    int settledFrames = 0;
    unsigned int settledSince = 0;
    int farTile = -1;
    int farFrames = 0;
    unsigned int farParts = 0;
    bool cameraChecked = false;
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
                autotest_log("player 2 '%s' joined at tile %d", coop_player(1)->name, startTile);
            } else if (p2->tile != startTile && !moved) {
                moved = true;
                autotest_log("player 2 moving (tile %d)", p2->tile);
            }

            // Player 2's camera follows them far from player 1 too (scroll
            // limits count from the player the view is for). Once player 2
            // stands still after walking, they are put some 24 hexes from
            // player 1, and their view is checked a little later.
            if (moved && farTile == -1 && !cameraChecked) {
                settledFrames = p2->tile == lastTile ? settledFrames + 1 : 0;
                if (settledFrames == 0) {
                    settledSince = getTicks();
                }
                lastTile = p2->tile;
                // 30 frames, or a second on a slow host (a big screen under
                // the address sanitizer: a few frames a second).
                if (settledFrames >= 30 || (settledFrames >= 3 && getTicksSince(settledSince) >= 1000)) {
                    farTile = coop_autotest_far_tile(p2);
                    if (farTile == -1) {
                        autotest_log("camera check: no free tile far from player 1 here");
                        cameraChecked = true;
                    } else {
                        reg_anim_clear(p2);
                        Rect rect;
                        objectSetLocation(p2, farTile, p2->elevation, &rect);
                        tile_refresh_rect(&rect, p2->elevation);
                        autotest_log("camera check: player 2 put at tile %d, %d hexes from player 1", farTile, tile_dist(obj_dude->tile, farTile));
                        farParts = coop_host_view_parts(1);
                    }
                }
            } else if (farTile != -1 && !cameraChecked && ++farFrames >= 3 && coop_host_view_parts(1) >= farParts + 2) {
                cameraChecked = true;
                int center = coop_host_view_center(1);
                if (center != farTile) {
                    autotest_fail("player 2's camera stayed at tile %d, %d hexes from player 2", center, tile_dist(center, farTile));
                } else {
                    autotest_log("camera check: player 2's view is centered on them");
                }
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
    } else if (!cameraChecked) {
        autotest_fail("player 2's camera wasn't checked");
    } else {
        autotest_log("player 2 ended at tile %d (distance %d from start)", coop_player(1)->obj->tile,
            tile_dist(startTile, coop_player(1)->obj->tile));
    }

    // Their mouse rested on the map (while they chatted): their 3D cursor
    // showed what's under it, as player 1's does.
    autotest_log("player 2's cursor showed what's under it in %u of %u parts", coop_host_view_hover_parts(1), coop_host_view_parts(1));
    if (startTile != -1 && coop_host_view_hover_parts(1) == 0) {
        autotest_fail("player 2's cursor never showed what's under it");
    }

    dump_screen();
    coop_host_exit();
    return 0;
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
    // Strength +5, Done, then the look list: leather jacket. (Given time
    // to come up: a big screen of player 1's takes a while.)
    inject_wait(480);
    inject_call(coop_autotest_screenshot);
    // (Slowly: over the network a click can arrive while the editor is
    // still redrawing the last one.)
    coop_autotest_click_centered(355, 31);
    inject_wait(45);
    coop_autotest_click_centered(355, 42);
    inject_wait(45);
    coop_autotest_click_centered(355, 53);
    inject_wait(45);
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

    // Player 2's inventory: open, look, close. Then a while before leaving:
    // the host checks player 2's camera once they stand still (a slow host
    // was still at it).
    inject_key(KEY_LOWERCASE_I);
    inject_wait(120);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_ESCAPE);
    inject_wait(360);
    inject_call(coop_net_client_request_exit);

    coop_net_set_client_exit_hook(coop_autotest_net_client_exit);
    int rc = coop_net_client_run();
    autotest_log("client finished with %d", rc);
    if (rc != 0) {
        autotest_fail("client failed");
    }

    return 0;
}

// The main menu, as it looks (screenshot).
static int coop_autotest_main_menu(AutotestNewGameProc* newGame)
{
    colorPaletteLoad("color.pal");
    paletteFadeTo(_cmap);

    if (mainMenuWindowInit() != 0) {
        autotest_fail("main menu failed");
        return -1;
    }

    mainMenuWindowUnhide(false);
    for (int frame = 0; frame < 30; frame++) {
        inputGetInput();
        renderPresent();
        SDL_Delay(10);
    }
    takeScreenshot();
    mainMenuWindowFree();
    return 0;
}

// Clicks the button of plate `slot` in the main menu's panel.
static void coop_autotest_click_menu_slot(int slot)
{
    // The main menu is centered on bigger screens.
    coop_autotest_click_centered(43, slot * 41 + 32);
    inject_wait(15);
}

// The multiplayer menu, driven like a player would: main menu MULTIPLAYER,
// JOIN GAME (type an address, give up), then HOST GAME and NEW GAME.
static int coop_autotest_menu_flow(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    mainMenuWindowInit();
    mainMenuWindowUnhide(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    int rc = mainMenuWindowHandleEvents();
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

    _mouse_set_position(100, 100);
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
static Object* coop_autotest_give_from = nullptr;
static Object* coop_autotest_give_to = nullptr;

// While the give screen is open: moves a stimpak across, as dragging it
// would, and checks the screen belongs to the giver.
static void coop_autotest_give_move()
{
    if (gDude != coop_autotest_give_from) {
        autotest_fail("give screen open for the wrong player");
    }
    Object* stimpak = nullptr;
    for (int index = 0; index < coop_autotest_give_from->data.inventory.length; index++) {
        Object* item = coop_autotest_give_from->data.inventory.items[index].item;
        if (item->pid == PROTO_ID_STIMPACK) {
            stimpak = item;
        }
    }
    if (stimpak == nullptr || itemMove(coop_autotest_give_from, coop_autotest_give_to, stimpak, 1) != 0) {
        autotest_fail("could not move the stimpak");
    }
    takeScreenshot();
}

// Talking to the other player opens the loot screen between the two to
// give each other items, both ways; it is not stealing (no experience,
// nobody gets caught).
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    Object* p1obj = coop_player(0)->obj;
    Object* p2obj = p2->obj;

    for (int round = 0; round < 2; round++) {
        Object* from = round == 0 ? p1obj : p2obj;
        Object* to = round == 0 ? p2obj : p1obj;
        Object* stimpak;
        if (objectCreateWithPid(&stimpak, PROTO_ID_STIMPACK) != 0 || itemAdd(from, stimpak, 1) != 0) {
            autotest_fail("no stimpak");
            return -1;
        }
        _obj_disconnect(stimpak, nullptr);
        int before = coop_autotest_stimpaks(to);
        int xp = pcGetStat(PC_STAT_EXPERIENCE);

        coop_autotest_give_from = from;
        coop_autotest_give_to = to;
        inject_wait(20);
        inject_call(coop_autotest_give_move);
        inject_wait(3);
        inject_key(KEY_ESCAPE);

        scriptsRequestGiving(from, to);
        scriptsHandleRequests();

        while (inject_pending()) {
            inject_update();
        }

        autotest_log("give %d: %s gave a stimpak to %s (%d -> %d)", round, critterGetName(from), critterGetName(to), before, coop_autotest_stimpaks(to));
        if (coop_autotest_stimpaks(to) != before + 1) {
            autotest_fail("the stimpak did not arrive");
        }
        if (pcGetStat(PC_STAT_EXPERIENCE) != xp) {
            autotest_fail("giving gave experience");
        }
        if (gDude != p1obj || coop_active_player() != coop_player(0)) {
            autotest_fail("giving left the wrong player active");
        }
    }

    return 0;
}

// Seen from inside coop_autotest_talk's conversation.
static Object* coop_autotest_talk_dude = nullptr;
static bool coop_autotest_talk_seen = false;

static void coop_autotest_talk_check()
{
    coop_autotest_talk_seen = _gdialogActive();
    coop_autotest_talk_dude = gDude;
    autotest_log("in the conversation: dialog %d, dude %s, active player %d", _gdialogActive() ? 1 : 0,
        gDude != nullptr ? critterGetName(gDude) : "-", coop_active_player() == coop_player(1) ? 2 : 1);
    takeScreenshot();
}

// A background process (runs in every input loop, dialogs too): answers a
// conversation (first option a few times, then Escape).
static void coop_autotest_answer_dialogs()
{
    static int presses = 0;
    static unsigned int last = 0;
    if (_gdialogActive() && getTicksSince(last) > 1000 && !inject_pending()) {
        inject_key(++presses % 4 == 0 ? KEY_ESCAPE : '1');
        last = getTicks();
    }
}

// Player 2 starts a conversation (COOP_AUTOTEST_TALK, default Klint at the
// temple): it runs as player 2's character, and afterwards player 1 is back
// in charge.
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    const char* name = getenv("COOP_AUTOTEST_TALK") != nullptr ? getenv("COOP_AUTOTEST_TALK") : "Klint";
    Object* npc = nullptr;
    for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critterGetName(obj), name) != nullptr) {
            npc = obj;
            break;
        }
    }
    if (npc == nullptr) {
        autotest_fail("no %s on %s", name, gMapHeader.name);
        return -1;
    }

    // Player 2 next to them.
    _obj_attempt_placement(p2->obj, npc->tile, npc->elevation, 1);
    tileSetCenter(p2->obj->tile, TILE_SET_CENTER_REFRESH_WINDOW);

    inject_wait(30);
    inject_call(coop_autotest_talk_check);
    tickersAdd(coop_autotest_answer_dialogs);

    scriptsRequestDialogBy(p2->obj, npc);
    scriptsHandleRequests();
    tickersRemove(coop_autotest_answer_dialogs);
    while (inject_pending()) {
        inject_update();
    }

    autotest_log("talked to %s", critterGetName(npc));
    if (!coop_autotest_talk_seen) {
        autotest_fail("the conversation did not start");
    } else if (coop_autotest_talk_dude != p2->obj) {
        autotest_fail("the conversation was not player 2's");
    }
    if (gDude != coop_player(0)->obj || coop_active_player() != coop_player(0)) {
        autotest_fail("the conversation left player 2 active");
    }
    if (_gdialogActive()) {
        autotest_fail("the conversation did not end");
    }

    tileWindowRefresh();
    takeScreenshot();
    return 0;
}

// One hit of `damage` from `attacker` on `defender`, through the game's own
// damage code.
static void coop_autotest_hit(Object* attacker, Object* defender, int damage)
{
    Attack attack;
    attackInit(&attack, attacker, defender, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
    attack.attackerFlags = DAM_HIT;
    attack.defenderDamage = damage;
    attackComputeDeathFlags(&attack);
    _apply_damage(&attack, false);
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

    // The main loop must not end the game while the other player is up (a
    // fight can end with player 2 still out).
    if (coop_game_lost()) {
        autotest_fail("a knocked out player with the other one up counts as game over");
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
    if (!coop_game_lost()) {
        autotest_fail("game over not detected with nobody left up");
    }

    return 0;
}

static void coop_autotest_session_shots()
{
    inject_wait(90);
    inject_call(coop_autotest_screenshot);
    inject_call(coop_autotest_session_shots);
}

static int coop_autotest_level(PlayerState* player)
{
    ActivePlayerScope scope(player);
    return pcGetStat(PC_STAT_LEVEL);
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
        // An enemy that keeps away (as Fallout: Sonora's rat) would make
        // it last forever: player 2 never fights here.
        Object* enemy = coop_autotest_enemy;
        if (coop_autotest_turns_total > 40 && enemy != nullptr && !critterIsDead(enemy)) {
            autotest_log("the enemy kept away: it dies");
            critterKill(enemy, -1, true);
        }
        return coop_autotest_attack_turn(player);
    }

    coop_autotest_session_p2_turns++;
    coop_autotest_session_turn_start = get_time();
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
    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }
    autotest_log("client joined");
    unsigned int joinsBefore = coop_net_client_joins();

    // COOP_AUTOTEST_XP: both players level up while player 2 is connected
    // (they then open their character screen).
    if (const char* xp = getenv("COOP_AUTOTEST_XP")) {
        pcAddExperience(atoi(xp));
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
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
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

    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
        autotest_fail("client did not come back");
        return -1;
    }
    coop_autotest_session_frames(2000, false, false);
    if ((coop_player(1)->obj->flags & OBJECT_HIDDEN) != 0 || objectGetDistanceBetween(coop_player(1)->obj, coop_player(0)->obj) > 5) {
        autotest_fail("player 2 did not come back next to player 1");
    }
    autotest_log("client back: %d players, player 2 '%s' level %d", coop_player_count(),
        critter_name(coop_player(1)->obj), coop_autotest_level(coop_player(1)));
    if (coop_player_count() != 2 || p2Name != critter_name(coop_player(1)->obj) || coop_autotest_level(coop_player(1)) != p2Level) {
        autotest_fail("player 2 changed after rejoining");
    }

    // Load with the client connected.
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("load with player 2 connected failed");
        return -1;
    }
    coop_autotest_session_frames(3000, false, false);
    autotest_log("after load: %d players, connected %d, player 2 tile %d (saved %d)", coop_player_count(),
        coop_net_client_connected() ? 1 : 0, coop_player(1)->obj->tile, savedTile);
    if (coop_player_count() != 2 || !coop_net_client_connected() || p2Name != critter_name(coop_player(1)->obj)) {
        autotest_fail("load broke the session");
    }

    // (Fallout 2: player 2 trades through conversations; no direct barter
    // screen to test here.)
    coop_autotest_session_frames(1500, false, false);

    // A fight where player 2 never acts: the turn timer has to move on.
    // (Only the two of them and the enemy: the map's own people, as on
    // Fallout: Sonora's first map, could keep it going.)
    coop_autotest_clear_critters();
    Object* enemy = coop_autotest_spawn_enemy(coop_player(0)->obj);
    if (enemy == NULL) {
        return -1;
    }
    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_set_turn_hook(coop_autotest_session_turn);

    CombatStartData attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = coop_player(0)->obj;
    attack.defender = enemy;
    unsigned int fightStart = get_time();
    _combat(&attack);
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
    _mouse_set_position(0, screenGetHeight() / 3);
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
    _mouse_set_position(screenGetWidth() / 2, screenGetHeight() / 3);
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

// The tour's players are hard to kill (so it gets everywhere) and never
// sick: healed every frame.
static void coop_autotest_heal_players()
{
    static bool tough = false;
    if (!tough) {
        tough = true;
        for (int index = 0; index < coop_player_count(); index++) {
            critterSetBonusStat(coop_player(index)->obj, STAT_MAXIMUM_HIT_POINTS, 800); // (the HUD shows 3 digits)
        }
    }

    for (int index = 0; index < coop_player_count(); index++) {
        Object* obj = coop_player(index)->obj;
        if (obj != nullptr && !critterIsDead(obj)) {
            int missing = critterGetStat(obj, STAT_MAXIMUM_HIT_POINTS) - critterGetHitPoints(obj);
            if (missing > 0) {
                critterAdjustHitPoints(obj, missing);
            }
            int rads = critterGetRadiation(obj);
            if (rads > 0) {
                ActivePlayerScope scope(coop_player(index));
                critterAdjustRadiation(obj, -rads);
                queueRemoveEventsByType(obj, EVENT_TYPE_RADIATION);
            }
        }
    }
}

// The tour's fights: player 1 clears the hostile critters around and ends
// the fight; player 2's turns are theirs (or the turn timer's).
static bool coop_autotest_rough_turn(Object* player)
{
    coop_autotest_heal_players();
    if (player != coop_player(0)->obj) {
        return false;
    }

    // Collect first: killing moves objects around in the object list.
    // Near ones first; if the fight goes on (enemies shooting from afar),
    // any distance.
    static unsigned int roughTurns = 0;
    static unsigned int lastRough = 0;
    if (getTicksSince(lastRough) > 120000) {
        roughTurns = 0;
    }
    lastRough = getTicks();
    int reach = ++roughTurns > 3 ? 500 : 20;
    int team = player->data.critter.combat.team;
    std::vector<Object*> targets;
    for (Object* obj = objectFindFirstAtElevation(player->elevation); obj != nullptr; obj = objectFindNextAtElevation()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && coop_player_of(obj) == nullptr && !critterIsDead(obj)
            && obj->data.critter.combat.team != team && objectGetDistanceBetween(obj, player) <= reach) {
            targets.push_back(obj);
        }
    }
    for (Object* obj : targets) {
        critterKill(obj, -1, true);
    }
    if (!targets.empty()) {
        autotest_log("%s: cleared %d hostile critters", gMapHeader.name, (int)targets.size());
    } else if (roughTurns == 5) {
        for (Object* obj = objectFindFirstAtElevation(player->elevation); obj != nullptr; obj = objectFindNextAtElevation()) {
            if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && !critterIsDead(obj)) {
                autotest_log("%s: %s team %d (players %d) distance %d", gMapHeader.name, critterGetName(obj),
                    obj->data.critter.combat.team, team, objectGetDistanceBetween(obj, player));
            }
        }
    }
    combatAttemptEnd();
    return true;
}

// Network tour: both players travel through every map (COOP_AUTOTEST_TOUR=
// ALL; or A.MAP,B.MAP), every elevation, a few seconds each with player 2
// walking around; fights are cleared, conversations answered. Paired with
// net_tour_client.
// COOP_AUTOTEST_VIDEO=1: screenshots become video frames,
// vid-<wall clock ms>.raw (width, height, palette, pixels), so the frames
// of the host and the clients can be matched up afterwards
// (tools/make_video.py, make_video4.py in Fallout 1 co-op).
static int coop_autotest_video_dump(int width, int height, unsigned char* buffer, unsigned char* palette)
{
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                       .count();
    char name[64];
    snprintf(name, sizeof(name), "vid-%lld.raw", ms);
    FILE* stream = fopen(name, "wb");
    if (stream == nullptr) {
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
    return getenv("COOP_AUTOTEST_VIDEO") != nullptr;
}

// Player 1 strolls to a free tile a few hexes away.
static void coop_autotest_stroll(Object* critter)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        int tile = tileGetTileInDirection(critter->tile, randomBetween(0, 5), randomBetween(3, 7));
        if (tile != critter->tile && _obj_blocking_at(nullptr, tile, critter->elevation) == nullptr
            && pathfinderFindPath(critter, critter->tile, tile, nullptr, 0, _obj_blocking_at) != 0) {
            reg_anim_begin(ANIMATION_REQUEST_RESERVED);
            animationRegisterMoveToTile(critter, tile, critter->elevation, -1, 0);
            reg_anim_end();
            return;
        }
    }
}

// Resident memory in MB (Linux; -1 elsewhere): a long tour shows whether
// the host keeps growing.
static int coop_autotest_memory_mb()
{
#ifdef __linux__
    FILE* stream = fopen("/proc/self/statm", "r");
    if (stream != NULL) {
        long pages = 0;
        long resident = 0;
        int read = fscanf(stream, "%ld %ld", &pages, &resident);
        fclose(stream);
        if (read == 2) {
            return (int)(resident * sysconf(_SC_PAGESIZE) / (1024 * 1024));
        }
    }
#endif
    return -1;
}

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
    if (coop_autotest_video()) {
        screenshotHandlerConfigure(KEY_F12, coop_autotest_video_dump);
    }

    PlayerState* p1 = coop_player(0);
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // COOP_AUTOTEST_PLAYERS=n: players 3 and up too (one client each, see
    // nettest.sh EXTRA_CLIENTS).
    int players = getenv("COOP_AUTOTEST_PLAYERS") != nullptr ? atoi(getenv("COOP_AUTOTEST_PLAYERS")) : 2;
    for (int index = 2; index < players && index < COOP_MAX_PLAYERS; index++) {
        if (coop_add_player(index == 2 ? "premade\\diplomat.gcd" : "premade\\combat.gcd") == nullptr) {
            autotest_fail("could not add player %d", index + 1);
            return -1;
        }
    }

    coop_host_init();
    if (!coop_host_active() || !coop_autotest_relay_host_setup()) {
        autotest_fail("cannot host");
        return -1;
    }

    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }

    unsigned int joinStart = getTicks();
    while (getTicksSince(joinStart) < 90000 && coop_net_clients_connected() < players - 1) {
        coop_autotest_session_frames(250, false, false);
    }
    if (coop_net_clients_connected() < players - 1) {
        autotest_fail("only %d clients joined", coop_net_clients_connected());
        return -1;
    }

    std::vector<std::string> tour;
    const char* tourList = getenv("COOP_AUTOTEST_TOUR");
    if (tourList == nullptr || strcmp(tourList, "ALL") == 0) {
        // (Map 0 in a transition means the world map.)
        for (int index = 1; index < 1000; index++) {
            char name[64];
            if (wmMapIdxToName(index, name, sizeof(name)) != 0) {
                break;
            }
            tour.push_back(name);
        }
    } else {
        std::string rest = tourList;
        size_t comma;
        while ((comma = rest.find(',')) != std::string::npos) {
            tour.push_back(rest.substr(0, comma));
            rest = rest.substr(comma + 1);
        }
        tour.push_back(rest);
    }

    // COOP_AUTOTEST_SEED=n: the maps in a shuffled order.
    if (const char* seed = getenv("COOP_AUTOTEST_SEED")) {
        std::mt19937 random((unsigned int)strtoul(seed, nullptr, 10));
        std::shuffle(tour.begin(), tour.end(), random);
        autotest_log("tour order from seed %s, first %s", seed, tour.empty() ? "-" : tour.front().c_str());
    }

    coop_set_turn_hook(coop_autotest_rough_turn);
    tickersAdd(coop_autotest_answer_dialogs);

    int visited = 0;
    int memoryFirst = -1;
    int memoryLast = -1;
    int memoryMost = -1;
    for (const std::string& stopName : tour) {
        char stop[64];
        snprintf(stop, sizeof(stop), "%s", stopName.c_str());
        int index = wmMapMatchNameToIdx(stop);
        if (index == -1) {
            autotest_log("no map %s", stop);
            continue;
        }

        if (coop_autotest_video()) {
            // Daylight for the video: noon (game time is in 1/10 s).
            gameTimeSetTime(gameTimeGetTime() / 864000 * 864000 + 12 * 36000);
        }

        MapTransition transition;
        memset(&transition, 0, sizeof(transition));
        transition.map = index;
        transition.tile = -1;
        mapSetTransition(&transition);

        unsigned int start = getTicks();
        mapHandleTransition();
        autotest_log("%s loaded in %u ms", stop, getTicksSince(start));

        // COOP_AUTOTEST_STAY_MS: longer on each map (videos).
        unsigned int stay = getenv("COOP_AUTOTEST_STAY_MS") != nullptr ? (unsigned int)atoi(getenv("COOP_AUTOTEST_STAY_MS")) : 6000u;
        start = getTicks();
        unsigned int lastFrame = 0;
        unsigned int lastStroll = 0;
        while (getTicksSince(start) < stay && coop_net_client_connected()) {
            if (!coop_autotest_video()) {
                coop_autotest_heal_players();
            }
            coop_autotest_host_frame();
            if (coop_autotest_video()) {
                if (getTicksSince(lastFrame) >= 100) {
                    lastFrame = getTicks();
                    takeScreenshot();
                }
                if (getTicksSince(lastStroll) >= 2500) {
                    lastStroll = getTicks();
                    coop_autotest_stroll(gDude);
                }
            }
        }

        // Each other player's camera follows them far from player 1 (some 24
        // hexes away, where a camera may center), then back next to player 1.
        if (!coop_autotest_video()) {
            coop_autotest_check_far_cameras(gMapHeader.name);
        }

        // The other elevations too (not for videos).
        for (int elevation = 0; elevation < ELEVATION_COUNT && coop_net_client_connected() && !coop_autotest_video(); elevation++) {
            if (gDude->elevation == elevation || objectFindFirstAtElevation(elevation) == nullptr) {
                continue;
            }
            objectSetLocation(gDude, gDude->tile, elevation, nullptr);
            mapSetElevation(elevation);
            // As the game does after a floor change: the party (the other
            // players) was placed again, which doesn't redraw.
            tile_refresh_display();
            unsigned int elevationStart = getTicks();
            while (getTicksSince(elevationStart) < 4000 && coop_net_client_connected()) {
                coop_autotest_heal_players();
                coop_autotest_host_frame();
            }
            autotest_log("%s elevation %d: p2 elevation %d", gMapHeader.name, elevation, coop_player(1)->obj->elevation);
        }

        if (!coop_net_client_connected()) {
            autotest_fail("client lost at %s", stop);
            break;
        }

        // (Fetched only now: a map change can replace the objects.)
        Object* p1obj = p1->obj;
        Object* p2obj = coop_player(1)->obj;
        int memory = coop_autotest_memory_mb();
        memoryFirst = visited == 0 ? memory : memoryFirst;
        memoryMost = std::max(memoryMost, memory);
        memoryLast = memory;
        autotest_log("%s: p1 tile %d elev %d hp %d, p2 tile %d elev %d hp %d, distance %d, memory %d MB",
            gMapHeader.name, p1obj->tile, p1obj->elevation, critterGetHitPoints(p1obj),
            p2obj->tile, p2obj->elevation, critterGetHitPoints(p2obj), objectGetDistanceBetween(p1obj, p2obj), memory);

        bool together = coop_player_count() == players;
        for (int index = 1; index < coop_player_count(); index++) {
            Object* other = coop_player(index)->obj;
            if (other->elevation != p1obj->elevation || other->tile == -1) {
                autotest_log("%s: player %d at tile %d elev %d", gMapHeader.name, index + 1, other->tile, other->elevation);
                together = false;
            }
        }
        if (!together) {
            autotest_fail("%s: players separated", stop);
        }
        if (coop_any_player_has(DAM_DEAD)) {
            autotest_fail("%s: a player died", stop);
            break;
        }
        visited++;
    }

    tickersRemove(coop_autotest_answer_dialogs);
    coop_set_turn_hook(nullptr);
    autotest_log("visited %d maps, memory %d MB after the first, %d on the last, %d at most", visited, memoryFirst, memoryLast, memoryMost);
    coop_host_exit();
    return 0;
}

static void coop_autotest_tour_walk()
{
    // Walk somewhere around player 2, look, again - forever (the host ends
    // the tour). With COOP_AUTOTEST_SEED: random spots.
    static int step = 0;
    static const int offsets[][2] = { { 90, 40 }, { -80, 50 }, { 60, -60 }, { -70, -30 } };
    int offset[2] = { offsets[step % 4][0], offsets[step % 4][1] };
    step++;
    if (const char* seed = getenv("COOP_AUTOTEST_SEED")) {
        static std::mt19937 random((unsigned int)strtoul(seed, nullptr, 10) + 1);
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
        screenshotHandlerConfigure(KEY_F12, coop_autotest_video_dump);
    }

    inject_wait(300);
    inject_call(coop_autotest_tour_walk);

    int rc = coop_net_client_run();
    autotest_log("client finished with %d, %s", rc, coop_net_last_error());
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
    c.level = pcGetStat(PC_STAT_LEVEL);
    c.hp = critter_get_hits(obj);
    c.maxHp = critterGetStat(obj, STAT_MAXIMUM_HIT_POINTS);
    c.strength = critterGetStat(obj, STAT_STRENGTH);
    c.smallGuns = skillGetValue(obj, SKILL_SMALL_GUNS);
    c.stimpaks = coop_autotest_stimpaks(obj);
    Object* armor = critterGetArmor(obj);
    c.armorPid = armor != nullptr ? armor->pid : -1;
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    Object* stimpak;
    if (objectCreateWithPid(&stimpak, PROTO_ID_STIMPACK) == 0 && itemAdd(p2->obj, stimpak, 3) == 0) {
        // (Created on the map; carried now.)
        _obj_disconnect(stimpak, nullptr);
    }
    {
        ActivePlayerScope scope(p2);
        pcAddExperience(3000);
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
    if (copy != nullptr) {
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
    if (back == nullptr) {
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
    if (back->obj->tile == -1 || back->obj->elevation != p1obj->elevation || objectGetDistanceBetween(back->obj, p1obj) > 5) {
        autotest_fail("player 2 is not next to player 1");
    }
    if (obj_dude != p1obj || critter_get_hits(p1obj) != p1Hp) {
        autotest_fail("player 1 changed");
    }

    // The game still saves and loads with the imported player 2.
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save with the imported player 2 failed");
    }
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1 || coop_player_count() != 2) {
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

    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
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
        level = pcGetStat(PC_STAT_LEVEL);
    }
    autotest_log("player 2: %s, level %d", critterGetName(p2->obj), level);
    const char* expected = getenv("COOP_AUTOTEST_CHARACTER_NAME");
    if (expected != nullptr && strcmp(critterGetName(p2->obj), expected) != 0) {
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
        if (stream != nullptr) {
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
    if (stream != nullptr) {
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

    // COOP_AUTOTEST_HEADS: lists every map's merchants and the talking head
    // their trade screen shows, and stops there.
    if (getenv("COOP_AUTOTEST_HEADS") != nullptr) {
        int merchants = 0;
        int heads = 0;
        for (int index = 0; index < 1000; index++) {
            char name[64];
            if (wmMapIdxToName(index, name, sizeof(name)) != 0) {
                break;
            }
            if (mapLoadById(index) != 0) {
                continue;
            }
            for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
                if (obj == gDude || !coop_can_barter_with(obj) || objectIsPartyMember(obj)) {
                    continue;
                }
                int head = -1;
                int background = -1;
                bool found = scriptsFindDialogHead(obj, &head, &background);
                merchants++;
                heads += found ? 1 : 0;
                autotest_log("%s: %s head %d background %d", name, critterGetName(obj), head, background);
            }
        }
        autotest_log("%d merchants, %d with a talking head", merchants, heads);
        return 0;
    }

    // COOP_AUTOTEST_MAP: the merchant's map (default Maida's in Klamath).
    const char* mapName = getenv("COOP_AUTOTEST_MAP");
    const char* merchantName = getenv("COOP_AUTOTEST_MERCHANT");
    if (mapName == nullptr) {
        mapName = "kladwtwn.map";
        if (merchantName == nullptr) {
            merchantName = "Maida";
        }
    }
    {
        char name[64];
        snprintf(name, sizeof(name), "%s", mapName);
        if (mapLoadByName(name) != 0) {
            autotest_fail("cannot load %s", mapName);
            return -1;
        }
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // A merchant of the map by name (COOP_AUTOTEST_MERCHANT), else the
    // first critter proto that barters (without a script: no trade).
    Object* merchant = nullptr;
    if (merchantName != nullptr) {
        for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
            if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critterGetName(obj), merchantName) != nullptr) {
                merchant = obj;
                break;
            }
        }
        if (merchant == nullptr) {
            autotest_log("no %s on this map", merchantName);
        }
    }

    if (merchant != nullptr) {
        _obj_attempt_placement(p2->obj, merchant->tile, merchant->elevation, 1);
        tileSetCenter(p2->obj->tile, TILE_SET_CENTER_REFRESH_WINDOW);
    } else {
        int merchantPid = -1;
        for (int index = 1; index < 1000 && merchantPid == -1; index++) {
            Proto* proto;
            if (protoGetProto(0x1000000 | index, &proto) != -1 && (proto->critter.data.flags & CRITTER_BARTER) != 0
                && strstr(protoGetName(0x1000000 | index), "Hero") == nullptr) {
                merchantPid = 0x1000000 | index;
            }
        }
        if (merchantPid == -1 || objectCreateWithPid(&merchant, merchantPid) == -1) {
            autotest_fail("could not create a merchant");
            return -1;
        }
        _obj_attempt_placement(merchant, p2->obj->tile, p2->obj->elevation, 1);
    }

    Object* stimpak;
    if (objectCreateWithPid(&stimpak, PROTO_ID_STIMPACK) == 0 && itemAdd(merchant, stimpak, 2) == 0) {
        _obj_disconnect(stimpak, nullptr);
    }

    if (!coop_can_barter_with(merchant)) {
        autotest_fail("merchant does not barter");
        return -1;
    }

    if (coop_can_barter_with(gDude)) {
        autotest_fail("player 1 counts as a merchant");
    }

    int head;
    int background;
    if (scriptsFindDialogHead(merchant, &head, &background)) {
        autotest_log("%s has talking head %d (background %d)", critterGetName(merchant), head, background);
    }

    // Screenshot of the barter screen, then leave it.
    inject_wait(20);
    inject_call(takeScreenshot);
    inject_wait(3);
    inject_key(KEY_ESCAPE);

    int itemsBefore = merchant->data.inventory.length;
    unsigned int start = getTicks();
    coop_request_barter(p2, merchant);
    scriptsHandleRequests();
    coop_process_requests();

    while (inject_pending()) {
        inject_update();
    }

    if (getTicksSince(start) < 200) {
        autotest_fail("the barter screen did not open");
    }
    autotest_log("%s had %d kinds of items before and %d after", critterGetName(merchant), itemsBefore, merchant->data.inventory.length);

    if (gDude != coop_player(0)->obj || coop_active_player() != coop_player(0)) {
        autotest_fail("barter left player 2 active");
    }

    autotest_log("barter with %s done", critterGetName(merchant));

    tileWindowRefresh();
    takeScreenshot();

    return 0;
}

// Each player looks as their gender (and the story) has it: tribal, then
// the vault suit after its movie, also after a map change and save/load.
static int coop_autotest_looks(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    // Chitsa, a woman.
    PlayerState* p2 = coop_add_player("premade\\diplomat.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    auto check = [&](const char* when, int look) {
        int p1Art = gDude->fid & 0xFFF;
        int p2Art = p2->obj->fid & 0xFFF;
        int p1Gender = critterGetStat(gDude, STAT_GENDER);
        int p2Gender = critterGetStat(p2->obj, STAT_GENDER);
        autotest_log("%s: player 1 art %d (gender %d), player 2 art %d (gender %d)", when, p1Art, p1Gender, p2Art, p2Gender);
        if (p1Art != _art_vault_person_nums[look][p1Gender] || p2Art != _art_vault_person_nums[look][p2Gender]) {
            autotest_fail("%s: a player looks wrong", when);
        }
    };

    if (critterGetStat(p2->obj, STAT_GENDER) != GENDER_FEMALE) {
        autotest_fail("player 2 is not a woman");
    }
    check("start", DUDE_NATIVE_LOOK_TRIBAL);

    gameMovieSetSeen(MOVIE_VSUIT);
    char mapName[] = "arvillag.map";
    if (mapLoadByName(mapName) != 0) {
        autotest_fail("cannot load %s", mapName);
        return -1;
    }
    p2 = coop_player(1);
    check("after the vault suit movie and a map change", DUDE_NATIVE_LOOK_JUMPSUIT);

    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1 || lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1 || coop_player_count() != 2) {
        autotest_fail("save and load failed");
        return -1;
    }
    p2 = coop_player(1);
    check("after save and load", DUDE_NATIVE_LOOK_JUMPSUIT);

    return 0;
}

// While net_talk_host's conversation runs: whose it is.
static void coop_autotest_net_talk_watch()
{
    if (_gdialogActive() && !coop_autotest_talk_seen) {
        coop_autotest_talk_seen = true;
        coop_autotest_talk_dude = gDude;
        autotest_log("conversation on: dude %s", critterGetName(gDude));
    }
}

// Over the network, player 2 talks to Klint at the temple, answering with
// their own keys (net_talk_client); the host never answers for them. With
// COOP_AUTOTEST_NET_BARTER (and [coop]talk=0) player 2 trades with Maida
// in Klamath instead and leaves the barter screen with Escape.
static int coop_autotest_net_talk_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    bool barter = getenv("COOP_AUTOTEST_NET_BARTER") != nullptr;

    // Player 2's character is ready (no character creation over the
    // network).
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_host_active() || !coop_autotest_relay_host_setup()) {
        autotest_fail("cannot host");
        return -1;
    }

    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }

    if (barter) {
        // Both players to Klamath.
        char mapName[] = "kladwtwn.map";
        MapTransition transition;
        memset(&transition, 0, sizeof(transition));
        transition.map = wmMapMatchNameToIdx(mapName);
        transition.tile = -1;
        mapSetTransition(&transition);
        mapHandleTransition();
        if (coop_player_count() != 2 || p2->obj->elevation != gDude->elevation) {
            autotest_fail("player 2 did not come along to %s", mapName);
        }
    }

    const char* name = barter ? "Maida" : "Klint";
    Object* npc = nullptr;
    for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && strstr(critterGetName(obj), name) != nullptr) {
            npc = obj;
            break;
        }
    }
    if (npc == nullptr) {
        autotest_fail("no %s", name);
        return -1;
    }
    _obj_attempt_placement(p2->obj, npc->tile, npc->elevation, 1);
    coop_autotest_session_frames(2000, false, false);

    coop_autotest_talk_seen = false;
    tickersAdd(coop_autotest_net_talk_watch);
    unsigned int start = getTicks();
    if (barter) {
        if (!coop_can_barter_with(npc)) {
            autotest_fail("%s does not trade", name);
        }
        coop_request_barter(p2, npc);
    } else {
        scriptsRequestDialogBy(p2->obj, npc);
    }
    scriptsHandleRequests();
    coop_process_requests();
    tickersRemove(coop_autotest_net_talk_watch);
    autotest_log("player 2's %s took %u ms", barter ? "trade" : "conversation", getTicksSince(start));

    if (!coop_autotest_talk_seen) {
        autotest_fail("the conversation did not start");
    } else if (coop_autotest_talk_dude != p2->obj) {
        autotest_fail("the conversation was not player 2's");
    }
    if (gDude != coop_player(0)->obj || coop_active_player() != coop_player(0)) {
        autotest_fail("the conversation left player 2 active");
    }
    if (_gdialogActive()) {
        autotest_fail("the conversation did not end");
    }

    coop_autotest_session_frames(1000, false, false);
    takeScreenshot();
    coop_host_exit();
    return 0;
}

static void coop_autotest_net_talk_keys()
{
    // The first option a few times, then Escape (which also leaves a
    // barter screen).
    static int presses = 0;
    inject_wait(60);
    inject_key(++presses % 4 == 0 ? KEY_ESCAPE : KEY_1);
    if (presses % 4 == 1) {
        inject_call(coop_autotest_screenshot);
    }
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

static int coop_autotest_net_combat_player2_turns = 0;

static bool coop_autotest_net_combat_turn(Object* player)
{
    if (player == coop_player(0)->obj) {
        // COOP_AUTOTEST_COMBAT_CHAT: player 1 chats in their first turn
        // (the space must not end it), then ends it with Space.
        static bool chatted = false;
        if (getenv("COOP_AUTOTEST_COMBAT_CHAT") != nullptr && !chatted) {
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

// Over the network: a fight in which player 2's turns come from the client
// (which ends them with Space); player 1 punches the enemy.
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_host_active() || !coop_autotest_relay_host_setup()) {
        autotest_fail("cannot host");
        return -1;
    }

    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }
    coop_autotest_session_frames(500, false, false);

    if (coop_player_count() != 2 || coop_player(1) != p2) {
        autotest_fail("player 2 was not kept for the joining client");
    }

    coop_autotest_clear_critters();

    Object* enemy = coop_autotest_spawn_enemy(coop_player(0)->obj);
    if (enemy == nullptr) {
        return -1;
    }

    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_set_turn_hook(coop_autotest_net_combat_turn);

    CombatStartData attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = coop_player(0)->obj;
    attack.defender = enemy;
    _combat(&attack);

    coop_set_turn_hook(nullptr);

    autotest_log("combat over: enemy dead %d, player 2 turns %d, players dead %d",
        critterIsDead(enemy) ? 1 : 0, coop_autotest_net_combat_player2_turns, coop_any_player_has(DAM_DEAD) ? 1 : 0);

    if (coop_autotest_net_combat_player2_turns == 0) {
        autotest_fail("player 2 never had a turn");
    }

    if (!critterIsDead(enemy) && !coop_any_player_has(DAM_DEAD)) {
        autotest_fail("combat ended without a result");
    }

    coop_host_exit();
    return 0;
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
    if (getenv("COOP_AUTOTEST_COMBAT_CHAT") != nullptr) {
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
    inject_call(coop_autotest_press_space_forever);

    int rc = coop_net_client_run();
    autotest_log("client finished with %d", rc);
    if (rc != 0) {
        autotest_fail("client failed");
    }

    return 0;
}

// Roofs open over the player whose view is drawn: player 2 (in control on
// one PC) inside a building in Klamath sees in, player 1 outside sees the
// roof (COOP_AUTOTEST_MAP, default kladwtwn.map).
static int coop_autotest_roofs(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    char mapName[64];
    snprintf(mapName, sizeof(mapName), "%s", getenv("COOP_AUTOTEST_MAP") != nullptr ? getenv("COOP_AUTOTEST_MAP") : "kladwtwn.map");
    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = wmMapMatchNameToIdx(mapName);
    transition.tile = -1;
    mapSetTransition(&transition);
    mapHandleTransition();
    p2 = coop_player(1);
    Object* p1obj = coop_player(0)->obj;

    auto roofAt = [](int tile, int elevation, bool* hidden) {
        int square = _square[elevation]->field_0[tile % 200 / 2 + 100 * (tile / 200 / 2)];
        int roof = (square >> 16) & 0xFFFF;
        *hidden = (((roof & 0xF000) >> 12) & 0x01) != 0;
        return buildFid(OBJ_TYPE_TILE, roof & 0xFFF, 0, 0, 0) != buildFid(OBJ_TYPE_TILE, 1, 0, 0, 0);
    };

    bool hidden;
    if (roofAt(p1obj->tile, p1obj->elevation, &hidden)) {
        autotest_fail("player 1 starts under a roof");
        return -1;
    }

    // A free tile under a roof, near player 1.
    int inside = -1;
    for (int tile = 0; tile < HEX_GRID_SIZE && inside == -1; tile++) {
        if (tileDistanceBetween(tile, p1obj->tile) < 40 && roofAt(tile, p1obj->elevation, &hidden)
            && _obj_blocking_at(nullptr, tile, p1obj->elevation) == nullptr) {
            inside = tile;
        }
    }
    if (inside == -1) {
        autotest_fail("no roof near player 1");
        return -1;
    }

    // Player 2 in control walks in.
    coop_handle_switch_key();
    if (coop_controlled_player() != p2) {
        autotest_fail("cannot switch to player 2");
        return -1;
    }
    {
        ActivePlayerScope scope(p2);
        objectSetLocation(p2->obj, inside, p1obj->elevation, nullptr);
    }
    tileSetCenter(p2->obj->tile, TILE_SET_CENTER_REFRESH_WINDOW);
    roofAt(inside, p1obj->elevation, &hidden);
    autotest_log("player 2 at %d, under a roof: hidden %d", inside, hidden ? 1 : 0);
    takeScreenshot();
    if (!hidden) {
        autotest_fail("the roof stays over player 2");
    }

    // Back to player 1, outside.
    coop_handle_switch_key();
    tileSetCenter(p1obj->tile, TILE_SET_CENTER_REFRESH_WINDOW);
    roofAt(inside, p1obj->elevation, &hidden);
    autotest_log("player 1 in control: the roof hidden %d", hidden ? 1 : 0);
    if (hidden) {
        autotest_fail("the roof stays open for player 1");
    }

    // And to player 2 again.
    coop_handle_switch_key();
    roofAt(inside, p1obj->elevation, &hidden);
    if (!hidden) {
        autotest_fail("the roof did not open again for player 2");
    }
    coop_handle_switch_key();
    return 0;
}

// Every player's turn counted; once each had one, they punch.
static bool coop_autotest_four_turn(Object* player)
{
    bool all = true;
    for (int index = 0; index < coop_player_count(); index++) {
        if (coop_autotest_turns[index] == 0 && coop_player(index)->obj != player) {
            all = false;
        }
    }
    if (!all) {
        coop_autotest_count_turn(player);
        return true;
    }
    return coop_autotest_attack_turn(player);
}

// Four players on one PC: their own critters, protos and stats, outline
// colors, travel together, a fight with a turn each, save and load, and a
// character copy of player 4 joining as player 2 of another game.
static int coop_autotest_four(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    const char* premades[] = { "premade\\stealth.gcd", "premade\\diplomat.gcd", "premade\\stealth.gcd" };
    for (int index = 0; index < 3; index++) {
        if (coop_add_player(premades[index]) == nullptr) {
            autotest_fail("could not add player %d", index + 2);
            return -1;
        }
    }
    if (coop_add_player("premade\\combat.gcd") != nullptr) {
        autotest_fail("a fifth player was added");
    }

    auto report = [](const char* when) {
        for (int index = 0; index < coop_player_count(); index++) {
            PlayerState* player = coop_player(index);
            ActivePlayerScope scope(player);
            autotest_log("%s: player %d %s pid %x tile %d level %d hp %d ST %d", when, index + 1, critterGetName(player->obj),
                player->obj->pid, player->obj->tile, pcGetStat(PC_STAT_LEVEL), critterGetHitPoints(player->obj),
                critterGetStat(player->obj, STAT_STRENGTH));
        }
    };
    report("start");

    for (int index = 1; index < coop_player_count(); index++) {
        Object* obj = coop_player(index)->obj;
        if (obj->pid != COOP_PLAYER_PID(index) || coop_player_by_pid(obj->pid) != coop_player(index)) {
            autotest_fail("player %d has the wrong proto id %x", index + 1, obj->pid);
        }
        Proto* proto;
        if (protoGetProto(obj->pid, &proto) == -1) {
            autotest_fail("player %d has no proto", index + 1);
        }
        for (int other = 0; other < index; other++) {
            if (coop_player(other)->obj == obj) {
                autotest_fail("players %d and %d share a critter", other + 1, index + 1);
            }
        }
    }

    // Stats stay each player's own.
    {
        ActivePlayerScope scope(coop_player(2));
        pcAddExperience(1000);
    }
    for (int index = 0; index < coop_player_count(); index++) {
        ActivePlayerScope scope(coop_player(index));
        autotest_log("player %d experience %d", index + 1, pcGetStat(PC_STAT_EXPERIENCE));
    }

    // Four different outline colors for each viewer.
    for (int viewer = 0; viewer < coop_player_count(); viewer++) {
        coop_set_viewer(coop_player(viewer));
        int colors[COOP_MAX_PLAYERS];
        for (int index = 0; index < coop_player_count(); index++) {
            colors[index] = coop_viewer_outline_color(index);
            for (int other = 0; other < index; other++) {
                if (colors[other] == colors[index]) {
                    autotest_fail("viewer %d sees players %d and %d in the same color", viewer + 1, other + 1, index + 1);
                }
            }
        }
        if (viewer != 0 && coop_outline_player(coop_player(0)->obj) != 0) {
            autotest_fail("viewer %d sees no outline on player 1", viewer + 1);
        }
        if (coop_outline_player(coop_player(viewer)->obj) != -1) {
            autotest_fail("viewer %d sees their own outline", viewer + 1);
        }
    }
    coop_set_viewer(nullptr);

    // F9 goes round all four.
    for (int step = 1; step <= 4; step++) {
        coop_handle_switch_key();
        if (coop_controlled_player() != coop_player(step % 4)) {
            autotest_fail("F9 step %d controls the wrong player", step);
        }
    }

    // Travel: everyone comes along (to COOP_AUTOTEST_TRAVEL_MAP, default
    // Klamath).
    char mapName[64];
    snprintf(mapName, sizeof(mapName), "%s", getenv("COOP_AUTOTEST_TRAVEL_MAP") != nullptr ? getenv("COOP_AUTOTEST_TRAVEL_MAP") : "kladwtwn.map");
    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = wmMapMatchNameToIdx(mapName);
    transition.tile = -1;
    mapSetTransition(&transition);
    mapHandleTransition();
    report("after travel");
    if (coop_player_count() != 4) {
        autotest_fail("players lost on the way: %d", coop_player_count());
        return -1;
    }
    for (int index = 1; index < 4; index++) {
        Object* obj = coop_player(index)->obj;
        if (obj->tile == -1 || obj->elevation != gDude->elevation || objectGetDistanceBetween(obj, gDude) > 8) {
            autotest_fail("player %d is not with player 1", index + 1);
        }
    }

    // A fight: every player gets a turn.
    coop_autotest_clear_critters();
    Object* enemy = coop_autotest_spawn_enemy(gDude);
    if (enemy == nullptr) {
        return -1;
    }

    // Harder: damage to enemies / 2.5 with four players; players and
    // companions take it in full.
    coop_set_harder(true);
    int enemyDamage = coop_harder_damage(enemy, 10);
    int playerDamage = coop_harder_damage(coop_player(2)->obj, 10);
    coop_set_harder(false);
    int enemyDamageOff = coop_harder_damage(enemy, 10);
    autotest_log("harder: 10 damage to the enemy -> %d (off: %d), to player 3 -> %d", enemyDamage, enemyDamageOff, playerDamage);
    if (enemyDamage != 4 || enemyDamageOff != 10 || playerDamage != 10) {
        autotest_fail("harder scales damage wrongly");
    }
    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_set_turn_hook(coop_autotest_four_turn);
    CombatStartData attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = gDude;
    attack.defender = enemy;
    _combat(&attack);
    coop_set_turn_hook(nullptr);
    autotest_log("fight: turns %d %d %d %d, enemy dead %d", coop_autotest_turns[0], coop_autotest_turns[1],
        coop_autotest_turns[2], coop_autotest_turns[3], critterIsDead(enemy) ? 1 : 0);
    for (int index = 0; index < 4; index++) {
        if (coop_autotest_turns[index] == 0) {
            autotest_fail("player %d never had a turn", index + 1);
        }
    }
    if (!critterIsDead(enemy)) {
        autotest_fail("the enemy survived");
    }

    // Save and load.
    int hp4 = critterGetHitPoints(coop_player(3)->obj);
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1 || (lsgSetQuickSlot(COOP_AUTOTEST_SLOT), false) || lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save and load failed");
        return -1;
    }
    report("after load");
    if (coop_player_count() != 4) {
        autotest_fail("%d players after load", coop_player_count());
        return -1;
    }
    if (critterGetHitPoints(coop_player(3)->obj) != hp4 || coop_player(3)->obj->pid != COOP_PLAYER_PID(3)) {
        autotest_fail("player 4 changed through save and load");
    }

    // Player 4's character joins another game as player 2.
    std::vector<unsigned char> data;
    if (!coop_export_player(3, &data)) {
        autotest_fail("export of player 4 failed");
        return -1;
    }
    std::string name4 = coop_player(3)->name;
    coop_reset();
    std::string error;
    PlayerState* back = coop_import_player(data, &error);
    if (back == nullptr) {
        autotest_fail("import failed: %s", error.c_str());
        return -1;
    }
    if (back != coop_player(1) || back->obj->pid != COOP_PLAYER2_PID || name4 != back->name) {
        autotest_fail("player 4's copy is not player 2 now");
    }
    report("player 4 as player 2");

    tileWindowRefresh();
    takeScreenshot();
    return 0;
}

static int coop_autotest_net_four_human_turns[COOP_MAX_PLAYERS];

// Player 1 punches once everyone else had a turn; the others' turns come
// from their clients (which end them with Space).
static bool coop_autotest_net_four_turn(Object* player)
{
    PlayerState* state = coop_player_of(player);
    int index = state != nullptr ? (int)(state - coop_player(0)) : -1;
    if (index > 0) {
        coop_autotest_net_four_human_turns[index]++;
        autotest_log("player %d's turn", index + 1);
        return false;
    }

    coop_autotest_count_turn(player);
    for (int other = 1; other < coop_player_count(); other++) {
        if (coop_autotest_net_four_human_turns[other] == 0) {
            return true;
        }
    }
    return coop_autotest_attack_turn(player);
}

// Four players over the network (tools/nettest.sh with EXTRA_CLIENTS=2):
// three clients join with their own character (COOP_AUTOTEST_CHARACTER_FILE
// on the clients), each gets their own view, and in a fight each plays
// their own turns.
static int coop_autotest_net_four_host(AutotestNewGameProc* newGame)
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

    unsigned int start = getTicks();
    while (coop_player_count() < COOP_MAX_PLAYERS && getTicksSince(start) < 120000) {
        coop_autotest_session_frames(250, false, false);
    }
    autotest_log("%d players after %u ms, %d clients", coop_player_count(), getTicksSince(start), coop_net_clients_connected());
    if (coop_player_count() != COOP_MAX_PLAYERS || coop_net_clients_connected() != COOP_MAX_PLAYERS - 1) {
        autotest_fail("not everyone joined");
        coop_host_exit();
        return -1;
    }
    for (int index = 0; index < coop_player_count(); index++) {
        PlayerState* player = coop_player(index);
        autotest_log("player %d: %s pid %x tile %d", index + 1, critterGetName(player->obj), player->obj->pid, player->obj->tile);
    }

    // Free roam for a while: each client sees their own view.
    coop_autotest_session_frames(5000, false, false);

    Object* enemy = coop_autotest_spawn_enemy(coop_player(0)->obj);
    if (enemy == nullptr) {
        coop_host_exit();
        return -1;
    }
    coop_autotest_enemy = enemy;
    coop_autotest_turns_total = 0;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    memset(coop_autotest_net_four_human_turns, 0, sizeof(coop_autotest_net_four_human_turns));
    coop_set_turn_hook(coop_autotest_net_four_turn);

    CombatStartData attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = coop_player(0)->obj;
    attack.defender = enemy;
    _combat(&attack);
    coop_set_turn_hook(nullptr);

    autotest_log("combat over: enemy dead %d, turns of players 2-4: %d %d %d", critterIsDead(enemy) ? 1 : 0,
        coop_autotest_net_four_human_turns[1], coop_autotest_net_four_human_turns[2], coop_autotest_net_four_human_turns[3]);
    for (int index = 1; index < COOP_MAX_PLAYERS; index++) {
        if (coop_autotest_net_four_human_turns[index] == 0) {
            autotest_fail("player %d never had a turn", index + 1);
        }
    }
    if (!critterIsDead(enemy) && !coop_any_player_has(DAM_DEAD)) {
        autotest_fail("combat ended without a result");
    }

    // Save and load with everyone connected: the players come back from
    // the save, each still played by their own client.
    std::string names[COOP_MAX_PLAYERS];
    for (int index = 0; index < coop_player_count(); index++) {
        names[index] = coop_player(index)->name;
    }
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save failed");
    }
    lsgSetQuickSlot(COOP_AUTOTEST_SLOT);
    if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("load failed");
    }
    coop_autotest_session_frames(4000, false, false);
    autotest_log("after load: %d players, %d clients", coop_player_count(), coop_net_clients_connected());
    if (coop_player_count() != COOP_MAX_PLAYERS || coop_net_clients_connected() != COOP_MAX_PLAYERS - 1) {
        autotest_fail("players or clients lost through save and load");
    }
    for (int index = 0; index < coop_player_count(); index++) {
        PlayerState* player = coop_player(index);
        if (names[index] != player->name || (player->obj->flags & OBJECT_HIDDEN) != 0 || player->obj->tile == -1) {
            autotest_fail("player %d changed or is hidden after load", index + 1);
        }
    }

    coop_autotest_session_frames(1000, false, false);
    coop_host_exit();
    return 0;
}

static int coop_autotest_net_four_width = 0;
static int coop_autotest_net_four_height = 0;

static void coop_autotest_net_four_seen()
{
    coop_net_client_frame_size(&coop_autotest_net_four_width, &coop_autotest_net_four_height);
}

static int coop_autotest_net_four_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();

    if (const char* path = getenv("COOP_AUTOTEST_CHARACTER_FILE")) {
        std::vector<unsigned char> data;
        FILE* stream = fopen(path, "rb");
        if (stream != nullptr) {
            unsigned char buffer[4096];
            size_t read;
            while ((read = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
                data.insert(data.end(), buffer, buffer + read);
            }
            fclose(stream);
        }
        coop_net_set_bring_character(data);
    }

    inject_wait(600);
    inject_call(coop_autotest_screenshot);
    inject_call(coop_autotest_net_four_seen);
    inject_call(coop_autotest_press_space_forever);
    int rc = coop_net_client_run();
    autotest_log("client finished with %d, frames %dx%d", rc, coop_autotest_net_four_width, coop_autotest_net_four_height);
    if (coop_autotest_net_four_width == 0) {
        autotest_fail("no frames came");
    }
    return 0;
}

// Stats and skills of `player` must be the same whether looked up from
// another player's scope or from its own.
static void coop_autotest_compare_lookups(PlayerState* player, const char* when)
{
    Object* obj = player->obj;
    int outside[STAT_COUNT + SKILL_COUNT];

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        outside[stat] = critterGetStat(obj, stat);
    }

    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        outside[STAT_COUNT + skill] = skillGetValue(obj, skill);
    }

    ActivePlayerScope scope(player);

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        if (critterGetStat(obj, stat) != outside[stat]) {
            autotest_fail("%s: stat %d of %s is %d from outside, %d inside", when, stat, critterGetName(obj), outside[stat], critterGetStat(obj, stat));
        }
    }

    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        if (skillGetValue(obj, skill) != outside[STAT_COUNT + skill]) {
            autotest_fail("%s: skill %d of %s is %d from outside, %d inside", when, skill, critterGetName(obj), outside[STAT_COUNT + skill], skillGetValue(obj, skill));
        }
    }
}

// Compares two text files line by line, ignoring lines starting with
// `skipPrefix`.
static bool coop_autotest_files_equal_except(const char* path1, const char* path2, const char* skipPrefix)
{
    FILE* stream1 = fopen(path1, "rt");
    FILE* stream2 = fopen(path2, "rt");
    bool equal = stream1 != nullptr && stream2 != nullptr;
    size_t prefixLength = strlen(skipPrefix);

    char line1[256];
    char line2[256];
    while (equal) {
        char* read1;
        do {
            read1 = fgets(line1, sizeof(line1), stream1);
        } while (read1 != nullptr && strncmp(line1, skipPrefix, prefixLength) == 0);

        char* read2;
        do {
            read2 = fgets(line2, sizeof(line2), stream2);
        } while (read2 != nullptr && strncmp(line2, skipPrefix, prefixLength) == 0);

        if (read1 == nullptr || read2 == nullptr) {
            equal = read1 == read2;
            break;
        }

        equal = strcmp(line1, line2) == 0;
    }

    if (stream1 != nullptr) {
        fclose(stream1);
    }

    if (stream2 != nullptr) {
        fclose(stream2);
    }

    return equal;
}

// JOIN GAME's list from the server set with [coop]server: how many games
// it shows, and how many it leaves out for another mod version and for
// the other game (COOP_AUTOTEST_LOBBY_EXPECT="<games> <versions> <game>").
// Run with COOP_AUTOTEST_NO_LAN=1, or games on the LAN count too.
static int coop_autotest_lobby_find(AutotestNewGameProc* newGame)
{
    (void)newGame;
    char* server;
    if (!config_get_string(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_SERVER_KEY, &server) || server[0] == '\0') {
        autotest_fail("no [coop]server set");
        return -1;
    }

    int otherVersions = -1;
    int otherGame = -1;
    std::vector<CoopLobbyGame> games = coop_lobby_find(server, &otherVersions, &otherGame);
    std::string names;
    for (const CoopLobbyGame& game : games) {
        names += (names.empty() ? "" : ", ") + game.name;
    }
    autotest_log("%s: %zu games (%s), %d left out for another version, %d for the other game",
        coop_net_game_name(), games.size(), names.c_str(), otherVersions, otherGame);

    const char* expect = getenv("COOP_AUTOTEST_LOBBY_EXPECT");
    int wantGames;
    int wantVersions;
    int wantGame;
    if (expect == NULL || sscanf(expect, "%d %d %d", &wantGames, &wantVersions, &wantGame) != 3) {
        autotest_fail("COOP_AUTOTEST_LOBBY_EXPECT not set");
        return -1;
    }
    if ((int)games.size() != wantGames || otherVersions != wantVersions || otherGame != wantGame) {
        autotest_fail("expected %d games, %d left out for another version and %d for the other game",
            wantGames, wantVersions, wantGame);
    }
    return 0;
}

// A map change while an explosion still plays (a timed charge, another
// player's grenade): the stopped sequence has to run the explosion's
// report while the explosion still exists, and erase it after (soak2 run
// 71: heap damage). For AddressSanitizer; the game must also take input
// again afterwards (the report gives the interface back).
// Player 2 walks in their scope, as on their turn in a fight: the egg
// (see-through walls) stays with player 1, at their place within the hex
// (as in Fallout 1 CE co-op).
// Which game this is (Fallout 2, or a game on its engine such as Fallout:
// Sonora) and the settings its own data gave: COOP_AUTOTEST_GAME, when set,
// is the game name expected.
static int coop_autotest_game_info(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    char* version = nullptr;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_VERSION_STRING, &version);
    int year = 0;
    int month = 0;
    int day = 0;
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_START_YEAR, &year);
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_START_MONTH, &month);
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_START_DAY, &day);
    int gameMonth;
    int gameDay;
    int gameYear;
    gameTimeGetDate(&gameMonth, &gameDay, &gameYear);
    autotest_log("game %s (%s), version string '%s', start %d-%d-%d, the game's date %d-%d-%d, map %s",
        coop_net_game_name(), coop_net_game_title(), version != nullptr ? version : "", year, month, day,
        gameYear, gameMonth, gameDay, gMapHeader.name);

    const char* expected = getenv("COOP_AUTOTEST_GAME");
    if (expected != nullptr && strcmp(expected, coop_net_game_name()) != 0) {
        autotest_fail("game %s, expected %s", coop_net_game_name(), expected);
    }
    return 0;
}

// Each player's own monitor: everyone's messages on all, what a player
// looks at only on theirs.
static int coop_autotest_monitor(AutotestNewGameProc* newGame)
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }
    Object* p1obj = p1->obj;
    Object* p2obj = p2->obj;

    char shared[] = "Everyone's message";
    displayMonitorAddMessage(shared);
    autotest_log("everyone's: player 1 '%s', player 2 '%s'", displayMonitorLastLine(0), displayMonitorLastLine(1));
    if (strstr(displayMonitorLastLine(0), shared) == nullptr || strstr(displayMonitorLastLine(1), shared) == nullptr) {
        autotest_fail("everyone's message not on both monitors");
    }

    // Player 2 looks at player 1: only player 2's monitor.
    {
        ActivePlayerScope scope(p2);
        _obj_look_at(p2obj, p1obj);
    }
    std::string p1Line = displayMonitorLastLine(0);
    std::string p2Line = displayMonitorLastLine(1);
    autotest_log("player 2 looked: player 1 '%s', player 2 '%s'", p1Line.c_str(), p2Line.c_str());
    if (strstr(p1Line.c_str(), shared) == nullptr) {
        autotest_fail("player 2's look reached player 1's monitor");
    }
    if (strstr(p2Line.c_str(), objectGetName(p1obj)) == nullptr) {
        autotest_fail("player 2's look not on their monitor");
    }

    // Player 1 examines player 2: only player 1's monitor.
    _obj_examine(p1obj, p2obj);
    autotest_log("player 1 examined: player 1 '%s', player 2 '%s'", displayMonitorLastLine(0), displayMonitorLastLine(1));
    if (strcmp(displayMonitorLastLine(1), p2Line.c_str()) != 0) {
        autotest_fail("player 1's examine reached player 2's monitor");
    }
    if (strcmp(displayMonitorLastLine(0), p1Line.c_str()) == 0) {
        autotest_fail("player 1's examine not on their monitor");
    }

    // Each screen shows its player's monitor.
    coop_set_viewer(p2);
    interfaceBarRefresh();
    coop_set_viewer(nullptr);
    interfaceBarRefresh();
    return 0;
}

static int coop_autotest_egg(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }
    Object* p1obj = gDude;
    Object* p2obj = p2->obj;

    int target = -1;
    for (int attempt = 0; attempt < 30 && target == -1; attempt++) {
        int tile = tileGetTileInDirection(p2obj->tile, attempt % 6, 4 + attempt / 6);
        if (_obj_blocking_at(nullptr, tile, p2obj->elevation) == nullptr
            && pathfinderFindPath(p2obj, p2obj->tile, tile, nullptr, 0, _obj_blocking_at) != 0) {
            target = tile;
        }
    }
    if (target == -1) {
        autotest_fail("no tile for player 2 to walk to");
        return -1;
    }

    int startTile = p2obj->tile;
    {
        ActivePlayerScope scope(p2);
        reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        animationRegisterMoveToTile(p2obj, target, p2obj->elevation, -1, 0);
        reg_anim_end();
        unsigned int start = getTicks();
        while (animationIsBusy(p2obj) && getTicksSince(start) < 10000) {
            inputGetInput();
        }
    }

    autotest_log("player 2 walked from %d to %d; player 1 at %d %d,%d, the egg at %d %d,%d", startTile, p2obj->tile,
        p1obj->tile, p1obj->x, p1obj->y, gEgg->tile, gEgg->x, gEgg->y);
    if (p2obj->tile == startTile) {
        autotest_fail("player 2 did not walk");
    }
    if (gEgg->tile != p1obj->tile || gEgg->x != p1obj->x || gEgg->y != p1obj->y) {
        autotest_fail("the egg left player 1");
    }
    return 0;
}

// Entering maps as from the world map (COOP_AUTOTEST_MAPS, comma
// separated, or ALL): the screen must then show the new map as a full
// redraw does (as in Fallout 1 CE co-op, where a map whose start can't be
// centered on was never drawn: the last one stayed on screen).
static int coop_autotest_map_entry(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    const char* list = getenv("COOP_AUTOTEST_MAPS");
    std::vector<std::string> maps;
    if (list != nullptr && strcmp(list, "ALL") == 0) {
        for (int index = 1; index < 1000; index++) {
            char name[64];
            if (wmMapIdxToName(index, name, sizeof(name)) != 0) {
                break;
            }
            maps.push_back(name);
        }
    } else {
        std::string rest = list != nullptr ? list : "arvillag.map,klamall.map,denbus1.map";
        size_t comma;
        while ((comma = rest.find(',')) != std::string::npos) {
            maps.push_back(rest.substr(0, comma));
            rest = rest.substr(comma + 1);
        }
        maps.push_back(rest);
    }

    int worst = 0;
    for (const std::string& name : maps) {
        int index = coop_autotest_map_index(name.c_str());
        if (index == -1) {
            autotest_fail("no map %s", name.c_str());
            continue;
        }

        MapTransition transition;
        memset(&transition, 0, sizeof(transition));
        transition.map = index;
        transition.tile = -1;
        mapSetTransition(&transition);
        mapHandleTransition();

        unsigned char* buffer = windowGetBuffer(gIsoWindow);
        size_t size = (size_t)windowGetWidth(gIsoWindow) * windowGetHeight(gIsoWindow);
        std::vector<unsigned char> shown(buffer, buffer + size);
        tileWindowRefresh();
        int pixels = 0;
        for (size_t at = 0; at < size; at++) {
            if (shown[at] != buffer[at]) {
                pixels++;
            }
        }
        autotest_log("%s: player at %d, camera %d; the screen differs from a full redraw in %d pixels", name.c_str(),
            gDude->tile, gCenterTile, pixels);
        worst = std::max(worst, pixels);
    }

    // (A few at most: the engine draws a part a pixel differently than a
    // whole.)
    if (worst > 100) {
        autotest_fail("a map was not drawn when entered (%d pixels differ)", worst);
    }
    return 0;
}

// The camera at the map's corners and edges, the floors and roofs of every
// elevation drawn (under AddressSanitizer: nothing read past the squares'
// table; at high resolutions the window's far corner is past the grid).
static int coop_autotest_map_edges(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    const char* list = getenv("COOP_AUTOTEST_MAPS");
    std::string rest = list != nullptr ? list : "arcaves.map,arvillag.map";
    std::vector<std::string> maps;
    size_t comma;
    while ((comma = rest.find(',')) != std::string::npos) {
        maps.push_back(rest.substr(0, comma));
        rest = rest.substr(comma + 1);
    }
    maps.push_back(rest);

    int width = windowGetWidth(gIsoWindow);
    int height = windowGetHeight(gIsoWindow);
    // Corners and edges' middles (column, row), each approached from the
    // edge until the camera may go there (the map's border).
    const int targets[8][2] = {
        { 0, 0 }, { HEX_GRID_WIDTH - 1, 0 }, { 0, HEX_GRID_HEIGHT - 1 }, { HEX_GRID_WIDTH - 1, HEX_GRID_HEIGHT - 1 },
        { HEX_GRID_WIDTH / 2, 0 }, { 0, HEX_GRID_HEIGHT / 2 }, { HEX_GRID_WIDTH - 1, HEX_GRID_HEIGHT / 2 }, { HEX_GRID_WIDTH / 2, HEX_GRID_HEIGHT - 1 },
    };
    for (const std::string& name : maps) {
        int index = coop_autotest_map_index(name.c_str());
        if (index == -1) {
            autotest_fail("no map %s", name.c_str());
            continue;
        }

        MapTransition transition;
        memset(&transition, 0, sizeof(transition));
        transition.map = index;
        transition.tile = -1;
        mapSetTransition(&transition);
        mapHandleTransition();

        int playerTile = gDude->tile;
        int past = 0;
        for (const auto& target : targets) {
            int tile = -1;
            int set = -1;
            for (int step = 0; step <= 50 && set != 0; step++) {
                int column = target[0] + (HEX_GRID_WIDTH / 2 - target[0]) * step / 50;
                int row = target[1] + (HEX_GRID_HEIGHT / 2 - target[1]) * step / 50;
                tile = row * HEX_GRID_WIDTH + column;
                set = tileSetCenter(tile, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
            }
            for (int elevation = 0; elevation < ELEVATION_COUNT; elevation++) {
                // How far past the grid the window's corners reach.
                const int corners[4][2] = { { 0, 0 }, { width - 1, 0 }, { 0, height - 1 }, { width - 1, height - 1 } };
                for (const auto& corner : corners) {
                    int x;
                    int y;
                    squareTileScreenToCoord(corner[0], corner[1], elevation, &x, &y);
                    if (x >= SQUARE_GRID_WIDTH || y >= SQUARE_GRID_HEIGHT) {
                        past++;
                    }
                }
                Rect rect = { 0, 0, width - 1, height - 1 };
                tileRenderFloorsInRect(&rect, elevation);
                tileRenderRoofsInRect(&rect, elevation);
            }
        }
        tileSetCenter(playerTile, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
        tileWindowRefresh();
        autotest_log("%s: %dx%d, window corners past the squares' grid %d times", name.c_str(), width, height, past);
    }
    return 0;
}

static int coop_autotest_explode_exit(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    for (int round = 0; round < 3; round++) {
        int tile = tileGetTileInDirection(gDude->tile, round * 2, 3);
        int rc = actionExplode(tile, gDude->elevation, 5, 10, gDude, true);
        // Stopped at once, a few frames in, or later.
        for (int frame = 0; frame < round * round * 2; frame++) {
            get_input();
        }
        char map[16];
        snprintf(map, sizeof(map), "%s", round % 2 == 0 ? "arvillag.map" : "artemple.map");
        if (mapLoadByName(map) != 0) {
            autotest_fail("round %d: loading %s failed", round, map);
            return -1;
        }
        autotest_log("round %d: explosion %d, then %s; interface %s", round, rc, map, gameUiIsDisabled() ? "still disabled" : "back");
        if (gameUiIsDisabled()) {
            autotest_fail("round %d: the interface stayed disabled", round);
        }
    }
    return 0;
}

static Object* coop_autotest_death_victim = nullptr;

static void coop_autotest_kill_victim()
{
    if (coop_autotest_death_victim != nullptr) {
        critterKill(coop_autotest_death_victim, -1, true);
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
    if (coop_add_player("premade\\stealth.gcd") == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }
    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }

    const char* victim = getenv("COOP_AUTOTEST_VICTIM");
    int index = victim != nullptr && atoi(victim) == 1 ? 0 : 1;
    coop_autotest_death_victim = coop_player(index)->obj;

    // In the real loop: a few seconds of play, then the death.
    inject_wait(240);
    inject_call(coop_autotest_kill_victim);

    unsigned int start = getTicks();
    bool death = main_game_loop_for_test();
    autotest_log("main loop ended after %u ms, death %d, player %d dead %d, client connected %d", getTicksSince(start),
        death ? 1 : 0, index + 1, critterIsDead(coop_autotest_death_victim) ? 1 : 0, coop_net_client_connected() ? 1 : 0);
    if (!death) {
        autotest_fail("the game did not end in death");
    }

    coop_host_exit();
    return 0;
}

// The ending with player 2 connected: the slides, the movie and the
// credits, then "continue playing?" (Escape: no) must end the game, and
// player 2 must still be there. Paired with net_tour_client.
static int coop_autotest_net_ending_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    if (coop_add_player("premade\\stealth.gcd") == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_host_init();
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }
    if (!coop_autotest_session_frames(COOP_AUTOTEST_JOIN_WAIT, true, false)) {
        autotest_fail("no client joined");
        return -1;
    }
    coop_autotest_session_frames(3000, false, false);

    unsigned int start = getTicks();
    endgamePlaySlideshow();
    autotest_log("slideshow took %u ms, client connected %d", getTicksSince(start), coop_net_client_connected() ? 1 : 0);

    // Escape now and then: skips the credits, then answers "continue
    // playing?" with no.
    for (int press = 0; press < 60; press++) {
        inject_wait(60);
        inject_key(KEY_ESCAPE);
    }
    start = getTicks();
    endgamePlayMovie();
    autotest_log("movie and credits took %u ms, game ends %d, client connected %d", getTicksSince(start),
        _game_user_wants_to_quit, coop_net_client_connected() ? 1 : 0);
    if (_game_user_wants_to_quit != 2) {
        autotest_fail("the ending did not end the game");
    }
    if (!coop_net_client_connected()) {
        autotest_fail("player 2 was dropped during the ending");
    }

    coop_host_exit();
    return 0;
}

// Client side of a nettest, joining through the menu (the address field
// comes filled in from [coop] host); the host leaves after a few seconds
// and the menu reports it. Paired with net_host.
static int coop_autotest_menu_join(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    mainMenuWindowInit();
    mainMenuWindowUnhide(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    int choice = mainMenuWindowHandleEvents();
    if (choice != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen (%d)", choice);
        return -1;
    }

    // JOIN GAME: the server browser finds the host (LAN search) and lists
    // it first; COOP_AUTOTEST_JOIN_ADDRESS takes ENTER ADDRESS instead.
    coop_autotest_click_menu_slot(1);
    inject_wait(30);
    inject_call(coop_autotest_screenshot);
    if (getenv("COOP_AUTOTEST_JOIN_ADDRESS") != nullptr) {
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

    if (strstr(coop_net_last_error(), "left") == nullptr) {
        autotest_fail("the host leaving was not reported");
    }

    if (coop_is_enabled() || coop_net_mode() != COOP_NET_NONE) {
        autotest_fail("co-op still on after leaving");
    }

    return 0;
}

// UPDATE (a newer version is offered and downloaded) and REPORT BUG (sent)
// against tools/coop_server.py, set as [coop] server.
// MULTIPLAYER with a newer version on the server (COOP_AUTOTEST_UPDATE_CHECK):
// it is offered at once, and UPDATE NOW installs it without asking again.
static int coop_autotest_menu_update_prompt(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    mainMenuWindowInit();
    mainMenuWindowUnhide(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (mainMenuWindowHandleEvents() != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen");
        return -1;
    }

    // "Version x is out", UPDATE NOW, "available", then "updated".
    inject_wait(60);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);
    inject_wait(15);
    coop_autotest_click_menu_slot(0);
    inject_wait(60);
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

static int coop_autotest_menu_server(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    mainMenuWindowInit();
    mainMenuWindowUnhide(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (mainMenuWindowHandleEvents() != MAIN_MENU_MULTIPLAYER) {
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

// HOST GAME, NEW GAME, PRIVATE CODE: the game gets a relay code (needs
// [coop] relay), shown in the hosting message.
static int coop_autotest_menu_host_code(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    mainMenuWindowInit();
    mainMenuWindowUnhide(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (mainMenuWindowHandleEvents() != MAIN_MENU_MULTIPLAYER) {
        autotest_fail("MULTIPLAYER not chosen");
        return -1;
    }

    coop_autotest_click_menu_slot(0); // HOST GAME
    coop_autotest_click_menu_slot(0); // NEW GAME
    inject_wait(10);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(3); // PLAYERS: 4 -> 2
    inject_wait(10);
    coop_autotest_click_menu_slot(3); // -> 3
    inject_wait(10);
    inject_call(coop_autotest_screenshot);
    coop_autotest_click_menu_slot(2); // PRIVATE CODE
    inject_wait(90);
    inject_call(coop_autotest_screenshot);
    inject_key(KEY_RETURN);

    int playersBefore = coop_net_max_clients_value() + 1;
    int rc = coop_menu_run();
    autotest_log("menu returned %d, relay code '%s'", rc, coop_net_relay_code());
    if (rc != MAIN_MENU_NEW_GAME || coop_net_relay_code()[0] == '\0') {
        autotest_fail("PRIVATE CODE did not host with a code");
    }

    int playersSaved = 0;
    config_get_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_PLAYERS_KEY, &playersSaved);
    autotest_log("PLAYERS: %d before, %d after, saved %d", playersBefore, coop_net_max_clients_value() + 1, playersSaved);
    if (playersBefore != COOP_MAX_PLAYERS || coop_net_max_clients_value() != 2 || playersSaved != 3) {
        autotest_fail("PLAYERS did not go 4, 2, 3 and save the setting");
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
    mainMenuWindowInit();
    mainMenuWindowUnhide(false);

    std::string report;
    if (!coop_crash_pending(&report)) {
        autotest_fail("no crash-report.txt to offer");
        return -1;
    }

    inject_wait(10);
    coop_autotest_click_menu_slot(3); // MULTIPLAYER
    if (mainMenuWindowHandleEvents() != MAIN_MENU_MULTIPLAYER) {
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

// MULTIPLAYER, MORE: COLORBLIND toggles and is saved.
static int coop_autotest_menu_more(AutotestNewGameProc* newGame)
{
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    mainMenuWindowInit();
    mainMenuWindowUnhide(false);

    inject_wait(10);
    coop_autotest_click_menu_slot(3);
    if (mainMenuWindowHandleEvents() != MAIN_MENU_MULTIPLAYER) {
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
        config_load(&resolutionConfig, "f2_res.ini", false);
        config_get_value(&resolutionConfig, "MAIN", "SCR_WIDTH", &width);
        config_get_value(&resolutionConfig, "MAIN", "SCR_HEIGHT", &height);
        config_exit(&resolutionConfig);
    }
    // Two steps from 640x480 (or whatever the test started with).
    autotest_log("f2_res.ini after RESOLUTION: %dx%d (screen %dx%d)", width, height, screenGetWidth(), screenGetHeight());
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

// Runs the game for up to `ms`, until `done` (if given) says so.
static bool coop_autotest_frames_until(unsigned int ms, bool (*done)())
{
    unsigned int start = getTicks();
    while (getTicksSince(start) < ms) {
        coop_autotest_host_frame();
        if (done != nullptr && done()) {
            return true;
        }
    }
    return done == nullptr;
}

static Object* coop_autotest_interact_item = nullptr;
static Object* coop_autotest_interact_door = nullptr;

static bool coop_autotest_item_picked_up()
{
    return coop_autotest_interact_item->owner == gDude;
}

static bool coop_autotest_door_open()
{
    return objectIsOpen(coop_autotest_interact_door);
}

static bool coop_autotest_anims_done()
{
    return !animationIsBusy(gDude);
}

// The nearest door without a script that the active player can walk to.
static Object* coop_autotest_find_door(int* distancePtr)
{
    Object* door = nullptr;
    int best = 9999;
    for (Object* obj = objectFindFirstAtElevation(gDude->elevation); obj != nullptr; obj = objectFindNextAtElevation()) {
        Proto* proto;
        if (PID_TYPE(obj->pid) != OBJ_TYPE_SCENERY || protoGetProto(obj->pid, &proto) == -1
            || proto->scenery.type != SCENERY_TYPE_DOOR || obj->sid != -1) {
            continue;
        }
        int distance = objectGetDistanceBetween(gDude, obj);
        if (distance < best && _make_path(gDude, gDude->tile, obj->tile, nullptr, 0) > 0) {
            best = distance;
            door = obj;
        }
    }
    *distancePtr = best;
    return door;
}

// ONE PC (F9) with player 2 in control: player 2 picks up an item, opens a
// door and uses a stimpak (on the first of a few maps with a door without
// a script: locks are up to door scripts).
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
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    // COOP_AUTOTEST_INTERACT_P1: the same with player 1, for comparison.
    bool asPlayer1 = getenv("COOP_AUTOTEST_INTERACT_P1") != nullptr;
    PlayerState* actor = asPlayer1 ? p1 : p2;

    const char* maps[] = { "denbus1.map", "klamall.map", "ncr1.map", "modmain.map", "newr1.map" };
    Object* door = nullptr;
    int best = 0;
    for (const char* name : maps) {
        MapTransition transition;
        transition.map = coop_autotest_map_index(name);
        transition.elevation = 0;
        transition.tile = -1;
        transition.rotation = 0;
        mapSetTransition(&transition);
        mapHandleTransition();
        coop_autotest_frames_until(1000, nullptr);
        if (!asPlayer1 && gDude != p2->obj) {
            coop_handle_switch_key();
        }
        door = coop_autotest_find_door(&best);
        autotest_log("map %s, p1 tile %d, p2 tile %d: %s", gMapHeader.name, p1->obj->tile, p2->obj->tile, door != nullptr ? "a door" : "no door");
        if (door != nullptr) {
            break;
        }
    }
    if (!asPlayer1 && gDude != p2->obj) {
        autotest_fail("F9 did not give player 2 control");
        return -1;
    }
    if (door == nullptr) {
        autotest_fail("no door to try");
        return -1;
    }

    // 1. An item on the ground next to player 2.
    Object* item;
    if (objectCreateWithPid(&item, PROTO_ID_STIMPACK) == -1) {
        autotest_fail("no stimpak");
        return -1;
    }
    _obj_attempt_placement(item, gDude->tile, gDude->elevation, 1);
    coop_autotest_interact_item = item;
    actionPickUp(gDude, item);
    if (!coop_autotest_frames_until(8000, coop_autotest_item_picked_up)) {
        autotest_fail("player 2 did not pick up the stimpak (owner %p)", (void*)item->owner);
    } else {
        autotest_log("player 2 picked up the stimpak");
    }
    coop_autotest_frames_until(5000, coop_autotest_anims_done);

    // 2. The nearest door without a script: open it.
    door = coop_autotest_find_door(&best);
    if (door == nullptr) {
        autotest_fail("the door is gone");
        return -1;
    }
    coop_autotest_interact_door = door;
    autotest_log("door at tile %d, distance %d, open %d", door->tile, best, objectIsOpen(door) ? 1 : 0);
    if (objectIsOpen(door)) {
        objectClose(door);
        coop_autotest_frames_until(2000, nullptr);
    }
    _action_use_an_object(gDude, door);
    if (!coop_autotest_frames_until(8000, coop_autotest_door_open)) {
        autotest_fail("player 2 did not open the door");
    } else {
        autotest_log("player 2 opened the door");
    }
    coop_autotest_frames_until(5000, coop_autotest_anims_done);

    // 3. The stimpak, used on themself while hurt.
    critterAdjustHitPoints(gDude, -10);
    int hurt = critterGetHitPoints(gDude);
    // As the inventory's USE does.
    if (_item_d_take_drug(gDude, item) == 1) {
        itemRemove(gDude, item, 1);
        _obj_connect(item, gDude->tile, gDude->elevation, nullptr);
        objectDestroy(item, nullptr);
    }
    coop_autotest_frames_until(2000, nullptr);
    autotest_log("stimpak: hp %d -> %d", hurt, critterGetHitPoints(gDude));
    if (critterGetHitPoints(gDude) <= hurt) {
        autotest_fail("the stimpak did not heal player 2");
    }

    if (gDude != actor->obj) {
        autotest_fail("control changed unexpectedly");
    }
    return 0;
}

// Whether player 1 stands next to the active player, on the same elevation.
static bool coop_autotest_p1_near_active(const char* when)
{
    Object* p1 = coop_player(0)->obj;
    int distance = objectGetDistanceBetween(p1, gDude);
    autotest_log("%s: active player tile %d elev %d, player 1 tile %d elev %d, distance %d", when, gDude->tile, gDude->elevation,
        p1->tile, p1->elevation, distance);
    if (p1->elevation != gDude->elevation || p1->tile == -1 || distance > 4) {
        autotest_fail("%s: player 1 was left behind", when);
        return false;
    }
    return true;
}

// Player 2 changes the elevation (a ladder, an elevator: scripts move
// dude_obj, which is player 2 then) and the map: player 1 must come along,
// as the others do when player 1 moves. With ONE PC's F9 here; in network
// play player 2's own actions run the same way.
static int coop_autotest_p2_moves(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    coop_handle_switch_key();
    if (gDude != p2->obj) {
        autotest_fail("F9 did not give player 2 control");
        return -1;
    }

    // As move_to() does for dude_obj on another elevation.
    int otherElevation = gDude->elevation == 0 ? 1 : 0;
    objectSetLocation(gDude, gDude->tile, otherElevation, nullptr);
    mapSetElevation(otherElevation);
    coop_autotest_frames_until(500, nullptr);
    coop_autotest_p1_near_active("player 2 changed the elevation");

    // A map change while player 2 is the one playing.
    MapTransition transition;
    transition.map = coop_autotest_map_index("arvillag.map");
    transition.elevation = 0;
    transition.tile = -1;
    transition.rotation = 0;
    mapSetTransition(&transition);
    mapHandleTransition();
    coop_autotest_frames_until(500, nullptr);
    coop_autotest_p1_near_active("map changed while player 2 played");
    return 0;
}

// A fight player 1 starts while player 2 is far away, out of sight: player
// 2 must still take part (left out, they could not even move until it
// ended).
static int coop_autotest_combat_far(AutotestNewGameProc* newGame)
{
    if (!coop_is_enabled()) {
        autotest_fail("needs [coop]enabled=1");
        return -1;
    }
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    PlayerState* p2 = coop_add_player("premade\\stealth.gcd");
    if (p2 == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }
    coop_autotest_clear_critters();

    int farTile = coop_autotest_far_tile(p2->obj);
    if (farTile == -1) {
        autotest_fail("no free tile far from player 1");
        return -1;
    }
    objectSetLocation(p2->obj, farTile, gDude->elevation, nullptr);
    Object* enemy = coop_autotest_spawn_enemy(gDude);
    if (enemy == nullptr) {
        return -1;
    }
    autotest_log("player 2 %d hexes from player 1, %d from the enemy", objectGetDistanceBetween(p2->obj, gDude),
        objectGetDistanceBetween(p2->obj, enemy));

    coop_autotest_enemy = enemy;
    memset(coop_autotest_turns, 0, sizeof(coop_autotest_turns));
    coop_autotest_turns_total = 0;
    coop_set_turn_hook(coop_autotest_attack_turn);
    CombatStartData attack;
    memset(&attack, 0, sizeof(attack));
    attack.attacker = gDude;
    attack.defender = enemy;
    _combat(&attack);
    coop_set_turn_hook(nullptr);

    autotest_log("turns p1 %d, p2 %d; enemy dead %d", coop_autotest_turns[0], coop_autotest_turns[1], critterIsDead(enemy) ? 1 : 0);
    if (coop_autotest_turns[1] == 0) {
        autotest_fail("player 2, far away, never had a turn");
    }
    return 0;
}

// The world map test's steps, run from the world map's input loop (see
// coop_autotest_net_worldmap_host()).
static bool coop_autotest_wm_open = false;
static int coop_autotest_wm_step = 0;
static int coop_autotest_wm_time = 0;
static int coop_autotest_wm_still = 0;

static void coop_autotest_wm_drive()
{
    if (!coop_autotest_wm_open) {
        return;
    }

    // Travelling makes time pass; standing on the world map doesn't.
    int now = gameTimeGetTime();
    coop_autotest_wm_still = now != coop_autotest_wm_time ? 0 : coop_autotest_wm_still + 1;
    coop_autotest_wm_time = now;

    switch (coop_autotest_wm_step) {
    case 0:
        // Somewhere south-east of the party (in the map's window;
        // COOP_AUTOTEST_WM_TRIP=x,y pixels, default 120,80: far enough
        // for a random encounter on the way, most likely).
        autotest_log("world map: travelling away from the town");
        if (true) {
            int tripX = 120;
            int tripY = 80;
            if (const char* trip = getenv("COOP_AUTOTEST_WM_TRIP")) {
                sscanf(trip, "%d,%d", &tripX, &tripY);
            }
            coop_autotest_click_centered(22 + 225 + tripX, 21 + 221 + tripY);
        }
        coop_autotest_wm_step++;
        break;
    case 1:
        // There (time stands still): back with the first town button.
        if (coop_autotest_wm_still >= 20) {
            autotest_log("world map: back to the first town on the list");
            inject_key(KEY_CTRL_F1);
            coop_autotest_wm_step++;
            coop_autotest_wm_still = 0;
        }
        break;
    case 2:
        // There: the view on the party (Home)...
        if (coop_autotest_wm_still >= 20) {
            inject_key(KEY_HOME);
            coop_autotest_wm_step++;
            coop_autotest_wm_still = 0;
        }
        break;
    case 3:
        // ... and a click on it: into the town (its town map, or straight
        // to its first map).
        if (coop_autotest_wm_still >= 5) {
            int x;
            int y;
            if (wmPartyWindowPosition(&x, &y)) {
                autotest_log("world map: into the town (the party at %d,%d)", x, y);
                coop_autotest_click_centered(x, y);
            }
            coop_autotest_wm_step++;
            coop_autotest_wm_still = 0;
        }
        break;
    default:
        // The town map: its entrances in turn (1, 2, ...: unknown ones do
        // nothing) until one takes us in.
        if (coop_autotest_wm_step < 4 + 7 && coop_autotest_wm_still >= 20) {
            int entrance = coop_autotest_wm_step - 4;
            autotest_log("world map: the town map's entrance %d", entrance + 1);
            inject_key(KEY_1 + entrance);
            coop_autotest_wm_step++;
            coop_autotest_wm_still = 0;
        }
        break;
    }

    if (coop_autotest_wm_step >= 4 + 7) {
        return;
    }

    inject_wait(5);
    inject_call(coop_autotest_wm_drive);
}

// The world map with players online: player 1 travels away from the town
// and back and goes in (or meets something on the way: a random encounter
// ends the trip), the others watching; everyone must arrive together with
// time passed. COOP_AUTOTEST_PLAYERS=n for more players. Paired with
// net_tour_client.
static int coop_autotest_net_worldmap_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    int players = getenv("COOP_AUTOTEST_PLAYERS") != nullptr ? atoi(getenv("COOP_AUTOTEST_PLAYERS")) : 2;
    for (int index = 1; index < players && index < COOP_MAX_PLAYERS; index++) {
        if (coop_add_player(index == 1 ? "premade\\stealth.gcd" : "premade\\diplomat.gcd") == nullptr) {
            autotest_fail("could not add player %d", index + 1);
            return -1;
        }
    }
    tile_refresh_display();

    coop_host_init();
    if (!coop_host_active()) {
        autotest_fail("cannot host");
        return -1;
    }
    if (!coop_autotest_relay_host_setup()) {
        return -1;
    }

    unsigned int start = getTicks();
    while (getTicksSince(start) < COOP_AUTOTEST_JOIN_WAIT && coop_net_clients_connected() < players - 1) {
        coop_autotest_host_frame();
    }
    if (coop_net_clients_connected() < players - 1) {
        autotest_fail("only %d clients joined", coop_net_clients_connected());
        return -1;
    }

    // A few seconds of the game first.
    start = getTicks();
    while (getTicksSince(start) < 3000) {
        coop_autotest_host_frame();
    }

    char mapName[16];
    strcpy(mapName, gMapHeader.name);
    int timeBefore = gameTimeGetTime();
    autotest_log("on %s, day %d", mapName, timeBefore / 864000);

    // To the world map (-2: straight there, no town map first).
    coop_autotest_wm_open = true;
    coop_autotest_wm_step = 0;
    coop_autotest_wm_time = timeBefore;
    coop_autotest_wm_still = 0;
    inject_wait(30);
    inject_call(coop_autotest_wm_drive);
    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = -2;
    transition.tile = -1;
    mapSetTransition(&transition);
    start = getTicks();
    mapHandleTransition();
    coop_autotest_wm_open = false;

    int hours = (gameTimeGetTime() - timeBefore) / 36000;
    autotest_log("after the world map (%u s): on %s (step %d), %d hours later", getTicksSince(start) / 1000, gMapHeader.name, coop_autotest_wm_step, hours);
    if (gameTimeGetTime() == timeBefore) {
        autotest_fail("no time passed on the world map");
    }
    if (coop_autotest_wm_step < 1) {
        autotest_fail("the world map never got its first click");
    }
    if (coop_net_clients_connected() < players - 1) {
        autotest_fail("a player left during the world map");
    }

    // Everyone there, next to player 1.
    for (int index = 1; index < coop_player_count(); index++) {
        Object* p1obj = coop_player(0)->obj;
        Object* obj = coop_player(index)->obj;
        autotest_log("player %d at tile %d elev %d, %d hexes from player 1", index + 1, obj->tile, obj->elevation, objectGetDistanceBetween(p1obj, obj));
        if (obj->elevation != p1obj->elevation || obj->tile == -1 || objectGetDistanceBetween(p1obj, obj) > 6) {
            autotest_fail("player %d didn't arrive with player 1", index + 1);
        }
    }

    // Then the game goes on there (a random encounter's fight too: player
    // 1 fights it out, the others' turns end by the turn timer).
    coop_set_turn_hook(coop_autotest_rough_turn);
    tickersAdd(coop_autotest_answer_dialogs);
    start = getTicks();
    while (getTicksSince(start) < 5000 && coop_net_clients_connected() >= players - 1) {
        coop_autotest_host_frame();
    }
    if (coop_net_clients_connected() < players - 1) {
        autotest_fail("a player left after the world map");
    }
    takeScreenshot();
    coop_host_exit();
    return 0;
}

// How long a map change takes (COOP_AUTOTEST_MAP, default denbus1.map),
// without co-op: to compare screen sizes and builds.
static int coop_autotest_map_time(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    const char* name = getenv("COOP_AUTOTEST_MAP") != nullptr ? getenv("COOP_AUTOTEST_MAP") : "denbus1.map";
    int index = coop_autotest_map_index(name);
    if (index == -1) {
        autotest_fail("no map %s", name);
        return -1;
    }

    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = index;
    transition.tile = -1;
    unsigned int start = getTicks();
    mapSetTransition(&transition);
    mapHandleTransition();
    autotest_log("%s loaded in %u ms (screen %dx%d)", gMapHeader.name, getTicksSince(start), screenGetWidth(), screenGetHeight());
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
    tileWindowRefresh();
    data.assign((const unsigned char*)gSdlSurface->pixels, (const unsigned char*)gSdlSurface->pixels + (size_t)gSdlSurface->pitch * gSdlSurface->h);
    check("the screen");
    return 0;
}

static void coop_autotest_log_story(const char* when)
{
    autotest_log("story %s: found_geck %d return_geck %d frank_dead %d reactor %d countdown %d timer %d alarm %d",
        when, gameGetGlobalVar(GVAR_VAULT13_FOUND_GECK), gameGetGlobalVar(GVAR_ARROYO_RETURN_GECK),
        gameGetGlobalVar(GVAR_ENCLAVE_FRANK_DEAD), gameGetGlobalVar(GVAR_ENCLAVE_REACTOR),
        gameGetGlobalVar(GVAR_ENCLAVE_COUNTDOWN), gameGetGlobalVar(GVAR_ENCLAVE_TIMER), gameGetGlobalVar(GVAR_ENCLAVE_ALARM));
}

// Single player (or with [coop]enabled=1 and player 2 added: co-op): on
// COOP_AUTOTEST_MAP, the critter named COOP_AUTOTEST_KILL dies as in a fight
// (its death script, player 1 as the killer), then the game runs; the
// story flags before and after, to compare the two.
static int coop_autotest_story_kill(AutotestNewGameProc* newGame)
{
    const char* mapName = getenv("COOP_AUTOTEST_MAP");
    const char* killName = getenv("COOP_AUTOTEST_KILL");
    if (mapName == nullptr || killName == nullptr) {
        autotest_fail("needs COOP_AUTOTEST_MAP and COOP_AUTOTEST_KILL");
        return -1;
    }

    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }
    if (coop_is_enabled() && coop_add_player("premade\\stealth.gcd") == nullptr) {
        autotest_fail("coop_add_player failed");
        return -1;
    }

    int index = coop_autotest_map_index(mapName);
    if (index == -1) {
        autotest_fail("no map %s", mapName);
        return -1;
    }

    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = index;
    transition.tile = -1;
    mapSetTransition(&transition);
    mapHandleTransition();
    coop_autotest_log_story("before");

    Object* victim = nullptr;
    for (Object* obj = objectFindFirst(); obj != nullptr && victim == nullptr; obj = objectFindNext()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && !critterIsDead(obj) && strcmp(critterGetName(obj), killName) == 0) {
            victim = obj;
        }
    }
    if (victim == nullptr) {
        autotest_fail("nobody to kill");
        return -1;
    }

    // Players' turns in the fight that follows pass by themselves.
    coop_set_turn_hook(coop_autotest_pass_turn);
    autotest_log("killing %s (pid %x, script %d)", critterGetName(victim), victim->pid, victim->sid);
    critterKill(victim, -1, true);
    if (victim->sid != -1) {
        scriptSetObjects(victim->sid, gDude, nullptr);
        scriptExecProc(victim->sid, SCRIPT_PROC_DESTROY);
    }
    coop_autotest_frames_until(15000, nullptr);
    coop_set_turn_hook(nullptr);
    coop_autotest_log_story("after");
    autotest_log("players alive: %d, quit %d", critterIsDead(gDude) ? 0 : 1, _game_user_wants_to_quit);
    return 0;
}

// Going through a map's objects meets each once, all of them and per
// elevation (Fallout 1 CE went through the first hex with objects twice).
static int coop_autotest_object_find(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    std::vector<Object*> seen;
    int twice = 0;
    for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
        if (std::find(seen.begin(), seen.end(), obj) != seen.end()) {
            twice++;
        } else {
            seen.push_back(obj);
        }
    }

    int twiceAt = 0;
    int countAt = 0;
    for (int elevation = 0; elevation < ELEVATION_COUNT; elevation++) {
        std::vector<Object*> seenAt;
        for (Object* obj = objectFindFirstAtElevation(elevation); obj != nullptr; obj = objectFindNextAtElevation()) {
            if (std::find(seenAt.begin(), seenAt.end(), obj) != seenAt.end()) {
                twiceAt++;
            } else {
                seenAt.push_back(obj);
                countAt++;
            }
        }
    }

    autotest_log("%s: %d objects, %d met twice; per elevation %d objects, %d met twice", gMapHeader.name, (int)seen.size(), twice,
        countAt, twiceAt);
    if (twice != 0 || twiceAt != 0) {
        autotest_fail("objects were met twice");
    }
    return 0;
}

// A config file whose values have brackets (an IPv6 address, a name):
// they stay values in their section.
static int coop_autotest_config_brackets(AutotestNewGameProc* newGame)
{
    (void)newGame;

    const char* path = "autotest/brackets.cfg";
    FILE* stream = fopen(path, "wt");
    if (stream == nullptr) {
        autotest_fail("cannot write %s", path);
        return -1;
    }
    fputs("[coop]\nhost=[2001:db8::1]:27015\nname=Bob [GER]\nport=27016\n  [sound]\nmusic_volume=12\n", stream);
    fclose(stream);

    Config config;
    configInit(&config);
    bool loaded = configRead(&config, path, false);
    char* host = nullptr;
    char* name = nullptr;
    char* port = nullptr;
    char* volume = nullptr;
    configGetString(&config, "coop", "host", &host);
    configGetString(&config, "coop", "name", &name);
    configGetString(&config, "coop", "port", &port);
    configGetString(&config, "sound", "music_volume", &volume);
    autotest_log("loaded %d: host %s, name %s, port %s, sound music_volume %s", loaded ? 1 : 0,
        host != nullptr ? host : "-", name != nullptr ? name : "-", port != nullptr ? port : "-", volume != nullptr ? volume : "-");
    if (host == nullptr || strcmp(host, "[2001:db8::1]:27015") != 0 || name == nullptr || strcmp(name, "Bob [GER]") != 0
        || port == nullptr || strcmp(port, "27016") != 0 || volume == nullptr || strcmp(volume, "12") != 0) {
        autotest_fail("values with brackets broke the config file");
    }
    configFree(&config);
    return 0;
}

// A sound in ACM band format 31 (as some Russian localizations' speech):
// its samples, 16 bits each, come out as they went in.
struct CoopAutotestMemoryReader {
    const std::vector<unsigned char>* bytes;
    size_t at;
};

static int coop_autotest_memory_read(void* data, void* buffer, unsigned int size)
{
    CoopAutotestMemoryReader* reader = static_cast<CoopAutotestMemoryReader*>(data);
    size_t count = std::min((size_t)size, reader->bytes->size() - reader->at);
    memcpy(buffer, reader->bytes->data() + reader->at, count);
    reader->at += count;
    return (int)count;
}

static int coop_autotest_acm_fmt31(AutotestNewGameProc* newGame)
{
    (void)newGame;

    // The bits least significant first, as the decoder reads them.
    std::vector<unsigned char> bytes;
    unsigned long long pending = 0;
    int pendingBits = 0;
    auto put = [&](unsigned int value, int bits) {
        pending |= (unsigned long long)(value & ((1u << bits) - 1)) << pendingBits;
        pendingBits += bits;
        while (pendingBits >= 8) {
            bytes.push_back((unsigned char)(pending & 0xFF));
            pending >>= 8;
            pendingBits -= 8;
        }
    };

    const short samples[] = { 1000, -1000, 32767, -32768, 0, 123, -123, 4242 };
    const int count = (int)(sizeof(samples) / sizeof(samples[0]));
    const int levels = 2;
    put(0x032897, 24); // ACM
    put(1, 8); // version
    put(count, 16); // samples in the file
    put(0, 16);
    put(1, 16); // channels
    put(22050, 16); // rate
    put(levels, 4); // 4 subbands ...
    put(count >> levels, 12); // ... of 2 samples each
    put(0, 4); // the block's scale
    put(0, 16);
    put(31, 5); // the format of the first band: the whole block
    for (short sample : samples) {
        put((unsigned short)sample, 16);
    }
    put(0, 8);
    bytes.resize(bytes.size() + 64, 0);

    CoopAutotestMemoryReader reader = { &bytes, 0 };
    int channels;
    int rate;
    int sampleCount;
    SoundDecoder* decoder = soundDecoderInit(coop_autotest_memory_read, &reader, &channels, &rate, &sampleCount);
    if (decoder == nullptr) {
        autotest_fail("the decoder did not open the sound");
        return -1;
    }

    short out[16];
    memset(out, 0x55, sizeof(out));
    size_t read = soundDecoderDecode(decoder, out, sizeof(out));
    soundDecoderFree(decoder);

    autotest_log("%d channel(s) at %d Hz, %d samples; read %zu bytes: %d %d %d %d %d %d %d %d", channels, rate, sampleCount, read,
        out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
    if (read != (size_t)count * 2 || memcmp(out, samples, sizeof(samples)) != 0) {
        autotest_fail("format 31 samples did not come out as they went in");
    }
    return 0;
}

static void coop_autotest_db_progress()
{
}

// How many bytes past the end of `path` (`size` bytes) reading it wrote,
// with guard bytes after a buffer of its size; -1 if it cannot be read.
static int coop_autotest_db_overrun(const char* path, int size)
{
    const int guard = 4096;
    std::vector<unsigned char> buffer(size + guard, 0xA5);
    if (dbGetFileContents(path, buffer.data()) != 0) {
        return -1;
    }

    for (int at = guard - 1; at >= 0; at--) {
        if (buffer[size + at] != 0xA5) {
            return at + 1;
        }
    }
    return 0;
}

// Every sound effect read into a buffer of exactly its size, guard bytes
// after it: none may be written past (Fallout 1 CE's sound cache heap was
// found damaged after reads with a progress callback set, as while a map
// loads).
static int coop_autotest_sfx_sizes(AutotestNewGameProc* newGame)
{
    (void)newGame;

    // The game's own list of them (sound\sfx\sndlist.lst; listing the
    // folder finds none in the archive).
    int checked = 0;
    // Twice: as usual, and with a progress callback, as while a map loads.
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            fileSetReadProgressHandler(coop_autotest_db_progress, 1024);
        }
        for (int tag = 2; soundEffectsListIsValidTag(tag); tag += 2) {
            char* path;
            if (soundEffectsListGetFilePath(tag, &path) != SFXL_OK) {
                continue;
            }
            int size;
            if (dbGetFileSize(path, &size) != 0 || size <= 0) {
                autotest_log("%s: no size", path);
                internal_free(path);
                continue;
            }
            int over = coop_autotest_db_overrun(path, size);
            if (over == -1) {
                autotest_log("%s: cannot read", path);
            } else {
                if (over != 0) {
                    autotest_fail("%s (%d bytes listed) wrote %d bytes past its size", path, size, over);
                }
                checked++;
            }
            internal_free(path);
        }
    }
    fileSetReadProgressHandler(nullptr, 0);
    autotest_log("%d sound effect reads checked", checked);
    if (checked == 0) {
        autotest_fail("no sound effects read");
    }
    return 0;
}

// Every file matching COOP_AUTOTEST_DB_PATTERN (default the sound effects)
// read from the game's data into a buffer of its listed size: nothing may
// be written past it.
static int coop_autotest_db_sfx(AutotestNewGameProc* newGame)
{
    (void)newGame;

    const char* pattern = getenv("COOP_AUTOTEST_DB_PATTERN") != nullptr ? getenv("COOP_AUTOTEST_DB_PATTERN") : "sound\\sfx\\*.acm";
    const char* slash = strrchr(pattern, '\\');
    int folderLength = slash != nullptr ? (int)(slash - pattern + 1) : 0;
    char** names;
    int count = fileNameListInit(pattern, &names, 0, 0);
    int bad = 0;
    for (int index = 0; index < count; index++) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%.*s%s", folderLength, pattern, names[index]);
        int size;
        if (dbGetFileSize(path, &size) != 0) {
            autotest_log("%s: no entry", path);
            continue;
        }
        int over = coop_autotest_db_overrun(path, size);
        if (over > 0) {
            bad++;
            autotest_log("%s: %d bytes and %d bytes written past them", path, size, over);
        }
    }
    autotest_log("%d files read, %d wrote past their size", count, bad);
    if (bad != 0) {
        autotest_fail("files are written past their size");
    }
    if (count != 0) {
        fileNameListFree(&names, 0);
    }
    return 0;
}

// How long one full redraw of the map and the interface takes at this
// screen size: what the host pays for each other player's view every frame
// (COOP_AUTOTEST_BENCH_MAP, default denbus1.map).
static int coop_autotest_draw_bench(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    const char* name = getenv("COOP_AUTOTEST_BENCH_MAP") != nullptr ? getenv("COOP_AUTOTEST_BENCH_MAP") : "denbus1.map";
    int index = coop_autotest_map_index(name);
    if (index == -1) {
        autotest_fail("no map %s", name);
        return -1;
    }

    MapTransition transition;
    memset(&transition, 0, sizeof(transition));
    transition.map = index;
    transition.tile = -1;
    mapSetTransition(&transition);
    mapHandleTransition();

    for (int round = 0; round < 3; round++) {
        unsigned int start = getTicks();
        int count = 0;
        while (count < 20 || getTicksSince(start) < 2000) {
            tileWindowRefresh();
            interfaceBarRefresh();
            count++;
        }
        unsigned int elapsed = getTicksSince(start);
        autotest_log("draw: %u us per full redraw at %dx%d (%d redraws)",
            (unsigned int)((unsigned long long)elapsed * 1000 / count), screenGetWidth(), screenGetHeight(), count);
    }

    // Where the time goes.
    unsigned long long ticks[TILE_REFRESH_STEPS] = { 0 };
    gTileRefreshStepTicks = ticks;
    int count = 0;
    unsigned int start = getTicks();
    while (count < 20 || getTicksSince(start) < 2000) {
        tileWindowRefresh();
        count++;
    }
    gTileRefreshStepTicks = nullptr;
    double perUs = (double)SDL_GetPerformanceFrequency() / 1000000.0;
    autotest_log("draw steps (us per redraw): clear %.0f, floor %.0f, objects %.0f, roofs %.0f, objects over roofs %.0f, to the screen %.0f",
        ticks[0] / perUs / count, ticks[1] / perUs / count, ticks[2] / perUs / count, ticks[3] / perUs / count, ticks[4] / perUs / count, ticks[5] / perUs / count);
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
    (void)newGame;

    inject_call(coop_autotest_screenshot_every_2s);
    creditsOpen("credits.txt", -1, false);
    autotest_log("credits done");
    return 0;
}

} // namespace fallout
