#include "coop_autotest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "autotest.h"
#include "combat.h"
#include "coop.h"
#include "coop_compat.h"
#include "coop_host.h"
#include "coop_lobby.h"
#include "coop_menu.h"
#include "coop_net.h"
#include "critter.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_dialog.h"
#include "game_vars.h"
#include "inject.h"
#include "input.h"
#include "kb.h"
#include "item.h"
#include "loadsave.h"
#include "mainmenu.h"
#include "map.h"
#include "object.h"
#include "palette.h"
#include "perk.h"
#include "platform_compat.h"
#include "proto.h"
#include "proto_instance.h"
#include "queue.h"
#include "scripts.h"
#include "skill.h"
#include "svga.h"
#include "stat.h"
#include "tile.h"
#include "worldmap.h"

namespace fallout {

// Quick save slot used by the co-op tests (0-based; SLOT08).
#define COOP_AUTOTEST_SLOT 7

static int coop_autotest_players(AutotestNewGameProc* newGame);
static int coop_autotest_rads(AutotestNewGameProc* newGame);
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
static void coop_autotest_screenshot();

// The enemy of the combat tests, and the player turns counted there.
static Object* coop_autotest_enemy = nullptr;
static int coop_autotest_turns[COOP_MAX_PLAYERS];
static int coop_autotest_turns_total = 0;
static bool coop_autotest_players_together(const char* when);
static int coop_autotest_poison(AutotestNewGameProc* newGame);
static void coop_autotest_compare_lookups(PlayerState* player, const char* when);
static bool coop_autotest_files_equal_except(const char* path1, const char* path2, const char* skipPrefix);

void coop_autotest_register()
{
    autotest_add_script("coop", coop_autotest_players);
    autotest_add_script("coop_rads", coop_autotest_rads);
    autotest_add_script("coop_travel", coop_autotest_travel);
    autotest_add_script("coop_combat", coop_autotest_combat);
    autotest_add_script("coop_join", coop_autotest_join);
    autotest_add_script("main_menu", coop_autotest_main_menu);
    autotest_add_script("gamepad", coop_autotest_gamepad);
    autotest_add_script("coop_give", coop_autotest_give);
    autotest_add_script("coop_knockout", coop_autotest_knockout);
    autotest_add_script("coop_character", coop_autotest_character);
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

// Removes every critter from the map except the players.
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

    autotest_log("removed %d critters from %s", (int)critters.size(), gMapHeader.name);
}

// Mouse positions in a 640x480 layout centered on the screen.
#define COOP_AUTOTEST_CENTERED 0x4000

static void coop_autotest_mouse_transform(int* x, int* y)
{
    if ((*x & COOP_AUTOTEST_CENTERED) != 0) {
        *x = (*x & ~COOP_AUTOTEST_CENTERED) + (screenGetWidth() - 640) / 2;
        *y += (screenGetHeight() - 480) / 2;
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
                autotest_log("player 2 '%s' joined at tile %d", coop_player(1)->name, startTile);
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
    if (!coop_autotest_session_frames(30000, true, false)) {
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

    if (!coop_autotest_session_frames(30000, true, false)) {
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
    int team = player->data.critter.combat.team;
    std::vector<Object*> targets;
    for (Object* obj = objectFindFirstAtElevation(player->elevation); obj != nullptr; obj = objectFindNextAtElevation()) {
        if (PID_TYPE(obj->pid) == OBJ_TYPE_CRITTER && coop_player_of(obj) == nullptr && !critterIsDead(obj)
            && obj->data.critter.combat.team != team && objectGetDistanceBetween(obj, player) <= 20) {
            targets.push_back(obj);
        }
    }
    for (Object* obj : targets) {
        critterKill(obj, -1, true);
    }
    if (!targets.empty()) {
        autotest_log("%s: cleared %d hostile critters", gMapHeader.name, (int)targets.size());
    }
    combatAttemptEnd();
    return true;
}

// Network tour: both players travel through every map (COOP_AUTOTEST_TOUR=
// ALL; or A.MAP,B.MAP), every elevation, a few seconds each with player 2
// walking around; fights are cleared, conversations answered. Paired with
// net_tour_client.
static int coop_autotest_net_tour_host(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_HOST) {
        autotest_fail("needs [coop]mode=host");
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

    coop_host_init();
    if (!coop_host_active() || !coop_autotest_relay_host_setup()) {
        autotest_fail("cannot host");
        return -1;
    }

    if (!coop_autotest_session_frames(30000, true, false)) {
        autotest_fail("no client joined");
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
    for (const std::string& stopName : tour) {
        char stop[64];
        snprintf(stop, sizeof(stop), "%s", stopName.c_str());
        int index = wmMapMatchNameToIdx(stop);
        if (index == -1) {
            autotest_log("no map %s", stop);
            continue;
        }

        MapTransition transition;
        memset(&transition, 0, sizeof(transition));
        transition.map = index;
        transition.tile = -1;
        mapSetTransition(&transition);

        unsigned int start = getTicks();
        mapHandleTransition();
        autotest_log("%s loaded in %u ms", stop, getTicksSince(start));

        start = getTicks();
        while (getTicksSince(start) < 6000 && coop_net_client_connected()) {
            coop_autotest_heal_players();
            coop_autotest_host_frame();
        }

        // The other elevations too.
        for (int elevation = 0; elevation < ELEVATION_COUNT && coop_net_client_connected(); elevation++) {
            if (gDude->elevation == elevation || objectFindFirstAtElevation(elevation) == nullptr) {
                continue;
            }
            objectSetLocation(gDude, gDude->tile, elevation, nullptr);
            mapSetElevation(elevation);
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
        autotest_log("%s: p1 tile %d elev %d hp %d, p2 tile %d elev %d hp %d, distance %d",
            gMapHeader.name, p1obj->tile, p1obj->elevation, critterGetHitPoints(p1obj),
            p2obj->tile, p2obj->elevation, critterGetHitPoints(p2obj), objectGetDistanceBetween(p1obj, p2obj));

        if (coop_player_count() != 2 || p2obj->elevation != p1obj->elevation || p2obj->tile == -1) {
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
    autotest_log("visited %d maps", visited);
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
    inject_wait(150);
    inject_call(coop_autotest_screenshot);
    inject_wait(90);
    inject_call(coop_autotest_tour_walk);
}

static int coop_autotest_net_tour_client(AutotestNewGameProc* newGame)
{
    if (coop_net_mode() != COOP_NET_CLIENT) {
        autotest_fail("needs [coop]mode=client");
        return -1;
    }

    coop_autotest_relay_client_setup();

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

} // namespace fallout
