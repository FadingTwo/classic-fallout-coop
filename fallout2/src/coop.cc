#include "coop.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "art.h"
#include "character_editor.h"
#include "color.h"
#include "combat.h"
#include "config.h"
#include "coop_autotest.h"
#include "coop_net.h"
#include "debug.h"
#include "display_monitor.h"
#include "game.h"
#include "game_config.h"
#include "game_mouse.h"
#include "game_vars.h"
#include "input.h"
#include "interface.h"
#include "object.h"
#include "palette.h"
#include "party_member.h"
#include "proto.h"
#include "proto_instance.h"
#include "scripts.h"
#include "queue.h"
#include "stat.h"
#include "perk.h"
#include "tile.h"
#include "trait.h"

namespace fallout {

#define COOP_SCOPE_STACK_MAX 64

// "COOP"
#define COOP_SAVE_MAGIC 0x434F4F50
#define COOP_SAVE_VERSION 1

// A copy of player 2's character for player 2 to keep (see
// coop_export_player2): magic, version, name, level, the player data as in
// a save, then the character with its inventory.
#define COOP_CHARACTER_MAGIC 0x43464332
#define COOP_CHARACTER_VERSION 1
// (Written through the game's file layer, in its data folder; it is just
// overwritten the next time.)
#define COOP_CHARACTER_TEMP "coop-character.tmp"

static const int coop_addiction_gvars[COOP_ADDICTION_COUNT] = {
    GVAR_NUKA_COLA_ADDICT,
    GVAR_BUFF_OUT_ADDICT,
    GVAR_MENTATS_ADDICT,
    GVAR_PSYCHO_ADDICT,
    GVAR_RADAWAY_ADDICT,
    GVAR_ALCOHOL_ADDICT,
    GVAR_ADDICT_JET,
    GVAR_ADDICT_TRAGIC,
};

// Zero-initialized player state must match the old
// `gInterfaceCurrentHand = HAND_LEFT`.
static_assert(HAND_LEFT == 0, "HAND_LEFT must be zero");

typedef struct ActivePlayerScopeFrame {
    PlayerState* player;
    Object* dude;
    // Scope entered for the already active player - nothing was changed.
    bool noop;
} ActivePlayerScopeFrame;

static void coop_scope_push(PlayerState* player);
static void coop_scope_pop();
static void coop_scope_unwind_to(int depth);
static void coop_sync_base();
static void coop_switch_active(PlayerState* player);

static void coop_remove_extra_players();
static void coop_display_message(const char* format, ...);

// Zero-initialized, matching the per-module globals this replaces.
static PlayerState coop_players[COOP_MAX_PLAYERS];

static int coop_players_count = 1;

PlayerState* coop_active = &(coop_players[0]);

static ActivePlayerScopeFrame coop_scope_stack[COOP_SCOPE_STACK_MAX];

static int coop_scope_stack_depth = 0;

static bool coop_enabled = false;

static unsigned int coop_turn_time_ms = 0;

static CoopTurnHook* coop_turn_hook = nullptr;

// See coop_set_viewer().
static PlayerState* coop_viewer = nullptr;

static int coop_outline_for[COOP_MAX_PLAYERS][2] = { { 0, 1 }, { 0, 1 } };


static bool coop_knockout_on = false;
static bool coop_talk_on = true;
static PlayerState* coop_dialog_talker = nullptr;

static unsigned int coop_longest_turns[COOP_MAX_PLAYERS];

void coop_init()
{
    int value;
    coop_enabled = configGetInt(&gGameConfig, COOP_CONFIG_KEY, COOP_CONFIG_ENABLED_KEY, &value) && value != 0;

    if (coop_enabled && configGetInt(&gGameConfig, COOP_CONFIG_KEY, COOP_CONFIG_TURN_TIME_KEY, &value) && value > 0) {
        coop_turn_time_ms = (unsigned int)value * 1000;
    }

    // (Read also when co-op is turned on later, from the menu.)
    coop_knockout_on = configGetInt(&gGameConfig, COOP_CONFIG_KEY, COOP_CONFIG_KNOCKOUT_KEY, &value) && value != 0;
    coop_talk_on = !configGetInt(&gGameConfig, COOP_CONFIG_KEY, COOP_CONFIG_TALK_KEY, &value) || value != 0;

    int outlines[2];
    coop_outline_colors_from_config(outlines);
    for (int index = 0; index < COOP_MAX_PLAYERS; index++) {
        coop_set_outline_colors(index, outlines);
    }

    if (coop_enabled) {
        debugPrint("\nCOOP: enabled (turn time: %u ms)\n", coop_turn_time_ms);
    }

    coop_net_init();

    coop_autotest_register();
}

bool coop_is_enabled()
{
    return coop_enabled;
}

void coop_set_enabled(bool enabled)
{
    coop_enabled = enabled;
}

int coop_player_count()
{
    return coop_players_count;
}

PlayerState* coop_player(int index)
{
    if (index < 0 || index >= coop_players_count) {
        return nullptr;
    }

    return &(coop_players[index]);
}

// Makes `player` active. Per-player data reached through `coop_active`
// switches with the pointer; the addiction globals are copied.
static void coop_switch_active(PlayerState* player)
{
    if (player == coop_active) {
        return;
    }

    if (gGameGlobalVars != nullptr) {
        for (int index = 0; index < COOP_ADDICTION_COUNT; index++) {
            coop_active->addictions[index] = gGameGlobalVars[coop_addiction_gvars[index]];
            gGameGlobalVars[coop_addiction_gvars[index]] = player->addictions[index];
        }
    }

    coop_active->vaultGuyNum = _art_vault_guy_num;
    _art_vault_guy_num = player->vaultGuyNum;

    coop_active = player;
}

// Outside of scopes `gDude` is the controlled player's critter. The engine
// may (re)create `gDude`, so pick it up from there.
static void coop_sync_base()
{
    if (coop_scope_stack_depth == 0) {
        coop_active->obj = gDude;
    }
}

PlayerState* coop_player_of(Object* obj)
{
    if (obj == nullptr) {
        return nullptr;
    }

    if (obj == gDude) {
        return coop_active;
    }

    coop_sync_base();

    for (int index = 0; index < coop_players_count; index++) {
        if (coop_players[index].obj == obj) {
            return &(coop_players[index]);
        }
    }

    return nullptr;
}

PlayerState* coop_player_by_pid(int pid)
{
    if (pid == 0x1000000) {
        return &(coop_players[0]);
    }

    if (pid == COOP_PLAYER2_PID && coop_players_count > 1) {
        return &(coop_players[1]);
    }

    return nullptr;
}

bool coop_primary_is_active()
{
    return coop_active == &(coop_players[0]);
}

PlayerState* coop_controlled_player()
{
    if (coop_scope_stack_depth == 0) {
        return coop_active;
    }

    return coop_scope_stack[0].player;
}

bool coop_set_controlled(PlayerState* player)
{
    if (player == nullptr || player->obj == nullptr || coop_scope_stack_depth != 0) {
        return false;
    }

    coop_sync_base();

    if (player == coop_active) {
        return true;
    }

    coop_switch_active(player);
    gDude = player->obj;

    tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
    interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
    interfaceBarRefresh();
    _gmouse_3d_refresh();

    return true;
}

PlayerState* coop_add_player(const char* premadePath)
{
    if (coop_players_count >= COOP_MAX_PLAYERS || gDude == nullptr || coop_scope_stack_depth != 0) {
        return nullptr;
    }

    coop_sync_base();
    Object* leader = coop_players[0].obj;

    protoPlayer2Reset();

    Object* obj;
    int fid = buildFid(OBJ_TYPE_CRITTER, _art_vault_guy_num, 0, 0, 0);
    if (objectCreateWithFidPid(&obj, fid, COOP_PLAYER2_PID) == -1) {
        return nullptr;
    }

    PlayerState* player = &(coop_players[coop_players_count]);
    memset(player, 0, sizeof(*player));
    player->obj = obj;
    player->vaultGuyNum = _art_vault_guy_num;
    coop_players_count++;

    {
        // Everything player-specific is (re)initialized in player 2's scope,
        // exactly as a new game does it for player 1.
        ActivePlayerScope scope(player);
        if (_proto_dude_init(premadePath) != 0) {
            debugPrint("\nCOOP: failed to load %s for player 2\n", premadePath);
        }
    }

    obj->flags |= OBJECT_LIGHT_THRU;
    objectSetLight(obj, 4, 0x10000, nullptr);
    _obj_attempt_placement(obj, leader->tile, leader->elevation, 2);
    objectSetRotation(obj, leader->rotation, nullptr);

    if (partyMemberAdd(obj) == -1) {
        debugPrint("\nCOOP: could not add player 2 to the party\n");
    }

    debugPrint("\nCOOP: added player 2 '%s'\n", player->name);

    return player;
}

static void coop_remove_extra_players()
{
    for (int index = 1; index < coop_players_count; index++) {
        Object* obj = coop_players[index].obj;
        if (obj != nullptr) {
            partyMemberRemove(obj);
            objectDestroy(obj, nullptr);
        }
        memset(&(coop_players[index]), 0, sizeof(coop_players[index]));
    }

    coop_players_count = 1;
}

static void coop_display_message(const char* format, ...)
{
    char message[160];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    displayMonitorAddMessage(message);
}

PlayerState* coop_create_player2()
{
    if (coop_players_count != 1 || isInCombat()) {
        return nullptr;
    }

    PlayerState* player = coop_add_player("premade\\player.gcd");
    if (player == nullptr) {
        return nullptr;
    }

    bool created;
    {
        ActivePlayerScope scope(player);
        _ResetPlayer();
        created = characterEditorShow(true) == 0;
        if (created) {
            _proto_dude_update_gender();
            critterUpdateDerivedStats(gDude);
            critterAdjustHitPoints(gDude, 10000);
        }
    }

    if (!created) {
        coop_remove_extra_players();
        tileWindowRefresh();
        return nullptr;
    }

    // Finishing character creation fades to black (a new game would
    // continue with the intro); come back to the map.
    tileWindowRefresh();
    paletteFadeTo(_cmap);
    coop_display_message("%s joins the game.", player->name);
    return player;
}

void coop_handle_switch_key()
{
    if (!coop_enabled) {
        return;
    }

    if (isInCombat()) {
        // In combat, each player's turn takes control automatically.
        coop_display_message("Players take turns in combat.");
        return;
    }

    if (coop_players_count == 1) {
        coop_create_player2();
        return;
    }

    PlayerState* next = coop_controlled_player() == &(coop_players[0]) ? &(coop_players[1]) : &(coop_players[0]);
    if (coop_set_controlled(next)) {
        coop_display_message("Now controlling %s.", next->name);
    }
}

void coop_reset()
{
    coop_scope_unwind_to(0);

    if (coop_active != &(coop_players[0])) {
        coop_active = &(coop_players[0]);
        gDude = coop_players[0].obj;
    }

    coop_remove_extra_players();
}

static void coop_scope_unwind_to(int depth)
{
    while (coop_scope_stack_depth > depth) {
        coop_scope_pop();
    }
}

static void coop_scope_push(PlayerState* player)
{
    if (coop_scope_stack_depth >= COOP_SCOPE_STACK_MAX) {
        // Unbounded nesting is a bug; refusing to switch is the safest thing
        // we can do without exceptions.
        debugPrint("\nCOOP: ActivePlayerScope stack overflow!\n");
        coop_scope_stack_depth++;
        return;
    }

    coop_sync_base();

    ActivePlayerScopeFrame* frame = &(coop_scope_stack[coop_scope_stack_depth++]);
    frame->player = coop_active;
    frame->dude = gDude;
    frame->noop = player == nullptr || player == coop_active;

    if (!frame->noop) {
        coop_switch_active(player);
        if (player->obj != nullptr) {
            gDude = player->obj;
        }
    }
}

static void coop_scope_pop()
{
    if (coop_scope_stack_depth <= 0) {
        debugPrint("\nCOOP: ActivePlayerScope stack underflow!\n");
        return;
    }

    coop_scope_stack_depth--;
    if (coop_scope_stack_depth >= COOP_SCOPE_STACK_MAX) {
        return;
    }

    ActivePlayerScopeFrame* frame = &(coop_scope_stack[coop_scope_stack_depth]);
    if (!frame->noop) {
        coop_switch_active(frame->player);
        gDude = frame->dude;
    }
}

ActivePlayerScope::ActivePlayerScope(PlayerState* player)
{
    coop_scope_push(player);
}

ActivePlayerScope::~ActivePlayerScope()
{
    coop_scope_pop();
}

bool coop_any_player_has(int results)
{
    coop_sync_base();

    for (int index = 0; index < coop_players_count; index++) {
        Object* obj = coop_players[index].obj;
        if (obj != nullptr && (obj->data.critter.combat.results & results) != 0) {
            return true;
        }
    }

    return false;
}

bool coop_any_player_has_perk(int perk)
{
    coop_sync_base();

    for (int index = 0; index < coop_players_count; index++) {
        Object* obj = coop_players[index].obj;
        if (obj != nullptr && perkGetRank(obj, perk) != 0) {
            return true;
        }
    }

    return false;
}

bool coop_any_player_has_trait(int trait)
{
    for (int index = 0; index < coop_players_count; index++) {
        ActivePlayerScope scope(&(coop_players[index]));
        if (traitIsSelected(trait)) {
            return true;
        }
    }

    return false;
}

bool coop_turn_time_expired(unsigned int turnStart)
{
    return coop_turn_time_ms != 0 && getTicksSince(turnStart) >= coop_turn_time_ms;
}

unsigned int coop_turn_time_limit()
{
    return coop_turn_time_ms;
}

void coop_set_turn_hook(CoopTurnHook* hook)
{
    coop_turn_hook = hook;
}

bool coop_run_turn_hook(Object* player)
{
    if (coop_turn_hook == nullptr) {
        return false;
    }

    return coop_turn_hook(player);
}

void coop_note_turn_length(Object* player, unsigned int ms)
{
    for (int index = 0; index < coop_players_count && index < COOP_MAX_PLAYERS; index++) {
        if (coop_players[index].obj == player && ms > coop_longest_turns[index]) {
            coop_longest_turns[index] = ms;
        }
    }
}

unsigned int coop_longest_turn(int index)
{
    return index >= 0 && index < COOP_MAX_PLAYERS ? coop_longest_turns[index] : 0;
}

void coop_process_requests()
{
}

bool coop_knockout_setting()
{
    return coop_knockout_on;
}

void coop_set_knockout(bool on)
{
    coop_knockout_on = on;
}

bool coop_knockout_enabled()
{
    return coop_knockout_on && coop_players_count > 1;
}

void coop_knockout_check(Object* critter, int* damage, int* flags)
{
    if (!coop_knockout_enabled() || critter == nullptr || coop_player_of(critter) == nullptr) {
        return;
    }

    if ((critter->data.critter.combat.results & DAM_KNOCKED_OUT) != 0) {
        *damage = 0;
        *flags &= ~DAM_DEAD;
        return;
    }

    if (*damage <= 0 || critterGetHitPoints(critter) - *damage > 0) {
        return;
    }

    // Only while someone is still up to help.
    bool partnerUp = false;
    for (int index = 0; index < coop_players_count; index++) {
        Object* other = coop_players[index].obj;
        if (other != nullptr && other != critter && (other->flags & OBJECT_HIDDEN) == 0
            && (other->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT)) == 0
            && critterGetHitPoints(other) > 0) {
            partnerUp = true;
        }
    }

