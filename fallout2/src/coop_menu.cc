#include "coop_menu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#include <SDL.h>

#include "autotest.h"
#include "coop_compat.h"
#include "dbox.h"
#include "config.h"
#include "coop.h"
#include "coop_crash.h"
#include "coop_host.h"
#include "coop_http.h"
#include "coop_lobby.h"
#include "coop_net.h"
#include "coop_update.h"
#include "game_config.h"
#include "mainmenu.h"
#include "palette.h"
#include "color.h"
#include "debug.h"
#include "kb.h"
#include "svga.h"

namespace fallout {

#define COOP_MENU_ADDRESS_MAX 40

static const char* coop_menu_labels[] = { "HOST GAME", "JOIN GAME", "MORE", "UPDATE", "REPORT BUG", "BACK" };
static const int coop_menu_keys[] = { KEY_LOWERCASE_H, KEY_LOWERCASE_J, KEY_LOWERCASE_M, KEY_LOWERCASE_U, KEY_LOWERCASE_R, KEY_LOWERCASE_B };
#define COOP_MENU_ENTRIES 6

// While working: the buttons above the bottom slot, which shows status.
#define COOP_MENU_BUSY_ENTRIES 3

// Co-op turned on by this menu (as opposed to fallout.cfg), to be turned
// off again when the game ends.
static bool coop_menu_session = false;

static std::string coop_menu_server();
static std::string coop_menu_relay();
static bool coop_menu_newer_version();
static int coop_menu_update(bool ask = true);

// The newest version on the co-op server (empty: not known).
static std::string coop_menu_latest_version;

static void coop_menu_message(const char* title, const char** lines, int count)
{
    // Pip-Boy green, like the setup terminal.
    dialog_out(title, lines, count, 169, 116, colorTable[992], NULL, colorTable[992], DIALOG_BOX_LARGE);
}

// NEW GAME / LOAD GAME; MAIN_MENU_NEW_GAME, MAIN_MENU_LOAD_GAME or -1.
static int coop_menu_choose_game()
{
    const char* labels[] = { "NEW GAME", "LOAD GAME", "BACK" };
    const int keys[] = { KEY_LOWERCASE_N, KEY_LOWERCASE_L, KEY_LOWERCASE_B };
    switch (main_menu_choose(labels, keys, 3)) {
    case 0:
        return MAIN_MENU_NEW_GAME;
    case 1:
        return MAIN_MENU_LOAD_GAME;
    }
    return -1;
}

// How many play in a game this player hosts: 2, 3, 4, 2... Saved.
static void coop_menu_toggle_players()
{
    int count = coop_net_max_clients_value() + 1;
    count = count >= COOP_MAX_PLAYERS ? 2 : count + 1;
    coop_net_set_max_clients(count - 1);
    config_set_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_PLAYERS_KEY, count);
    gconfig_save();
}

