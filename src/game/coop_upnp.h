#ifndef FALLOUT_GAME_COOP_UPNP_H_
#define FALLOUT_GAME_COOP_UPNP_H_

#include <string>

namespace fallout {

// Asks the router (UPnP, an "internet gateway device") to forward TCP port
// `port` from the internet to this machine, so players can connect to
// player 1 directly instead of through the co-op server's relay. Blocking:
// a few seconds at most (run it off the main thread). Returns the port
// opened on the router (usually `port`; another when that one is taken),
// or 0 when no router answered or it said no. `log` gets what happened.
//
// COOP_UPNP_SSDP=host:port asks that address instead of the local
// network's (tests: tools/fake_igd.py).
int coop_upnp_map(int port, std::string* log);

// Opens the same port again before the router lets it go (it is opened for
// an hour at a time). Blocking.
bool coop_upnp_refresh(std::string* log);

// Closes what coop_upnp_map opened. Blocking (a second at most).
void coop_upnp_unmap();

} // namespace fallout

#endif /* FALLOUT_GAME_COOP_UPNP_H_ */
