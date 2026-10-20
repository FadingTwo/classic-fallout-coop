#include "coop_setup.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <SDL.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "config.h"
#include "coop.h"
#include "coop_compat.h"
#include "coop_http.h"
#include "coop_setup_font.h"
#include "coop_update.h"
#include "coop_zip.h"
#include "game_config.h"
#include "platform_compat.h"

namespace fallout {

#define SETUP_WIDTH 640
#define SETUP_HEIGHT 480
#define SETUP_MARGIN 24
#define SETUP_LINE 22

// Pip-Boy green.
#define SETUP_GREEN 0x33FF66
#define SETUP_DIM 0x1E9940

namespace fs = std::filesystem;

typedef struct SetupInstall {
    std::string path;
    std::string label;
} SetupInstall;

// -----------------------------------------------------------------------------
// Finding the game

// Finds `name` in `dir`, ignoring case (GOG and Steam ship upper case names).
static bool setup_find(const fs::path& dir, const char* name, fs::path* out)
{
    std::error_code error;
    for (fs::directory_iterator it(dir, error); !error && it != fs::directory_iterator(); it.increment(error)) {
        if (compat_stricmp(it->path().filename().string().c_str(), name) == 0) {
            *out = it->path();
            return true;
        }
    }
    return false;
}

// Fallout 1 has a MASTER.DAT and CRITTER.DAT too (and many own both);
// Fallout 2 also ships fallout2.exe or patch000.dat.
static bool setup_is_fallout2(const fs::path& dir)
{
    fs::path found;
    return setup_find(dir, "fallout2.exe", &found) || setup_find(dir, "patch000.dat", &found);
}

static bool setup_is_install(const fs::path& dir)
{
    fs::path master;
    fs::path critter;
    return setup_find(dir, "master.dat", &master) && setup_find(dir, "critter.dat", &critter) && setup_is_fallout2(dir);
}

static void setup_add(std::vector<SetupInstall>* installs, const fs::path& dir, const char* label)
{
    std::error_code error;
    if (!fs::is_directory(dir, error) || !setup_is_install(dir)) {
        return;
    }

    fs::path canonical = fs::weakly_canonical(dir, error);
    std::string path = (error ? dir : canonical).string();
    for (const SetupInstall& install : *installs) {
        if (install.path == path) {
            return;
        }
    }

    installs->push_back({ path, label });
}

// Stores whose folder names we can't be sure of (Epic, Amazon): Fallout 1
// anywhere up to two folders below `root`.
static void setup_scan(std::vector<SetupInstall>* installs, const fs::path& root, const char* label, int depth = 2)
{
    std::error_code error;
    if (depth < 0 || !fs::is_directory(root, error)) {
        return;
    }
    setup_add(installs, root, label);
    for (fs::directory_iterator it(root, error); !error && it != fs::directory_iterator(); it.increment(error)) {
        if (it->is_directory(error)) {
            setup_scan(installs, it->path(), label, depth - 1);
        }
    }
}

// Steam libraries listed in libraryfolders.vdf ("path" "...").
static void setup_add_steam_libraries(std::vector<SetupInstall>* installs, const fs::path& steam)
{
    setup_add(installs, steam / "steamapps" / "common" / "Fallout 2", "Steam");

    FILE* stream = fopen((steam / "steamapps" / "libraryfolders.vdf").string().c_str(), "rt");
    if (stream == NULL) {
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), stream) != NULL) {
        char* key = strstr(line, "\"path\"");
        if (key == NULL) {
            continue;
        }

        char* start = strchr(key + 6, '"');
        char* end = start != NULL ? strchr(start + 1, '"') : NULL;
        if (end == NULL) {
            continue;
        }

        std::string library(start + 1, end);
        // Escaped backslashes on Windows.
        std::string unescaped;
        for (size_t index = 0; index < library.size(); index++) {
            if (library[index] == '\\' && index + 1 < library.size() && library[index + 1] == '\\') {
                index++;
            }
            unescaped += library[index];
        }

        setup_add(installs, fs::path(unescaped) / "steamapps" / "common" / "Fallout 2", "Steam");
    }

    fclose(stream);
}

static std::vector<SetupInstall> setup_find_installs()
{
    std::vector<SetupInstall> installs;

    // Testing aid and power users: extra places to look.
    const char* extra = getenv("FALLOUT_SETUP_SEARCH");
    if (extra != NULL) {
        std::string list = extra;
        size_t start = 0;
        while (start <= list.size()) {
            size_t end = list.find(';', start);
            if (end == std::string::npos) {
                end = list.size();
            }
            if (end > start) {
                setup_add(&installs, list.substr(start, end - start), "");
            }
            start = end + 1;
        }
    }

    setup_add(&installs, fs::current_path(), "this folder");

    char* basePath = SDL_GetBasePath();
    if (basePath != NULL) {
        setup_add(&installs, basePath, "next to the game");
        SDL_free(basePath);
    }

#ifdef _WIN32
    DWORD driveMask = GetLogicalDrives();
    for (int letter = 2; letter < 26; letter++) {
        if ((driveMask & (1u << letter)) == 0 || GetDriveTypeA((std::string(1, (char)('A' + letter)) + ":\\").c_str()) != DRIVE_FIXED) {
            continue;
        }
        std::string root = std::string(1, (char)('A' + letter)) + ":\\";
        setup_scan(&installs, root + "Program Files\\Epic Games", "Epic");
        setup_scan(&installs, root + "Epic Games", "Epic");
        setup_scan(&installs, root + "Amazon Games\\Library", "Amazon");
        setup_scan(&installs, root + "Program Files\\Amazon Games\\Library", "Amazon");
        setup_add(&installs, root + "GOG Games\\Fallout 2", "GOG");
        setup_add(&installs, root + "Program Files (x86)\\GOG Galaxy\\Games\\Fallout 2", "GOG");
        setup_add(&installs, root + "Program Files\\GOG Galaxy\\Games\\Fallout 2", "GOG");
        setup_add(&installs, root + "Games\\Fallout 2", "");
        setup_add(&installs, root + "Fallout 2", "");
        setup_add(&installs, root + "Program Files (x86)\\Interplay\\Fallout 2", "CD");
        setup_add_steam_libraries(&installs, root + "Program Files (x86)\\Steam");
        setup_add_steam_libraries(&installs, root + "Program Files\\Steam");
        setup_add_steam_libraries(&installs, root + "SteamLibrary");
    }
#else
    const char* home = getenv("HOME");
    if (home != NULL) {
        fs::path h = home;
        setup_add(&installs, h / "Games" / "Heroic" / "Fallout 2", "Heroic");
        setup_add(&installs, h / "GOG Games" / "Fallout 2", "GOG");
        setup_add(&installs, h / "Games" / "Fallout 2", "");
        // Lutris.
        setup_add(&installs, h / "Games" / "fallout-2", "Lutris");
        setup_add(&installs, h / "Games" / "gog" / "fallout-2", "Lutris");
        setup_add(&installs, h / "Games" / "GOG" / "Fallout 2", "GOG");
        setup_add(&installs, h / "Fallout 2", "");
        setup_add(&installs, h / ".wine" / "drive_c" / "GOG Games" / "Fallout 2", "Wine");
        setup_add(&installs, h / ".wine" / "drive_c" / "Program Files (x86)" / "GOG Galaxy" / "Games" / "Fallout 2", "Wine");
        setup_add_steam_libraries(&installs, h / ".steam" / "steam");
        setup_add_steam_libraries(&installs, h / ".local" / "share" / "Steam");
        setup_add_steam_libraries(&installs, h / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
        setup_add_steam_libraries(&installs, h / "Library" / "Application Support" / "Steam");
        // Heroic's Epic and Amazon installs.
        setup_scan(&installs, h / "Games" / "Heroic", "Heroic", 3);
    }
#endif

    return installs;
}

// Writes the paths of the installation in `dir` into fallout2.cfg.
static bool setup_write_config(const char* configPath, const fs::path& dir)
{
    fs::path master;
    fs::path critter;
    fs::path data;
    if (!setup_find(dir, "master.dat", &master) || !setup_find(dir, "critter.dat", &critter)) {
        return false;
    }

    if (!setup_find(dir, "data", &data)) {
        // Saves and patches need it; the original installer creates it.
        std::error_code error;
        data = dir / "DATA";
        fs::create_directories(data, error);
    }

    fs::path sound;
    fs::path music;
    std::string musicPath;
    if ((setup_find(dir, "sound", &sound) && setup_find(sound, "music", &music))
        || (setup_find(data, "sound", &sound) && setup_find(sound, "music", &music))) {
        musicPath = music.string() + std::string(1, (char)fs::path::preferred_separator);
    }

    Config config;
    if (!config_init(&config)) {
        return false;
    }

    config_load(&config, configPath, false);
    config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, master.string().c_str());
    config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, critter.string().c_str());
    config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_PATCHES_KEY, data.string().c_str());
    config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_PATCHES_KEY, data.string().c_str());
    if (!musicPath.empty()) {
        config_set_string(&config, GAME_CONFIG_SOUND_KEY, GAME_CONFIG_MUSIC_PATH1_KEY, musicPath.c_str());
        config_set_string(&config, GAME_CONFIG_SOUND_KEY, GAME_CONFIG_MUSIC_PATH2_KEY, musicPath.c_str());
    }

    bool saved = config_save(&config, configPath, false);
    config_exit(&config);
    return saved;
}

