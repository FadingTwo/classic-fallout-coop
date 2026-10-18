#include "game/coop_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <ifaddrs.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <SDL.h>

#include "game/config.h"
#include "game/coop.h"
#include "game/cycle.h"
#include "game/display.h"
#include "game/gconfig.h"
#include "game/gmouse.h"
#include "game/gsound.h"
#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

// "FOCP"
#define COOP_NET_MAGIC 0x46434F50

// Bumped whenever the wire format changes; both sides must match.
#define COOP_NET_PROTOCOL_VERSION 1

// Which game this is (sent in HELLO since 1.13.3): a Fallout 1 player can't
// join a Fallout 2 game.
#define COOP_NET_GAME 1

// Which game we say we are: this one, or ([coop]test_game=1 or 2, for
// tests) another.
static int coop_net_game_id()
{
    int value;
    if (config_get_value(&game_config, COOP_CONFIG_KEY, "test_game", &value) && (value == 1 || value == 2)) {
        return value;
    }
    return COOP_NET_GAME;
}

// Which version we say we are: this one, or ([coop]test_version=x, for
// tests) another.
static const char* coop_net_version()
{
    char* value;
    if (config_get_string(&game_config, COOP_CONFIG_KEY, "test_version", &value) && value[0] != '\0') {
        return value;
    }
    return COOP_VERSION;
}

// Whether version a ("1.15.3") is older than b; none counts as oldest.
static bool coop_net_version_older(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) {
        return a.empty() && !b.empty();
    }
    size_t at = 0;
    size_t bt = 0;
    while (at < a.size() || bt < b.size()) {
        long an = at < a.size() ? strtol(a.c_str() + at, NULL, 10) : 0;
        long bn = bt < b.size() ? strtol(b.c_str() + bt, NULL, 10) : 0;
        if (an != bn) {
            return an < bn;
        }
        at = a.find('.', at);
        at = at == std::string::npos ? a.size() : at + 1;
        bt = b.find('.', bt);
        bt = bt == std::string::npos ? b.size() : bt + 1;
    }
    return false;
}

const char* coop_net_game_name()
{
    return coop_net_game_id() == 1 ? "fallout1" : "fallout2";
}

// Messages: u32 length (of type + payload), u8 type, payload.
#define COOP_NET_HEADER_SIZE 5
#define COOP_NET_MAX_MESSAGE (4 * 1024 * 1024)

// Something is sent at least this often (ms) on a quiet connection.
#define COOP_NET_KEEPALIVE 30000

#define COOP_NET_HANDSHAKE_TIMEOUT 10000

// Sending to a peer that has gone must fail, not kill the process.
#if defined(MSG_NOSIGNAL)
#define COOP_SEND_FLAGS MSG_NOSIGNAL
#else
#define COOP_SEND_FLAGS 0
#endif

#ifdef _WIN32
typedef SOCKET CoopSocket;
#define COOP_INVALID_SOCKET INVALID_SOCKET
#define coop_close_socket closesocket
#else
typedef int CoopSocket;
#define COOP_INVALID_SOCKET (-1)
#define coop_close_socket close
#endif

// A message-framed, non-blocking TCP connection.
typedef struct CoopConnection {
    CoopSocket socket = COOP_INVALID_SOCKET;
    std::vector<unsigned char> in;
    std::vector<unsigned char> out;
    bool failed = false;
} CoopConnection;

typedef struct CoopMessage {
    int type;
    std::vector<unsigned char> payload;
} CoopMessage;

// Little-endian payload writer/reader.
class CoopWriter {
public:
    std::vector<unsigned char> data;

    void u8(unsigned int value) { data.push_back((unsigned char)value); }
    void u16(unsigned int value)
    {
        u8(value & 0xFF);
        u8((value >> 8) & 0xFF);
    }
    void u32(unsigned int value)
    {
        u16(value & 0xFFFF);
        u16((value >> 16) & 0xFFFF);
    }
    void u64(uint64_t value)
    {
        u32((unsigned int)(value & 0xFFFFFFFF));
        u32((unsigned int)(value >> 32));
    }
    void bytes(const void* src, size_t length)
    {
        const unsigned char* p = (const unsigned char*)src;
        data.insert(data.end(), p, p + length);
    }
    void str(const char* text)
    {
        size_t length = strlen(text);
        u16((unsigned int)length);
        bytes(text, length);
    }
};

class CoopReader {
public:
    CoopReader(const std::vector<unsigned char>& data)
        : data(data)
    {
    }

    bool ok() const { return !overrun; }
    bool atEnd() const { return pos >= data.size(); }

    unsigned int u8()
    {
        if (pos + 1 > data.size()) {
            overrun = true;
            return 0;
        }
        return data[pos++];
    }
    unsigned int u16() { return u8() | (u8() << 8); }
    unsigned int u32() { return u16() | (u16() << 16); }
    uint64_t u64()
    {
        uint64_t low = u32();
        uint64_t high = u32();
        return low | (high << 32);
    }
    const unsigned char* bytes(size_t length)
    {
        if (pos + length > data.size()) {
            overrun = true;
            return NULL;
        }
        const unsigned char* p = data.data() + pos;
        pos += length;
        return p;
    }
    std::string str()
    {
        size_t length = u16();
        const unsigned char* p = bytes(length);
        return p != NULL ? std::string((const char*)p, length) : std::string();
    }

private:
    const std::vector<unsigned char>& data;
    size_t pos = 0;
    bool overrun = false;
};

static bool coop_socket_startup();
static bool coop_socket_set_non_blocking(CoopSocket socket);
static void coop_relay_host_pump();
static CoopSocket coop_relay_take_incoming();
static void coop_connection_close(CoopConnection* connection);
static void coop_connection_send(CoopConnection* connection, int type, const std::vector<unsigned char>& payload);
static void coop_connection_flush(CoopConnection* connection);
static void coop_connection_receive(CoopConnection* connection);
static bool coop_connection_next(CoopConnection* connection, CoopMessage* message);
static void coop_host_handle(const CoopMessage& message);
static void coop_net_write_sound(int event, const char* name, int a, int b, int c);

static CoopNetMode coop_net_mode_value = COOP_NET_NONE;
static int coop_net_port_value = COOP_NET_DEFAULT_PORT;
static std::string coop_net_error;
static std::string coop_net_host_name;
// Hosting online: any free port will do (see coop_net_host_start).
static bool coop_net_any_port_ok = false;
// The port to go back to after hosting on another one (0: none).
static int coop_net_configured_port = 0;

static bool coop_net_checksum_done = false;
static uint64_t coop_net_checksum_value = 0;

// Host state.
static CoopSocket coop_listen_socket = COOP_INVALID_SOCKET;

// Host state of each client (player 2 and up: `coop_peers[n]` is player
// n + 1's), and the one the host functions below act on (coop_net_select).
typedef struct CoopPeer {
    CoopConnection client;
    bool ready = false;
    // How many times a client has joined (tests: a client that left and
    // came back between two looks still counts as having left).
    unsigned int joins = 0;
    // The player's own character, brought by the client.
    bool bringsCharacter = false;
    std::vector<unsigned char> character;
    // Flow control (see COOP_NET_FRAMES_IN_FLIGHT).
    bool acks = false;
    bool unpacks = false;
    // Moves its last picture with the camera (flag 16, 1.15).
    bool shifts = false;
    unsigned int framesSent = 0;
    unsigned int framesAcked = 0;
    std::deque<unsigned int> frameSentAt;
    unsigned long long statsDelayTotal = 0;
    unsigned int statsDelayCount = 0;
    unsigned int statsDelayMax = 0;
    // The client's screen size (since 1.5.7; 0 = unknown): frames bigger
    // than it are scaled down before sending (it would only scale them down
    // anyway), and its mouse positions back up.
    int screenWidth = 0;
    int screenHeight = 0;
    int sentScaledFromWidth = 0;
    int sentScaledFromHeight = 0;
    std::vector<unsigned char> scaledFrame;
    unsigned int since = 0;
    std::deque<CoopNetInput> input;
    // Traffic statistics for the debug log.
    unsigned long long statsBytes = 0;
    unsigned long long statsRawBytes = 0;
    unsigned int statsFrames = 0;
    unsigned int statsSince = 0;
    // Last frame sent, to send only what changed.
    std::vector<unsigned char> sentFrame;
    unsigned char sentPalette[768];
    int sentWidth = 0;
    int sentHeight = 0;
    std::string sentStatus;
    // Testing (COOP_FRAME_CHECK): each sent picture's hash, to compare
    // with what the client says it shows.
    std::deque<std::pair<unsigned int, unsigned int>> sentHashes;
    unsigned int frameMismatches = 0;
    unsigned int framesChecked = 0;
} CoopPeer;

static CoopPeer coop_peers[COOP_MAX_PLAYERS];
static CoopPeer* coop_peer = &(coop_peers[1]);

// How many clients may join (players 2 and up).
static int coop_net_max_clients = COOP_MAX_PLAYERS - 1;

#define coop_client (coop_peer->client)
#define coop_client_ready (coop_peer->ready)
#define coop_net_joins (coop_peer->joins)
#define coop_client_brings_character (coop_peer->bringsCharacter)
#define coop_client_character (coop_peer->character)
#define coop_client_acks (coop_peer->acks)
#define coop_client_unpacks (coop_peer->unpacks)
#define coop_client_shifts (coop_peer->shifts)
#define coop_frames_sent (coop_peer->framesSent)
#define coop_frames_acked (coop_peer->framesAcked)
#define coop_frame_sent_at (coop_peer->frameSentAt)
#define coop_stats_delay_total (coop_peer->statsDelayTotal)
#define coop_stats_delay_count (coop_peer->statsDelayCount)
#define coop_stats_delay_max (coop_peer->statsDelayMax)
#define coop_client_screen_width (coop_peer->screenWidth)
#define coop_client_screen_height (coop_peer->screenHeight)
#define coop_sent_scaled_from_width (coop_peer->sentScaledFromWidth)
#define coop_sent_scaled_from_height (coop_peer->sentScaledFromHeight)
#define coop_scaled_frame (coop_peer->scaledFrame)
#define coop_client_since (coop_peer->since)
#define coop_client_input (coop_peer->input)
#define coop_stats_bytes (coop_peer->statsBytes)
#define coop_stats_raw_bytes (coop_peer->statsRawBytes)
#define coop_stats_frames (coop_peer->statsFrames)
#define coop_stats_since (coop_peer->statsSince)
#define coop_sent_frame (coop_peer->sentFrame)
#define coop_sent_palette (coop_peer->sentPalette)
#define coop_sent_width (coop_peer->sentWidth)
#define coop_sent_height (coop_peer->sentHeight)
#define coop_sent_status (coop_peer->sentStatus)

// The character to bring (client side; the host's are in CoopPeer).
static std::vector<unsigned char> coop_bring_character;

// Testing: a client that doesn't acknowledge frames, like those before 1.5.7.
static bool coop_net_no_acks = getenv("COOP_NET_NO_ACKS") != NULL;
// Testing: the client sends a hash of the picture it shows with each
// acknowledgement, and the host compares it with what it sent (a picture
// put together wrongly on the client shows in the host's log).
static bool coop_net_frame_check = getenv("COOP_FRAME_CHECK") != NULL;

