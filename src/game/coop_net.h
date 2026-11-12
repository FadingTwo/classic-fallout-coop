#ifndef FALLOUT_GAME_COOP_NET_H_
#define FALLOUT_GAME_COOP_NET_H_

#include <string>
#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace fallout {

// Co-op over the network (see coop.h): player 1's machine hosts and runs the
// whole game; player 2's machine is a terminal that shows frames rendered by
// the host and sends its mouse and keyboard input back.
//
// fallout.cfg:
//   [coop]
//   enabled=1
//   mode=host          ; or "client"; absent = both players on one machine
//   host=192.168.1.10  ; client only
//   port=27015

#define COOP_CONFIG_MODE_KEY "mode"
#define COOP_CONFIG_HOST_KEY "host"
#define COOP_CONFIG_PORT_KEY "port"

#define COOP_NET_DEFAULT_PORT 27015

typedef enum CoopNetMode {
    COOP_NET_NONE,
    COOP_NET_HOST,
    COOP_NET_CLIENT,
} CoopNetMode;

typedef enum CoopNetMessageType {
    // client -> host: magic, protocol version, data checksum.
    COOP_NET_MSG_HELLO = 1,
    // host -> client: accepted flag, reason.
    COOP_NET_MSG_WELCOME = 2,
    // client -> host: input event.
    COOP_NET_MSG_INPUT = 3,
    // host -> client: screen update.
    COOP_NET_MSG_FRAME = 4,
    // host -> client: status line shown over the frame.
    COOP_NET_MSG_STATUS = 5,
    // host -> client: sound effect to play.
    COOP_NET_MSG_SOUND = 6,
    // either way: leaving.
    COOP_NET_MSG_BYE = 7,
    // Client -> host (since 1.5.7, HELLO flag 2): u32 frames received so
    // far, so the host never has more than a few frames on the way.
    COOP_NET_MSG_ACK = 8,
    // Host -> client (since 1.5.7, HELLO flag 4): a FRAME, compressed (u32
    // its size, then coop_net_compress data).
    COOP_NET_MSG_FRAME_PACKED = 9,
    // Either way (since 1.9): player 2's character, as coop_export_player2
    // makes it. Host -> client: a copy for player 2 to keep. Client -> host
    // (with HELLO flag 8): player 2 joins with it, if the game has no
    // player 2 yet.
    COOP_NET_MSG_CHARACTER = 10,
    // Host -> client (since 1.21, HELLO flag 32): the player's cursor, which
    // the client draws itself where its mouse is (it moves at once, not a
    // network trip later): u16 width, u16 height (0: none, it's on the
    // picture), u16 hotspot x, y, u8 transparent color, the pixels.
    COOP_NET_MSG_CURSOR = 11,
    // Host -> client (since 1.21, HELLO flag 32): u32 input messages the
    // host has acted on, before the frame that shows it (the client
    // measures how long its clicks take to show).
    COOP_NET_MSG_INPUT_SEEN = 12,
    // Client -> host (1.22): u8 menu (COOP_LOCAL_MENU_*): the player opens
    // it on their own computer. The host answers with CHARACTER, WORLD and
    // LOCAL_OK, and sends no pictures until LOCAL_DONE.
    COOP_NET_MSG_LOCAL_MENU = 13,
    // Host -> client: u32 game time, u32 count, the global variables.
    COOP_NET_MSG_WORLD = 14,
    // Host -> client: u8 1 (go ahead) or 0 (not now: the key goes to the
    // host as before).
    COOP_NET_MSG_LOCAL_OK = 15,
    // Client -> host: the menu closed (then what it changed, 1.22 step 2);
    // a whole picture follows.
    COOP_NET_MSG_LOCAL_DONE = 16,
    // Host -> client: close it now (a fight began).
    COOP_NET_MSG_LOCAL_CLOSE = 17,
    // Host -> client (since 1.22.1, HELLO flag 64): the player's loot screen
    // runs on their computer: u32 game time, u32 count, the global
    // variables, u32 size + their character, u32 size + the container.
    // LOCAL_DONE answers as for the inventory (with the loot's moves).
    COOP_NET_MSG_LOCAL_LOOT = 18,
} CoopNetMessageType;