// -----------------------------------------------------------------------------
// Terminal screen

typedef struct SetupScreen {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    unsigned int pixels[SETUP_WIDTH * SETUP_HEIGHT];
} SetupScreen;

static void setup_clear(SetupScreen* screen)
{
    for (int index = 0; index < SETUP_WIDTH * SETUP_HEIGHT; index++) {
        screen->pixels[index] = 0x020A04;
    }
}

static void setup_fill(SetupScreen* screen, int x, int y, int width, int height, unsigned int color)
{
    for (int row = std::max(y, 0); row < std::min(y + height, SETUP_HEIGHT); row++) {
        for (int column = std::max(x, 0); column < std::min(x + width, SETUP_WIDTH); column++) {
            screen->pixels[row * SETUP_WIDTH + column] = color;
        }
    }
}

// Draws `text` (truncated to `maxChars`); returns its width.
static int setup_text(SetupScreen* screen, int x, int y, const char* text, unsigned int color, int maxChars = 60)
{
    int drawn = 0;
    for (const char* p = text; *p != '\0' && drawn < maxChars; p++, drawn++) {
        int ch = (unsigned char)*p;
        if (ch < COOP_SETUP_FONT_FIRST || ch > COOP_SETUP_FONT_LAST) {
            ch = '?';
        }

        const unsigned short* glyph = coop_setup_font[ch - COOP_SETUP_FONT_FIRST];
        for (int row = 0; row < COOP_SETUP_FONT_HEIGHT; row++) {
            for (int column = 0; column < COOP_SETUP_FONT_WIDTH; column++) {
                if ((glyph[row] >> (COOP_SETUP_FONT_WIDTH - 1 - column)) & 1) {
                    int px = x + drawn * COOP_SETUP_FONT_WIDTH + column;
                    int py = y + row;
                    if (px >= 0 && px < SETUP_WIDTH && py >= 0 && py < SETUP_HEIGHT) {
                        screen->pixels[py * SETUP_WIDTH + px] = color;
                    }
                }
            }
        }
    }
    return drawn * COOP_SETUP_FONT_WIDTH;
}

// Paths under the home folder as ~/..., as in a terminal.
static std::string setup_display_path(const std::string& path)
{
#ifndef _WIN32
    const char* home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        std::string prefix = std::string(home);
        if (prefix.back() != '/') {
            prefix += '/';
        }
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return "~/" + path.substr(prefix.size());
        }
    }
#endif
    return path;
}

// Long paths: keep the end, which tells them apart.
static std::string setup_shorten(const std::string& text, size_t maxChars)
{
    if (text.size() <= maxChars) {
        return text;
    }
    return "..." + text.substr(text.size() - (maxChars - 3));
}

// Phosphor glow and scan lines, then present.
static void setup_present(SetupScreen* screen)
{
    static unsigned int lit[SETUP_WIDTH * SETUP_HEIGHT];
    memcpy(lit, screen->pixels, sizeof(lit));

    for (int y = 1; y < SETUP_HEIGHT - 1; y++) {
        for (int x = 1; x < SETUP_WIDTH - 1; x++) {
            unsigned int pixel = lit[y * SETUP_WIDTH + x];
            if ((pixel & 0xFF00) > 0x8000) {
                continue;
            }
            int glow = 0;
            glow += (lit[y * SETUP_WIDTH + x - 1] & 0xFF00) > 0x8000;
            glow += (lit[y * SETUP_WIDTH + x + 1] & 0xFF00) > 0x8000;
            glow += (lit[(y - 1) * SETUP_WIDTH + x] & 0xFF00) > 0x8000;
            glow += (lit[(y + 1) * SETUP_WIDTH + x] & 0xFF00) > 0x8000;
            if (glow != 0) {
                unsigned int g = std::min(0x10 + glow * 0x14, 0x60);
                screen->pixels[y * SETUP_WIDTH + x] = (g / 4 << 16) | (g << 8) | (g / 3);
            }
        }
    }

    for (int y = 1; y < SETUP_HEIGHT; y += 2) {
        for (int x = 0; x < SETUP_WIDTH; x++) {
            unsigned int pixel = screen->pixels[y * SETUP_WIDTH + x];
            unsigned int r = ((pixel >> 16) & 0xFF) * 3 / 4;
            unsigned int g = ((pixel >> 8) & 0xFF) * 3 / 4;
            unsigned int b = (pixel & 0xFF) * 3 / 4;
            screen->pixels[y * SETUP_WIDTH + x] = (r << 16) | (g << 8) | b;
        }
    }

    SDL_UpdateTexture(screen->texture, NULL, screen->pixels, SETUP_WIDTH * 4);
    SDL_RenderClear(screen->renderer);
    SDL_RenderCopy(screen->renderer, screen->texture, NULL, NULL);
    SDL_RenderPresent(screen->renderer);
}

static void setup_save_screenshot(SetupScreen* screen, const char* path)
{
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(screen->pixels, SETUP_WIDTH, SETUP_HEIGHT, 32, SETUP_WIDTH * 4, SDL_PIXELFORMAT_RGB888);
    if (surface != NULL) {
        SDL_SaveBMP(surface, path);
        SDL_FreeSurface(surface);
    }
}