    if (!partnerUp) {
        return;
    }

    *damage = critterGetHitPoints(critter) - 1;
    *flags &= ~DAM_DEAD;
    *flags |= DAM_KNOCKED_OUT;
    debugPrint("\nCOOP: %s knocked out instead of killed\n", critterGetName(critter));
}

void coop_knockout_healed(Object* critter)
{
    if (!coop_knockout_enabled() || critter == nullptr || coop_player_of(critter) == nullptr) {
        return;
    }

    if ((critter->data.critter.combat.results & DAM_KNOCKED_OUT) == 0 || critterGetHitPoints(critter) <= 1) {
        return;
    }

    queueRemoveEventsByType(critter, EVENT_TYPE_KNOCKOUT);
    knockoutEventProcess(critter, nullptr);
    debugPrint("\nCOOP: %s woke up after healing\n", critterGetName(critter));
}

bool coop_game_lost()
{
    if (coop_any_player_has(DAM_DEAD)) {
        return true;
    }

    if (!coop_any_player_has(DAM_KNOCKED_OUT)) {
        return false;
    }

    if (!coop_knockout_enabled()) {
        return true;
    }

    // Knocked out players wake up (or are healed) while someone is up.
    for (int index = 0; index < coop_players_count; index++) {
        Object* obj = coop_players[index].obj;
        if (obj != nullptr && (obj->flags & OBJECT_HIDDEN) == 0
            && (obj->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT)) == 0) {
            return false;
        }
    }

    return true;
}