static int coop_menu_host()
{
    int game = coop_menu_choose_game();
    if (game == -1) {
        return -1;
    }

    // On the LAN games are found anyway. Online, player 2 joins through the
    // co-op server (no open port needed): listed in everyone's server
    // browser, or only with the game's code.
    bool listOnline = false;
    bool online = false;
    if (!coop_menu_server().empty()) {
        int choice;
        for (;;) {
            char players[32];
            snprintf(players, sizeof(players), "PLAYERS: %d", coop_net_max_clients_value() + 1);
            char harder[32];
            snprintf(harder, sizeof(harder), "HARDER: %s", coop_harder_setting() ? "ON" : "OFF");
            const char* labels[] = { "LAN ONLY", "LIST ONLINE", "WITH A FRIEND", players, harder, "BACK" };
            const int keys[] = { KEY_LOWERCASE_L, KEY_LOWERCASE_O, KEY_LOWERCASE_P, KEY_LOWERCASE_N, KEY_LOWERCASE_H, KEY_LOWERCASE_B };
            choice = main_menu_choose(labels, keys, 6);
            if (choice == 3) {
                coop_menu_toggle_players();
            } else if (choice == 4) {
                coop_set_harder(!coop_harder_setting());
                config_set_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_HARDER_KEY, coop_harder_setting() ? 1 : 0);
                gconfig_save();
                if (coop_harder_setting()) {
                    const char* lines[] = { "Enemies hold out longer the", "more of you play: 1.5 times", "with two players, twice with", "three, 2.5 times with four.", "Your setting counts." };
                    coop_menu_message("HARDER", lines, 5);
                }
            } else {
                break;
            }
        }
        if (choice < 0 || choice > 2) {
            return -1;
        }
        listOnline = choice == 1;
        online = choice != 0;
    }

    coop_set_enabled(true);
    coop_net_set_mode(COOP_NET_HOST);
    main_menu_status("Starting...");
    coop_net_set_any_port_ok(online);
    coop_host_init();
    coop_net_set_any_port_ok(false);
    main_menu_status("");

    if (!coop_host_active()) {
        coop_net_set_mode(COOP_NET_NONE);
        coop_set_enabled(false);

        char port[64];
        snprintf(port, sizeof(port), "Port %d may be in use, or blocked.", coop_net_port());
        const char* lines[] = { "Could not start hosting.", port };
        coop_menu_message("MULTIPLAYER", lines, 2);
        return -1;
    }

    coop_menu_session = true;

    std::string relayError;
    if (online) {
        main_menu_status("Connecting to the co-op server...");
        coop_net_set_relay_server(coop_menu_relay());
        if (!coop_net_relay_host_start(&relayError)) {
            listOnline = false;
        }
        main_menu_status("");
    }

    if (listOnline) {
        coop_lobby_host_start(coop_menu_server(), true);
    }

    if (online && coop_net_relay_code()[0] != '\0') {
        // The code copied, to paste to a friend; their JOIN GAME offers a
        // copied code by itself.
        bool copied = SDL_SetClipboardText(coop_net_relay_code()) == 0;
        char title[64];
        snprintf(title, sizeof(title), "GAME CODE  %s", coop_net_relay_code());
        const char* lines[] = {
            copied ? "Copied: paste it to your friends." : "Send this code to your friends.",
            "They press MULTIPLAYER, JOIN GAME",
            listOnline ? "and pick this game, or enter the code." : "and enter (or paste) the code.",
            "No open ports needed. Everyone",
            "needs the same version of the mod.",
        };
        coop_menu_message(title, lines, 5);
        return game;
    }

    if (online) {
        const char* lines[] = { "Could not reach the co-op server:", relayError.c_str(), "Hosting on this network only." };
        coop_menu_message("HOSTING", lines, 3);
    }

    char addresses[160];
    coop_net_local_addresses(addresses, sizeof(addresses));
    if (const char* shown = getenv("FALLOUT_COOP_SHOW_ADDRESS")) {
        // Screenshots without the real address.
        snprintf(addresses, sizeof(addresses), "%s", shown);
    }
    if (addresses[0] == '\0') {
        strcpy(addresses, "(see your network settings)");
    }

    char port[64];
    snprintf(port, sizeof(port), "Port %d (TCP) must be open.", coop_net_port());

    const char* lines[] = {
        "The others: MULTIPLAYER, JOIN GAME,",
        listOnline ? "then this game in the list" : "then this game (same network)",
        "or ENTER ADDRESS and:",
        addresses,
        port,
    };
    coop_menu_message("HOSTING", lines, 5);

    return game;
}

// The copy of our character from earlier games (coop-character.dat), if
// any: join with it or make a new one. False: don't join after all.
static bool coop_menu_choose_character()
{
    std::vector<unsigned char> data;
    FILE* stream = fopen(COOP_NET_CHARACTER_FILE, "rb");
    if (stream != NULL) {
        unsigned char buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
            data.insert(data.end(), buffer, buffer + read);
        }
        fclose(stream);
    }

    std::string name;
    int level;
    coop_net_set_bring_character(std::vector<unsigned char>());
    if (!coop_character_info(data, &name, &level)) {
        return true;
    }

    // "NATALIA (LEVEL 3)", short enough for a button.
    if (name.size() > 12) {
        name.resize(12);
    }
    for (char& c : name) {
        c = (char)toupper((unsigned char)c);
    }
    char mine[40];
    snprintf(mine, sizeof(mine), "%s (LEVEL %d)", name.c_str(), level);

    main_menu_status("Join with your own character?");
    const char* labels[] = { mine, "NEW CHARACTER", "BACK" };
    const int keys[] = { KEY_LOWERCASE_M, KEY_LOWERCASE_N, KEY_LOWERCASE_B };
    int choice = main_menu_choose(labels, keys, 3);
    main_menu_status("");
    if (choice == 0) {
        coop_net_set_bring_character(data);
    }
    return choice == 0 || choice == 1;
}