// -----------------------------------------------------------------------------
// Fallout: Sonora: downloaded from its makers' release and unpacked here

// The release (its makers' GitHub; free, not for sale). FALLOUT_SONORA_URL
// and FALLOUT_SONORA_SHA256 replace them in tests.
#define SONORA_URL "https://github.com/cambragol/Fallout-Sonora-English/releases/download/1.16.4/Fallout_Sonora_1.16.4E.zip"
#define SONORA_SHA256 "4a96b3326b62c6a908b80d479a5eb598d3f7bc97c14ced3621e92945615b8dfc"
#define SONORA_SIZE_TEXT "1.7 GB"
#define SONORA_FOLDER "Fallout_Sonora_1_16_E/"
#define SONORA_DOWNLOAD "sonora-download.zip"

struct ModDownload;

// What a background job (download, check, unpack) has done so far.
typedef struct ModJob {
    const ModDownload* mod;
    std::atomic<int> stage; // 0 idle, 1 downloading, 2 checking, 3 unpacking, 4 done, 5 failed
    std::atomic<unsigned long long> done;
    std::atomic<unsigned long long> total;
    std::atomic<bool> cancel;
    std::string error;
} ModJob;

static bool mod_progress(void* data, unsigned long long done, unsigned long long total)
{
    ModJob* job = (ModJob*)data;
    job->done = done;
    job->total = total;
    return !job->cancel;
}

// A story mod the launcher downloads from its makers' release: where, what
// to check, how it is unpacked here and made ready, and what its screen says.
typedef struct ModDownload {
    const char* name; // "Fallout: Sonora"
    const char* header; // the screen's title bar
    const char* url;
    const char* urlEnv; // replaces the URL in tests
    const char* sha256;
    const char* sha256Env;
    const char* downloadFile;
    const char* const* about;
    int aboutCount;
    bool (*unpack)(ModJob* job);
    bool (*finish)(const char* configPath, std::string* error);
} ModDownload;

// The game's files: its archives and loose data (not F.I.S.S.I.O.N.'s
// programs, nor macOS' extras).
static bool sonora_wanted(const std::string& name)
{
    if (name.find(".DS_Store") != std::string::npos) {
        return false;
    }
    return compat_stricmp(name.c_str(), "master.dat") == 0
        || compat_stricmp(name.c_str(), "critter.dat") == 0
        || compat_stricmp(name.c_str(), "readme.txt") == 0
        || name.compare(0, 5, "data/") == 0;
}

static int mod_run(void* data)
{
    ModJob* job = (ModJob*)data;
    const ModDownload* mod = job->mod;
    const char* url = getenv(mod->urlEnv) != NULL ? getenv(mod->urlEnv) : mod->url;
    const char* hash = getenv(mod->sha256Env) != NULL ? getenv(mod->sha256Env) : mod->sha256;

    job->done = 0;
    job->total = 0;
    job->stage = 1;
    if (!coop_http_download(url, mod->downloadFile, mod_progress, job, &job->error)) {
        job->stage = 5;
        return 0;
    }

    job->done = 0;
    job->stage = 2;
    std::string got = coop_update_sha256_file(mod->downloadFile, mod_progress, job);
    if (got != hash) {
        job->error = job->cancel ? "Cancelled." : "The download is not the expected file (its check failed). Try again.";
        remove(mod->downloadFile);
        job->stage = 5;
        return 0;
    }

    job->done = 0;
    job->stage = 3;
    bool ok = mod->unpack(job);
    remove(mod->downloadFile);
    job->stage = ok ? 4 : 5;
    return 0;
}

static bool sonora_unpack(ModJob* job)
{
    return coop_zip_extract(SONORA_DOWNLOAD, SONORA_FOLDER, ".", sonora_wanted, mod_progress, job, &job->error);
}

static bool sonora_finish(const char* configPath, std::string* error)
{
    if (!setup_write_config(configPath, fs::current_path())) {
        *error = "Could not write fallout2.cfg here.";
        return false;
    }
    return true;
}

static const char* const kSonoraAbout[] = {
    "Fallout: Sonora is not installed here yet.",
    "",
    "A free game on the Fallout 2 engine by the Nevada",
    "Band; English version by cambragol. It is",
    "downloaded from their release on GitHub:",
    "  github.com/cambragol/Fallout-Sonora-English",
    "Version 1.16.4E, " SONORA_SIZE_TEXT " to download, as much on disk.",
    "Its terms: free, not for sale; for players over 18.",
    "Nothing else on this computer is changed.",
};

static const ModDownload kSonora = {
    "Fallout: Sonora",
    "VAULT-TEC FALLOUT: SONORA SETUP",
    SONORA_URL,
    "FALLOUT_SONORA_URL",
    SONORA_SHA256,
    "FALLOUT_SONORA_SHA256",
    SONORA_DOWNLOAD,
    kSonoraAbout,
    (int)(sizeof(kSonoraAbout) / sizeof(kSonoraAbout[0])),
    sonora_unpack,
    sonora_finish,
};