// The menus a client runs itself (COOP_NET_MSG_LOCAL_MENU).
#define COOP_LOCAL_MENU_CHARACTER 1
#define COOP_LOCAL_MENU_PIPBOY 2
#define COOP_LOCAL_MENU_INVENTORY 3
#define COOP_LOCAL_MENU_LOOT 4

typedef enum CoopNetInputKind {
    // x, y: absolute screen position; value: mouse buttons (MOUSE_STATE_*).
    COOP_NET_INPUT_MOUSE = 0,
    // value: game key code (KEY_*).
    COOP_NET_INPUT_KEY = 1,
} CoopNetInputKind;

typedef struct CoopNetInput {
    int kind;
    int x;
    int y;
    int value;
    // Which input message this is (counted from 1), and when it came.
    unsigned int number;
    unsigned int receivedAt;
} CoopNetInput;

CoopNetMode coop_net_mode();

// For this session (the multiplayer menu); not saved.
void coop_net_set_mode(CoopNetMode mode);
void coop_net_set_host(const char* host);
int coop_net_port();
// For one connection (a game picked in the server browser).
void coop_net_set_port(int port);

// Why the last connection attempt failed.
const char* coop_net_last_error();

// This computer's LAN addresses, comma separated ("" if unknown).
void coop_net_local_addresses(char* buffer, size_t size);

// Reads the [coop] network settings. Called by coop_init().
void coop_net_init();
void coop_net_exit();

// How much of one CPU core the game used in a co-op session: mark at its
// start, report (to the debug log) at its end, as `who` ("host", "player").
// The next picture sent to the selected player: the map part (x, y,
// width, height) moved by (dx, dy) pixels since their last one (their
// camera moved). Their game moves its copy the same way, so walking sends
// only what's new instead of the whole map.
void coop_net_set_frame_shift(int dx, int dy, int x, int y, int width, int height);
void coop_net_cpu_mark();
void coop_net_cpu_report(const char* who);

// Checksum over the game data both machines must share (MASTER.DAT,
// CRITTER.DAT, the patches directory without saves, and the language).
uint64_t coop_net_data_checksum();

// Host: starts listening. Returns false on failure.
// Host: whether another port may be used when the set one is taken
// (online, where players come through the relay). Back to the set port
// when hosting ends.
void coop_net_set_any_port_ok(bool ok);

bool coop_net_host_start();

// Host: accepts a client, completes the handshake and receives input.
// Cheap to call every frame.
void coop_net_host_pump();

// Host: true once a client has passed the handshake.
// The client (player `slot` + 1) the host functions act on: whether it's
// connected, its input, frames, status and character (default 1, player 2).
// Sounds go to every client.
void coop_net_select(int slot);
int coop_net_selected();
// How many clients may join (1 to COOP_MAX_PLAYERS - 1), and how many are
// connected.
void coop_net_set_max_clients(int count);
int coop_net_max_clients_value();
int coop_net_clients_connected();

bool coop_net_client_connected();

// Host: how many times a client has joined so far.
unsigned int coop_net_client_joins();

// Host: whether player 2 said they bring their own character (HELLO flag
// 8), and that character once it has arrived (taken once).
bool coop_net_client_brings_character();
bool coop_net_take_client_character(std::vector<unsigned char>* data);

// Host: sends player 2 a copy of their character to keep.
void coop_net_send_character(const std::vector<unsigned char>& data);

// Client: join with this character (empty: make a new one, as before).
void coop_net_set_bring_character(const std::vector<unsigned char>& data);

// Client: where the copy of player 2's character is kept.
#define COOP_NET_CHARACTER_FILE "coop-character.dat"

// Client: the local menu open now (COOP_LOCAL_MENU_*, 0: none), and how
// many ran (tests).
int coop_net_client_local_menu_open();
unsigned int coop_net_client_local_menus_run();

// Host: menus of player `slot` + 1 run on their own computer (tests).
unsigned int coop_net_local_menus_served(int slot);