// FNV-1a over 64-bit words (then the rest byte by byte), folded to 32 bits.
static unsigned int coop_net_frame_hash(const unsigned char* data, size_t size)
{
    unsigned long long hash = 1469598103934665603ull;
    size_t index = 0;
    for (; index + 8 <= size; index += 8) {
        unsigned long long word;
        memcpy(&word, data + index, 8);
        hash = (hash ^ word) * 1099511628211ull;
    }
    for (; index < size; index++) {
        hash = (hash ^ data[index]) * 1099511628211ull;
    }
    return (unsigned int)(hash ^ (hash >> 32));
}
// Testing: a client that doesn't take packed frames.
static bool coop_net_no_packing = getenv("COOP_NET_NO_PACKING") != NULL;
// Testing: a client that doesn't move its picture with the camera (as
// before 1.15).
static bool coop_net_no_shifts = getenv("COOP_NET_NO_SHIFTS") != NULL;
// Testing: the host doesn't hold frames back (still measures the delay).
static bool coop_net_no_flow = getenv("COOP_NET_NO_FLOW") != NULL;

// Flow control (clients that acknowledge frames): at most this many frames
// on the way, so a slow line can't pile up seconds of delay in the buffers
// between (the system's, the relay's).
#define COOP_NET_FRAMES_IN_FLIGHT 2

// Background music playing, for clients joining later.
static bool coop_music_playing = false;
static std::string coop_music_name;
static int coop_music_args[3];

// Frames are dropped while this much is still waiting to be sent.
#define COOP_NET_MAX_BACKLOG (2 * 1024 * 1024)

static void coop_client_exit_hook_default() { }

static bool coop_client_exit_requested = false;
static void (*coop_client_exit_hook)() = coop_client_exit_hook_default;

void coop_net_init()
{
    char* mode;
    if (config_get_string(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_MODE_KEY, &mode)) {
        if (compat_stricmp(mode, "host") == 0) {
            coop_net_mode_value = COOP_NET_HOST;
        } else if (compat_stricmp(mode, "client") == 0) {
            coop_net_mode_value = COOP_NET_CLIENT;
        }
    }

    int port;
    if (config_get_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_PORT_KEY, &port) && port > 0 && port < 65536) {
        coop_net_port_value = port;
    }

    char* host;
    if (config_get_string(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_HOST_KEY, &host)) {
        coop_net_host_name = host;
    }
}

void coop_net_exit()
{
    CoopPeer* selected = coop_peer;
    for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        coop_peer = &(coop_peers[slot]);
        if (coop_net_frame_check && coop_client_ready) {
            debug_printf("\nCOOP NET: player %d: %u pictures checked, %u wrong\n", slot + 1, coop_peer->framesChecked, coop_peer->frameMismatches);
        }
        if (coop_client.socket != COOP_INVALID_SOCKET) {
            coop_connection_send(&coop_client, COOP_NET_MSG_BYE, std::vector<unsigned char>());
            coop_connection_flush(&coop_client);
            coop_connection_close(&coop_client);
        }
        coop_client_ready = false;
    }
    coop_peer = selected;

    if (coop_listen_socket != COOP_INVALID_SOCKET) {
        coop_close_socket(coop_listen_socket);
        coop_listen_socket = COOP_INVALID_SOCKET;
    }

    coop_net_relay_host_stop();

    if (coop_net_configured_port != 0) {
        coop_net_port_value = coop_net_configured_port;
        coop_net_configured_port = 0;
    }

    coop_client_ready = false;
}

CoopNetMode coop_net_mode()
{
    return coop_net_mode_value;
}

void coop_net_set_mode(CoopNetMode mode)
{
    coop_net_mode_value = mode;
}

void coop_net_set_host(const char* host)
{
    coop_net_host_name = host;
}

int coop_net_port()
{
    return coop_net_port_value;
}

void coop_net_set_port(int port)
{
    if (port > 0 && port < 65536) {
        coop_net_port_value = port;
    }
}

const char* coop_net_last_error()
{
    return coop_net_error.c_str();
}

void coop_net_local_addresses(char* buffer, size_t size)
{
    std::string result;

#ifdef _WIN32
    if (coop_socket_startup()) {
        char name[256];
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        addrinfo* addresses = NULL;
        if (gethostname(name, sizeof(name)) == 0 && getaddrinfo(name, NULL, &hints, &addresses) == 0) {
            for (addrinfo* it = addresses; it != NULL; it = it->ai_next) {
                char text[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &(((sockaddr_in*)it->ai_addr)->sin_addr), text, sizeof(text)) != NULL) {
                    result += (result.empty() ? "" : ", ") + std::string(text);
                }
            }
            freeaddrinfo(addresses);
        }
    }
#else
    ifaddrs* interfaces = NULL;
    if (getifaddrs(&interfaces) == 0) {
        for (ifaddrs* it = interfaces; it != NULL; it = it->ifa_next) {
            if (it->ifa_addr == NULL || it->ifa_addr->sa_family != AF_INET || (it->ifa_flags & IFF_LOOPBACK) != 0) {
                continue;
            }

            char text[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &(((sockaddr_in*)it->ifa_addr)->sin_addr), text, sizeof(text)) != NULL) {
                result += (result.empty() ? "" : ", ") + std::string(text);
            }
        }
        freeifaddrs(interfaces);
    }
#endif

    snprintf(buffer, size, "%s", result.c_str());
}

// -----------------------------------------------------------------------------
// Data checksum

static uint64_t coop_fnv1a(uint64_t hash, const void* data, size_t length)
{
    const unsigned char* p = (const unsigned char*)data;
    for (size_t index = 0; index < length; index++) {
        hash ^= p[index];
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

static uint64_t coop_hash_file(uint64_t hash, const std::filesystem::path& path)
{
    FILE* stream = fopen(path.string().c_str(), "rb");
    if (stream == NULL) {
        return coop_fnv1a(hash, "missing", 7);
    }

    std::vector<unsigned char> buffer(1 << 20);
    size_t read;
    while ((read = fread(buffer.data(), 1, buffer.size(), stream)) != 0) {
        hash = coop_fnv1a(hash, buffer.data(), read);
    }

    fclose(stream);
    return hash;
}

// Game data files: master.dat and critter.dat as configured, and every file
// under the patches directory except saves and files the game writes.
uint64_t coop_net_data_checksum()
{
    if (coop_net_checksum_done) {
        return coop_net_checksum_value;
    }

    uint64_t hash = 0xCBF29CE484222325ULL;

    const char* keys[] = { GAME_CONFIG_MASTER_DAT_KEY, GAME_CONFIG_CRITTER_DAT_KEY };
    for (const char* key : keys) {
        char* path;
        if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, key, &path)) {
            hash = coop_hash_file(hash, path);
        }
    }

    char* patches;
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_PATCHES_KEY, &patches)) {
        std::vector<std::string> files;
        std::error_code error;
        std::filesystem::recursive_directory_iterator it(patches, error);
        for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            std::string relative = std::filesystem::relative(it->path(), patches, error).generic_string();
            std::string upper = relative;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

            if (it->is_directory(error)) {
                if (upper == "SAVEGAME") {
                    it.disable_recursion_pending();
                }
                continue;
            }

            std::string extension = std::filesystem::path(upper).extension().string();
            if (extension == ".SAV" || extension == ".BAK" || extension == ".CFG") {
                continue;
            }

            files.push_back(relative);
        }

        std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
            return compat_stricmp(a.c_str(), b.c_str()) < 0;
        });

        for (const std::string& file : files) {
            std::string upper = file;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            hash = coop_fnv1a(hash, upper.c_str(), upper.size());
            hash = coop_hash_file(hash, std::filesystem::path(patches) / file);
        }
    }

    char* language;
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_LANGUAGE_KEY, &language)) {
        std::string upper = language;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        hash = coop_fnv1a(hash, upper.c_str(), upper.size());
    }

    coop_net_checksum_value = hash;
    coop_net_checksum_done = true;

    debug_printf("\nCOOP NET: data checksum %016llx\n", (unsigned long long)hash);

    return hash;
}

// -----------------------------------------------------------------------------
// Sockets

static bool coop_socket_startup()
{
#ifdef _WIN32
    static bool started = false;
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return false;
        }
        started = true;
    }
#endif
    return true;
}

static bool coop_socket_set_non_blocking(CoopSocket socket)
{
#ifdef SO_NOSIGPIPE
    // macOS has no MSG_NOSIGNAL.
    int noSigPipe = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif

#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    return flags != -1 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int coop_socket_error()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool coop_socket_would_block()
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

static void coop_connection_close(CoopConnection* connection)
{
    if (connection->socket != COOP_INVALID_SOCKET) {
        coop_close_socket(connection->socket);
    }

    connection->socket = COOP_INVALID_SOCKET;
    connection->in.clear();
    connection->out.clear();
    connection->failed = false;
}

static void coop_connection_send(CoopConnection* connection, int type, const std::vector<unsigned char>& payload)
{
    if (connection->socket == COOP_INVALID_SOCKET) {
        return;
    }

    CoopWriter header;
    header.u32((unsigned int)payload.size() + 1);
    header.u8(type);

    connection->out.insert(connection->out.end(), header.data.begin(), header.data.end());
    connection->out.insert(connection->out.end(), payload.begin(), payload.end());
    coop_connection_flush(connection);
}

static void coop_connection_flush(CoopConnection* connection)
{
    while (connection->socket != COOP_INVALID_SOCKET && !connection->out.empty()) {
        int sent = (int)send(connection->socket, (const char*)connection->out.data(), (int)connection->out.size(), COOP_SEND_FLAGS);
        if (sent > 0) {
            connection->out.erase(connection->out.begin(), connection->out.begin() + sent);
        } else {
            if (sent < 0 && !coop_socket_would_block()) {
                debug_printf("\nCOOP NET: send failed (error %d)\n", coop_socket_error());
                connection->failed = true;
            }
            break;
        }
    }
}

static void coop_connection_receive(CoopConnection* connection)
{
    unsigned char buffer[64 * 1024];
    while (connection->socket != COOP_INVALID_SOCKET) {
        int received = (int)recv(connection->socket, (char*)buffer, sizeof(buffer), 0);
        if (received > 0) {
            connection->in.insert(connection->in.end(), buffer, buffer + received);
        } else {
            if (received == 0 || !coop_socket_would_block()) {
                debug_printf("\nCOOP NET: %s\n", received == 0 ? "the other side closed the connection" : "receive failed");
                if (received != 0) {
                    debug_printf("COOP NET: (error %d)\n", coop_socket_error());
                }
                connection->failed = true;
            }
            break;
        }
    }
}

static bool coop_connection_next(CoopConnection* connection, CoopMessage* message)
{
    if (connection->in.size() < COOP_NET_HEADER_SIZE) {
        return false;
    }

    CoopReader reader(connection->in);
    unsigned int length = reader.u32();
    if (length == 0 || length > COOP_NET_MAX_MESSAGE) {
        debug_printf("\nCOOP NET: bad message length %u\n", length);
        connection->failed = true;
        return false;
    }

    if (connection->in.size() < 4 + length) {
        return false;
    }

    message->type = connection->in[4];
    message->payload.assign(connection->in.begin() + COOP_NET_HEADER_SIZE, connection->in.begin() + 4 + length);
    connection->in.erase(connection->in.begin(), connection->in.begin() + 4 + length);
    return true;
}

// -----------------------------------------------------------------------------
// Host

void coop_net_select(int slot)
{
    if (slot >= 1 && slot < COOP_MAX_PLAYERS) {
        coop_peer = &(coop_peers[slot]);
    }
}

int coop_net_selected()
{
    return (int)(coop_peer - coop_peers);
}

void coop_net_set_max_clients(int count)
{
    coop_net_max_clients = std::max(1, std::min(count, COOP_MAX_PLAYERS - 1));
}

int coop_net_max_clients_value()
{
    return coop_net_max_clients;
}

int coop_net_clients_connected()
{
    int count = 0;
    for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        count += coop_peers[slot].ready ? 1 : 0;
    }
    return count;
}