// The install screen of a story mod the launcher downloads; true once it is
// here.
static bool setup_mod_install(const ModDownload* mod, const char* configPath)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    SetupScreen* screen = new SetupScreen();
    std::string windowTitle = std::string(mod->name) + " - Setup";
    screen->window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SETUP_WIDTH * 3 / 2, SETUP_HEIGHT * 3 / 2, SDL_WINDOW_RESIZABLE);
    screen->renderer = screen->window != NULL ? SDL_CreateRenderer(screen->window, -1, 0) : NULL;
    if (screen->renderer == NULL) {
        if (screen->window != NULL) {
            SDL_DestroyWindow(screen->window);
        }
        delete screen;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_RenderSetLogicalSize(screen->renderer, SETUP_WIDTH, SETUP_HEIGHT);
    screen->texture = SDL_CreateTexture(screen->renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SETUP_WIDTH, SETUP_HEIGHT);

    const char* screenshot = getenv("FALLOUT_SETUP_SCREENSHOT");
    const char* choose = getenv("FALLOUT_SETUP_CHOOSE");

    const char* entries[] = { "[ DOWNLOAD AND INSTALL ]", "[ QUIT ]" };
    const int count = 2;
    const int listTop = SETUP_MARGIN + 12 * SETUP_LINE;
    int selected = 0;
    bool result = false;
    bool done = false;
    int frame = 0;
    ModJob job;
    job.mod = mod;
    job.stage = 0;
    job.done = 0;
    job.total = 0;
    job.cancel = false;
    SDL_Thread* worker = NULL;
    std::string message;

    while (!done) {
        int stage = job.stage;
        bool busy = stage >= 1 && stage <= 3;
        int confirm = -1;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                if (busy) {
                    job.cancel = true;
                } else {
                    done = true;
                }
                break;
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
                if (!busy) {
                    int y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
                    int line = (y - listTop) / SETUP_LINE;
                    if (y >= listTop && line >= 0 && line < count) {
                        selected = line;
                        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                            confirm = line;
                        }
                    }
                }
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_DOWN:
                    selected = 1 - selected;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    confirm = selected;
                    break;
                case SDLK_ESCAPE:
                    if (busy) {
                        job.cancel = true;
                    } else {
                        confirm = 1;
                    }
                    break;
                }
                break;
            }
        }

        if (frame == 1 && choose != NULL) {
            confirm = atoi(choose);
        }

        if (confirm != -1 && !busy) {
            if (confirm == 1) {
                done = true;
            } else {
                if (worker != NULL) {
                    SDL_WaitThread(worker, NULL);
                }
                message.clear();
                job.cancel = false;
                job.error.clear();
                job.stage = 1;
                worker = SDL_CreateThread(mod_run, "mod download", &job);
            }
        }

        if (stage == 4) {
            if (worker != NULL) {
                SDL_WaitThread(worker, NULL);
                worker = NULL;
            }
            std::string error;
            if (mod->finish(configPath, &error)) {
                message = std::string(mod->name) + " is installed. Starting...";
                result = true;
            } else {
                message = "! " + error;
                job.stage = 5;
            }
            done = result;
        } else if (stage == 5 && worker != NULL) {
            SDL_WaitThread(worker, NULL);
            worker = NULL;
            message = "! " + job.error;
            fprintf(stderr, "%s setup: %s\n", mod->name, job.error.c_str());
            // (Tests choose by themselves: they stop here.)
            if (choose != NULL) {
                done = true;
            }
        }

        // Draw.
        setup_clear(screen);
        setup_fill(screen, 0, 0, SETUP_WIDTH, SETUP_LINE + 6, SETUP_GREEN);
        setup_text(screen, SETUP_MARGIN, 4, mod->header, 0x041A08);
        setup_text(screen, SETUP_WIDTH - SETUP_MARGIN - (int)strlen("CO-OP v" COOP_VERSION) * COOP_SETUP_FONT_WIDTH, 4, "CO-OP v" COOP_VERSION, 0x041A08);

        int y = SETUP_MARGIN + 2 * SETUP_LINE;
        for (int index = 0; index < mod->aboutCount; index++) {
            setup_text(screen, SETUP_MARGIN, y, mod->about[index], index == 0 ? SETUP_GREEN : SETUP_DIM);
            y += SETUP_LINE;
        }

        if (busy) {
            const char* what = stage == 1 ? "DOWNLOADING" : stage == 2 ? "CHECKING" : "UNPACKING";
            unsigned long long amount = job.done;
            unsigned long long total = job.total;
            char line[96];
            if (total != 0) {
                snprintf(line, sizeof(line), "%s  %llu / %llu MB", what, amount >> 20, total >> 20);
            } else {
                snprintf(line, sizeof(line), "%s  %llu MB", what, amount >> 20);
            }
            setup_text(screen, SETUP_MARGIN, listTop, line, SETUP_GREEN);
            int barWidth = SETUP_WIDTH - 2 * SETUP_MARGIN;
            setup_fill(screen, SETUP_MARGIN, listTop + SETUP_LINE, barWidth, 12, SETUP_DIM);
            if (total != 0) {
                setup_fill(screen, SETUP_MARGIN, listTop + SETUP_LINE, (int)(barWidth * (double)amount / (double)total), 12, SETUP_GREEN);
            }
        } else {
            for (int index = 0; index < count; index++) {
                int lineY = listTop + index * SETUP_LINE;
                if (index == selected) {
                    setup_fill(screen, SETUP_MARGIN - 6, lineY - 1, SETUP_WIDTH - 2 * SETUP_MARGIN + 12, SETUP_LINE - 1, SETUP_GREEN);
                    setup_text(screen, SETUP_MARGIN, lineY, (std::string("> ") + entries[index]).c_str(), 0x041A08);
                } else {
                    setup_text(screen, SETUP_MARGIN, lineY, (std::string("  ") + entries[index]).c_str(), SETUP_GREEN);
                }
            }
        }

        if (!message.empty()) {
            setup_text(screen, SETUP_MARGIN, SETUP_HEIGHT - SETUP_MARGIN - 3 * SETUP_LINE, message.c_str(), SETUP_GREEN, 58);
        }
        setup_text(screen, SETUP_MARGIN, SETUP_HEIGHT - SETUP_MARGIN - SETUP_LINE, busy ? "Esc: cancel" : "Up/Down or mouse: choose   Enter: confirm   Esc: quit", SETUP_DIM);
        setup_present(screen);

        if (frame == 0 && screenshot != NULL) {
            setup_save_screenshot(screen, screenshot);
        }
        frame++;
        SDL_Delay(16);
    }

    if (worker != NULL) {
        job.cancel = true;
        SDL_WaitThread(worker, NULL);
    }
    if (result) {
        SDL_Delay(400);
    }

    SDL_DestroyTexture(screen->texture);
    SDL_DestroyRenderer(screen->renderer);
    SDL_DestroyWindow(screen->window);
    delete screen;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
}

// -----------------------------------------------------------------------------
// Fallout: Nevada: installed by its makers' installer (over Fallout 2), then
// played in its folder

// Whether `dir` holds Fallout: Nevada: its patches and sfall's settings
// naming it.
static bool setup_is_nevada(const fs::path& dir)
{
    fs::path patch;
    fs::path ini;
    if (!setup_find(dir, "patch000.dat", &patch) || !setup_find(dir, "ddraw.ini", &ini)) {
        return false;
    }
    FILE* stream = compat_fopen(ini.string().c_str(), "rb");
    if (stream == NULL) {
        return false;
    }
    std::string text;
    char buffer[4096];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        text.append(buffer, read);
    }
    fclose(stream);
    for (char& ch : text) {
        ch = (char)tolower((unsigned char)ch);
    }
    return text.find("versionstring=fallout: nevada") != std::string::npos;
}

// Nevada folders in and under the Fallout 2 installations found (its
// installer puts it in a folder of Fallout 2's).
static std::vector<SetupInstall> setup_find_nevadas(const std::vector<SetupInstall>& fallout2s)
{
    std::vector<SetupInstall> found;
    for (const SetupInstall& install : fallout2s) {
        std::vector<fs::path> dirs = { fs::u8path(install.path) };
        std::error_code error;
        for (fs::directory_iterator it(fs::u8path(install.path), error); !error && it != fs::directory_iterator(); it.increment(error)) {
            if (it->is_directory(error)) {
                dirs.push_back(it->path());
                std::error_code inner;
                for (fs::directory_iterator sub(it->path(), inner); !inner && sub != fs::directory_iterator(); sub.increment(inner)) {
                    if (sub->is_directory(inner)) {
                        dirs.push_back(sub->path());
                    }
                }
            }
        }
        for (const fs::path& dir : dirs) {
            if (setup_is_nevada(dir)) {
                SetupInstall nevada;
                nevada.path = dir.string();
                nevada.label = install.label;
                bool known = false;
                for (const SetupInstall& other : found) {
                    known = known || other.path == nevada.path;
                }
                if (!known) {
                    found.push_back(nevada);
                }
            }
        }
    }
    return found;
}

// The settings file a folder's ddraw.ini names (sfall's ConfigFile), else
// fallout2.cfg.
static std::string setup_config_file_name(const std::string& ddrawPath)
{
    Config ddraw;
    std::string name = DEFAULT_GAME_CONFIG_FILE_NAME;
    if (config_init(&ddraw)) {
        char* value;
        if (config_load(&ddraw, ddrawPath.c_str(), false) && config_get_string(&ddraw, "Misc", "ConfigFile", &value) && value[0] != '\0') {
            name = value;
        }
        config_exit(&ddraw);
    }
    return name;
}