bool coop_talk_setting()
{
    return coop_talk_on;
}

void coop_set_talk(bool on)
{
    coop_talk_on = on;
}

PlayerState* coop_dialog_player()
{
    return coop_dialog_talker != nullptr ? coop_dialog_talker : coop_player(0);
}

void coop_set_dialog_player(PlayerState* player)
{
    coop_dialog_talker = player;
}


void coop_set_viewer(PlayerState* player)
{
    coop_viewer = player;
}

int coop_outline_player(Object* obj)
{
    if (coop_players_count <= 1 || obj == nullptr || (obj->flags & OBJECT_HIDDEN) != 0) {
        return -1;
    }

    PlayerState* player = coop_player_of(obj);
    PlayerState* viewer = coop_viewer != nullptr ? coop_viewer : coop_controlled_player();
    if (player == nullptr || player == viewer || critterIsDead(obj)) {
        return -1;
    }

    return player == &(coop_players[0]) ? 0 : 1;
}

// Green is Pip-Boy green, yellow close to New Vegas' amber. Red is left
// out: enemies are outlined red in fights.
static const char* const coop_outline_names[COOP_OUTLINE_COLORS] = {
    "GREEN",
    "YELLOW",
    "WHITE",
    "ORANGE",
    "PINK",
    "BLUE",
};