// The first slot without a client (that may take one), or 0.
static int coop_host_free_slot()
{
    for (int slot = 1; slot <= coop_net_max_clients; slot++) {
        if (coop_peers[slot].client.socket == COOP_INVALID_SOCKET) {
            return slot;
        }
    }
    return 0;
}

// Sends the screen as it is, cursor included (the client shows no cursor
// of its own: the host draws player 2's, with its real shape).
void coop_net_send_current_screen()
{
    if (!coop_client_ready || gSdlSurface == NULL || gSdlSurface->format->palette == NULL) {
        return;
    }

    unsigned char palette[768];
    SDL_Color* colors = gSdlSurface->format->palette->colors;
    for (int index = 0; index < 256; index++) {
        palette[index * 3] = colors[index].r;
        palette[index * 3 + 1] = colors[index].g;
        palette[index * 3 + 2] = colors[index].b;
    }

    coop_net_send_screen((const unsigned char*)gSdlSurface->pixels, gSdlSurface->pitch, gSdlSurface->w, gSdlSurface->h, palette);
}

void coop_net_set_any_port_ok(bool ok)
{
    coop_net_any_port_ok = ok;
}

bool coop_net_host_start()
{
    if (!coop_socket_startup()) {
        return false;
    }

    coop_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (coop_listen_socket == COOP_INVALID_SOCKET) {
        debug_printf("\nCOOP NET: cannot create socket\n");
        return false;
    }

    int reuse = 1;
    setsockopt(coop_listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)coop_net_port_value);

    bool bound = bind(coop_listen_socket, (sockaddr*)&address, sizeof(address)) == 0;
    if (!bound && coop_net_any_port_ok) {
        // Online the port doesn't matter (players come through the relay):
        // when it's taken (another copy, a game server), any free one.
        address.sin_port = 0;
        bound = bind(coop_listen_socket, (sockaddr*)&address, sizeof(address)) == 0;
        socklen_t length = sizeof(address);
        if (bound && getsockname(coop_listen_socket, (sockaddr*)&address, &length) == 0) {
            debug_printf("\nCOOP NET: port %d is taken, using %d\n", coop_net_port_value, ntohs(address.sin_port));
            coop_net_configured_port = coop_net_port_value;
            coop_net_port_value = ntohs(address.sin_port);
        }
    }

    if (!bound
        || listen(coop_listen_socket, 1) != 0
        || !coop_socket_set_non_blocking(coop_listen_socket)) {
        debug_printf("\nCOOP NET: cannot listen on port %d\n", coop_net_port_value);
        coop_close_socket(coop_listen_socket);
        coop_listen_socket = COOP_INVALID_SOCKET;
        return false;
    }

    // Compute up front so the handshake does not stall the game.
    coop_net_data_checksum();

    debug_printf("\nCOOP NET: hosting on port %d\n", coop_net_port_value);
    return true;
}

// The next joining player: directly, or through the relay.
static CoopSocket coop_host_accept()
{
    CoopSocket socket = accept(coop_listen_socket, NULL, NULL);
    if (socket == COOP_INVALID_SOCKET) {
        socket = coop_relay_take_incoming();
    }
    return socket;
}

// Someone else trying to join while player 2 is here: told the game is
// full, and closed a little later (closing at once could lose the answer).
typedef struct CoopRefused {
    CoopConnection connection;
    unsigned int since;
} CoopRefused;

static std::deque<CoopRefused> coop_refused;

static void coop_host_refuse_extra_clients()
{
    for (int count = 0; count < 4 && coop_host_free_slot() == 0; count++) {
        CoopSocket socket = coop_host_accept();
        if (socket == COOP_INVALID_SOCKET) {
            break;
        }
        coop_socket_set_non_blocking(socket);

        CoopRefused refused;
        refused.connection.socket = socket;
        refused.since = get_time();
        CoopWriter writer;
        writer.u8(0);
        writer.str("This game is full.");
        coop_connection_send(&(refused.connection), COOP_NET_MSG_WELCOME, writer.data);
        coop_refused.push_back(refused);
        debug_printf("\nCOOP NET: refused another client (game full)\n");
    }

    for (auto it = coop_refused.begin(); it != coop_refused.end();) {
        coop_connection_flush(&(it->connection));
        coop_connection_receive(&(it->connection));
        it->connection.in.clear();
        if (elapsed_time(it->since) > 2000 || it->connection.failed) {
            coop_connection_close(&(it->connection));
            it = coop_refused.erase(it);
        } else {
            ++it;
        }
    }
}

void coop_net_host_pump()
{
    if (coop_listen_socket == COOP_INVALID_SOCKET) {
        return;
    }

    coop_relay_host_pump();

    if (coop_host_free_slot() == 0 || !coop_refused.empty()) {
        coop_host_refuse_extra_clients();
    }

    CoopPeer* selected = coop_peer;

    // New clients take the first free slot (player 2's first).
    for (int slot = coop_host_free_slot(); slot != 0; slot = coop_host_free_slot()) {
        CoopSocket socket = coop_host_accept();
        if (socket == COOP_INVALID_SOCKET) {
            break;
        }

        int noDelay = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
        coop_socket_set_non_blocking(socket);

        coop_peer = &(coop_peers[slot]);
        coop_client = CoopConnection();
        coop_client.socket = socket;
        coop_client_ready = false;
        coop_client_since = get_time();
        debug_printf("\nCOOP NET: client connecting (player %d)\n", slot + 1);
    }

    for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        coop_peer = &(coop_peers[slot]);
        if (coop_client.socket == COOP_INVALID_SOCKET) {
            continue;
        }

        coop_connection_receive(&coop_client);

        CoopMessage message;
        while (coop_connection_next(&coop_client, &message)) {
            coop_host_handle(message);
        }

        coop_connection_flush(&coop_client);

        if (!coop_client_ready && coop_client.socket != COOP_INVALID_SOCKET && elapsed_time(coop_client_since) > COOP_NET_HANDSHAKE_TIMEOUT) {
            debug_printf("\nCOOP NET: handshake timed out\n");
            coop_client.failed = true;
        }

        if (coop_client.failed) {
            debug_printf("\nCOOP NET: client disconnected (player %d)\n", slot + 1);
            if (coop_net_frame_check) {
                debug_printf("\nCOOP NET: player %d: %u pictures checked, %u wrong\n", slot + 1, coop_peer->framesChecked, coop_peer->frameMismatches);
            }
            coop_connection_close(&coop_client);
            coop_client_ready = false;
            coop_client_input.clear();
        }
    }

    coop_peer = selected;
}

static void coop_host_welcome(bool accepted, const char* reason)
{
    CoopWriter writer;
    writer.u8(accepted ? 1 : 0);
    writer.str(reason);
    // Since 1.5.7: this screen's size (frames may come scaled down).
    writer.u16((unsigned int)screenGetWidth());
    writer.u16((unsigned int)screenGetHeight());
    // Since 1.15.3: this game's version.
    writer.str(coop_net_version());
    coop_connection_send(&coop_client, COOP_NET_MSG_WELCOME, writer.data);
}

