#ifndef FALLOUT_GAME_COOP_UPDATE_H_
#define FALLOUT_GAME_COOP_UPDATE_H_

#include <string>

namespace fallout {

// Self-update for the multiplayer menu's UPDATE (see coop_menu.cc).

// Remembers the command line, to restart the same way. Call at startup.
void coop_update_init(int argc, char** argv);

// Removes what an earlier update left behind (replaced files). Call at
// startup.
void coop_update_cleanup();

// Lower case hex SHA-256 of `data`.
std::string coop_update_sha256(const std::string& data);

// Unpacks the package at `packagePath` (.zip or .tar.gz, with the game's
// files in one top folder) over the game's folder. The running executable
// is renamed out of the way first, so this works on Windows too.
bool coop_update_install(const std::string& packagePath, std::string* error);

// Starts the game again with the same command line and exits.
[[noreturn]] void coop_update_restart();

// What the co-op server's latest.txt (key=value lines) offers this system.
typedef struct CoopUpdateLatest {
    std::string version;
    std::string notes;
    std::string url;
    std::string sha256;
} CoopUpdateLatest;

// Reads latest.txt: false when it has no version.
bool coop_update_parse_latest(const std::string& body, CoopUpdateLatest* latest);

// -1, 0 or 1 as version `a` is older than, the same as or newer than `b`
// ("1.18.1" style).
int coop_update_compare_versions(const std::string& a, const std::string& b);

// Downloads the package, checks it against its SHA-256 (when given), saves
// it next to the game and installs it (then call coop_update_restart()).
bool coop_update_download_install(const CoopUpdateLatest& latest, std::string* error);

} // namespace fallout

#endif /* FALLOUT_GAME_COOP_UPDATE_H_ */