// In Nevada's folder now: its settings must find Fallout 2's master.dat and
// critter.dat (its own patches and data stay as they are).
static bool setup_nevada_point_at_fallout2(const std::vector<SetupInstall>& fallout2s)
{
    std::string configPath = setup_config_file_name("ddraw.ini");
    Config config;
    if (!config_init(&config)) {
        return false;
    }
    config_load(&config, configPath.c_str(), false);
    char* master;
    char* critter;
    std::string masterPath = config_get_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, &master) ? master : "master.dat";
    std::string critterPath = config_get_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, &critter) ? critter : "critter.dat";
    std::error_code error;
    if (fs::exists(fs::u8path(masterPath), error) && fs::exists(fs::u8path(critterPath), error)) {
        config_exit(&config);
        return true;
    }

    // The Fallout 2 it was installed in, else one found.
    std::vector<fs::path> candidates;
    fs::path here = fs::current_path(error);
    candidates.push_back(here.parent_path());
    candidates.push_back(here.parent_path().parent_path());
    for (const SetupInstall& install : fallout2s) {
        candidates.push_back(fs::u8path(install.path));
    }
    bool ok = false;
    for (const fs::path& dir : candidates) {
        fs::path masterFile;
        fs::path critterFile;
        if (!dir.empty() && setup_find(dir, "master.dat", &masterFile) && setup_find(dir, "critter.dat", &critterFile) && setup_is_fallout2(dir)) {
            config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, masterFile.string().c_str());
            config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, critterFile.string().c_str());
            ok = config_save(&config, configPath.c_str(), false);
            break;
        }
    }
    config_exit(&config);
    return ok;
}

// Where Nevada is: remembered in nevada.txt here, else picked on a screen.
// Then the game runs in its folder. False: quit.
static bool setup_nevada_enter()
{
    const char* rememberFile = "nevada.txt";
    std::string remembered;
    FILE* stream = fopen(rememberFile, "rt");
    if (stream != NULL) {
        char line[COMPAT_MAX_PATH];
        if (fgets(line, sizeof(line), stream) != NULL) {
            line[strcspn(line, "\r\n")] = '\0';
            remembered = line;
        }
        fclose(stream);
    }

    std::vector<SetupInstall> fallout2s = setup_find_installs();
    std::error_code error;
    fs::path chosen;
    if (!remembered.empty() && setup_is_nevada(fs::u8path(remembered))) {
        chosen = fs::u8path(remembered);
    } else {
        std::vector<SetupInstall> nevadas = setup_find_nevadas(fallout2s);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            return false;
        }
        SetupScreen* screen = new SetupScreen();
        screen->window = SDL_CreateWindow("Fallout: Nevada - Setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SETUP_WIDTH * 3 / 2, SETUP_HEIGHT * 3 / 2, SDL_WINDOW_RESIZABLE);
        screen->renderer = screen->window != NULL ? SDL_CreateRenderer(screen->window, -1, 0) : NULL;
        if (screen->renderer == NULL) {
            if (screen->window != NULL) {
                SDL_DestroyWindow(screen->window);
            }
            delete screen;
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return false;
        }
        SDL_RenderSetLogicalSize(screen->renderer, SETUP_WIDTH, SETUP_HEIGHT);
        screen->texture = SDL_CreateTexture(screen->renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SETUP_WIDTH, SETUP_HEIGHT);

        const char* screenshot = getenv("FALLOUT_SETUP_SCREENSHOT");
        const char* choose = getenv("FALLOUT_SETUP_CHOOSE");
        int typeIndex = (int)nevadas.size();
        int quitIndex = typeIndex + 1;
        int selected = 0;
        bool typing = false;
        std::string typed;
        std::string message;
        bool done = false;
        int frame = 0;
        const int listTop = SETUP_MARGIN + 9 * SETUP_LINE;

        while (!done) {
            int confirm = -1;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                    done = true;
                    break;
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONDOWN:
                    if (!typing) {
                        int y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
                        int line = (y - listTop) / SETUP_LINE;
                        if (y >= listTop && line >= 0 && line <= quitIndex) {
                            selected = line;
                            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                                confirm = line;
                            }
                        }
                    }
                    break;
                case SDL_TEXTINPUT:
                    if (typing) {
                        typed += event.text.text;
                    }
                    break;
                case SDL_KEYDOWN:
                    if (typing) {
                        SDL_Keycode key = event.key.keysym.sym;
                        if (key == SDLK_BACKSPACE && !typed.empty()) {
                            typed.pop_back();
                        } else if (key == SDLK_v && (event.key.keysym.mod & KMOD_CTRL) != 0) {
                            char* clipboard = SDL_GetClipboardText();
                            if (clipboard != NULL) {
                                typed += clipboard;
                                SDL_free(clipboard);
                            }
                        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                            confirm = typeIndex;
                        } else if (key == SDLK_ESCAPE) {
                            typing = false;
                            SDL_StopTextInput();
                        }
                    } else {
                        switch (event.key.keysym.sym) {
                        case SDLK_UP:
                            selected = (selected + quitIndex) % (quitIndex + 1);
                            break;
                        case SDLK_DOWN:
                            selected = (selected + 1) % (quitIndex + 1);
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            confirm = selected;
                            break;
                        case SDLK_ESCAPE:
                            confirm = quitIndex;
                            break;
                        }
                    }
                    break;
                }
            }

            if (frame == 1 && choose != NULL) {
                confirm = atoi(choose);
            }

            if (confirm != -1) {
                if (confirm == quitIndex) {
                    done = true;
                } else if (confirm == typeIndex && !typing) {
                    typing = true;
                    SDL_StartTextInput();
                    message.clear();
                } else {
                    std::string path = confirm == typeIndex ? typed : nevadas[confirm].path;
                    while (!path.empty() && (path.back() == ' ' || path.back() == '\n' || path.back() == '\r' || path.back() == '"')) {
                        path.pop_back();
                    }
                    while (!path.empty() && (path.front() == ' ' || path.front() == '"')) {
                        path.erase(path.begin());
                    }
                    if (path.empty() || !fs::is_directory(fs::u8path(path), error)) {
                        message = "! That folder does not exist.";
                    } else if (!setup_is_nevada(fs::u8path(path))) {
                        message = "! No Fallout: Nevada in that folder (PATCH000.DAT).";
                    } else {
                        chosen = fs::u8path(path);
                        done = true;
                    }
                }
            }

            setup_clear(screen);
            setup_fill(screen, 0, 0, SETUP_WIDTH, SETUP_LINE + 6, SETUP_GREEN);
            setup_text(screen, SETUP_MARGIN, 4, "VAULT-TEC FALLOUT: NEVADA SETUP", 0x041A08);
            setup_text(screen, SETUP_WIDTH - SETUP_MARGIN - (int)strlen("CO-OP v" COOP_VERSION) * COOP_SETUP_FONT_WIDTH, 4, "CO-OP v" COOP_VERSION, 0x041A08);
            int y = SETUP_MARGIN + 2 * SETUP_LINE;
            const char* about[] = {
                nevadas.empty() ? "Fallout: Nevada was not found." : "Where is Fallout: Nevada installed?",
                "A free game on the Fallout 2 engine by the Nevada Band.",
                "Install it first with its makers' installer (it needs",
                "Fallout 2), from: fnevada.bgforge.net",
                "Then pick its folder here (the one with PATCH000.DAT).",
            };
            for (const char* line : about) {
                setup_text(screen, SETUP_MARGIN, y, line, line == about[0] ? SETUP_GREEN : SETUP_DIM);
                y += SETUP_LINE;
            }

            y = listTop;
            for (int index = 0; index <= quitIndex; index++, y += SETUP_LINE) {
                std::string line;
                if (index < typeIndex) {
                    line = setup_shorten(setup_display_path(nevadas[index].path), 56);
                } else if (index == typeIndex) {
                    line = "[ TYPE OR PASTE A PATH ]";
                } else {
                    line = "[ QUIT ]";
                }
                bool highlight = index == selected && !(typing && index != typeIndex);
                if (highlight) {
                    setup_fill(screen, SETUP_MARGIN - 6, y - 1, SETUP_WIDTH - 2 * SETUP_MARGIN + 12, SETUP_LINE - 1, SETUP_GREEN);
                    setup_text(screen, SETUP_MARGIN, y, ("> " + line).c_str(), 0x041A08, 58);
                } else {
                    setup_text(screen, SETUP_MARGIN, y, ("  " + line).c_str(), SETUP_GREEN, 58);
                }
            }
            if (typing) {
                y += SETUP_LINE;
                setup_text(screen, SETUP_MARGIN, y, "PATH:", SETUP_GREEN);
                std::string shown = setup_shorten(typed, 52);
                int width = setup_text(screen, SETUP_MARGIN + 6 * COOP_SETUP_FONT_WIDTH, y, shown.c_str(), SETUP_GREEN);
                if ((SDL_GetTicks() / 400) % 2 == 0) {
                    setup_fill(screen, SETUP_MARGIN + 6 * COOP_SETUP_FONT_WIDTH + width + 1, y + 2, COOP_SETUP_FONT_WIDTH - 2, COOP_SETUP_FONT_HEIGHT - 4, SETUP_GREEN);
                }
            }
            if (!message.empty()) {
                setup_text(screen, SETUP_MARGIN, SETUP_HEIGHT - SETUP_MARGIN - 3 * SETUP_LINE, message.c_str(), SETUP_GREEN, 58);
            }
            setup_text(screen, SETUP_MARGIN, SETUP_HEIGHT - SETUP_MARGIN - SETUP_LINE,
                typing ? "Enter: use this folder   Ctrl+V: paste   Esc: back" : "Up/Down or mouse: choose   Enter: confirm   Esc: quit", SETUP_DIM);
            setup_present(screen);
            if (frame == 0 && screenshot != NULL) {
                setup_save_screenshot(screen, screenshot);
            }
            frame++;
            SDL_Delay(16);
        }

        SDL_StopTextInput();
        SDL_DestroyTexture(screen->texture);
        SDL_DestroyRenderer(screen->renderer);
        SDL_DestroyWindow(screen->window);
        delete screen;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        if (chosen.empty()) {
            return false;
        }
        stream = fopen(rememberFile, "wt");
        if (stream != NULL) {
            fprintf(stream, "%s\n", chosen.string().c_str());
            fclose(stream);
        }
    }

    fs::current_path(chosen, error);
    if (error) {
        return false;
    }
    if (!setup_nevada_point_at_fallout2(fallout2s)) {
        fprintf(stderr, "Fallout: Nevada setup: Fallout 2 (master.dat, critter.dat) was not found.\n");
    }
    return true;
}