static void coop_host_handle(const CoopMessage& message)
{
    CoopReader reader(message.payload);

    switch (message.type) {
    case COOP_NET_MSG_HELLO:
        if (true) {
            unsigned int magic = reader.u32();
            unsigned int protocol = reader.u32();
            uint64_t checksum = reader.u64();
            // Since 1.2: flags (1: colorblind outlines on player 2's
            // screen); since 1.3 followed by player 2's outline colors.
            // Since 1.5.7 flag 2: the client acknowledges frames.
            unsigned int flags = reader.atEnd() ? 0 : reader.u8();
            coop_client_acks = (flags & 2) != 0;
            coop_client_unpacks = (flags & 4) != 0;
            // Since 1.15 flag 16: the client moves its last picture with
            // the camera (older ones would misread such a frame).
            coop_client_shifts = (flags & 16) != 0;
            // Since 1.9 flag 8: player 2 brings their own character (a
            // CHARACTER message follows).
            coop_client_brings_character = (flags & 8) != 0;
            coop_client_character.clear();
            coop_frames_sent = 0;
            coop_frames_acked = 0;
            coop_frame_sent_at.clear();
            int outlines[2] = { (flags & 1) != 0 ? 2 : 0, (flags & 1) != 0 ? 3 : 1 };
            if (!reader.atEnd()) {
                outlines[0] = reader.u8();
                outlines[1] = reader.u8();
            }
            // Since 1.5.7: player 2's screen size.
            coop_client_screen_width = 0;
            coop_client_screen_height = 0;
            if (!reader.atEnd()) {
                coop_client_screen_width = reader.u16();
                coop_client_screen_height = reader.u16();
                debug_printf("COOP: player 2's screen is %dx%d\n", coop_client_screen_width, coop_client_screen_height);
            }
            // Since 1.13.3: which game they play (0: an older client).
            int clientGame = 0;
            if (!reader.atEnd()) {
                clientGame = reader.u8();
            }
            // Since 1.15.3: their version (none: older than that).
            std::string clientVersion;
            if (!reader.atEnd()) {
                clientVersion = reader.str();
            }
            coop_set_outline_colors(coop_net_selected(), outlines);
            debug_printf("COOP: player %d outlines: %s, %s\n", coop_net_selected() + 1, coop_outline_color_name(outlines[0]), coop_outline_color_name(outlines[1]));

            if (!reader.ok() || magic != COOP_NET_MAGIC) {
                coop_host_welcome(false, "Not a Classic Fallout Co-op client.");
                coop_client.failed = true;
            } else if (protocol != COOP_NET_PROTOCOL_VERSION) {
                coop_host_welcome(false, "Different co-op version.");
                coop_client.failed = true;
            } else if (clientGame != 0 && clientGame != COOP_NET_GAME) {
                coop_host_welcome(false, COOP_NET_GAME == 1 ? "Player 1 is playing Fallout 1: start Fallout 1 to join." : "Player 1 is playing Fallout 2: start Fallout 2 to join.");
                coop_client.failed = true;
            } else if (checksum != coop_net_data_checksum() && clientGame == 0) {
                coop_host_welcome(false, "Game data differs (version, language or patches).");
                coop_client.failed = true;
            } else if (clientVersion != coop_net_version()) {
                // Both need the same version: each side says which one
                // (older clients show the reason as it comes).
                bool clientOlder = coop_net_version_older(clientVersion, coop_net_version());
                char reason[220];
                snprintf(reason, sizeof(reason), "Player 1 has version %s and you have %s: both need the same version. %s",
                    coop_net_version(), clientVersion.empty() ? "an older one" : clientVersion.c_str(),
                    clientOlder ? "Update yours with MULTIPLAYER > UPDATE (or from the website)." : "Player 1 needs to update: MULTIPLAYER > UPDATE.");
                coop_host_welcome(false, reason);
                coop_client.failed = true;
                char message[200];
                snprintf(message, sizeof(message), "A player could not join: they have version %s, you have %s. %s",
                    clientVersion.empty() ? "an older one" : clientVersion.c_str(), coop_net_version(),
                    clientOlder ? "They need to update." : "Update yours: MULTIPLAYER > UPDATE.");
                display_print(message);
                debug_printf("\nCOOP NET: refused a client with version %s\n", clientVersion.empty() ? "older than 1.15.3" : clientVersion.c_str());
            } else {
                // Other game files (another store's release, language or
                // patches) are fine: the game runs here, the other player
                // only sees it. Their own character stays home though (its
                // items may not exist here), and some sounds may be missing.
                if (checksum != coop_net_data_checksum()) {
                    debug_printf("\nCOOP NET: player %d's game files differ from ours\n", coop_net_selected() + 1);
                    char message[160];
                    snprintf(message, sizeof(message), "Player %d's game files differ from yours (another release or language)%s.", coop_net_selected() + 1,
                        coop_client_brings_character ? ": they make a new character here" : "");
                    display_print(message);
                    coop_client_brings_character = false;
                }
                coop_host_welcome(true, "");
                coop_client_ready = true;
                coop_sent_width = 0;
                coop_sent_height = 0;
                coop_sent_status.clear();

                if (coop_music_playing) {
                    coop_net_write_sound(GSOUND_EVENT_MUSIC, coop_music_name.c_str(), coop_music_args[0], coop_music_args[1], coop_music_args[2]);
                }
                coop_net_joins++;
                debug_printf("\nCOOP NET: client joined\n");
            }
        }
        break;
    case COOP_NET_MSG_INPUT:
        if (coop_client_ready) {
            CoopNetInput input;
            input.kind = reader.u8();
            input.x = (short)reader.u16();
            input.y = (short)reader.u16();
            input.value = (int)reader.u32();
            // Positions in a scaled-down frame: back to this screen.
            if (input.kind == COOP_NET_INPUT_MOUSE && coop_sent_scaled_from_width != 0 && coop_sent_width > 0 && coop_sent_height > 0) {
                // (The middle of the frame pixel: the integer rounding
                // was a pixel short, enough to miss a small button.)
                input.x = (2 * input.x + 1) * coop_sent_scaled_from_width / (2 * coop_sent_width);
                input.y = (2 * input.y + 1) * coop_sent_scaled_from_height / (2 * coop_sent_height);
            }
            if (reader.ok()) {
                coop_client_input.push_back(input);
            }
        }
        break;
    case COOP_NET_MSG_ACK:
        if (true) {
            unsigned int received = reader.u32();
            while (reader.ok() && coop_frames_acked < received && coop_frames_acked < coop_frames_sent) {
                if (!coop_frame_sent_at.empty()) {
                    unsigned int delay = elapsed_time(coop_frame_sent_at.front());
                    coop_frame_sent_at.pop_front();
                    coop_stats_delay_total += delay;
                    coop_stats_delay_count++;
                    coop_stats_delay_max = std::max(coop_stats_delay_max, delay);
                }
                coop_frames_acked++;
            }
            if (coop_net_frame_check && reader.ok() && !reader.atEnd()) {
                unsigned int hash = reader.u32();
                while (!coop_peer->sentHashes.empty() && coop_peer->sentHashes.front().first <= received) {
                    if (coop_peer->sentHashes.front().first == received) {
                        coop_peer->framesChecked++;
                    }
                    if (coop_peer->sentHashes.front().first == received && coop_peer->sentHashes.front().second != hash) {
                        coop_peer->frameMismatches++;
                        debug_printf("\nCOOP NET: player %d's picture %u differs from the host's (%u so far)\n",
                            coop_net_selected() + 1, received, coop_peer->frameMismatches);
                    }
                    coop_peer->sentHashes.pop_front();
                }
            }
        }
        break;
    case COOP_NET_MSG_CHARACTER:
        if (coop_client_brings_character) {
            coop_client_character = message.payload;
            debug_printf("\nCOOP NET: player 2 brought their character (%d bytes)\n", (int)message.payload.size());
        }
        break;
    case COOP_NET_MSG_BYE:
        coop_client.failed = true;
        break;
    }
}

bool coop_net_client_brings_character()
{
    return coop_client_ready && coop_client_brings_character;
}

bool coop_net_take_client_character(std::vector<unsigned char>* data)
{
    if (coop_client_character.empty()) {
        return false;
    }
    data->swap(coop_client_character);
    coop_client_character.clear();
    coop_client_brings_character = false;
    return true;
}

void coop_net_send_character(const std::vector<unsigned char>& data)
{
    if (coop_client_ready && !data.empty()) {
        coop_connection_send(&coop_client, COOP_NET_MSG_CHARACTER, data);
    }
}

void coop_net_set_bring_character(const std::vector<unsigned char>& data)
{
    coop_bring_character = data;
}

bool coop_net_client_connected()
{
    return coop_client_ready;
}

unsigned int coop_net_client_joins()
{
    return coop_net_joins;
}

bool coop_net_next_input(CoopNetInput* input)
{
    if (coop_client_input.empty()) {
        return false;
    }

    *input = coop_client_input.front();
    coop_client_input.pop_front();
    return true;
}

// Frame compression: a small LZ77. Tokens: a byte c < 0x80 is followed by
// c + 1 literal bytes; c >= 0x80 copies (c & 0x7F) + 4 bytes from u16
// (little endian) bytes back. Game screens have long flat or repeating
// stretches, so this usually saves a lot.
#define COOP_PACK_MIN_MATCH 4
#define COOP_PACK_MAX_MATCH (0x7F + COOP_PACK_MIN_MATCH)
#define COOP_PACK_HASH_BITS 15

static void coop_net_compress(const unsigned char* data, size_t size, std::vector<unsigned char>* out)
{
    static std::vector<int> table(1 << COOP_PACK_HASH_BITS);
    std::fill(table.begin(), table.end(), -1);

    out->clear();
    out->reserve(size / 2 + 16);
    size_t literalStart = 0;
    size_t pos = 0;

    auto flushLiterals = [&](size_t end) {
        while (literalStart < end) {
            size_t count = std::min<size_t>(end - literalStart, 128);
            out->push_back((unsigned char)(count - 1));
            out->insert(out->end(), data + literalStart, data + literalStart + count);
            literalStart += count;
        }
    };

    while (pos + COOP_PACK_MIN_MATCH <= size) {
        unsigned int key = (unsigned int)data[pos] | ((unsigned int)data[pos + 1] << 8) | ((unsigned int)data[pos + 2] << 16) | ((unsigned int)data[pos + 3] << 24);
        unsigned int hash = (key * 2654435761u) >> (32 - COOP_PACK_HASH_BITS);
        int candidate = table[hash];
        table[hash] = (int)pos;

        if (candidate >= 0 && pos - candidate <= 0xFFFF && memcmp(data + candidate, data + pos, COOP_PACK_MIN_MATCH) == 0) {
            size_t length = COOP_PACK_MIN_MATCH;
            while (length < COOP_PACK_MAX_MATCH && pos + length < size && data[candidate + length] == data[pos + length]) {
                length++;
            }
            flushLiterals(pos);
            unsigned int offset = (unsigned int)(pos - candidate);
            out->push_back((unsigned char)(0x80 | (length - COOP_PACK_MIN_MATCH)));
            out->push_back((unsigned char)(offset & 0xFF));
            out->push_back((unsigned char)(offset >> 8));
            pos += length;
            literalStart = pos;
        } else {
            pos++;
        }
    }
    flushLiterals(size);
}

// False when `data` is damaged.
static bool coop_net_decompress(const unsigned char* data, size_t size, size_t expected, std::vector<unsigned char>* out)
{
    out->clear();
    out->reserve(expected);
    size_t pos = 0;
    while (pos < size) {
        unsigned char token = data[pos++];
        if (token < 0x80) {
            size_t count = (size_t)token + 1;
            if (pos + count > size || out->size() + count > expected) {
                return false;
            }
            out->insert(out->end(), data + pos, data + pos + count);
            pos += count;
        } else {
            if (pos + 2 > size) {
                return false;
            }
            size_t length = (size_t)(token & 0x7F) + COOP_PACK_MIN_MATCH;
            size_t offset = (size_t)data[pos] | ((size_t)data[pos + 1] << 8);
            pos += 2;
            if (offset == 0 || offset > out->size() || out->size() + length > expected) {
                return false;
            }
            size_t from = out->size() - offset;
            for (size_t index = 0; index < length; index++) {
                out->push_back((*out)[from + index]);
            }
        }
    }
    return out->size() == expected;
}

int coop_net_pack_selftest(const unsigned char* data, size_t size)
{
    std::vector<unsigned char> packed;
    std::vector<unsigned char> unpacked;
    coop_net_compress(data, size, &packed);
    if (!coop_net_decompress(packed.data(), packed.size(), size, &unpacked) || unpacked.size() != size
        || (size != 0 && memcmp(unpacked.data(), data, size) != 0)) {
        return -1;
    }
    return (int)packed.size();
}

static void coop_write_varint(CoopWriter* writer, unsigned int value)
{
    while (value >= 0x80) {
        writer->u8((value & 0x7F) | 0x80);
        value >>= 7;
    }
    writer->u8(value);
}

static unsigned int coop_read_varint(CoopReader* reader)
{
    unsigned int value = 0;
    for (int shift = 0; shift < 32; shift += 7) {
        unsigned int byte = reader->u8();
        value |= (byte & 0x7F) << shift;
        if ((byte & 0x80) == 0 || !reader->ok()) {
            break;
        }
    }
    return value;
}

// Frame: u16 width, u16 height, u8 flags (1: palette follows, 768 bytes
// RGB), then runs over the pixels (row by row) until all are covered:
// varint pixels unchanged, varint count, count new pixels.
static std::vector<unsigned char> coop_frame_scratch;

bool coop_net_can_send_frame()
{
    if (!coop_client_ready || coop_client.out.size() > COOP_NET_MAX_BACKLOG) {
        return false;
    }

    // Enough on the way already: the next frame shows the changes since.
    if (coop_client_acks && !coop_net_no_flow && coop_frames_sent - coop_frames_acked >= COOP_NET_FRAMES_IN_FLIGHT) {
        return false;
    }

    return true;
}

// A move of part of the picture (see coop_net_set_frame_shift).
typedef struct CoopFrameShift {
    bool set;
    int dx;
    int dy;
    int x;
    int y;
    int width;
    int height;
} CoopFrameShift;

static CoopFrameShift coop_net_pending_shift = { false, 0, 0, 0, 0, 0, 0 };

void coop_net_set_frame_shift(int dx, int dy, int x, int y, int width, int height)
{
    coop_net_pending_shift = { true, dx, dy, x, y, width, height };
}

