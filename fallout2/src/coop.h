#ifndef FALLOUT_COOP_H_
#define FALLOUT_COOP_H_

#include <string>
#include <vector>

#include "critter.h"
#include "db.h"
#include "interface.h"
#include "inventory.h"
#include "obj_types.h"
#include "perk_defs.h"
#include "proto_types.h"
#include "skill.h"
#include "stat_defs.h"
#include "trait_defs.h"

namespace fallout {

// CE: two-player co-op (ported from Classic Fallout Co-op for Fallout 1).
//
// Player 1 is the game's own player (`gDude`, proto 0x1000000). Player 2 is
// a second player character with its own critter and proto
// (COOP_PLAYER2_PID). Everything the engine keeps "for the player" in
// module globals lives in a PlayerState per player; the modules read and
// write the active player's copy. `ActivePlayerScope` makes another player
// active (and `gDude` their critter) for the code inside it.

// The co-op version (shared with Classic Fallout Co-op for Fallout 1).
#define COOP_VERSION "1.12"

#define COOP_MAX_PLAYERS 2

// Player 2's critter proto (not a proto file; see protoGetProto()).
#define COOP_PLAYER2_PID 0x01003FFF

// Drug addiction global variables, per player.
#define COOP_ADDICTION_COUNT 8

#define COOP_CONFIG_KEY "coop"
#define COOP_CONFIG_ENABLED_KEY "enabled"
#define COOP_CONFIG_TURN_TIME_KEY "turn_time"
#define COOP_CONFIG_SERVER_KEY "server"
#define COOP_CONFIG_KNOCKOUT_KEY "knockout"
#define COOP_CONFIG_TALK_KEY "talk"
#define COOP_CONFIG_COLORBLIND_KEY "colorblind"
#define COOP_CONFIG_OUTLINE1_KEY "outline_player1"
#define COOP_CONFIG_OUTLINE2_KEY "outline_player2"

// Everything that makes a player character "theirs". Before co-op this lived
// in per-module globals implicitly owned by `gDude`; those modules now read
// and write the active player's copy.
//
// Karma, reputation and town reputations are deliberately NOT here: they are
// shared between players.
typedef struct PlayerState {
    // The player's critter. For player 1 this mirrors `gDude` while no
    // `ActivePlayerScope` is active.
    Object* obj;

    // stat.cc - PC_STAT_REPUTATION and PC_STAT_KARMA slots are unused.
    int pcStats[PC_STAT_COUNT];

    // perk.cc - player 2's ranks (player 1's stay in the party member list,
    // where saves keep them).
    int perkRanks[PERK_COUNT];

    // trait.cc
    int traits[TRAITS_MAX_SELECTED_COUNT];

    // skill.cc
    int taggedSkills[NUM_TAGGED_SKILLS];
    int skillUses[SKILL_COUNT][SKILLS_MAX_USES_PER_DAY];

    // critter.cc
    char name[DUDE_NAME_MAX_LENGTH];
    int killCounts[KILL_TYPE_COUNT];
    int sneakWorking;
    int oldRadLevel;

    // character_editor.cc
    int lastLevel;
    unsigned char freePerk;

    // interface.cc
    int currentHand;
    InterfaceItemState hands[HAND_COUNT];

    // item.cc - drug withdrawal tracking.
    int withdrawalOnset;
    Object* withdrawalObj;
    int withdrawalGvar;

    // art.cc - the player's own look when not wearing armor.
    int vaultGuyNum;

    // inventory.cc - whose inventory the inventory screen works on.
    Object* inventoryOwner;
    int inventoryPid;

    // Addiction global variables while this player is not active.
    int addictions[COOP_ADDICTION_COUNT];
} PlayerState;

extern PlayerState* coop_active;

// Makes `player` the active player for its lifetime (and `gDude` their
// critter). Nests; NULL or the active player change nothing.
class ActivePlayerScope {
public:
    explicit ActivePlayerScope(PlayerState* player);
    ~ActivePlayerScope();