static const int coop_outline_rgb[COOP_OUTLINE_COLORS] = {
    (4 << 10) | (31 << 5) | 4,
    (31 << 10) | (27 << 5) | 7,
    (31 << 10) | (31 << 5) | 31,
    (31 << 10) | (14 << 5) | 0,
    (31 << 10) | (22 << 5) | 30,
    (12 << 10) | (17 << 5) | 18,
};

const char* coop_outline_color_name(int color)
{
    return color >= 0 && color < COOP_OUTLINE_COLORS ? coop_outline_names[color] : coop_outline_names[0];
}

void coop_outline_colors_from_config(int colors[2])
{
    int colorblind = 0;
    configGetInt(&gGameConfig, COOP_CONFIG_KEY, COOP_CONFIG_COLORBLIND_KEY, &colorblind);
    colors[0] = colorblind != 0 ? 2 : 0;
    colors[1] = colorblind != 0 ? 3 : 1;

    int value;
    if (configGetInt(&gGameConfig, COOP_CONFIG_KEY, COOP_CONFIG_OUTLINE1_KEY, &value) && value >= 0 && value < COOP_OUTLINE_COLORS) {
        colors[0] = value;
    }
    if (configGetInt(&gGameConfig, COOP_CONFIG_KEY, COOP_CONFIG_OUTLINE2_KEY, &value) && value >= 0 && value < COOP_OUTLINE_COLORS) {
        colors[1] = value;
    }
}