// Moves the pixels of the shift's rectangle in a `width` x `height`
// picture by (dx, dy). What moves in from outside the rectangle keeps the
// old pixels; the changes sent after it fix those. The same on both
// sides, so the pictures stay alike whatever the shift.
static void coop_net_shift_frame(unsigned char* frame, int width, int height, const CoopFrameShift& shift)
{
    int left = std::max(0, shift.x);
    int top = std::max(0, shift.y);
    int right = std::min(width, shift.x + shift.width);
    int bottom = std::min(height, shift.y + shift.height);
    int w = right - left;
    int h = bottom - top;
    if (w <= 0 || h <= 0 || abs(shift.dx) >= w || abs(shift.dy) >= h || (shift.dx == 0 && shift.dy == 0)) {
        return;
    }

    size_t count = (size_t)(w - abs(shift.dx));
    int fromX = left + std::max(0, -shift.dx);
    int toX = left + std::max(0, shift.dx);
    if (shift.dy > 0) {
        for (int row = h - 1; row >= shift.dy; row--) {
            memmove(frame + (size_t)(top + row) * width + toX, frame + (size_t)(top + row - shift.dy) * width + fromX, count);
        }
    } else {
        for (int row = 0; row < h + shift.dy; row++) {
            memmove(frame + (size_t)(top + row) * width + toX, frame + (size_t)(top + row - shift.dy) * width + fromX, count);
        }
    }
}

void coop_net_send_screen(const unsigned char* pixels, int pitch, int width, int height, const unsigned char* palette)
{
    // Only for this picture, whether it goes out or not.
    CoopFrameShift shift = coop_net_pending_shift;
    coop_net_pending_shift.set = false;

    if (!coop_net_can_send_frame()) {
        return;
    }

    // Bigger than player 2's screen: scaled down to fit it (same shape).
    coop_sent_scaled_from_width = 0;
    coop_sent_scaled_from_height = 0;
    int clientWidth = coop_client_screen_width;
    int clientHeight = coop_client_screen_height;
    // Not for a game that moves its picture with the camera (1.15): it
    // scales the picture itself as it shows it, the same way, and a scaled
    // picture can't move with the camera (it moves by fractions of its
    // pixels), so walking sent all of it (some 0.5 MB/s at 1920x1080).
    if (clientWidth > 0 && clientHeight > 0 && (width > clientWidth || height > clientHeight) && !coop_client_shifts) {
        int scaledWidth = clientWidth;
        int scaledHeight = height * clientWidth / width;
        if (scaledHeight > clientHeight) {
            scaledHeight = clientHeight;
            scaledWidth = width * clientHeight / height;
        }
        if (scaledWidth > 0 && scaledHeight > 0) {
            coop_scaled_frame.resize((size_t)scaledWidth * scaledHeight);
            static std::vector<int> columns;
            columns.resize(scaledWidth);
            for (int column = 0; column < scaledWidth; column++) {
                columns[column] = column * width / scaledWidth;
            }
            for (int row = 0; row < scaledHeight; row++) {
                const unsigned char* src = pixels + (size_t)(row * height / scaledHeight) * pitch;
                unsigned char* dest = coop_scaled_frame.data() + (size_t)row * scaledWidth;
                for (int column = 0; column < scaledWidth; column++) {
                    dest[column] = src[columns[column]];
                }
            }
            coop_sent_scaled_from_width = width;
            coop_sent_scaled_from_height = height;
            pixels = coop_scaled_frame.data();
            pitch = scaledWidth;
            width = scaledWidth;
            height = scaledHeight;
        }
    }

    bool full = width != coop_sent_width || height != coop_sent_height;
    if (full) {
        coop_sent_frame.assign((size_t)width * height, 0);
        coop_sent_width = width;
        coop_sent_height = height;
    }

    bool paletteChanged = full || memcmp(palette, coop_sent_palette, sizeof(coop_sent_palette)) != 0;

    // The camera's move, on our copy of the player's picture first (not for
    // a picture scaled down to their screen: the move wouldn't be whole
    // pixels there).
    bool shifted = coop_client_shifts && shift.set && !full && coop_sent_scaled_from_width == 0 && (shift.dx != 0 || shift.dy != 0);
    if (shifted) {
        coop_net_shift_frame(coop_sent_frame.data(), width, height, shift);
    }

    CoopWriter writer;
    writer.u16(width);
    writer.u16(height);
    writer.u8((paletteChanged ? 1 : 0) | (shifted ? 2 : 0));
    if (paletteChanged) {
        writer.bytes(palette, 768);
        memcpy(coop_sent_palette, palette, sizeof(coop_sent_palette));
    }
    if (shifted) {
        writer.u16((unsigned int)(unsigned short)(short)shift.dx);
        writer.u16((unsigned int)(unsigned short)(short)shift.dy);
        writer.u16(std::max(0, shift.x));
        writer.u16(std::max(0, shift.y));
        writer.u16(std::max(0, shift.width));
        writer.u16(std::max(0, shift.height));
    }

    // (A shifted copy has to reach the player, changes or not.)
    bool anyChange = paletteChanged || shifted;
    size_t total = (size_t)width * height;

    // The pixels as one run (rows packed), for the comparison below.
    coop_frame_scratch.resize(total);
    unsigned char* current = coop_frame_scratch.data();
    for (int row = 0; row < height; row++) {
        memcpy(current + (size_t)row * width, pixels + (size_t)row * pitch, width);
    }
    unsigned char* sent = coop_sent_frame.data();

    size_t index = 0;
    while (index < total) {
        size_t start = index;
        if (!full) {
            // Unchanged pixels, a word at a time while they match.
            while (index + 8 <= total && memcmp(current + index, sent + index, 8) == 0) {
                index += 8;
            }
            while (index < total && current[index] == sent[index]) {
                index++;
            }
        }
        size_t unchanged = index - start;

        start = index;
        while (index < total && (full || current[index] != sent[index])) {
            index++;
        }
        size_t changed = index - start;

        coop_write_varint(&writer, (unsigned int)unchanged);
        coop_write_varint(&writer, (unsigned int)changed);
        if (changed != 0) {
            writer.bytes(current + start, changed);
            memcpy(sent + start, current + start, changed);
            anyChange = true;
        }
    }

    // An unchanged frame now and then keeps a quiet connection open.
    static unsigned int lastFrameSent = 0;
    if (anyChange || elapsed_time(lastFrameSent) > COOP_NET_KEEPALIVE) {
        lastFrameSent = get_time();
        // Compressed when player 2 understands it and it's smaller.
        static std::vector<unsigned char> packed;
        bool sendPacked = false;
        if (coop_client_unpacks && writer.data.size() > 256) {
            coop_net_compress(writer.data.data(), writer.data.size(), &packed);
            sendPacked = packed.size() + 4 < writer.data.size();
        }
        if (sendPacked) {
            CoopWriter packedWriter;
            packedWriter.u32((unsigned int)writer.data.size());
            packedWriter.bytes(packed.data(), packed.size());
            coop_connection_send(&coop_client, COOP_NET_MSG_FRAME_PACKED, packedWriter.data);
            coop_stats_raw_bytes += writer.data.size();
            writer.data.swap(packedWriter.data);
        } else {
            coop_connection_send(&coop_client, COOP_NET_MSG_FRAME, writer.data);
            coop_stats_raw_bytes += writer.data.size();
        }
        coop_frames_sent++;
        if (coop_net_frame_check) {
            coop_peer->sentHashes.push_back(std::make_pair(coop_frames_sent, coop_net_frame_hash(coop_sent_frame.data(), coop_sent_frame.size())));
            if (coop_peer->sentHashes.size() > 64) {
                coop_peer->sentHashes.pop_front();
            }
        }
        if (coop_client_acks) {
            coop_frame_sent_at.push_back(get_time());
        }

        coop_stats_bytes += writer.data.size() + COOP_NET_HEADER_SIZE;
        coop_stats_frames++;
        if (coop_stats_since == 0) {
            coop_stats_since = get_time();
        } else if (elapsed_time(coop_stats_since) >= 10000) {
            unsigned int elapsed = elapsed_time(coop_stats_since);
            debug_printf("\nCOOP NET: %u frames, %llu KB/s (%llu KB/s unpacked), %llu bytes/frame, delay %u ms (max %u)\n",
                coop_stats_frames,
                coop_stats_bytes * 1000 / elapsed / 1024,
                coop_stats_raw_bytes * 1000 / elapsed / 1024,
                coop_stats_bytes / coop_stats_frames,
                coop_stats_delay_count != 0 ? (unsigned int)(coop_stats_delay_total / coop_stats_delay_count) : 0,
                coop_stats_delay_max);
            coop_stats_delay_total = 0;
            coop_stats_raw_bytes = 0;
            coop_stats_delay_count = 0;
            coop_stats_delay_max = 0;
            coop_stats_bytes = 0;
            coop_stats_frames = 0;
            coop_stats_since = get_time();
        }
    }
}

void coop_net_send_status(const char* text)
{
    if (!coop_client_ready || coop_sent_status == text) {
        return;
    }

    coop_sent_status = text;

    CoopWriter writer;
    writer.str(text);
    coop_connection_send(&coop_client, COOP_NET_MSG_STATUS, writer.data);
}

void coop_net_client_request_exit()
{
    coop_client_exit_requested = true;
}

static void coop_net_write_sound(int event, const char* name, int a, int b, int c)
{
    CoopWriter writer;
    writer.u8(event);
    writer.str(name);
    writer.u32((unsigned int)a);
    writer.u32((unsigned int)b);
    writer.u32((unsigned int)c);
    coop_connection_send(&coop_client, COOP_NET_MSG_SOUND, writer.data);
}

void coop_net_send_sound(int event, const char* name, int a, int b, int c)
{
    if (event == GSOUND_EVENT_MUSIC) {
        coop_music_playing = true;
        coop_music_name = name;
        coop_music_args[0] = a;
        coop_music_args[1] = b;
        coop_music_args[2] = c;
    } else if (event == GSOUND_EVENT_MUSIC_STOP) {
        coop_music_playing = false;
    }

    // Every player hears it, effects as loud as they are where that player
    // is.
    CoopPeer* selected = coop_peer;
    for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        coop_peer = &(coop_peers[slot]);
        if (coop_client_ready && coop_client.out.size() < COOP_NET_MAX_BACKLOG) {
            int volume = event == GSOUND_EVENT_SFX ? coop_effect_volume_for(slot, b, c, a) : a;
            coop_net_write_sound(event, name, volume, b, c);
        }
    }
    coop_peer = selected;
}

void coop_net_set_client_exit_hook(void (*proc)())
{
    coop_client_exit_hook = proc != NULL ? proc : coop_client_exit_hook_default;
}

// -----------------------------------------------------------------------------
// Relay: both players connect out to the co-op server, which passes the
// bytes on, so player 1 needs no open port (see tools/coop_server.py).

#define COOP_RELAY_CONNECT_TIMEOUT 5000
#define COOP_RELAY_JOIN_TIMEOUT 20000
#define COOP_RELAY_RETRY_INTERVAL 5000

// host:port of the relay, and the game's code (host: own room; client:
// the room to join, empty for a direct connection).
static std::string coop_relay_server;
static std::string coop_relay_code_value;
static std::string coop_relay_join_code;
// Client: the host's screen size (0 = not told, before 1.5.7).
static int coop_client_host_width = 0;
static int coop_client_host_height = 0;
static CoopSocket coop_relay_control = COOP_INVALID_SOCKET;
static std::string coop_relay_control_in;
static unsigned int coop_relay_retry_at = 0;
static std::deque<CoopSocket> coop_relay_incoming;