// Host, for the selected client: a menu it asked to run itself (0: none;
// taken), the answer (`character` NULL: not now; else their character, the
// world's global variables and time, and for the Pip-Boy the automaps), the
// menu open there (0: none), telling
// it to close (once), and what a closed one changed (taken).
int coop_net_take_local_request();
void coop_net_local_answer(int menu, const std::vector<unsigned char>* character, const int* globals, int count, int time, const std::vector<unsigned char>* automap);
int coop_net_local_menu();
void coop_net_local_close();
bool coop_net_take_local_done(std::vector<unsigned char>* changes);
// Host, for the selected client: whether its loot screens can run there now,
// and running one there.
bool coop_net_local_loot_ok();
void coop_net_local_loot(const std::vector<unsigned char>& character, const int* globals, int count, int time, const std::vector<unsigned char>& container);

// Client: the status line shown over the picture ("" when it's our own
// view or turn; tests).
const char* coop_net_client_status();

// Host: next input event received from the client, if any.
bool coop_net_next_input(CoopNetInput* input);

// Host: the input event after that one, if any (left in the queue).
bool coop_net_peek_input(CoopNetInput* input);

// Host: sends the current screen (8-bit, with palette) to the client.
void coop_net_send_screen(const unsigned char* pixels, int pitch, int width, int height, const unsigned char* palette);

// Host: sends the screen as currently composed.
void coop_net_send_current_screen();

// Host: whether the selected client draws its player's cursor itself
// (since 1.21): their frames then come without it.
bool coop_net_client_draws_cursor();

// Host: the selected client's cursor shape (as mouse_get_shape gives it;
// width 0: none, the cursor on the frames is someone else's). Sent when it
// changed.
void coop_net_send_cursor(const unsigned char* shape, int width, int height, int full, int hotx, int hoty, int trans);

// Whether the selected client takes a frame now: frames wait for its
// acknowledgements (only a few on the way at once) and for a long send
// queue to empty. A view drawn while it can't is drawn for nothing.
bool coop_net_can_send_frame();

// "fallout1" or "fallout2": which game this is, for the co-op server (its
// relay and listing keep the two apart) and the game search.
const char* coop_net_game_name();

// Host: a sound for the client to play (GSOUND_EVENT_*, see gsound.h).
void coop_net_send_sound(int event, const char* name, int a, int b, int c);
// Client: music and speech from the host that started here, and that didn't.
int coop_net_client_streams_started();
int coop_net_client_streams_failed();

// Host: status line the client shows over the frames ("" for none).
void coop_net_send_status(const char* text);

// Client: connects, then shows the host's frames and sends input until
// the host leaves or the window is closed. Returns a process exit code.
int coop_net_client_run();

// Relay through the co-op server ("host:port"): no open port needed.
// Host: opens a room and gets its code ("" without one); client: joins a
// code instead of connecting to [coop] host ("" for a direct connection).
void coop_net_set_relay_server(const std::string& server);
bool coop_net_relay_host_start(std::string* error, const std::string& wanted = std::string());
void coop_net_relay_host_stop();
const char* coop_net_relay_code();
void coop_net_set_relay_join(const std::string& code);
// A game code typed by a player (ABC123, ABC-123): true, and the code.
bool coop_net_parse_code(const char* text, std::string* code);

// Tests: packs and unpacks `data`; the packed size, or -1 if it didn't
// come back the same.
int coop_net_pack_selftest(const unsigned char* data, size_t size);

// Client: the size of player 1's frames (false before the first), and a
// point of a frame to this screen's coordinates (frames are scaled to fit
// when the screen sizes differ).
bool coop_net_client_frame_size(int* width, int* height);
// Client: the host's own screen size (frames may be scaled down to fit
// this screen); false when the host didn't say.
bool coop_net_client_host_size(int* width, int* height);
void coop_net_client_frame_to_window(int* x, int* y);

// Client: leave at the next frame.
void coop_net_client_request_exit();

// Client, for tests: called when the host has left, before returning.
void coop_net_set_client_exit_hook(void (*proc)());

} // namespace fallout

#endif /* FALLOUT_GAME_COOP_NET_H_ */