// -----------------------------------------------------------------------------
// Fallout 1.5: Resurrection: its makers' installer puts it in a folder of
// Fallout 2's (played there); or the launcher downloads their release and
// unpacks it into its own folder next to the co-op files, pointed at Fallout
// 2's data (nothing of Fallout 2's is changed)

#define RESURRECTION_URL "https://bitbucket.org/resurrection/fallout-resurrection/downloads/FoRes_EN_1_5.zip"
#define RESURRECTION_SHA256 "7857d1f5cb870035e05ff8d08a784dc7796c7cc09988f1ca86debd711f9edc02"
#define RESURRECTION_DOWNLOAD "resurrection-download.zip"

// Whether `dir` holds Resurrection: its fores.dat and sfall's settings
// naming it.
static bool setup_is_resurrection(const fs::path& dir)
{
    fs::path dat;
    fs::path ini;
    if (!setup_find(dir, "fores.dat", &dat) || !setup_find(dir, "ddraw.ini", &ini)) {
        return false;
    }
    FILE* stream = compat_fopen(ini.string().c_str(), "rb");
    if (stream == NULL) {
        return false;
    }
    std::string text;
    char buffer[4096];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        text.append(buffer, read);
    }
    fclose(stream);
    for (char& ch : text) {
        ch = (char)tolower((unsigned char)ch);
    }
    return text.find("versionstring=fo1.5:resurrection") != std::string::npos;
}

// The game's files, not its Windows programs (fores.exe and its copy of
// Fallout 2's, sfall's and the high resolution patch's DLLs, the
// configuration program).
static bool resurrection_wanted(const std::string& name)
{
    std::string lower = name;
    for (char& ch : lower) {
        ch = (char)tolower((unsigned char)ch);
    }
    size_t dot = lower.rfind('.');
    std::string extension = dot != std::string::npos ? lower.substr(dot) : std::string();
    return extension != ".exe" && extension != ".dll" && lower != "fallout2";
}

static bool resurrection_unpack(ModJob* job)
{
    return coop_zip_extract(RESURRECTION_DOWNLOAD, "Resurrection/", ".", resurrection_wanted, mod_progress, job, &job->error)
        && coop_zip_extract(RESURRECTION_DOWNLOAD, "sound/", "sound", NULL, mod_progress, job, &job->error);
}

// Fallout 2's master.dat, critter.dat and music: the ones the package plays
// Fallout 2 with (its fallout2.cfg, a folder up), else an installation found.
static bool setup_fallout2_files(fs::path* master, fs::path* critter, std::string* music)
{
    std::error_code error;
    std::vector<fs::path> dirs;
    Config config;
    if (config_init(&config)) {
        char* value;
        if (config_load(&config, "../" DEFAULT_GAME_CONFIG_FILE_NAME, false)
            && config_get_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, &value)) {
            fs::path path = fs::u8path(value);
            if (path.is_relative()) {
                path = fs::path("..") / path;
            }
            dirs.push_back(fs::absolute(path, error).parent_path());
        }
        config_exit(&config);
    }
    for (const SetupInstall& install : setup_find_installs()) {
        dirs.push_back(fs::u8path(install.path));
    }

    for (const fs::path& dir : dirs) {
        if (!dir.empty() && setup_is_install(dir)) {
            setup_find(dir, "master.dat", master);
            setup_find(dir, "critter.dat", critter);
            fs::path sound;
            fs::path found;
            fs::path data;
            music->clear();
            if ((setup_find(dir, "sound", &sound) && setup_find(sound, "music", &found))
                || (setup_find(dir, "data", &data) && setup_find(data, "sound", &sound) && setup_find(sound, "music", &found))) {
                *music = found.string() + std::string(1, (char)fs::path::preferred_separator);
            }
            return true;
        }
    }
    return false;
}