// Connects to "host:port" (blocking, as before relays existed).
static CoopSocket coop_connect_direct(const std::string& host, int port, std::string* error)
{
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portText[16];
    snprintf(portText, sizeof(portText), "%d", port);

    addrinfo* addresses = NULL;
    if (getaddrinfo(host.c_str(), portText, &hints, &addresses) != 0 || addresses == NULL) {
        *error = "Cannot resolve host " + host + ".";
        return COOP_INVALID_SOCKET;
    }

    CoopSocket socket = ::socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    bool connected = socket != COOP_INVALID_SOCKET && connect(socket, addresses->ai_addr, (int)addresses->ai_addrlen) == 0;
    freeaddrinfo(addresses);

    if (!connected) {
        if (socket != COOP_INVALID_SOCKET) {
            coop_close_socket(socket);
        }
        *error = "Cannot connect to " + host + ".";
        return COOP_INVALID_SOCKET;
    }

    return socket;
}

// Waits up to `ms` for `socket` to be readable (or writable).
static bool coop_socket_wait(CoopSocket socket, bool write, unsigned int ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeval wait = { (long)(ms / 1000), (long)((ms % 1000) * 1000) };
    return select((int)socket + 1, write ? NULL : &set, write ? &set : NULL, NULL, &wait) > 0;
}

static bool coop_relay_send_line(CoopSocket socket, const std::string& line);
static bool coop_relay_read_line(CoopSocket socket, unsigned int ms, std::string* line);

// Connects to the relay without hanging the game for long when it can't
// be reached. The relay is "host:port", or "http://host[:port]/path" when
// it sits behind a web server (an HTTP upgrade first; port 80 is open
// almost everywhere). The socket is non-blocking.
static CoopSocket coop_relay_connect(const std::string& server, std::string* error)
{
    std::string address = server;
    std::string path;
    bool http = address.compare(0, 7, "http://") == 0;
    if (http) {
        address = address.substr(7);
        size_t slash = address.find('/');
        path = slash != std::string::npos ? address.substr(slash) : "/";
        address = address.substr(0, slash);
        if (address.find(':') == std::string::npos) {
            address += ":80";
        }
    }

    size_t colon = address.rfind(':');
    if (address.empty() || colon == std::string::npos) {
        *error = "No co-op server set.";
        return COOP_INVALID_SOCKET;
    }
    std::string host = address.substr(0, colon);
    std::string port = address.substr(colon + 1);

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = NULL;
    if (!coop_socket_startup() || getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses) != 0 || addresses == NULL) {
        *error = "Cannot reach the co-op server.";
        return COOP_INVALID_SOCKET;
    }

    CoopSocket socket = ::socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    bool connected = false;
    if (socket != COOP_INVALID_SOCKET && coop_socket_set_non_blocking(socket)) {
        if (connect(socket, addresses->ai_addr, (int)addresses->ai_addrlen) == 0) {
            connected = true;
        } else if (coop_socket_would_block() && coop_socket_wait(socket, true, COOP_RELAY_CONNECT_TIMEOUT)) {
            int socketError = 0;
            socklen_t length = sizeof(socketError);
            connected = getsockopt(socket, SOL_SOCKET, SO_ERROR, (char*)&socketError, &length) == 0 && socketError == 0;
        }
    }
    freeaddrinfo(addresses);

    if (!connected) {
        if (socket != COOP_INVALID_SOCKET) {
            coop_close_socket(socket);
        }
        *error = "Cannot reach the co-op server.";
        return COOP_INVALID_SOCKET;
    }

    int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

    if (http) {
        // The web server routes this host name to the relay.
        std::string status;
        bool upgraded = coop_relay_send_line(socket, "GET " + path + " HTTP/1.1\r\nHost: fallout-coop-relay\r\nUpgrade: fcoop-relay\r\nConnection: Upgrade\r")
            && coop_relay_send_line(socket, "\r")
            && coop_relay_read_line(socket, COOP_RELAY_CONNECT_TIMEOUT, &status)
            && status.compare(0, 12, "HTTP/1.1 101") == 0;
        std::string header = "-";
        while (upgraded && !header.empty()) {
            upgraded = coop_relay_read_line(socket, COOP_RELAY_CONNECT_TIMEOUT, &header);
        }
        if (!upgraded) {
            debug_printf("\nCOOP NET: relay upgrade failed: %s\n", status.c_str());
            coop_close_socket(socket);
            *error = "The co-op server did not answer.";
            return COOP_INVALID_SOCKET;
        }
    }

    return socket;
}

static bool coop_relay_send_line(CoopSocket socket, const std::string& line)
{
    std::string text = line + "\n";
    size_t sent = 0;
    unsigned int start = get_time();
    while (sent < text.size() && elapsed_time(start) < COOP_RELAY_CONNECT_TIMEOUT) {
        int count = (int)send(socket, text.data() + sent, (int)(text.size() - sent), COOP_SEND_FLAGS);
        if (count > 0) {
            sent += count;
        } else if (!coop_socket_would_block() || !coop_socket_wait(socket, true, 100)) {
            if (!coop_socket_would_block()) {
                return false;
            }
        }
    }
    return sent == text.size();
}

// Reads one line, a byte at a time (nothing after it is taken: the game's
// own bytes follow).
static bool coop_relay_read_line(CoopSocket socket, unsigned int ms, std::string* line)
{
    line->clear();
    unsigned int start = get_time();
    while (elapsed_time(start) < ms && line->size() < 200) {
        char ch;
        int count = (int)recv(socket, &ch, 1, 0);
        if (count == 1) {
            if (ch == '\n') {
                return true;
            }
            if (ch != '\r') {
                *line += ch;
            }
        } else if (count == 0 || !coop_socket_would_block()) {
            return false;
        } else {
            coop_socket_wait(socket, false, 100);
        }
    }
    return false;
}

void coop_net_set_relay_server(const std::string& server)
{
    coop_relay_server = server;
}

void coop_net_set_relay_join(const std::string& code)
{
    coop_relay_join_code = code;
}

const char* coop_net_relay_code()
{
    return coop_relay_code_value.c_str();
}

// Asks the relay for player 1's room (`wanted`: the code to keep when
// reconnecting). Returns the control connection, and the code.
static CoopSocket coop_relay_request_room(const std::string& server, const std::string& wanted, std::string* code, std::string* error)
{
    CoopSocket socket = coop_relay_connect(server, error);
    if (socket == COOP_INVALID_SOCKET) {
        return COOP_INVALID_SOCKET;
    }

    std::string line;
    if (!coop_relay_send_line(socket, "FCOOP-RELAY HOST " + (wanted.empty() ? std::string("-") : wanted) + " " + coop_net_game_name() + " " + coop_net_version())
        || !coop_relay_read_line(socket, COOP_RELAY_CONNECT_TIMEOUT, &line)
        || line.compare(0, 5, "ROOM ") != 0) {
        *error = line.compare(0, 4, "ERR ") == 0 ? line.substr(4) : "The co-op server did not answer.";
        coop_close_socket(socket);
        return COOP_INVALID_SOCKET;
    }

    *code = line.substr(5);
    return socket;
}

static void coop_relay_set_control(CoopSocket socket, const std::string& code)
{
    coop_relay_control = socket;
    coop_relay_control_in.clear();
    coop_relay_code_value = code;
    debug_printf("\nCOOP NET: relay room %s\n", code.c_str());
}

// Reconnecting runs in the background (the server may be slow to answer
// or down for a while), and the game picks up the result.
// state: 0 running, 1 done (result ready), 2 abandoned by the game; who
// moves it last closes an unwanted socket.
typedef struct CoopRelayReconnect {
    std::atomic<int> state { 0 };
    std::string wanted;
    CoopSocket socket = COOP_INVALID_SOCKET;
    std::string code;
    std::string error;
} CoopRelayReconnect;

static std::shared_ptr<CoopRelayReconnect> coop_relay_reconnect;

bool coop_net_relay_host_start(std::string* error, const std::string& wanted)
{
    coop_net_relay_host_stop();

    std::string code;
    CoopSocket socket = coop_relay_request_room(coop_relay_server, wanted, &code, error);
    if (socket == COOP_INVALID_SOCKET) {
        return false;
    }
    coop_relay_set_control(socket, code);
    return true;
}

void coop_net_relay_host_stop()
{
    if (coop_relay_control != COOP_INVALID_SOCKET) {
        coop_close_socket(coop_relay_control);
        coop_relay_control = COOP_INVALID_SOCKET;
    }
    for (CoopSocket socket : coop_relay_incoming) {
        coop_close_socket(socket);
    }
    coop_relay_incoming.clear();
    coop_relay_code_value.clear();

    // A reconnect still running closes its own socket when it is done.
    if (coop_relay_reconnect != NULL) {
        std::shared_ptr<CoopRelayReconnect> pending = coop_relay_reconnect;
        coop_relay_reconnect = NULL;
        if (pending->state.exchange(2) == 1 && pending->socket != COOP_INVALID_SOCKET) {
            coop_close_socket(pending->socket);
        }
    }
}

static void coop_relay_host_pump()
{
    if (coop_relay_code_value.empty()) {
        return;
    }

    if (coop_relay_control == COOP_INVALID_SOCKET) {
        // Lost (the server restarted, say): try to get the same code back,
        // in the background.
        if (coop_relay_reconnect != NULL) {
            if (coop_relay_reconnect->state != 1) {
                return;
            }
            std::shared_ptr<CoopRelayReconnect> result = coop_relay_reconnect;
            coop_relay_reconnect = NULL;
            if (result->socket != COOP_INVALID_SOCKET) {
                if (result->code != result->wanted) {
                    // Someone else got the old code meanwhile: tell player 1.
                    std::string message = "New online game code: " + result->code;
                    display_print((char*)message.c_str());
                }
                coop_relay_set_control(result->socket, result->code);
            } else {
                debug_printf("\nCOOP NET: relay: %s\n", result->error.c_str());
            }
            coop_relay_retry_at = get_time();
            return;
        }

        if (elapsed_time(coop_relay_retry_at) < COOP_RELAY_RETRY_INTERVAL) {
            return;
        }

        std::shared_ptr<CoopRelayReconnect> attempt = std::make_shared<CoopRelayReconnect>();
        attempt->wanted = coop_relay_code_value;
        coop_relay_reconnect = attempt;
        std::string server = coop_relay_server;
        std::thread([attempt, server]() {
            std::string code;
            std::string error;
            CoopSocket socket = coop_relay_request_room(server, attempt->wanted, &code, &error);
            attempt->socket = socket;
            attempt->code = code;
            attempt->error = error;
            if (attempt->state.exchange(1) == 2 && socket != COOP_INVALID_SOCKET) {
                coop_close_socket(socket);
            }
        }).detach();
        return;
    }

    char buffer[256];
    for (;;) {
        int count = (int)recv(coop_relay_control, buffer, sizeof(buffer), 0);
        if (count > 0) {
            coop_relay_control_in.append(buffer, count);
            continue;
        }
        if (count == 0 || !coop_socket_would_block()) {
            debug_printf("\nCOOP NET: relay connection lost\n");
            coop_close_socket(coop_relay_control);
            coop_relay_control = COOP_INVALID_SOCKET;
            coop_relay_retry_at = get_time() - COOP_RELAY_RETRY_INTERVAL + 1000;
        }
        break;
    }

    size_t end;
    while ((end = coop_relay_control_in.find('\n')) != std::string::npos) {
        std::string line = coop_relay_control_in.substr(0, end);
        coop_relay_control_in.erase(0, end + 1);
        if (line.compare(0, 5, "CONN ") != 0) {
            continue; // PING
        }

        // Someone joins: a connection of our own for them.
        std::string error;
        CoopSocket socket = coop_relay_connect(coop_relay_server, &error);
        if (socket == COOP_INVALID_SOCKET) {
            debug_printf("\nCOOP NET: relay: %s\n", error.c_str());
            continue;
        }
        if (!coop_relay_send_line(socket, "FCOOP-RELAY ACCEPT " + coop_relay_code_value + " " + line.substr(5))) {
            coop_close_socket(socket);
            continue;
        }
        debug_printf("\nCOOP NET: relay: player joining\n");
        coop_relay_incoming.push_back(socket);
    }
}