static int coop_menu_connect(const char* address, int port, const std::string& relay = std::string())
{
    if (!coop_menu_choose_character()) {
        return -1;
    }

    main_menu_show_buttons(coop_menu_labels, coop_menu_keys, COOP_MENU_BUSY_ENTRIES);
    main_menu_status("Connecting...");

    int configuredPort = coop_net_port();
    if (port > 0) {
        coop_net_set_port(port);
    }

    coop_set_enabled(true);
    coop_net_set_mode(COOP_NET_CLIENT);
    coop_net_set_host(address);
    coop_net_set_relay_server(coop_menu_relay());
    coop_net_set_relay_join(relay);

    int rc = coop_net_client_run();

    coop_net_set_relay_join("");
    coop_net_set_bring_character(std::vector<unsigned char>());

    coop_net_set_mode(COOP_NET_NONE);
    coop_set_enabled(false);
    coop_net_set_port(configuredPort);

    // The host's palette may still be set.
    loadColorTable("color.pal");
    palette_fade_to(cmap);
    main_menu_status("");

    std::string error = coop_net_last_error();
    if (rc != 0 || !error.empty()) {
        const char* lines[] = { !error.empty() ? error.c_str() : "The connection failed." };
        coop_menu_message(rc != 0 ? "COULD NOT JOIN" : "MULTIPLAYER", lines, 1);

        // Another version, and a newer one is out: updating is a click
        // away (the game starts again with it).
        if (error.find("same version") != std::string::npos && coop_menu_newer_version()) {
            const char* labels[] = { "UPDATE NOW", "BACK" };
            const int keys[] = { KEY_LOWERCASE_U, KEY_LOWERCASE_B };
            if (main_menu_choose(labels, keys, 2) == 0) {
                coop_menu_update(false);
            }
        }
    }

    return -1;
}

// A game code on the clipboard (copied from a friend's message), or empty.
static std::string coop_menu_clipboard_code()
{
    std::string code;
    if (!SDL_HasClipboardText()) {
        return code;
    }
    char* text = SDL_GetClipboardText();
    if (text == NULL) {
        return code;
    }
    std::string copied = text;
    SDL_free(text);
    size_t first = copied.find_first_not_of(" \t\r\n");
    size_t last = copied.find_last_not_of(" \t\r\n");
    if (first != std::string::npos && last - first < 12) {
        copied = copied.substr(first, last - first + 1);
        if (!coop_net_parse_code(copied.c_str(), &code)) {
            code.clear();
        }
    }
    return code;
}

static int coop_menu_join_address()
{
    char address[COOP_MENU_ADDRESS_MAX + 1] = "";
    char* lastAddress;
    if (config_get_string(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_HOST_KEY, &lastAddress)) {
        snprintf(address, sizeof(address), "%s", lastAddress);
    }

    if (!main_menu_input("ADDRESS OR CODE", "Enter: connect   Esc: back", address, COOP_MENU_ADDRESS_MAX) || address[0] == '\0') {
        return -1;
    }

    // A game code joins through the co-op server.
    std::string code;
    if (coop_net_parse_code(address, &code)) {
        return coop_menu_connect(address, 0, code);
    }

    // Remembered for next time.
    config_set_string(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_HOST_KEY, address);
    return coop_menu_connect(address, 0);
}

// The server browser: games on this network and listed online, then
// ENTER ADDRESS for anything else.
static int coop_menu_join()
{
    std::string copiedCode = coop_menu_clipboard_code();
    if (!copiedCode.empty()) {
        std::string label = "JOIN " + copiedCode;
        const char* labels[] = { label.c_str(), "FIND GAMES", "BACK" };
        const int keys[] = { KEY_LOWERCASE_J, KEY_LOWERCASE_F, KEY_LOWERCASE_B };
        main_menu_status("A game code is copied.");
        int choice = main_menu_choose(labels, keys, 3);
        main_menu_status("");
        if (choice == 0) {
            return coop_menu_connect(copiedCode.c_str(), 0, copiedCode);
        }
        if (choice != 1) {
            return -1;
        }
    }

    for (;;) {
        main_menu_show_buttons(coop_menu_labels, coop_menu_keys, COOP_MENU_BUSY_ENTRIES);
        main_menu_status("Looking for games...");
        int otherVersions = 0;
        int otherGame = 0;
        std::vector<CoopLobbyGame> games = coop_lobby_find(coop_menu_server(), &otherVersions, &otherGame);
        main_menu_status("");

        // Games are there, but for another version or the other game: say
        // so, or players think nobody is hosting (and updating doesn't
        // help with the other game).
        if (games.empty() && otherVersions > 0) {
            const char* lines[] = {
                "Games were found, but they use",
                "another version of the mod.",
                "Both players need the same one:",
                "MULTIPLAYER, UPDATE.",
            };
            coop_menu_message("JOIN GAME", lines, 4);
        } else if (games.empty() && otherGame > 0) {
            const char* lines[] = {
                "Games were found, but only for",
                "another game. To join one, quit",
                "and pick that game when it",
                "starts.",
            };
            coop_menu_message("JOIN GAME", lines, 4);
        }

        for (size_t first = 0;;) {
            // Up to three games a page, then MORE (or SEARCH AGAIN), ENTER
            // ADDRESS and BACK.
            std::string names[3];
            const char* labels[6];
            int keys[6];
            int count = 0;
            for (size_t index = first; index < games.size() && count < 3; index++) {
                std::string name = games[index].name.empty() ? "WANDERER" : games[index].name;
                for (char& ch : name) {
                    ch = (char)toupper((unsigned char)ch);
                }
                names[count] = name.substr(0, 14);
                labels[count] = names[count].c_str();
                keys[count] = KEY_1 + count;
                count++;
            }
            int games_on_page = count;
            bool more = games.size() > 3;
            labels[count] = more ? "MORE GAMES" : "SEARCH AGAIN";
            keys[count++] = more ? KEY_LOWERCASE_M : KEY_LOWERCASE_S;
            labels[count] = "ENTER ADDRESS";
            keys[count++] = KEY_LOWERCASE_E;
            labels[count] = "BACK";
            keys[count++] = KEY_LOWERCASE_B;

            if (games.empty()) {
                main_menu_status("No open games found.");
            }
            int choice = main_menu_choose(labels, keys, count);
            main_menu_status("");

            if (choice >= 0 && choice < games_on_page) {
                const CoopLobbyGame& game = games[first + choice];
                return coop_menu_connect(game.address.c_str(), game.port, game.relay);
            }
            if (choice == games_on_page) {
                if (!more) {
                    break; // Search again.
                }
                first += 3;
                if (first >= games.size()) {
                    first = 0;
                }
                continue;
            }
            if (choice == games_on_page + 1) {
                return coop_menu_join_address();
            }
            return -1;
        }
    }
}

