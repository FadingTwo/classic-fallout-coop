#include "autotest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "critter.h"
#include "debug.h"
#include "game.h"
#include "game_config.h"
#include "game_vars.h"
#include "input.h"
#include "item.h"
#include "loadsave.h"
#include "object.h"
#include "perk.h"
#include "platform_compat.h"
#include "proto.h"
#include "random.h"
#include "scripts.h"
#include "skill.h"
#include "stat.h"
#include "tile.h"
#include "trait.h"

namespace fallout {

#define AUTOTEST_DIR "autotest"
#define AUTOTEST_MAX_CHECKS 8
#define AUTOTEST_MAX_SCRIPTS 128
#define AUTOTEST_SEED 0x0C00F00D

// Quick save slots used by the harness (0-based; SLOT09 and SLOT10).
#define AUTOTEST_SLOT_A 8
#define AUTOTEST_SLOT_B 9

typedef struct AutotestCheck {
    const char* name;
    AutotestCheckProc* proc;
} AutotestCheck;

typedef struct AutotestScript {
    const char* name;
    AutotestScriptProc* proc;
} AutotestScript;

static int autotest_regress(AutotestNewGameProc* newGame);
static int autotest_load(AutotestNewGameProc* newGame);

static AutotestCheck autotest_checks[AUTOTEST_MAX_CHECKS];
static int autotest_checks_count = 0;
static AutotestScript autotest_scripts[AUTOTEST_MAX_SCRIPTS];
static int autotest_scripts_count = 0;
static int autotest_failures = 0;
static FILE* autotest_result = NULL;

bool autotest_requested()
{
    char* script;
    return configGetString(&gGameConfig, AUTOTEST_CONFIG_KEY, AUTOTEST_CONFIG_SCRIPT_KEY, &script)
        && script[0] != '\0'
        && strcmp(script, "0") != 0;
}

void autotest_add_check(const char* name, AutotestCheckProc* proc)
{
    if (autotest_checks_count < AUTOTEST_MAX_CHECKS) {
        autotest_checks[autotest_checks_count].name = name;
        autotest_checks[autotest_checks_count].proc = proc;
        autotest_checks_count++;
    }
}

void autotest_add_script(const char* name, AutotestScriptProc* proc)
{
    if (autotest_scripts_count < AUTOTEST_MAX_SCRIPTS) {
        autotest_scripts[autotest_scripts_count].name = name;
        autotest_scripts[autotest_scripts_count].proc = proc;
        autotest_scripts_count++;
    } else {
        // Was silent: the scripts past the limit just didn't exist.
        debugPrint("\nautotest: too many scripts, '%s' left out (raise AUTOTEST_MAX_SCRIPTS)\n", name);
    }
}

int autotest_run(AutotestNewGameProc* newGame)
{
    char* script;
    configGetString(&gGameConfig, AUTOTEST_CONFIG_KEY, AUTOTEST_CONFIG_SCRIPT_KEY, &script);

    compat_mkdir(AUTOTEST_DIR);
    autotest_result = compat_fopen(AUTOTEST_DIR "/result.txt", "wt");

    autotest_failures = 0;
    autotest_log("script: %s", script);

    AutotestScriptProc* proc = NULL;
    if (strcmp(script, "regress") == 0) {
        proc = autotest_regress;
    } else if (strcmp(script, "load") == 0) {
        proc = autotest_load;
    } else {
        for (int index = 0; index < autotest_scripts_count; index++) {
            if (strcmp(script, autotest_scripts[index].name) == 0) {
                proc = autotest_scripts[index].proc;
            }
        }
    }

    if (proc != NULL) {
        proc(newGame);
    } else {
        autotest_fail("unknown script '%s'", script);
    }

    autotest_log("result: %s (%d failures)", autotest_failures == 0 ? "PASS" : "FAIL", autotest_failures);

    if (autotest_result != NULL) {
        fclose(autotest_result);
        autotest_result = NULL;
    }

    return autotest_failures == 0 ? 0 : 1;
}

// Deterministic single-player run: new game, scripted state changes,
// save -> load round trip, second save. Outputs are meant to be compared
// between builds (state dumps and save files must be byte-identical when
// behavior is unchanged).
bool autotest_new_game(AutotestNewGameProc* newGame, const char* premade)
{
    randomSeedPrerandom(AUTOTEST_SEED);

    if (premade != NULL) {
        _proto_dude_init(premade);
    }

    char map[] = "artemple.map";
    if (newGame(map) != 0) {
        autotest_fail("new game failed");
        return false;
    }

    return true;
}

static int autotest_regress(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, "premade\\combat.gcd")) {
        return -1;
    }

    // Exercise the per-player state: level ups, perks, skills, kills,
    // inventory and hit points.
    pcAddExperience(6500);
    autotest_log("level after xp: %d", pcGetStat(PC_STAT_LEVEL));

    // The first three perks the character qualifies for.
    int perksAdded = 0;
    for (int perk = 0; perk < PERK_COUNT && perksAdded < 3; perk++) {
        if (perkAdd(gDude, perk) == 0) {
            autotest_log("perk added: %d", perk);
            perksAdded++;
        }
    }

    if (perksAdded == 0) {
        autotest_fail("no perk could be added");
    }

    for (int index = 0; index < 3; index++) {
        skillAdd(gDude, SKILL_SMALL_GUNS);
    }
    skillAdd(gDude, SKILL_LOCKPICK);

    killsIncByType(KILL_TYPE_RAT);
    killsIncByType(KILL_TYPE_RAT);
    killsIncByType(KILL_TYPE_MAN);

    Object* stimpak;
    if (objectCreateWithPid(&stimpak, PROTO_ID_STIMPACK) == 0) {
        if (itemAdd(gDude, stimpak, 3) == 0) {
            _obj_disconnect(stimpak, NULL);
        } else {
            autotest_fail("could not add stimpak");
        }
    } else {
        autotest_fail("could not create stimpak");
    }

    critterAdjustHitPoints(gDude, -5);

    autotest_run_checks("after setup");

    if (!autotest_dump_state("state1.txt")) {
        return -1;
    }

    lsgSetQuickSlot(AUTOTEST_SLOT_A);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save to slot A failed");
        return -1;
    }

    lsgSetQuickSlot(AUTOTEST_SLOT_A);
    if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("load from slot A failed");
        return -1;
    }

    autotest_run_checks("after load");

    if (!autotest_dump_state("state2.txt")) {
        return -1;
    }

    if (!autotest_files_equal(AUTOTEST_DIR "/state1.txt", AUTOTEST_DIR "/state2.txt")) {
        autotest_fail("state differs after save/load round trip");
    }

    lsgSetQuickSlot(AUTOTEST_SLOT_B);
    if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("save to slot B failed");
        return -1;
    }

    tileWindowRefresh();
    takeScreenshot();

    return 0;
}