static CoopSocket coop_relay_take_incoming()
{
    if (coop_relay_incoming.empty()) {
        return COOP_INVALID_SOCKET;
    }
    CoopSocket socket = coop_relay_incoming.front();
    coop_relay_incoming.pop_front();
    return socket;
}

static CoopSocket coop_relay_join(std::string* error)
{
    CoopSocket socket = coop_relay_connect(coop_relay_server, error);
    if (socket == COOP_INVALID_SOCKET) {
        return COOP_INVALID_SOCKET;
    }

    std::string line;
    if (!coop_relay_send_line(socket, "FCOOP-RELAY JOIN " + coop_relay_join_code + " " + coop_net_game_name() + " " + coop_net_version())
        || !coop_relay_read_line(socket, COOP_RELAY_JOIN_TIMEOUT, &line)
        || line != "OK") {
        *error = line.compare(0, 4, "ERR ") == 0 ? line.substr(4) : "No answer through the co-op server.";
        coop_close_socket(socket);
        return COOP_INVALID_SOCKET;
    }

    debug_printf("\nCOOP NET: joined %s through the relay\n", coop_relay_join_code.c_str());
    return socket;
}

bool coop_net_parse_code(const char* text, std::string* code)
{
    // Three letters and three digits; a dash or space between is fine.
    std::string compact;
    for (const char* ch = text; *ch != '\0'; ch++) {
        if (*ch != '-' && *ch != ' ') {
            compact += (char)toupper((unsigned char)*ch);
        }
    }
    if (compact.size() != 6) {
        return false;
    }
    for (int index = 0; index < 6; index++) {
        bool letter = compact[index] >= 'A' && compact[index] <= 'Z';
        bool digit = compact[index] >= '0' && compact[index] <= '9';
        if (index < 3 ? !letter : !digit) {
            return false;
        }
    }
    *code = compact;
    return true;
}

// -----------------------------------------------------------------------------
// Client

static bool coop_client_connect(CoopConnection* connection, std::string* error)
{
    if (!coop_socket_startup()) {
        *error = "Networking unavailable.";
        return false;
    }

    // Before connecting: reading the game data takes a while on a slow
    // machine, and the host only waits so long for the hello.
    coop_net_data_checksum();

    CoopSocket socket;
    if (!coop_relay_join_code.empty()) {
        socket = coop_relay_join(error);
        if (socket == COOP_INVALID_SOCKET) {
            return false;
        }
    } else {
        if (coop_net_host_name.empty()) {
            *error = "No host configured ([coop] host=...).";
            return false;
        }

        socket = coop_connect_direct(coop_net_host_name, coop_net_port_value, error);
        if (socket == COOP_INVALID_SOCKET) {
            return false;
        }
    }

    int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
    coop_socket_set_non_blocking(socket);
    connection->socket = socket;

    CoopWriter hello;
    hello.u32(COOP_NET_MAGIC);
    hello.u32(COOP_NET_PROTOCOL_VERSION);
    hello.u64(coop_net_data_checksum());
    int outlines[2];
    coop_outline_colors_from_config(outlines);
    // Flags: 1 colorblind (1.2), 2 acknowledges frames, 4 packed frames (1.5.7).
    // 8: brings their own character (1.9). 16: moves its picture with the
    // camera (1.15).
    hello.u8((outlines[0] == 2 && outlines[1] == 3 ? 1 : 0) | (coop_net_no_acks ? 0 : 2) | (coop_net_no_packing ? 0 : 4)
        | (coop_bring_character.empty() ? 0 : 8) | (coop_net_no_shifts ? 0 : 16));
    hello.u8(outlines[0]);
    hello.u8(outlines[1]);
    hello.u16((unsigned int)screenGetWidth());
    hello.u16((unsigned int)screenGetHeight());
    hello.u8(coop_net_game_id());
    hello.str(coop_net_version());
    coop_connection_send(connection, COOP_NET_MSG_HELLO, hello.data);
    if (!coop_bring_character.empty()) {
        coop_connection_send(connection, COOP_NET_MSG_CHARACTER, coop_bring_character);
    }

    unsigned int start = get_time();
    while (elapsed_time(start) < COOP_NET_HANDSHAKE_TIMEOUT) {
        coop_connection_flush(connection);
        coop_connection_receive(connection);

        CoopMessage message;
        if (coop_connection_next(connection, &message) && message.type == COOP_NET_MSG_WELCOME) {
            CoopReader reader(message.payload);
            bool accepted = reader.u8() != 0;
            std::string reason = reader.str();
            coop_client_host_width = 0;
            coop_client_host_height = 0;
            if (!reader.atEnd()) {
                coop_client_host_width = reader.u16();
                coop_client_host_height = reader.u16();
            }
            // Since 1.15.3: player 1's version (none: older than that).
            std::string hostVersion;
            if (!reader.atEnd()) {
                hostVersion = reader.str();
            }
            if (!accepted) {
                *error = "Host refused: " + reason;
                coop_connection_close(connection);
                return false;
            }
            if (hostVersion != coop_net_version()) {
                *error = std::string("Player 1 has ") + (hostVersion.empty() ? "an older version" : "version " + hostVersion) + " and you have " + coop_net_version()
                    + ": both need the same version. Player 1 can update with MULTIPLAYER > UPDATE.";
                debug_printf("\nCOOP NET: player 1 has version %s\n", hostVersion.empty() ? "older than 1.15.3" : hostVersion.c_str());
                coop_connection_close(connection);
                return false;
            }
            return true;
        }

        if (connection->failed) {
            break;
        }

        SDL_Delay(10);
    }

    *error = "No answer from the host.";
    coop_connection_close(connection);
    return false;
}

// Player 1's frames on this screen: their size, and where they are drawn
// (scaled to fit, keeping their shape, when the screen sizes differ).
static int coop_client_frame_width = 0;
static int coop_client_frame_height = 0;
static int coop_client_draw_x = 0;
static int coop_client_draw_y = 0;
static int coop_client_draw_width = 0;
static int coop_client_draw_height = 0;

bool coop_net_client_host_size(int* width, int* height)
{
    *width = coop_client_host_width;
    *height = coop_client_host_height;
    return coop_client_host_width > 0 && coop_client_host_height > 0;
}

bool coop_net_client_frame_size(int* width, int* height)
{
    *width = coop_client_frame_width;
    *height = coop_client_frame_height;
    return coop_client_draw_width != 0;
}

void coop_net_client_frame_to_window(int* x, int* y)
{
    if (coop_client_draw_width != 0) {
        *x = coop_client_draw_x + (2 * *x + 1) * coop_client_draw_width / (2 * coop_client_frame_width);
        *y = coop_client_draw_y + (2 * *y + 1) * coop_client_draw_height / (2 * coop_client_frame_height);
    }
}

// Process CPU time (user and system) in ms, or -1.
static long long coop_net_cpu_ms()
{
#ifdef _WIN32
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return -1;
    }
    ULARGE_INTEGER kernelTime;
    kernelTime.LowPart = kernel.dwLowDateTime;
    kernelTime.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER userTime;
    userTime.LowPart = user.dwLowDateTime;
    userTime.HighPart = user.dwHighDateTime;
    return (long long)((kernelTime.QuadPart + userTime.QuadPart) / 10000);
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
    return (long long)usage.ru_utime.tv_sec * 1000 + usage.ru_utime.tv_usec / 1000
        + (long long)usage.ru_stime.tv_sec * 1000 + usage.ru_stime.tv_usec / 1000;
#endif
}

static long long coop_net_cpu_start = -1;
static unsigned int coop_net_cpu_wall_start = 0;

void coop_net_cpu_mark()
{
    coop_net_cpu_start = coop_net_cpu_ms();
    coop_net_cpu_wall_start = SDL_GetTicks();
}

void coop_net_cpu_report(const char* who)
{
    long long cpu = coop_net_cpu_ms();
    unsigned int wall = SDL_GetTicks() - coop_net_cpu_wall_start;
    if (coop_net_cpu_start < 0 || cpu < 0 || wall == 0) {
        return;
    }
    long long used = cpu - coop_net_cpu_start;
    debug_printf("\nCOOP NET: %s used %lld%% of a core (%lld ms CPU in %u ms)\n", who, used * 100 / wall, used, wall);
}