void coop_set_outline_colors(int playerIndex, const int colors[2])
{
    if (playerIndex >= 0 && playerIndex < COOP_MAX_PLAYERS) {
        for (int player = 0; player < 2; player++) {
            int color = colors[player];
            coop_outline_for[playerIndex][player] = color >= 0 && color < COOP_OUTLINE_COLORS ? color : player;
        }
    }
}

int coop_viewer_outline_color(int player)
{
    PlayerState* viewer = coop_viewer != nullptr ? coop_viewer : coop_controlled_player();
    int index = viewer != nullptr ? (int)(viewer - coop_players) : 0;
    if (index < 0 || index >= COOP_MAX_PLAYERS) {
        index = 0;
    }
    return _colorTable[coop_outline_rgb[coop_outline_for[index][player != 0 ? 1 : 0]]];
}





static int coop_write_player(File* stream, PlayerState* player)
{
    Proto* proto;
    if (protoGetProto(player->obj->pid, &proto) == -1) return -1;

    if (fileWriteInt32(stream, player->obj->pid) == -1) return -1;
    if (fileWriteInt32List(stream, player->pcStats, PC_STAT_COUNT) == -1) return -1;
    if (fileWriteInt32List(stream, player->perkRanks, PERK_COUNT) == -1) return -1;
    if (fileWriteInt32List(stream, player->traits, TRAITS_MAX_SELECTED_COUNT) == -1) return -1;
    if (fileWriteInt32List(stream, player->taggedSkills, NUM_TAGGED_SKILLS) == -1) return -1;
    if (fileWriteInt32List(stream, &(player->skillUses[0][0]), SKILL_COUNT * SKILLS_MAX_USES_PER_DAY) == -1) return -1;
    if (fileWrite(player->name, DUDE_NAME_MAX_LENGTH, 1, stream) != 1) return -1;
    if (fileWriteInt32List(stream, player->killCounts, KILL_TYPE_COUNT) == -1) return -1;
    if (fileWriteInt32(stream, player->sneakWorking) == -1) return -1;
    if (fileWriteInt32(stream, player->oldRadLevel) == -1) return -1;
    if (fileWriteInt32(stream, player->lastLevel) == -1) return -1;
    if (fileWriteUInt8(stream, player->freePerk) == -1) return -1;
    if (fileWriteInt32(stream, player->currentHand) == -1) return -1;
    if (fileWriteInt32(stream, player == coop_active ? _art_vault_guy_num : player->vaultGuyNum) == -1) return -1;
    if (fileWriteInt32List(stream, player->addictions, COOP_ADDICTION_COUNT) == -1) return -1;
    if (fileWriteInt32(stream, proto->fid) == -1) return -1;
    if (protoCritterDataWrite(stream, &(proto->critter.data)) == -1) return -1;

    return 0;
}