// Loads the save `regress` left in slot A (possibly made by another build)
// and dumps the state, to be compared with that run's state1.txt.
static int autotest_load(AutotestNewGameProc* newGame)
{
    if (!autotest_new_game(newGame, NULL)) {
        return -1;
    }

    lsgSetQuickSlot(AUTOTEST_SLOT_A);
    if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
        autotest_fail("load from slot A failed");
        return -1;
    }

    autotest_run_checks("after load");

    if (!autotest_dump_state("state_loaded.txt")) {
        return -1;
    }

    return 0;
}

void autotest_run_checks(const char* when)
{
    for (int index = 0; index < autotest_checks_count; index++) {
        int failures = autotest_checks[index].proc();
        if (failures != 0) {
            autotest_fail("%s %s: %d failures", autotest_checks[index].name, when, failures);
        } else {
            autotest_log("%s %s: ok", autotest_checks[index].name, when);
        }
    }
}

static void autotest_vlog(const char* prefix, const char* format, va_list args)
{
    char message[512];
    vsnprintf(message, sizeof(message), format, args);

    debugPrint("\nAUTOTEST: %s%s\n", prefix, message);
    if (autotest_result != NULL) {
        fprintf(autotest_result, "%s%s\n", prefix, message);
        fflush(autotest_result);
    }
}

void autotest_log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    autotest_vlog("", format, args);
    va_end(args);
}