int coop_net_client_run()
{
    CoopConnection connection;
    std::string error;
    coop_client_exit_requested = false;
    coop_net_error.clear();
    if (!coop_client_connect(&connection, &error)) {
        debug_printf("\nCOOP NET: %s\n", error.c_str());
        coop_net_error = error;
        return 1;
    }

    debug_printf("\nCOOP NET: connected to %s\n", coop_net_host_name.c_str());
    coop_net_cpu_mark();

    // The host's palette comes with its frames, color cycling included; the
    // client's own cycling would overwrite parts of it (wrong colors in the
    // ending slides, say, where the host stops cycling).
    struct NoColorCycling {
        bool wasEnabled = cycle_is_enabled();
        NoColorCycling() { cycle_disable(); }
        ~NoColorCycling()
        {
            if (wasEnabled) {
                cycle_enable();
            }
        }
    } noColorCycling;

    std::vector<unsigned char> frame;
    int& frameWidth = coop_client_frame_width;
    int& frameHeight = coop_client_frame_height;
    int& drawX = coop_client_draw_x;
    int& drawY = coop_client_draw_y;
    int& drawWidth = coop_client_draw_width;
    int& drawHeight = coop_client_draw_height;
    frameWidth = 0;
    frameHeight = 0;
    drawWidth = 0;
    drawHeight = 0;
    std::vector<int> drawColumns;
    unsigned int framesReceived = 0;
    unsigned int framesAcked = 0;
    std::string status;

    int soundsPlayed = 0;

    int sentX = -1;
    int sentY = -1;
    int sentButtons = -1;

    // Frames go into a full-screen window, so the window system (mouse
    // cursor, screenshots) sees them.
    int window = win_add(0, 0, screenGetWidth(), screenGetHeight(), colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (window == -1) {
        coop_connection_close(&connection);
        return 1;
    }
    unsigned char* windowBuffer = win_get_buf(window);
    int windowWidth = screenGetWidth();
    int windowHeight = screenGetHeight();
    win_draw(window);

    // The host draws player 2's cursor into the frames; keep the local one
    // invisible (but working, so the mouse is still tracked).
    // (Static: the mouse keeps a pointer to the shape.)
    static unsigned char blankCursor = 0;
    mouse_set_shape(&blankCursor, 1, 1, 1, 0, 0, 0);
    mouse_show();

    bool hostLeft = false;
    bool hostSaidBye = false;
    unsigned int lastPresent = 0;
    while (!hostLeft) {
        sharedFpsLimiter.mark();

        // Local input goes to the host, except the quit keys: they leave.
        int keyCode = get_input();
        if (keyCode == KEY_F10 || keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X) {
            coop_client_exit_requested = true;
        } else if (keyCode >= 0) {
            CoopWriter writer;
            writer.u8(COOP_NET_INPUT_KEY);
            writer.u16(0);
            writer.u16(0);
            writer.u32((unsigned int)keyCode);
            coop_connection_send(&connection, COOP_NET_MSG_INPUT, writer.data);
        }

        int x;
        int y;
        mouse_get_position(&x, &y);
        // Window -> frame: also when the frame is shown at its own size but
        // off the corner (black bars: player 1's screen was scaled down to
        // fit ours in one direction). Those clicks went that far off, e.g.
        // 40 pixels with a 1920x1080 player 1 on a 1280x800 Steam Deck.
        if (drawWidth != 0 && drawHeight != 0) {
            x = std::clamp((x - drawX) * frameWidth / drawWidth, 0, frameWidth - 1);
            y = std::clamp((y - drawY) * frameHeight / drawHeight, 0, frameHeight - 1);
        }
        int mouseEvents = mouse_get_buttons();
        int buttons = 0;
        if ((mouseEvents & MOUSE_EVENT_LEFT_BUTTON_DOWN_REPEAT) != 0) {
            buttons |= MOUSE_STATE_LEFT_BUTTON_DOWN;
        }
        if ((mouseEvents & MOUSE_EVENT_RIGHT_BUTTON_DOWN_REPEAT) != 0) {
            buttons |= MOUSE_STATE_RIGHT_BUTTON_DOWN;
        }

        // Also now and then while nothing changes: a quiet connection may
        // be closed on the way (proxies, the co-op server's relay).
        static unsigned int lastMouseSent = 0;
        if (x != sentX || y != sentY || buttons != sentButtons || elapsed_time(lastMouseSent) > COOP_NET_KEEPALIVE) {
            lastMouseSent = get_time();
            CoopWriter writer;
            writer.u8(COOP_NET_INPUT_MOUSE);
            writer.u16((unsigned int)x);
            writer.u16((unsigned int)y);
            writer.u32((unsigned int)buttons);
            coop_connection_send(&connection, COOP_NET_MSG_INPUT, writer.data);
            sentX = x;
            sentY = y;
            sentButtons = buttons;
        }

        // Host output.
        coop_connection_flush(&connection);
        coop_connection_receive(&connection);

        bool redraw = false;
        CoopMessage message;
        while (coop_connection_next(&connection, &message)) {
            if (message.type == COOP_NET_MSG_FRAME_PACKED) {
                // Unpacked, it's a normal frame.
                static std::vector<unsigned char> unpacked;
                CoopReader packed(message.payload);
                size_t size = packed.u32();
                bool ok = packed.ok() && size <= COOP_NET_MAX_MESSAGE
                    && coop_net_decompress(message.payload.data() + 4, message.payload.size() - 4, size, &unpacked);
                if (!ok) {
                    debug_printf("\nCOOP NET: damaged packed frame\n");
                    connection.failed = true;
                    break;
                }
                message.payload.swap(unpacked);
                message.type = COOP_NET_MSG_FRAME;
            }
            CoopReader reader(message.payload);
            switch (message.type) {
            case COOP_NET_MSG_FRAME:
                framesReceived++;
                if (true) {
                    int width = reader.u16();
                    int height = reader.u16();
                    int flags = reader.u8();

                    if (width != frameWidth || height != frameHeight) {
                        frame.assign((size_t)width * height, 0);
                        frameWidth = width;
                        frameHeight = height;

                        if (width > 0 && height > 0) {
                            if (width * windowHeight <= height * windowWidth) {
                                drawHeight = windowHeight;
                                drawWidth = width * windowHeight / height;
                            } else {
                                drawWidth = windowWidth;
                                drawHeight = height * windowWidth / width;
                            }
                            drawX = (windowWidth - drawWidth) / 2;
                            drawY = (windowHeight - drawHeight) / 2;
                            drawColumns.resize(drawWidth);
                            for (int column = 0; column < drawWidth; column++) {
                                drawColumns[column] = column * width / drawWidth;
                            }
                            memset(windowBuffer, colorTable[0], (size_t)windowWidth * windowHeight);
                            debug_printf("COOP NET: frames %dx%d shown at %dx%d\n", width, height, drawWidth, drawHeight);
                        }
                    }

                    if ((flags & 1) != 0) {
                        const unsigned char* rgb = reader.bytes(768);
                        if (rgb != NULL) {
                            unsigned char palette[768];
                            for (int index = 0; index < 768; index++) {
                                palette[index] = rgb[index] >> 2;
                            }
                            setSystemPalette(palette);
                        }
                    }

                    // The camera moved: the map part of our copy moves the
                    // same way before the changes go on it.
                    if ((flags & 2) != 0) {
                        CoopFrameShift shift;
                        shift.set = true;
                        shift.dx = (short)reader.u16();
                        shift.dy = (short)reader.u16();
                        shift.x = reader.u16();
                        shift.y = reader.u16();
                        shift.width = reader.u16();
                        shift.height = reader.u16();
                        if (reader.ok()) {
                            coop_net_shift_frame(frame.data(), frameWidth, frameHeight, shift);
                        }
                    }

                    size_t total = frame.size();
                    size_t index = 0;
                    while (index < total && reader.ok()) {
                        index += coop_read_varint(&reader);
                        size_t count = coop_read_varint(&reader);
                        const unsigned char* pixels = reader.bytes(count);
                        if (pixels == NULL || index + count > total) {
                            break;
                        }
                        memcpy(frame.data() + index, pixels, count);
                        index += count;
                    }

                    redraw = true;
                }
                break;
            case COOP_NET_MSG_STATUS:
                status = reader.str();
                redraw = true;
                break;
            case COOP_NET_MSG_SOUND:
                if (true) {
                    int event = reader.u8();
                    std::string name = reader.str();
                    int a = (int)reader.u32();
                    int b = (int)reader.u32();
                    int c = (int)reader.u32();
                    if (reader.ok()) {
                        soundsPlayed++;
                        switch (event) {
                        case GSOUND_EVENT_SFX:
                            gsound_play_sfx_file_volume(name.c_str(), a);
                            break;
                        case GSOUND_EVENT_MUSIC:
                            gsound_background_play(name.c_str(), a, b, c);
                            break;
                        case GSOUND_EVENT_MUSIC_STOP:
                            gsound_background_stop();
                            break;
                        case GSOUND_EVENT_SPEECH:
                            if (a == 0 && b == 0 && c == 0) {
                                // A conversation's lip-synced speech.
                                gsound_speech_play(name.c_str(), 10, 14, 15);
                            } else {
                                gsound_speech_play(name.c_str(), a, b, c);
                            }
                            break;
                        case GSOUND_EVENT_SPEECH_STOP:
                            gsound_speech_stop();
                            break;
                        }
                    }
                }
                break;
            case COOP_NET_MSG_CHARACTER:
                // A copy of our character to keep; the last one stays as a
                // backup.
                if (!message.payload.empty()) {
                    remove(COOP_NET_CHARACTER_FILE ".bak");
                    rename(COOP_NET_CHARACTER_FILE, COOP_NET_CHARACTER_FILE ".bak");
                    FILE* stream = fopen(COOP_NET_CHARACTER_FILE, "wb");
                    if (stream != NULL) {
                        fwrite(message.payload.data(), 1, message.payload.size(), stream);
                        fclose(stream);
                        debug_printf("\nCOOP NET: saved a copy of our character (%d bytes)\n", (int)message.payload.size());
                    }
                }
                break;
            case COOP_NET_MSG_BYE:
                hostLeft = true;
                hostSaidBye = true;
                break;
            }
        }

        // Frames received so far, so the host sends the next ones.
        if (framesReceived != framesAcked && !coop_net_no_acks) {
            CoopWriter writer;
            writer.u32(framesReceived);
            if (coop_net_frame_check) {
                writer.u32(coop_net_frame_hash(frame.data(), frame.size()));
            }
            coop_connection_send(&connection, COOP_NET_MSG_ACK, writer.data);
            framesAcked = framesReceived;
        }

        if (connection.failed || coop_client_exit_requested) {
            hostLeft = true;
        }

        if (redraw && frameWidth != 0) {
            int width = drawX + drawWidth;
            int height = drawY + drawHeight;
            for (int row = 0; row < drawHeight; row++) {
                unsigned char* dest = windowBuffer + (size_t)(drawY + row) * windowWidth + drawX;
                const unsigned char* src = frame.data() + (size_t)(row * frameHeight / drawHeight) * frameWidth;
                if (drawWidth == frameWidth) {
                    memcpy(dest, src, drawWidth);
                } else {
                    for (int column = 0; column < drawWidth; column++) {
                        dest[column] = src[drawColumns[column]];
                    }
                }
            }

            if (!status.empty()) {
                // Status line over the top of the picture.
                for (int row = 0; row < 14 && row < height; row++) {
                    memset(windowBuffer + row * windowWidth, colorTable[0], width);
                }
                int oldFont = text_curr();
                text_font(101);
                text_to_buf(windowBuffer + 2 * windowWidth + 4, status.c_str(), width - 8, windowWidth, colorTable[992]);
                text_font(oldFont);
            }

            win_draw(window);
        }

        // Only a new picture needs presenting (each present uploads the
        // whole window, 60 times a second: over a core on a big screen);
        // otherwise ten times a second, in case the window was covered.
        if (redraw || elapsed_time(lastPresent) >= 100) {
            renderPresent();
            lastPresent = get_time();
        }
        sharedFpsLimiter.throttle();
    }

    debug_printf("\nCOOP NET: host left (%d sounds played)\n", soundsPlayed);
    coop_net_cpu_report("player");
    if (!coop_client_exit_requested) {
        // A goodbye from the host, or the connection broke (network, or the
        // co-op server between restarting): then joining again may work.
        // (Before a first picture: the join itself broke, say why it may.)
        coop_net_error = hostSaidBye ? "Player 1 has left the game."
            : framesReceived == 0 ? "The connection to player 1 closed before the game showed. Try again; if it keeps happening, check that you both have the same version and send a bug report (MULTIPLAYER > REPORT BUG)."
                                  : "The connection to player 1 was lost.";
    }
    coop_client_exit_hook();
    win_delete(window);
    frameWidth = 0;
    frameHeight = 0;
    drawWidth = 0;
    drawHeight = 0;

    // The game's own cursor again (the menu after leaving).
    gmouse_set_cursor(MOUSE_CURSOR_ARROW);

    coop_connection_send(&connection, COOP_NET_MSG_BYE, std::vector<unsigned char>());
    coop_connection_flush(&connection);
    coop_connection_close(&connection);
    return 0;
}

} // namespace fallout