// Unpacked here: its settings (fores.cfg) find Fallout 2's files, and its own
// music here first.
static bool resurrection_finish(const char* configPath, std::string* error)
{
    (void)configPath;
    fs::path master;
    fs::path critter;
    std::string music;
    if (!setup_fallout2_files(&master, &critter, &music)) {
        *error = "Fallout 2 was not found; Resurrection needs it.";
        return false;
    }

    std::string ownMusic = std::string("sound") + (char)fs::path::preferred_separator + "music" + (char)fs::path::preferred_separator;
    Config config;
    if (!config_init(&config)) {
        *error = "Could not write fores.cfg here.";
        return false;
    }
    config_load(&config, "fores.cfg", false);
    config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, master.string().c_str());
    config_set_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, critter.string().c_str());
    config_set_string(&config, GAME_CONFIG_SOUND_KEY, GAME_CONFIG_MUSIC_PATH1_KEY, ownMusic.c_str());
    config_set_string(&config, GAME_CONFIG_SOUND_KEY, GAME_CONFIG_MUSIC_PATH2_KEY, music.empty() ? ownMusic.c_str() : music.c_str());
    bool saved = config_save(&config, "fores.cfg", false);
    config_exit(&config);
    if (!saved) {
        *error = "Could not write fores.cfg here.";
    }
    return saved;
}

static const char* const kResurrectionAbout[] = {
    "Fallout 1.5: Resurrection is not installed here yet.",
    "",
    "A free game on the Fallout 2 engine by a team of",
    "fans from the Czech Republic; it needs Fallout 2.",
    "It is downloaded from their release:",
    "  bitbucket.org/resurrection/fallout-resurrection",
    "Version 1.5 (English), 340 MB to download, 460 MB on",
    "disk, next to these co-op files. Nothing of Fallout 2's",
    "is changed. (Installed with their own installer, it is",
    "found and played there instead.)",
};

static const ModDownload kResurrection = {
    "Fallout 1.5: Resurrection",
    "VAULT-TEC FALLOUT 1.5: RESURRECTION SETUP",
    RESURRECTION_URL,
    "FALLOUT_RESURRECTION_URL",
    RESURRECTION_SHA256,
    "FALLOUT_RESURRECTION_SHA256",
    RESURRECTION_DOWNLOAD,
    kResurrectionAbout,
    (int)(sizeof(kResurrectionAbout) / sizeof(kResurrectionAbout[0])),
    resurrection_unpack,
    resurrection_finish,
};

// Started for Resurrection (in its folder next to the co-op files): when
// it isn't unpacked here, one its makers' installer put in a Fallout 2
// folder (two levels down at most) is played there.
static void setup_resurrection_enter()
{
    std::error_code error;
    if (setup_is_resurrection(fs::current_path(error))) {
        return;
    }

    for (const SetupInstall& install : setup_find_installs()) {
        std::vector<fs::path> dirs;
        for (fs::directory_iterator it(fs::u8path(install.path), error); !error && it != fs::directory_iterator(); it.increment(error)) {
            if (it->is_directory(error)) {
                dirs.push_back(it->path());
                std::error_code inner;
                for (fs::directory_iterator sub(it->path(), inner); !inner && sub != fs::directory_iterator(); sub.increment(inner)) {
                    if (sub->is_directory(inner)) {
                        dirs.push_back(sub->path());
                    }
                }
            }
        }
        for (const fs::path& dir : dirs) {
            if (setup_is_resurrection(dir)) {
                fs::current_path(dir, error);
                if (!error) {
                    return;
                }
            }
        }
    }
}