void autotest_fail(const char* format, ...)
{
    autotest_failures++;

    va_list args;
    va_start(args, format);
    autotest_vlog("FAIL: ", format, args);
    va_end(args);
}

// Writes everything that defines the player character in a stable text
// format.
bool autotest_dump_state(const char* fileName)
{
    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", AUTOTEST_DIR, fileName);

    FILE* stream = compat_fopen(path, "wt");
    if (stream == NULL) {
        autotest_fail("cannot write %s", path);
        return false;
    }

    fprintf(stream, "name %s\n", critterGetName(gDude));
    fprintf(stream, "tile %d elevation %d rotation %d fid %d\n", gDude->tile, gDude->elevation, gDude->rotation, gDude->fid);
    fprintf(stream, "game_time %d\n", gameTimeGetTime());
    fprintf(stream, "reputation_gvar %d\n", gameGetGlobalVar(GVAR_PLAYER_REPUTATION));
    fprintf(stream, "addictions %d %d %d %d %d %d\n",
        gameGetGlobalVar(GVAR_NUKA_COLA_ADDICT), gameGetGlobalVar(GVAR_BUFF_OUT_ADDICT), gameGetGlobalVar(GVAR_MENTATS_ADDICT),
        gameGetGlobalVar(GVAR_PSYCHO_ADDICT), gameGetGlobalVar(GVAR_RADAWAY_ADDICT), gameGetGlobalVar(GVAR_ALCOHOL_ADDICT));

    for (int pcStat = 0; pcStat < PC_STAT_COUNT; pcStat++) {
        fprintf(stream, "pc_stat %d %d\n", pcStat, pcGetStat(pcStat));
    }

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        fprintf(stream, "stat %d level %d", stat, critterGetStat(gDude, stat));
        if (stat < SAVEABLE_STAT_COUNT) {
            fprintf(stream, " base %d bonus %d", critterGetBaseStat(gDude, stat), critterGetBonusStat(gDude, stat));
        }
        fprintf(stream, "\n");
    }

    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        fprintf(stream, "skill %d level %d points %d\n", skill, skillGetValue(gDude, skill), skillGetBaseValue(gDude, skill));
    }

    int tags[NUM_TAGGED_SKILLS];
    skillsGetTagged(tags, NUM_TAGGED_SKILLS);
    fprintf(stream, "tags %d %d %d %d\n", tags[0], tags[1], tags[2], tags[3]);

    int trait1;
    int trait2;
    traitsGetSelected(&trait1, &trait2);
    fprintf(stream, "traits %d %d\n", trait1, trait2);

    for (int perk = 0; perk < PERK_COUNT; perk++) {
        if (perkGetRank(gDude, perk) != 0) {
            fprintf(stream, "perk %d %d\n", perk, perkGetRank(gDude, perk));
        }
    }

    for (int killType = 0; killType < KILL_TYPE_COUNT; killType++) {
        fprintf(stream, "kills %d %d\n", killType, killsGetByType(killType));
    }

    for (int flag = 0; flag < 8; flag++) {
        fprintf(stream, "pc_flag %d %d\n", flag, dudeHasState(flag) ? 1 : 0);
    }

    Inventory* inventory = &(gDude->data.inventory);
    fprintf(stream, "inventory %d\n", inventory->length);
    for (int index = 0; index < inventory->length; index++) {
        fprintf(stream, "item %d x%d\n", inventory->items[index].item->pid, inventory->items[index].quantity);
    }

    fclose(stream);
    return true;
}

bool autotest_files_equal(const char* path1, const char* path2)
{
    FILE* stream1 = compat_fopen(path1, "rb");
    FILE* stream2 = compat_fopen(path2, "rb");
    bool equal = stream1 != NULL && stream2 != NULL;

    while (equal) {
        int ch1 = fgetc(stream1);
        int ch2 = fgetc(stream2);
        if (ch1 != ch2) {
            equal = false;
        }
        if (ch1 == EOF || ch2 == EOF) {
            break;
        }
    }

    if (stream1 != NULL) {
        fclose(stream1);
    }

    if (stream2 != NULL) {
        fclose(stream2);
    }

    return equal;
}

} // namespace fallout