static int coop_read_player(File* stream, PlayerState* player, int* pidPtr, CritterProtoData* protoData, int* protoFidPtr)
{
    if (fileReadInt32(stream, pidPtr) == -1) return -1;
    if (fileReadInt32List(stream, player->pcStats, PC_STAT_COUNT) == -1) return -1;
    if (fileReadInt32List(stream, player->perkRanks, PERK_COUNT) == -1) return -1;
    if (fileReadInt32List(stream, player->traits, TRAITS_MAX_SELECTED_COUNT) == -1) return -1;
    if (fileReadInt32List(stream, player->taggedSkills, NUM_TAGGED_SKILLS) == -1) return -1;
    if (fileReadInt32List(stream, &(player->skillUses[0][0]), SKILL_COUNT * SKILLS_MAX_USES_PER_DAY) == -1) return -1;
    if (fileRead(player->name, DUDE_NAME_MAX_LENGTH, 1, stream) != 1) return -1;
    if (fileReadInt32List(stream, player->killCounts, KILL_TYPE_COUNT) == -1) return -1;
    if (fileReadInt32(stream, &(player->sneakWorking)) == -1) return -1;
    if (fileReadInt32(stream, &(player->oldRadLevel)) == -1) return -1;
    if (fileReadInt32(stream, &(player->lastLevel)) == -1) return -1;
    if (fileReadUInt8(stream, &(player->freePerk)) == -1) return -1;
    if (fileReadInt32(stream, &(player->currentHand)) == -1) return -1;
    if (fileReadInt32(stream, &(player->vaultGuyNum)) == -1) return -1;
    if (fileReadInt32List(stream, player->addictions, COOP_ADDICTION_COUNT) == -1) return -1;
    if (fileReadInt32(stream, protoFidPtr) == -1) return -1;
    if (protoCritterDataRead(stream, protoData) == -1) return -1;

    return 0;
}

int coop_save(File* stream)
{
    if (coop_players_count <= 1) {
        return 0;
    }

    if (fileWriteInt32(stream, COOP_SAVE_MAGIC) == -1) return -1;
    if (fileWriteInt32(stream, COOP_SAVE_VERSION) == -1) return -1;
    if (fileWriteInt32(stream, coop_players_count - 1) == -1) return -1;

    for (int index = 1; index < coop_players_count; index++) {
        if (coop_write_player(stream, &(coop_players[index])) == -1) {
            return -1;
        }
    }

    // Player 2 gets a copy of the character as saved.
    coop_request_character_copy();

    return 0;
}

static Object* coop_find_critter_by_pid(int pid)
{
    for (Object* obj = objectFindFirst(); obj != nullptr; obj = objectFindNext()) {
        if (obj->pid == pid) {
            return obj;
        }
    }

    return nullptr;
}