static int coop_menu_one_pc()
{
    int game = coop_menu_choose_game();
    if (game == -1) {
        return -1;
    }

    coop_set_enabled(true);
    coop_net_set_mode(COOP_NET_NONE);
    coop_menu_session = true;

    const char* lines[] = {
        "In the game, press F9 to let",
        "player 2 join, and again to",
        "switch between the players.",
        "Shift+F9: one more player.",
    };
    coop_menu_message("ONE PC", lines, 4);

    return game;
}

#ifndef COOP_DEFAULT_SERVER
#define COOP_DEFAULT_SERVER ""
#endif

// [coop] server, or the one built in; without a trailing slash; "" if none.
static std::string coop_menu_server()
{
    char* server;
    // An empty setting means the built-in server too.
    std::string result = config_get_string(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_SERVER_KEY, &server) && server[0] != '\0' ? server : COOP_DEFAULT_SERVER;
    while (!result.empty() && (result.back() == '/' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

// The relay: [coop] relay ("host:port" or "http://host/path"), else the
// co-op server's relay behind its web server, on port 80.
static std::string coop_menu_relay()
{
    char* relay;
    if (config_get_string(&game_config, COOP_CONFIG_KEY, "relay", &relay) && relay[0] != '\0') {
        return relay;
    }

    std::string host = coop_menu_server();
    size_t scheme = host.find("://");
    if (scheme != std::string::npos) {
        host = host.substr(scheme + 3);
    }
    size_t end = host.find_first_of(":/");
    if (end != std::string::npos) {
        host = host.substr(0, end);
    }
    return host.empty() ? std::string() : "http://" + host + "/fallout-coop/relay";
}

// "1.10" > "1.9".
static int coop_menu_compare_versions(const std::string& a, const std::string& b)
{
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() || j < b.size()) {
        long x = strtol(a.c_str() + i, NULL, 10);
        long y = strtol(b.c_str() + j, NULL, 10);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        i = a.find('.', i);
        j = b.find('.', j);
        i = i == std::string::npos ? a.size() : i + 1;
        j = j == std::string::npos ? b.size() : j + 1;
    }
    return 0;
}

// The file name at the end of a URL.
static std::string coop_menu_url_file(const std::string& url)
{
    size_t slash = url.rfind('/');
    std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
    size_t query = name.find('?');
    if (query != std::string::npos) {
        name = name.substr(0, query);
    }
    return name.empty() ? "classic-fallout-coop-update.zip" : name;
}

// Where downloads and reports go: next to the game.
static std::string coop_menu_output_path(const std::string& name)
{
    std::string path = name;
    char* basePath = SDL_GetBasePath();
    if (basePath != NULL) {
        path = std::string(basePath) + name;
        SDL_free(basePath);
    }
    return path;
}

static void coop_menu_no_server(const char* title)
{
    const char* lines[] = {
        "No server is set up. Add",
        "server=http://your.server",
        "under [coop] in fallout.cfg.",
    };
    coop_menu_message(title, lines, 3);
}

// UPDATE: <server>/fallout-coop/latest.txt is key=value lines: version,
// notes, and the package to download: url_windows, url_linux, url_macos
// or url.
static int coop_menu_update(bool ask)
{
    std::string server = coop_menu_server();
    if (server.empty()) {
        coop_menu_no_server("UPDATE");
        return -1;
    }

    main_menu_show_buttons(coop_menu_labels, coop_menu_keys, COOP_MENU_BUSY_ENTRIES);
    main_menu_status("Checking...");

    std::string body;
    std::string error;
    bool ok = coop_http_get(server + "/fallout-coop/latest.txt", &body, &error);
    main_menu_status("");

    if (!ok) {
        const char* lines[] = { "Could not check for updates.", error.c_str() };
        coop_menu_message("UPDATE", lines, 2);
        return -1;
    }

#if defined(_WIN32)
    const char* platformKey = "url_windows";
    const char* hashKey = "sha256_windows";
#elif defined(__APPLE__)
    const char* platformKey = "url_macos";
    const char* hashKey = "sha256_macos";
#else
    const char* platformKey = "url_linux";
    const char* hashKey = "sha256_linux";
#endif

    std::string version;
    std::string notes;
    std::string url;
    std::string anyUrl;
    std::string hash;
    std::string anyHash;
    size_t start = 0;
    while (start < body.size()) {
        size_t end = body.find('\n', start);
        if (end == std::string::npos) {
            end = body.size();
        }
        std::string line = body.substr(start, end - start);
        start = end + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }

        size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        if (key == "version") {
            version = value;
        } else if (key == "notes") {
            notes = value;
        } else if (key == platformKey) {
            url = value;
        } else if (key == "url") {
            anyUrl = value;
        } else if (key == hashKey) {
            hash = value;
        } else if (key == "sha256") {
            anyHash = value;
        }
    }

    if (url.empty()) {
        url = anyUrl;
        hash = anyHash;
    }

    if (version.empty()) {
        const char* lines[] = { "The update server's answer", "was not understood." };
        coop_menu_message("UPDATE", lines, 2);
        return -1;
    }

    if (coop_menu_compare_versions(version, COOP_VERSION) <= 0) {
        std::string current = std::string("You have the newest, ") + COOP_VERSION + ".";
        const char* lines[] = { current.c_str() };
        coop_menu_message("UPDATE", lines, 1);
        return -1;
    }

    std::string available = "Version " + version + " is available.";
    std::string notesLine = notes.substr(0, 34);
    const char* lines[] = { available.c_str(), notesLine.c_str() };
    coop_menu_message("UPDATE", lines, notes.empty() ? 1 : 2);

    if (url.empty()) {
        const char* noPackage[] = { "There is no download for", "this system yet." };
        coop_menu_message("UPDATE", noPackage, 2);
        return -1;
    }

    if (ask) {
        const char* labels[] = { "INSTALL", "LATER" };
        const int keys[] = { KEY_LOWERCASE_I, KEY_LOWERCASE_L };
        if (main_menu_choose(labels, keys, 2) != 0) {
            return -1;
        }
    }

    main_menu_status("Downloading...");
    ok = coop_http_get(url, &body, &error, 60000);
    main_menu_status("");

    if (!ok) {
        const char* failed[] = { "The download failed.", error.c_str() };
        coop_menu_message("UPDATE", failed, 2);
        return -1;
    }

    // A damaged download is never installed.
    if (!hash.empty() && coop_update_sha256(body) != hash) {
        const char* failed[] = { "The download is damaged", "(checksum differs). Try again." };
        coop_menu_message("UPDATE", failed, 2);
        return -1;
    }

    std::string path = coop_menu_output_path(coop_menu_url_file(url));
    FILE* stream = fopen(path.c_str(), "wb");
    bool written = stream != NULL && fwrite(body.data(), 1, body.size(), stream) == body.size();
    if (stream != NULL) {
        fclose(stream);
    }

    if (!written) {
        const char* failed[] = { "Could not save the download", "next to the game." };
        coop_menu_message("UPDATE", failed, 2);
        return -1;
    }

    main_menu_status("Installing...");
    ok = coop_update_install(path, &error);
    main_menu_status("");

    if (!ok) {
        const char* failed[] = { "The update failed.", error.c_str() };
        coop_menu_message("UPDATE", failed, 2);
        return -1;
    }

    std::string updated = "Updated to version " + version + ".";
    const char* done[] = { updated.c_str(), "The game starts again now." };
    coop_menu_message("UPDATE", done, 2);

    // Testing aid: install without restarting.
    if (getenv("FALLOUT_UPDATE_NO_RESTART") != NULL) {
        return -1;
    }

    coop_update_restart();
}

// REPORT BUG: a short description plus what helps finding the problem,
// sent to <server>/fallout-coop/report, or saved next to the game.
static int coop_menu_report()
{
    // (The field scrolls: room to say what happened.)
    char description[201] = "";
    if (!main_menu_input("REPORT BUG", "Describe it, then Enter", description, (int)sizeof(description) - 1)) {
        return -1;
    }

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    std::string report = "Classic Fallout Co-op bug report\n";
    report += std::string("Version: ") + COOP_VERSION + "\n";
    report += std::string("Game: ") + coop_net_game_title() + "\n";
    report += std::string("Platform: ") + SDL_GetPlatform() + "\n";
    char checksum[32];
    snprintf(checksum, sizeof(checksum), "%016llx", (unsigned long long)coop_net_data_checksum());
    report += std::string("Game files: ") + checksum + "\n";
    report += std::string("Time: ") + timestamp + "\n";
    report += std::string("Description: ") + description + "\n";

    char* language;
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_LANGUAGE_KEY, &language)) {
        report += std::string("Language: ") + language + "\n";
    }

    char* address;
    if (config_get_string(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_HOST_KEY, &address)) {
        report += std::string("Last host joined: ") + address + "\n";
    }

    std::vector<char> recent(48 * 1024 + 1);
    debug_get_recent(recent.data(), recent.size());
    report += "\n--- Recent log ---\n";
    report += recent.data();
    report += "\n";

    std::string server = coop_menu_server();
    std::string error = "No report server is set up.";
    if (!server.empty()) {
        main_menu_show_buttons(coop_menu_labels, coop_menu_keys, COOP_MENU_BUSY_ENTRIES);
        main_menu_status("Sending...");
        std::string body;
        bool sent = coop_http_post(server + "/fallout-coop/report", "text/plain; charset=utf-8", report, &body, &error);
        main_menu_status("");

        if (sent) {
            const char* lines[] = { "Thank you!", "The report was sent." };
            coop_menu_message("REPORT BUG", lines, 2);
            return -1;
        }
    }

    // Saved for the player to send.
    char name[64];
    strftime(name, sizeof(name), "bug-report-%Y%m%d-%H%M%S.txt", localtime(&now));
    std::string path = coop_menu_output_path(name);
    FILE* stream = fopen(path.c_str(), "wb");
    bool written = stream != NULL && fwrite(report.data(), 1, report.size(), stream) == report.size();
    if (stream != NULL) {
        fclose(stream);
    }

    if (written) {
        const char* lines[] = {
            error.c_str(),
            "Saved next to the game as",
            name,
            "Please send it to us.",
        };
        coop_menu_message("REPORT BUG", lines, 4);
    } else {
        const char* lines[] = { error.c_str(), "Could not save the report." };
        coop_menu_message("REPORT BUG", lines, 2);
    }

    return -1;
}

