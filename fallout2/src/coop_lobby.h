#ifndef FALLOUT_COOP_LOBBY_H_
#define FALLOUT_COOP_LOBBY_H_

#include <string>
#include <vector>

namespace fallout {

// Finding games: hosts answer searches on the LAN (UDP on the game's port)
// and can list themselves on the co-op server; JOIN GAME shows both.

// The player count a full game announces (games with room announce how
// many play).
#define COOP_LOBBY_FULL 9

typedef struct CoopLobbyGame {
    std::string address;
    int port;
    std::string name;
    bool lan;
    // Joined through the co-op server's relay with this code ("" for a
    // direct connection).
    std::string relay;
} CoopLobbyGame;

// Host side. `server` is the co-op server ("" for none); with `listPublic`
// the game is announced there (the host's internet address becomes
// visible to others).
void coop_lobby_host_start(const std::string& server, bool listPublic);

// Answers LAN searches and keeps the public listing fresh (in the
// background). Call often while hosting.
void coop_lobby_host_pump(const std::string& name, int players);

void coop_lobby_host_stop();

// Whether the running host is listed publicly.
bool coop_lobby_host_public();

// Client side: games on the LAN (about a second) and on `server`, only
// those this copy can join (same mod version and game data).
// `otherVersions` (if given) gets how many games were left out for
// another mod version or game data, `otherGame` how many for the other
// game (Fallout 2 for a Fallout 1 player, or the other way round).
std::vector<CoopLobbyGame> coop_lobby_find(const std::string& server, int* otherVersions = nullptr, int* otherGame = nullptr);

} // namespace fallout

#endif /* FALLOUT_COOP_LOBBY_H_ */