int coop_load(File* stream)
{
    int magic;
    if (fileReadInt32(stream, &magic) == -1 || magic != COOP_SAVE_MAGIC) {
        // Single player or pre-co-op save.
        return 0;
    }

    int version;
    if (fileReadInt32(stream, &version) == -1) return -1;
    if (version < 1 || version > COOP_SAVE_VERSION) {
        debugPrint("\nCOOP: unsupported save version %d\n", version);
        return -1;
    }

    int extraPlayers;
    if (fileReadInt32(stream, &extraPlayers) == -1) return -1;
    if (extraPlayers < 0 || extraPlayers >= COOP_MAX_PLAYERS) {
        debugPrint("\nCOOP: bad player count %d in save\n", extraPlayers);
        return -1;
    }

    for (int index = 0; index < extraPlayers; index++) {
        PlayerState state;
        memset(&state, 0, sizeof(state));

        int pid;
        int protoFid;
        CritterProtoData protoData;
        if (coop_read_player(stream, &state, &pid, &protoData, &protoFid) == -1) {
            return -1;
        }

        if (pid != COOP_PLAYER2_PID) {
            debugPrint("\nCOOP: unexpected player pid %x in save\n", pid);
            return -1;
        }

        Object* obj = coop_find_critter_by_pid(pid);
        if (obj == nullptr) {
            debugPrint("\nCOOP: player %d's critter is missing from the map\n", index + 2);
            return -1;
        }

        protoPlayer2Reset();

        Proto* proto;
        protoGetProto(pid, &proto);
        proto->fid = protoFid;
        critterProtoDataCopy(&(proto->critter.data), &protoData);

        PlayerState* player = &(coop_players[coop_players_count++]);
        memcpy(player, &state, sizeof(*player));
        player->obj = obj;
        player->inventoryOwner = obj;
        player->inventoryPid = pid;

        ActivePlayerScope scope(player);
        critterUpdateDerivedStats(obj);

        debugPrint("\nCOOP: loaded player %d '%s'\n", index + 2, player->name);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// A copy of player 2's character that player 2 keeps, to bring into another
// game (see coop.h).

// Scripts of items are numbers in this game's script list: in another game
// they would mean something else. They stay out of the copy.
static void coop_character_scripts(Object* obj, std::vector<int>* saved, bool clear)
{
    if (clear) {
        saved->push_back(obj->sid);
        obj->sid = -1;
    } else {
        obj->sid = (*saved)[0];
        saved->erase(saved->begin());
    }

    Inventory* inventory = &(obj->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        coop_character_scripts(inventory->items[index].item, saved, clear);
    }
}

static bool coop_character_copy_requested = false;

void coop_request_character_copy()
{
    coop_character_copy_requested = true;
}

bool coop_take_character_copy_request()
{
    bool requested = coop_character_copy_requested;
    coop_character_copy_requested = false;
    return requested;
}

bool coop_export_player2(std::vector<unsigned char>* out)
{
    out->clear();
    if (coop_players_count < 2 || coop_players[1].obj == nullptr) {
        return false;
    }

    coop_sync_base();
    PlayerState* player = &(coop_players[1]);
    Object* obj = player->obj;

    File* stream = fileOpen(COOP_CHARACTER_TEMP, "wb");
    if (stream == nullptr) {
        return false;
    }

    std::vector<int> scripts;
    coop_character_scripts(obj, &scripts, true);
    int flags = obj->flags;
    obj->flags &= ~OBJECT_NO_SAVE;

    bool ok = fileWriteInt32(stream, COOP_CHARACTER_MAGIC) != -1
        && fileWriteInt32(stream, COOP_CHARACTER_VERSION) != -1
        && fileWrite(player->name, DUDE_NAME_MAX_LENGTH, 1, stream) == 1
        && fileWriteInt32(stream, player->pcStats[PC_STAT_LEVEL]) != -1
        && coop_write_player(stream, player) != -1
        && _obj_save_obj(stream, obj) != -1;

    obj->flags = flags;
    coop_character_scripts(obj, &scripts, false);
    fileClose(stream);

    if (ok) {
        stream = fileOpen(COOP_CHARACTER_TEMP, "rb");
        if (stream == nullptr) {
            ok = false;
        } else {
            long length = fileGetSize(stream);
            out->resize(length > 0 ? (size_t)length : 0);
            ok = length > 0 && fileRead(out->data(), 1, out->size(), stream) == out->size();
            fileClose(stream);
        }
    }

    if (!ok) {
        out->clear();
    }
    return ok;
}

bool coop_character_info(const std::vector<unsigned char>& data, std::string* name, int* level)
{
    if (data.size() < 12 + DUDE_NAME_MAX_LENGTH) {
        return false;
    }

    // The game writes big-endian ints.
    auto readInt = [&](size_t at) {
        return (int)((unsigned int)data[at] << 24 | (unsigned int)data[at + 1] << 16 | (unsigned int)data[at + 2] << 8 | (unsigned int)data[at + 3]);
    };
    if ((unsigned int)readInt(0) != COOP_CHARACTER_MAGIC || readInt(4) != COOP_CHARACTER_VERSION) {
        return false;
    }

    char text[DUDE_NAME_MAX_LENGTH + 1];
    memcpy(text, data.data() + 8, DUDE_NAME_MAX_LENGTH);
    text[DUDE_NAME_MAX_LENGTH] = '\0';
    *name = text;
    *level = readInt(8 + DUDE_NAME_MAX_LENGTH);
    return true;
}

// Gives `obj` and everything it carries new object ids of this game.
static void coop_character_new_ids(Object* obj)
{
    obj->id = scriptsNewObjectId();
    Inventory* inventory = &(obj->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        coop_character_new_ids(inventory->items[index].item);
    }
}

PlayerState* coop_import_player2(const std::vector<unsigned char>& data, std::string* error)
{
    std::string name;
    int level;
    if (!coop_character_info(data, &name, &level)) {
        *error = "not a Fallout 2 character file";
        return nullptr;
    }
    if (coop_players_count != 1 || gDude == nullptr || coop_scope_stack_depth != 0 || isInCombat()) {
        *error = "player 2 can't join now";
        return nullptr;
    }

    File* stream = fileOpen(COOP_CHARACTER_TEMP, "wb");
    if (stream == nullptr || fileWrite(data.data(), 1, data.size(), stream) != data.size()) {
        if (stream != nullptr) {
            fileClose(stream);
        }
        *error = "can't write a temporary file";
        return nullptr;
    }
    fileClose(stream);

    stream = fileOpen(COOP_CHARACTER_TEMP, "rb");
    if (stream == nullptr) {
        *error = "can't read the temporary file";
        return nullptr;
    }

    int magic;
    int version;
    char fileName[DUDE_NAME_MAX_LENGTH];
    int fileLevel;
    PlayerState state;
    memset(&state, 0, sizeof(state));
    int pid;
    int protoFid;
    CritterProtoData protoData;
    Object* obj = nullptr;
    bool ok = fileReadInt32(stream, &magic) != -1
        && fileReadInt32(stream, &version) != -1
        && fileRead(fileName, DUDE_NAME_MAX_LENGTH, 1, stream) == 1
        && fileReadInt32(stream, &fileLevel) != -1
        && coop_read_player(stream, &state, &pid, &protoData, &protoFid) != -1
        && pid == COOP_PLAYER2_PID
        && _obj_load_obj(stream, &obj, gElevation, nullptr) == 0;
    fileClose(stream);

    if (!ok || obj == nullptr || obj->pid != COOP_PLAYER2_PID) {
        *error = "the character file is damaged";
        return nullptr;
    }

    coop_sync_base();
    Object* leader = coop_players[0].obj;

    protoPlayer2Reset();
    Proto* proto;
    protoGetProto(pid, &proto);
    proto->fid = protoFid;
    critterProtoDataCopy(&(proto->critter.data), &protoData);

    // A fresh start in this game: new ids, no fight in progress.
    coop_character_new_ids(obj);
    obj->sid = -1;
    obj->flags &= ~(OBJECT_HIDDEN | OBJECT_NO_SAVE);
    obj->data.critter.combat.results &= ~(DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN | DAM_LOSE_TURN);
    obj->data.critter.combat.whoHitMe = nullptr;
    obj->data.critter.combat.whoHitMeCid = -1;
    if (_obj_connect(obj, leader->tile, leader->elevation, nullptr) == -1) {
        *error = "can't place the character";
        return nullptr;
    }

    PlayerState* player = &(coop_players[coop_players_count++]);
    memcpy(player, &state, sizeof(*player));
    player->obj = obj;
    player->inventoryOwner = obj;
    player->inventoryPid = pid;

    obj->flags |= OBJECT_LIGHT_THRU;
    objectSetLight(obj, 4, 0x10000, nullptr);
    _obj_attempt_placement(obj, leader->tile, leader->elevation, 2);
    objectSetRotation(obj, leader->rotation, nullptr);
    if (partyMemberAdd(obj) == -1) {
        debugPrint("\nCOOP: could not add player 2 to the party\n");
    }

    {
        ActivePlayerScope scope(player);
        critterUpdateDerivedStats(obj);
    }

    tileWindowRefresh();
    coop_display_message("%s joins the game with their own character (level %d).", player->name, level);
    debugPrint("\nCOOP: player 2 '%s' (level %d) imported from their character file\n", player->name, level);
    return player;
}

} // namespace fallout