// OUTLINES: the colors this player sees the players' outlines in
// (colorblind players can pick colors they tell apart). Saved.
static void coop_menu_outlines()
{
    for (;;) {
        int colors[2];
        coop_outline_colors_from_config(colors);

        char player1[32];
        char player2[32];
        snprintf(player1, sizeof(player1), "PLAYER 1: %s", coop_outline_color_name(colors[0]));
        snprintf(player2, sizeof(player2), "PLAYER 2: %s", coop_outline_color_name(colors[1]));
        const char* labels[] = { player1, player2, "BACK" };
        const int keys[] = { KEY_1, KEY_2, KEY_LOWERCASE_B };
        int choice = main_menu_choose(labels, keys, 3);
        if (choice != 0 && choice != 1) {
            return;
        }

        // Next color, never the other player's.
        int other = colors[1 - choice];
        do {
            colors[choice] = (colors[choice] + 1) % COOP_OUTLINE_COLORS;
        } while (colors[choice] == other);

        config_set_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_OUTLINE1_KEY, colors[0]);
        config_set_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_OUTLINE2_KEY, colors[1]);
        gconfig_save();
        for (int index = 0; index < COOP_MAX_PLAYERS; index++) {
            coop_set_outline_colors(index, colors);
        }
    }
}

// Screen sizes for RESOLUTION. Bigger shows more of the map; player 2
// sees player 1's size (scaled to their screen), so bigger also means
// more to send over the network.
static const int coop_menu_resolutions[][2] = {
    { 640, 480 },
    { 800, 600 },
    { 1024, 768 },
    { 1280, 720 },
    { 1280, 960 },
    { 1600, 900 },
    { 1920, 1080 },
};