bool coop_setup_ensure_game_data(int argc, char** argv)
{
    // Started for Fallout: Nevada (the launcher's choice): the game runs in
    // the folder its makers' installer put it in.
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--nevada") == 0) {
            if (!setup_nevada_enter()) {
                return false;
            }
            break;
        }
        if (strcmp(argv[index], "--resurrection") == 0) {
            setup_resurrection_enter();
            break;
        }
    }

    // fallout2.cfg (or the file ddraw.ini names), found the way
    // gameConfigInit() finds it.
    char configPath[COMPAT_MAX_PATH];
    char* sep = argc > 0 ? strrchr(argv[0], '\\') : NULL;
    if (sep != NULL) {
        *sep = '\0';
        std::string ddrawPath = std::string(argv[0]) + "\\ddraw.ini";
        snprintf(configPath, sizeof(configPath), "%s\\%s", argv[0], setup_config_file_name(ddrawPath).c_str());
        *sep = '\\';
    } else {
        snprintf(configPath, sizeof(configPath), "%s", setup_config_file_name("ddraw.ini").c_str());
    }

    Config config;
    if (!config_init(&config)) {
        return true;
    }

    config_load(&config, configPath, false);
    // Command line overrides, as the game applies them.
    configParseCommandLineArguments(&config, argc, argv);

    char* master;
    char* critter;
    std::string masterPath = config_get_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, &master) ? master : "master.dat";
    std::string critterPath = config_get_string(&config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, &critter) ? critter : "critter.dat";
    config_exit(&config);

    FILE* masterStream = compat_fopen(masterPath.c_str(), "rb");
    FILE* critterStream = compat_fopen(critterPath.c_str(), "rb");
    bool found = masterStream != NULL && critterStream != NULL;
    if (masterStream != NULL) {
        fclose(masterStream);
    }
    if (critterStream != NULL) {
        fclose(critterStream);
    }

    if (found) {
        return true;
    }

    // Started for Fallout: Sonora (the launcher's choice, in its own folder):
    // installs it.
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--sonora") == 0) {
            return setup_mod_install(&kSonora, configPath);
        }
        if (strcmp(argv[index], "--resurrection") == 0) {
            return setup_mod_install(&kResurrection, configPath);
        }
    }

    // Not found: the setup screen.
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return true;
    }

    SetupScreen* screen = new SetupScreen();
    screen->window = SDL_CreateWindow("Fallout 2 - Setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SETUP_WIDTH * 3 / 2, SETUP_HEIGHT * 3 / 2, SDL_WINDOW_RESIZABLE);
    screen->renderer = screen->window != NULL ? SDL_CreateRenderer(screen->window, -1, 0) : NULL;
    if (screen->renderer == NULL) {
        if (screen->window != NULL) {
            SDL_DestroyWindow(screen->window);
        }
        delete screen;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return true;
    }

    SDL_RenderSetLogicalSize(screen->renderer, SETUP_WIDTH, SETUP_HEIGHT);
    screen->texture = SDL_CreateTexture(screen->renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SETUP_WIDTH, SETUP_HEIGHT);

    std::vector<SetupInstall> installs = setup_find_installs();

    // Entries: the installations found, then "type a path", then "quit".
    int typeIndex = (int)installs.size();
    int quitIndex = typeIndex + 1;
    int selected = 0;
    bool typing = installs.empty();
    std::string typed;
    std::string message;
    bool messageIsError = false;
    bool done = false;
    bool result = false;
    int frame = 0;

    const char* screenshot = getenv("FALLOUT_SETUP_SCREENSHOT");
    const char* choose = getenv("FALLOUT_SETUP_CHOOSE");

    if (typing) {
        SDL_StartTextInput();
    }

    // Where each entry's line is, for the mouse.
    const int listTop = SETUP_MARGIN + 5 * SETUP_LINE;

    while (!done) {
        int confirm = -1;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                done = true;
                break;
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
                if (!typing) {
                    int y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
                    int line = (y - listTop) / SETUP_LINE;
                    if (y >= listTop && line >= 0 && line <= quitIndex) {
                        selected = line;
                        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                            confirm = line;
                        }
                    }
                }
                break;
            case SDL_TEXTINPUT:
                if (typing) {
                    typed += event.text.text;
                }
                break;
            case SDL_KEYDOWN:
                if (typing) {
                    SDL_Keycode key = event.key.keysym.sym;
                    if (key == SDLK_BACKSPACE && !typed.empty()) {
                        typed.pop_back();
                    } else if (key == SDLK_v && (event.key.keysym.mod & KMOD_CTRL) != 0) {
                        char* clipboard = SDL_GetClipboardText();
                        if (clipboard != NULL) {
                            typed += clipboard;
                            SDL_free(clipboard);
                        }
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        confirm = typeIndex;
                    } else if (key == SDLK_ESCAPE) {
                        if (installs.empty()) {
                            confirm = quitIndex;
                        } else {
                            typing = false;
                            SDL_StopTextInput();
                            message.clear();
                        }
                    }
                } else {
                    switch (event.key.keysym.sym) {
                    case SDLK_UP:
                        selected = (selected + quitIndex) % (quitIndex + 1);
                        break;
                    case SDLK_DOWN:
                        selected = (selected + 1) % (quitIndex + 1);
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        confirm = selected;
                        break;
                    case SDLK_ESCAPE:
                        confirm = quitIndex;
                        break;
                    }
                }
                break;
            }
        }

        if (frame == 1 && choose != NULL) {
            confirm = atoi(choose);
        }

        if (confirm != -1) {
            if (confirm == quitIndex) {
                done = true;
            } else if (confirm == typeIndex && !typing) {
                typing = true;
                SDL_StartTextInput();
                message.clear();
            } else {
                // Trim what was typed or pasted.
                std::string path = confirm == typeIndex ? typed : installs[confirm].path;
                while (!path.empty() && (path.back() == ' ' || path.back() == '\n' || path.back() == '\r' || path.back() == '"')) {
                    path.pop_back();
                }
                while (!path.empty() && (path.front() == ' ' || path.front() == '"')) {
                    path.erase(path.begin());
                }

                std::error_code error;
                if (path.empty() || !fs::is_directory(path, error)) {
                    message = "That folder does not exist.";
                    messageIsError = true;
                } else if (!setup_is_fallout2(path) && setup_is_install(path)) {
                    message = "That is Fallout 1. Pick Fallout 2 here.";
                    messageIsError = true;
                } else if (!setup_is_install(path)) {
                    message = "No MASTER.DAT and CRITTER.DAT in that folder.";
                    messageIsError = true;
                } else if (!setup_write_config(configPath, path)) {
                    message = "Could not write fallout2.cfg here.";
                    messageIsError = true;
                } else {
                    message = "Fallout 2 found. Starting...";
                    messageIsError = false;
                    result = true;
                    done = true;
                }
            }
        }

        // Draw.
        setup_clear(screen);
        setup_fill(screen, 0, 0, SETUP_WIDTH, SETUP_LINE + 6, SETUP_GREEN);
        setup_text(screen, SETUP_MARGIN, 4, "VAULT-TEC FALLOUT 2 SETUP TERMINAL", 0x041A08);
        setup_text(screen, SETUP_WIDTH - SETUP_MARGIN - (int)strlen("CO-OP v" COOP_VERSION) * COOP_SETUP_FONT_WIDTH, 4, "CO-OP v" COOP_VERSION, 0x041A08);

        int y = SETUP_MARGIN + 2 * SETUP_LINE;
        if (installs.empty()) {
            setup_text(screen, SETUP_MARGIN, y, "No Fallout 2 installation was found.", SETUP_GREEN);
        } else {
            setup_text(screen, SETUP_MARGIN, y, "Where is Fallout 2 installed?", SETUP_GREEN);
        }
        y += SETUP_LINE;
        setup_text(screen, SETUP_MARGIN, y, "The folder with MASTER.DAT and CRITTER.DAT.", SETUP_DIM);

        y = listTop;
        for (int index = 0; index <= quitIndex; index++, y += SETUP_LINE) {
            std::string line;
            if (index < typeIndex) {
                std::string label = installs[index].label.empty() ? "" : "  (" + installs[index].label + ")";
                line = setup_shorten(setup_display_path(installs[index].path), 56 - label.size()) + label;
            } else if (index == typeIndex) {
                line = "[ TYPE OR PASTE A PATH ]";
            } else {
                line = "[ QUIT ]";
            }

            bool highlight = index == selected && !(typing && index != typeIndex);
            if (highlight) {
                setup_fill(screen, SETUP_MARGIN - 6, y - 1, SETUP_WIDTH - 2 * SETUP_MARGIN + 12, SETUP_LINE - 1, SETUP_GREEN);
                setup_text(screen, SETUP_MARGIN, y, ("> " + line).c_str(), 0x041A08, 58);
            } else {
                setup_text(screen, SETUP_MARGIN, y, ("  " + line).c_str(), SETUP_GREEN, 58);
            }
        }

        if (typing) {
            y += SETUP_LINE;
            setup_text(screen, SETUP_MARGIN, y, "PATH:", SETUP_GREEN);
            std::string shown = setup_shorten(typed, 52);
            int width = setup_text(screen, SETUP_MARGIN + 6 * COOP_SETUP_FONT_WIDTH, y, shown.c_str(), SETUP_GREEN);
            if ((SDL_GetTicks() / 400) % 2 == 0) {
                setup_fill(screen, SETUP_MARGIN + 6 * COOP_SETUP_FONT_WIDTH + width + 1, y + 2, COOP_SETUP_FONT_WIDTH - 2, COOP_SETUP_FONT_HEIGHT - 4, SETUP_GREEN);
            }
        }

        if (!message.empty()) {
            std::string text = (messageIsError ? "! " : "") + message;
            setup_text(screen, SETUP_MARGIN, SETUP_HEIGHT - SETUP_MARGIN - 3 * SETUP_LINE, text.c_str(), SETUP_GREEN);
        }

        const char* help = typing ? "Enter: use this folder   Ctrl+V: paste   Esc: back" : "Up/Down or mouse: choose   Enter: confirm   Esc: quit";
        setup_text(screen, SETUP_MARGIN, SETUP_HEIGHT - SETUP_MARGIN - SETUP_LINE, help, SETUP_DIM);

        setup_present(screen);

        if (frame == 0 && screenshot != NULL) {
            setup_save_screenshot(screen, screenshot);
        }

        frame++;
        SDL_Delay(16);
    }

    if (result) {
        // Let "Starting..." show for a moment.
        SDL_Delay(400);
    }

    SDL_StopTextInput();
    SDL_DestroyTexture(screen->texture);
    SDL_DestroyRenderer(screen->renderer);
    SDL_DestroyWindow(screen->window);
    delete screen;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    return result;
}

} // namespace fallout