    ActivePlayerScope(const ActivePlayerScope&) = delete;
    ActivePlayerScope& operator=(const ActivePlayerScope&) = delete;
};

void coop_init();
bool coop_is_enabled();
void coop_set_enabled(bool enabled);

// The player whose data the per-player globals currently refer to.
inline PlayerState* coop_active_player()
{
    return coop_active;
}

int coop_player_count();
PlayerState* coop_player(int index);

// The player state owned by `obj`, or NULL if `obj` is not a player
// character. In single player this is exactly `obj == gDude`.
PlayerState* coop_player_of(Object* obj);

// The player whose critter has proto id `pid`, or NULL.
PlayerState* coop_player_by_pid(int pid);

// True when player 1 is the active player.
bool coop_primary_is_active();

// The player in control outside of any scope.
PlayerState* coop_controlled_player();

// Hands local control to `player` (outside of scopes). False if refused.
bool coop_set_controlled(PlayerState* player);

// Adds player 2 next to player 1, made from the premade character file
// `premadePath` (e.g. "premade\\stealth.gcd"). NULL on failure.
PlayerState* coop_add_player(const char* premadePath);

// Forgets player 2 and returns control to player 1 (game reset).
void coop_reset();

// Lets player 2 join through the character creation screen, run in player
// 2's scope. NULL if cancelled.
PlayerState* coop_create_player2();

// F9 on one PC: the first press lets player 2 join (character creation),
// later presses switch local control between the players.
void coop_handle_switch_key();

// True when any player character has one of the DAM_* `results` (e.g.
// DAM_DEAD: the game is lost when any player dies).
bool coop_any_player_has(int results);

// "Global" perks and traits (e.g. Jinxed) apply when any player has them.
bool coop_any_player_has_perk(int perk);
bool coop_any_player_has_trait(int trait);

// Combat: [coop]turn_time (seconds per human turn, 0 = no limit).
bool coop_turn_time_expired(unsigned int turnStart);
unsigned int coop_turn_time_limit();

// Tests: plays a player's combat turn instead of waiting for input.
// Returns true when it did.
typedef bool(CoopTurnHook)(Object* player);
void coop_set_turn_hook(CoopTurnHook* hook);
bool coop_run_turn_hook(Object* player);

// The length of `player`'s own part of their last combat turn (for the
// turn timer's tests).
void coop_note_turn_length(Object* player, unsigned int ms);
unsigned int coop_longest_turn(int index);

// Handles pending requests (barter); called by the main game loop.
void coop_process_requests();

// [coop]knockout=1: a player who would die is knocked out instead while
// another player is still up, can't be hurt while out, and wakes up by
// themselves or when healed. Off by default (Fallout's rules).
bool coop_knockout_enabled();
void coop_knockout_check(Object* critter, int* damage, int* flags);
void coop_knockout_healed(Object* critter);

// The main loop's game over check: a player died, or is knocked out (as in
// Fallout, unless KNOCKOUT is on: then only when nobody is left up).
bool coop_game_lost();

// [coop]knockout (MULTIPLAYER, MORE, KNOCKOUT) and [coop]talk (TALK).
bool coop_knockout_setting();
void coop_set_knockout(bool on);
bool coop_talk_setting();
void coop_set_talk(bool on);

// The player a starting conversation belongs to: player 1, unless another
// player started it (set around gameDialogEnter()).
PlayerState* coop_dialog_player();
void coop_set_dialog_player(PlayerState* player);

// Name and level in a character copy (see coop_export_player2).
bool coop_character_info(const std::vector<unsigned char>& data, std::string* name, int* level);

// The player whose view is being drawn: the controlled player, except
// while the co-op host renders player 2's view (nullptr = controlled).
void coop_set_viewer(PlayerState* player);

// Outline colors, chosen by each player for their own screen: player 1's
// from [coop] outline_player1/outline_player2, player 2's sent by their
// client. Without those, [coop] colorblind=1 means white and orange.
#define COOP_OUTLINE_COLORS 6
const char* coop_outline_color_name(int color);
void coop_outline_colors_from_config(int colors[2]);
void coop_set_outline_colors(int playerIndex, const int colors[2]);
// The palette color the viewer draws `player`'s (0 or 1) outline with, or
// -1 when `obj` gets no co-op outline (see coop_outline_player()).
int coop_viewer_outline_color(int player);
// Which player's outline `obj` gets on the viewer's screen (0 or 1), or -1
// for none (not a player, the viewer, hidden or dead).
int coop_outline_player(Object* obj);

// Player 2's own copy of their character, to bring into another game.
// (Fallout 2: not yet; no copies are made.)
bool coop_export_player2(std::vector<unsigned char>* out);
PlayerState* coop_import_player2(const std::vector<unsigned char>& data, std::string* error);
void coop_request_character_copy();
bool coop_take_character_copy_request();

// Save games: the other players follow all regular data (the last save
// handler); a save without them reads as single player.
int coop_save(File* stream);
int coop_load(File* stream);

} // namespace fallout

#endif /* FALLOUT_COOP_H_ */