#define COOP_MENU_RESOLUTIONS (int)(sizeof(coop_menu_resolutions) / sizeof(coop_menu_resolutions[0]))

// RESOLUTION: steps through the screen sizes, saved in f2_res.ini (other
// settings there stay); used from the next start.
static void coop_menu_resolution()
{
    Config resolutionConfig;
    if (!config_init(&resolutionConfig)) {
        return;
    }
    config_load(&resolutionConfig, "f2_res.ini", false);

    int width = 640;
    int height = 480;
    config_get_value(&resolutionConfig, "MAIN", "SCR_WIDTH", &width);
    config_get_value(&resolutionConfig, "MAIN", "SCR_HEIGHT", &height);

    int current = -1;
    for (int index = 0; index < COOP_MENU_RESOLUTIONS; index++) {
        if (coop_menu_resolutions[index][0] == width && coop_menu_resolutions[index][1] == height) {
            current = index;
        }
    }

    for (;;) {
        char label[32];
        if (current >= 0) {
            snprintf(label, sizeof(label), "SCREEN: %dx%d", coop_menu_resolutions[current][0], coop_menu_resolutions[current][1]);
        } else {
            snprintf(label, sizeof(label), "SCREEN: %dx%d", width, height);
        }
        const char* labels[] = { label, "BACK" };
        const int keys[] = { KEY_LOWERCASE_R, KEY_LOWERCASE_B };
        int choice = main_menu_choose(labels, keys, 2);
        if (choice != 0) {
            break;
        }

        current = (current + 1) % COOP_MENU_RESOLUTIONS;
        config_set_value(&resolutionConfig, "MAIN", "SCR_WIDTH", coop_menu_resolutions[current][0]);
        config_set_value(&resolutionConfig, "MAIN", "SCR_HEIGHT", coop_menu_resolutions[current][1]);
        config_save(&resolutionConfig, "f2_res.ini", false);
    }

    config_exit(&resolutionConfig);

    if (current >= 0
        && (coop_menu_resolutions[current][0] != screenGetWidth() || coop_menu_resolutions[current][1] != screenGetHeight())) {
        const char* lines[] = { "Restart the game to use", "the new screen size." };
        coop_menu_message("RESOLUTION", lines, 2);
    }
}

