#ifndef FALLOUT_AUTOTEST_H_
#define FALLOUT_AUTOTEST_H_

namespace fallout {

// CE (co-op): a headless test harness. With `[autotest]script=<name>` on
// the command line the game runs that script instead of the main menu and
// writes autotest/result.txt (see tools/regress.sh).

#define AUTOTEST_CONFIG_KEY "autotest"
#define AUTOTEST_CONFIG_SCRIPT_KEY "script"

typedef int(AutotestNewGameProc)(char* mapFileName);
typedef int(AutotestCheckProc)();
typedef int(AutotestScriptProc)(AutotestNewGameProc* newGame);

bool autotest_requested();
void autotest_add_check(const char* name, AutotestCheckProc* proc);
void autotest_add_script(const char* name, AutotestScriptProc* proc);
int autotest_run(AutotestNewGameProc* newGame);
void autotest_log(const char* format, ...);
void autotest_fail(const char* format, ...);
void autotest_run_checks(const char* when);
bool autotest_dump_state(const char* fileName);
bool autotest_files_equal(const char* path1, const char* path2);
bool autotest_new_game(AutotestNewGameProc* newGame, const char* premade);

} // namespace fallout

#endif /* FALLOUT_AUTOTEST_H_ */