// MORE: ONE PC, the outline colors, the screen size, KNOCKOUT (a player
// who would die is knocked out instead while the other is up) and TALK
// (who starts conversations). Player 1's choices count. Saved.
static int coop_menu_more()
{
    for (;;) {
        char knockout[32];
        snprintf(knockout, sizeof(knockout), "KNOCKOUT: %s", coop_knockout_setting() ? "ON" : "OFF");
        char talk[32];
        snprintf(talk, sizeof(talk), "TALK: %s", coop_talk_setting() ? "BOTH" : "PLAYER 1");
        const char* labels[] = { "ONE PC", "OUTLINES", "RESOLUTION", knockout, talk, "BACK" };
        const int keys[] = { KEY_LOWERCASE_O, KEY_LOWERCASE_C, KEY_LOWERCASE_R, KEY_LOWERCASE_K, KEY_LOWERCASE_T, KEY_LOWERCASE_B };
        int choice = main_menu_choose(labels, keys, 6);
        if (choice == 0) {
            return coop_menu_one_pc();
        }
        if (choice == 1) {
            coop_menu_outlines();
        } else if (choice == 2) {
            coop_menu_resolution();
        } else if (choice == 3) {
            coop_set_knockout(!coop_knockout_setting());
            config_set_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_KNOCKOUT_KEY, coop_knockout_setting() ? 1 : 0);
            gconfig_save();
            if (coop_knockout_setting()) {
                const char* lines[] = { "A player who would die is", "knocked out instead while the", "other is still up. Heal them", "(Stimpak, First Aid) to wake", "them. Player 1's setting counts." };
                coop_menu_message("KNOCKOUT", lines, 5);
            }
        } else if (choice == 4) {
            coop_set_talk(!coop_talk_setting());
            config_set_value(&game_config, COOP_CONFIG_KEY, COOP_CONFIG_TALK_KEY, coop_talk_setting() ? 1 : 0);
            gconfig_save();
            const char* both[] = { "Every player starts", "conversations, each as their", "own character. Player 1's", "setting counts." };
            const char* one[] = { "Only player 1 talks to people.", "The others trade directly", "with merchants. Player 1's", "setting counts." };
            coop_menu_message("TALK", coop_talk_setting() ? both : one, 4);
        } else {
            return -1;
        }
    }
}

// Once per start, when MULTIPLAYER opens: is there a newer version? Many
// players never press UPDATE and then miss what's new (and can't join
// friends who updated). Not in automated tests, unless asked for.
static bool coop_menu_newer_version()
{
    static bool checked = false;
    static bool newer = false;
    if (checked) {
        return newer;
    }
    checked = true;

    std::string server = coop_menu_server();
    if (server.empty() || (autotest_requested() && getenv("COOP_AUTOTEST_UPDATE_CHECK") == NULL)) {
        return false;
    }

    main_menu_show_buttons(coop_menu_labels, coop_menu_keys, COOP_MENU_BUSY_ENTRIES);
    main_menu_status("Checking for updates...");
    std::string body;
    std::string error;
    if (coop_http_get(server + "/fallout-coop/latest.txt", &body, &error, 2000)) {
        size_t at = body.find("version=");
        if (at != std::string::npos) {
            size_t end = body.find_first_of("\r\n", at);
            std::string version = body.substr(at + 8, end == std::string::npos ? std::string::npos : end - at - 8);
            newer = coop_menu_compare_versions(version, COOP_VERSION) > 0;
            coop_menu_latest_version = version;
            debug_printf("\nCOOP MENU: latest version %s%s\n", version.c_str(), newer ? " (newer)" : "");
        }
    }
    main_menu_status("");
    return newer;
}

// The game crashed last time: offer to send what it left behind.
static void coop_menu_offer_crash_report()
{
    static bool asked = false;
    std::string report;
    if (asked || !coop_crash_pending(&report)) {
        return;
    }
    asked = true;

    const char* lines[] = {
        "The game crashed last time.",
        "A crash report helps us fix it:",
        "the mod version, where it crashed",
        "and the recent game log.",
    };
    coop_menu_message("CRASH REPORT", lines, 4);

    const char* labels[] = { "SEND REPORT", "DON'T SEND" };
    const int keys[] = { KEY_LOWERCASE_S, KEY_LOWERCASE_D };
    if (main_menu_choose(labels, keys, 2) != 0) {
        coop_crash_done();
        return;
    }

    // What the player plays, as in a bug report (not known while crashing:
    // the report was written then without them).
    std::string info = std::string("Game: ") + coop_net_game_title() + "\n";
    char checksum[32];
    snprintf(checksum, sizeof(checksum), "%016llx", (unsigned long long)coop_net_data_checksum());
    info += std::string("Game files: ") + checksum + "\n";
    char* language;
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_LANGUAGE_KEY, &language)) {
        info += std::string("Language: ") + language + "\n";
    }
    size_t firstLineEnd = report.find('\n');
    report.insert(firstLineEnd != std::string::npos ? firstLineEnd + 1 : 0, info);

    std::string server = coop_menu_server();
    std::string body;
    std::string error = "No report server is set up.";
    main_menu_show_buttons(coop_menu_labels, coop_menu_keys, COOP_MENU_BUSY_ENTRIES);
    main_menu_status("Sending...");
    bool sent = !server.empty()
        && coop_http_post(server + "/fallout-coop/report", "text/plain; charset=utf-8", report, &body, &error);
    main_menu_status("");
    if (sent) {
        coop_crash_done();
        const char* thanks[] = { "Thank you!", "The crash report was sent." };
        coop_menu_message("CRASH REPORT", thanks, 2);
    } else {
        // Kept for next time.
        const char* failed[] = { "Could not send it now:", error.c_str(), "We'll ask again next time." };
        coop_menu_message("CRASH REPORT", failed, 3);
    }
}

int coop_menu_run()
{
    coop_menu_offer_crash_report();

    const char* labels[COOP_MENU_ENTRIES];
    memcpy(labels, coop_menu_labels, sizeof(labels));
    if (coop_menu_newer_version()) {
        labels[3] = "NEW UPDATE!";

        // Asked once a session: with different versions players can't
        // play together.
        static bool asked = false;
        if (!asked) {
            asked = true;
            std::string line = "Version " + coop_menu_latest_version + " is out (you have " + COOP_VERSION + ").";
            const char* lines[] = { line.c_str(), "Everyone needs the same version", "to play together." };
            coop_menu_message("NEW UPDATE", lines, 3);
            const char* updateLabels[] = { "UPDATE NOW", "LATER" };
            const int updateKeys[] = { KEY_LOWERCASE_U, KEY_LOWERCASE_L };
            if (main_menu_choose(updateLabels, updateKeys, 2) == 0) {
                coop_menu_update(false);
            }
        }
    }
    const int* keys = coop_menu_keys;

    int rc = -1;
    for (;;) {
        int choice = main_menu_choose(labels, keys, COOP_MENU_ENTRIES);
        if (choice == 0) {
            rc = coop_menu_host();
        } else if (choice == 1) {
            rc = coop_menu_join();
        } else if (choice == 2) {
            rc = coop_menu_more();
        } else if (choice == 3) {
            rc = coop_menu_update();
        } else if (choice == 4) {
            rc = coop_menu_report();
        } else {
            break;
        }

        if (rc != -1) {
            break;
        }
    }

    main_menu_restore();
    return rc;
}

void coop_menu_end_session()
{
    if (!coop_menu_session) {
        return;
    }

    coop_host_exit();
    coop_net_set_mode(COOP_NET_NONE);
    coop_set_enabled(false);
    coop_menu_session = false;
}

} // namespace fallout
