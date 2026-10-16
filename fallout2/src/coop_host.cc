#include "coop_host.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <string>
#include <vector>

#include "combat.h"
#include "coop.h"
#include "coop_compat.h"
#include "coop_lobby.h"
#include "critter.h"
#include "display_monitor.h"
#include "coop_net.h"
#include "game.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "interface.h"
#include "map.h"
#include "object.h"
#include "proto_instance.h"
#include "tile.h"
#include "color.h"
#include "debug.h"
#include "inject.h"
#include "kb.h"
#include "input.h"
#include "mouse.h"
#include "svga.h"
#include "text_font.h"

namespace fallout {

// What makes a player's view of the map: camera, mouse and cursor mode.
typedef struct CoopView {
    bool initialized;
    int cameraTile;
    // Player's tile when the camera last followed them.
    int followedTile;
    MouseContext mouse;
    int cursor;
    int mode;
} CoopView;

static void coop_host_poll();
static void coop_host_present();
static void coop_host_feed_input();
static void coop_host_player_turn(int slot);
static void coop_host_update_status();
static void coop_host_player_joins(int slot);
static void coop_host_send_character_copy(int slot, bool requested);
static void coop_host_marker_place(int index);
static void coop_host_marker_draw();
static void coop_host_marker_restore();

static bool coop_host_on = false;

// Inside the main loop's own get_input() call.
static bool coop_host_main_input = false;

// Inside player `slot` + 1's part of the frame (0: none).
static int coop_host_turn_slot = 0;

// For the debug log: time spent drawing the others' views and sending them.
static unsigned int coop_host_draw_ms = 0;
static unsigned int coop_host_send_ms = 0;
// Views not drawn because their client wasn't taking frames.
static unsigned int coop_host_views_skipped = 0;

// That player opened a menu (an input loop runs inside their part).
static bool coop_host_turn_menu = false;

// Set by coop_host_frame() for the main loop's following renderPresent():
// that one shows player 1's own view and is not streamed.
static bool coop_host_free_roam_present = false;

// Each other player's view while player 1's is current (and player 1's in
// that player's slot during their part of the frame).
static CoopView coop_host_views[COOP_MAX_PLAYERS];

// Testing: the camera's tile in each player's last part of a frame, and
// how many parts they had.
static int coop_host_view_centers[COOP_MAX_PLAYERS];
static unsigned int coop_host_view_part_count[COOP_MAX_PLAYERS];

void coop_host_init()
{
    if (!coop_is_enabled() || coop_net_mode() != COOP_NET_HOST) {
        return;
    }

    if (!coop_net_host_start()) {
        return;
    }

    input_poll_hook = coop_host_poll;
    svga_present_hook = coop_host_present;
    gsound_event_hook = coop_net_send_sound;
    coop_host_on = true;

    // Found by LAN searches; the menu may restart this with a public listing.
    coop_lobby_host_start("", false);
}

void coop_host_exit()
{
    if (!coop_host_on) {
        return;
    }

    coop_lobby_host_stop();
    input_poll_hook = NULL;
    svga_present_hook = NULL;
    gsound_event_hook = NULL;
    inject_set_exclusive(false);
    coop_net_exit();
    coop_host_on = false;
}

bool coop_host_active()
{
    return coop_host_on;
}

void coop_host_begin_main_input()
{
    coop_host_main_input = true;
}

void coop_host_end_main_input()
{
    coop_host_main_input = false;
}

// The client whose player has the focus (a menu or combat turn), or 0 for
// player 1.
static int coop_host_focus_slot()
{
    for (int index = 1; index < coop_player_count(); index++) {
        if (coop_active_player() == coop_player(index)) {
            return index;
        }
    }
    return 0;
}

// For LAN searches and the server browser.
static void coop_host_pump_lobby()
{
    std::string name = "Wanderer";
    if (coop_player_count() > 0 && coop_player(0)->obj != NULL) {
        name = critter_name(coop_player(0)->obj);
    }
    int clients = coop_net_clients_connected();
    coop_lobby_host_pump(name, clients >= coop_net_max_clients_value() ? COOP_LOBBY_FULL : 1 + clients);
}

// Every frame of every input loop.
static void coop_host_poll()
{
    coop_net_host_pump();
    coop_host_pump_lobby();

    // The main loop's own input is player 1's; the client's waits for
    // player 2's part of the frame. (That part reads input without polling,
    // so a poll during it comes from a menu player 2 opened.)
    if (coop_host_main_input) {
        return;
    }

    // Polled inside a player's part of the frame: they opened a menu.
    if (coop_host_turn_slot != 0) {
        coop_host_turn_menu = true;
    }

    // A menu or combat turn: the active player has the focus.
    static int hadFocus = 0;
    int focus = coop_host_focus_slot();
    if (focus != hadFocus) {
        debug_printf("\nCOOP HOST: focus: player %d\n", focus + 1);
        hadFocus = focus;
    }

    int selected = coop_net_selected();
    if (focus != 0) {
        coop_net_select(focus);
    }
    if (focus != 0 && !coop_net_client_connected()) {
        // The player left in the middle of a menu or turn: close it (Escape)
        // and end the turn (Space), so the game does not wait forever.
        static unsigned int lastNudge = 0;
        inject_set_exclusive(true);
        if (!inject_pending() && elapsed_time(lastNudge) > 500) {
            inject_key(KEY_ESCAPE);
            if (isInCombat()) {
                inject_key(KEY_SPACE);
            }
            lastNudge = get_time();
        }
    } else if (focus != 0) {
        inject_set_exclusive(true);
        coop_host_feed_input();
    } else {
        inject_set_exclusive(false);
    }

    // The others watch; their clicks do nothing meanwhile. Keep where their
    // mouse is, though, and let them chat.
    for (int slot = 1; slot < coop_player_count() && slot < COOP_MAX_PLAYERS; slot++) {
        if (slot == focus) {
            continue;
        }
        coop_net_select(slot);
        CoopNetInput input;
        while (coop_net_next_input(&input)) {
            if (input.kind == COOP_NET_INPUT_MOUSE) {
                coop_host_views[slot].mouse.x = input.x;
                coop_host_views[slot].mouse.y = input.y;
            } else if (input.kind == COOP_NET_INPUT_KEY && input.value > 0) {
                ActivePlayerScope scope(coop_player(slot));
                coop_host_chat_key(input.value);
            }
        }
    }
    coop_net_select(selected);

    coop_host_update_status();
}

// Moves the client's input into the injection queue.
static void coop_host_feed_input()
{
    CoopNetInput input;
    while (coop_net_next_input(&input)) {
        if (input.kind == COOP_NET_INPUT_MOUSE) {
            inject_mouse(input.x, input.y, input.value);
        } else if (input.kind == COOP_NET_INPUT_KEY) {
            // Quitting is player 1's (the client uses these keys to leave).
            if (input.value != KEY_F10 && input.value != KEY_CTRL_Q && input.value != KEY_CTRL_X) {
                inject_key(input.value);
            }
        }
    }
}

// What each player is typing while chatting.
#define COOP_HOST_CHAT_MAX 60
static bool coop_host_chat_typing[COOP_MAX_PLAYERS];
static std::string coop_host_chat_text[COOP_MAX_PLAYERS];

static int coop_host_active_index()
{
    for (int index = 0; index < coop_player_count(); index++) {
        if (coop_player(index) == coop_active_player()) {
            return index;
        }
    }
    return -1;
}

// The line shown while typing.
static std::string coop_host_chat_line(int index)
{
    return "Say: " + coop_host_chat_text[index] + "_   (Enter: send, Esc: cancel)";
}

bool coop_host_chat_key(int keyCode)
{
    if (coop_net_clients_connected() == 0 || coop_player_count() < 2) {
        for (int index = 0; index < COOP_MAX_PLAYERS; index++) {
            coop_host_chat_typing[index] = false;
        }
        return false;
    }

    int index = coop_host_active_index();
    if (index < 0 || index >= COOP_MAX_PLAYERS) {
        return false;
    }

    std::string& text = coop_host_chat_text[index];
    if (!coop_host_chat_typing[index]) {
        if (keyCode == KEY_LOWERCASE_G || keyCode == KEY_UPPERCASE_G) {
            coop_host_marker_place(index);
            return true;
        }
        if (keyCode != KEY_LOWERCASE_T && keyCode != KEY_UPPERCASE_T) {
            return false;
        }
        coop_host_chat_typing[index] = true;
        text.clear();
    } else if (keyCode == KEY_RETURN) {
        coop_host_chat_typing[index] = false;
        if (!text.empty()) {
            std::string message = std::string(coop_player(index)->name) + ": " + text;
            display_print((char*)message.c_str());
            debug_printf("COOP CHAT: %s\n", message.c_str());
        }
    } else if (keyCode == KEY_ESCAPE) {
        coop_host_chat_typing[index] = false;
    } else if (keyCode == KEY_BACKSPACE) {
        if (!text.empty()) {
            text.pop_back();
        }
    } else if (keyCode >= 32 && keyCode < 127 && text.size() < COOP_HOST_CHAT_MAX) {
        text += (char)keyCode;
    }

    coop_host_update_status();
    return true;
}

static void coop_host_update_status()
{
    // Free roam (a player's part of the frame): everyone has their own
    // view. Otherwise (a menu, a combat turn) the one who has the focus.
    int busy = coop_host_turn_slot != 0 && !coop_host_turn_menu ? -1 : coop_host_focus_slot();
    int selected = coop_net_selected();
    for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        coop_net_select(slot);
        if (!coop_net_client_connected()) {
            continue;
        }

        // The player's own view and focus need no explanation; otherwise
        // they are looking at another player's screen.
        const char* status = "";
        char text[80];
        std::string chat;
        if (slot < coop_player_count() && coop_host_chat_typing[slot]) {
            chat = coop_host_chat_line(slot);
            status = chat.c_str();
        } else if (coop_player_count() < 2) {
            status = "Waiting for player 1 to start or load a game.";
        } else if (slot > coop_player_count()) {
            status = "Waiting for the player before you to join.";
        } else if (busy >= 0 && busy != slot) {
            snprintf(text, sizeof(text), isInCombat() ? "Player %d's turn." : "Player %d is busy - please wait.", busy + 1);
            status = text;
        }

        coop_net_send_status(status);
    }
    coop_net_select(selected);
}

// Banner on the host's screen while player 2 has the focus, so player 1
// knows why nothing reacts. Drawn on the presented picture only.
#define COOP_HOST_BANNER_HEIGHT 14

static bool coop_host_banner_shown = false;

// Markers: a player presses G to point at the spot under their mouse for
// the other player. It blinks on both screens for a few seconds.
#define COOP_HOST_MARKER_MS 5000

static int coop_host_marker_tile = -1;
static int coop_host_marker_elevation = 0;
static unsigned int coop_host_marker_start = 0;

// Where the marker is drawn into gSdlSurface: the pixels it covers, put
// back right after the frame is shown or sent.
static SDL_Rect coop_host_marker_rect;
static std::vector<unsigned char> coop_host_marker_saved;
static bool coop_host_marker_drawn = false;

// The texture still shows the last marker until this is blitted over it.
static SDL_Rect coop_host_marker_shown;
static bool coop_host_marker_on_texture = false;

static void coop_host_marker_line(int x0, int y0, int x1, int y1, unsigned char color)
{
    int steps = std::max(abs(x1 - x0), abs(y1 - y0));
    for (int step = 0; step <= steps; step++) {
        int x = steps == 0 ? x0 : x0 + (x1 - x0) * step / steps;
        int y = steps == 0 ? y0 : y0 + (y1 - y0) * step / steps;
        for (int dy = 0; dy < 2; dy++) {
            int px = x;
            int py = y + dy;
            if (px >= coop_host_marker_rect.x && px < coop_host_marker_rect.x + coop_host_marker_rect.w
                && py >= coop_host_marker_rect.y && py < coop_host_marker_rect.y + coop_host_marker_rect.h) {
                ((unsigned char*)gSdlSurface->pixels)[py * gSdlSurface->pitch + px] = color;
            }
        }
    }
}

// Draws the marker into gSdlSurface for the view that is current now.
static void coop_host_marker_draw()
{
    if (coop_host_marker_tile == -1 || gSdlSurface == NULL) {
        return;
    }

    unsigned int age = elapsed_time(coop_host_marker_start);
    if (age > COOP_HOST_MARKER_MS) {
        coop_host_marker_tile = -1;
        return;
    }

    if (coop_host_marker_elevation != map_elevation || (age / 250) % 2 != 0) {
        return;
    }

    int x;
    int y;
    tile_coord(coop_host_marker_tile, &x, &y, coop_host_marker_elevation);
    int centerX = x + 16;
    int centerY = y + 8;

    SDL_Rect rect = { centerX - 20, centerY - 40, 40, 52 };
    SDL_Rect screen = { 0, 0, gSdlSurface->w, gSdlSurface->h };
    if (!SDL_IntersectRect(&rect, &screen, &coop_host_marker_rect)) {
        return;
    }

    coop_host_marker_saved.resize(coop_host_marker_rect.w * coop_host_marker_rect.h);
    for (int row = 0; row < coop_host_marker_rect.h; row++) {
        memcpy(coop_host_marker_saved.data() + row * coop_host_marker_rect.w,
            (unsigned char*)gSdlSurface->pixels + (coop_host_marker_rect.y + row) * gSdlSurface->pitch + coop_host_marker_rect.x,
            coop_host_marker_rect.w);
    }
    coop_host_marker_drawn = true;

    // A hex outline on the spot and an arrow pointing down at it.
    unsigned char color = colorTable[32736];
    coop_host_marker_line(centerX - 16, centerY, centerX, centerY - 8, color);
    coop_host_marker_line(centerX, centerY - 8, centerX + 16, centerY, color);
    coop_host_marker_line(centerX + 16, centerY, centerX, centerY + 8, color);
    coop_host_marker_line(centerX, centerY + 8, centerX - 16, centerY, color);
    coop_host_marker_line(centerX - 8, centerY - 34, centerX, centerY - 14, color);
    coop_host_marker_line(centerX + 8, centerY - 34, centerX, centerY - 14, color);
    coop_host_marker_line(centerX - 1, centerY - 36, centerX - 1, centerY - 16, color);
}

static void coop_host_marker_restore()
{
    if (!coop_host_marker_drawn) {
        return;
    }

    for (int row = 0; row < coop_host_marker_rect.h; row++) {
        memcpy((unsigned char*)gSdlSurface->pixels + (coop_host_marker_rect.y + row) * gSdlSurface->pitch + coop_host_marker_rect.x,
            coop_host_marker_saved.data() + row * coop_host_marker_rect.w,
            coop_host_marker_rect.w);
    }
    coop_host_marker_drawn = false;
}

// Player `index` points at the spot under their mouse.
static void coop_host_marker_place(int index)
{
    int x;
    int y;
    if (index == 0 || coop_host_turn_slot == index) {
        mouse_get_position(&x, &y);
    } else {
        // A player watching player 1's screen.
        x = coop_host_views[index].mouse.x;
        y = coop_host_views[index].mouse.y;
    }

    int tile = tile_num(x, y, map_elevation, false);
    if (tile == -1) {
        return;
    }

    coop_host_marker_tile = tile;
    coop_host_marker_elevation = map_elevation;
    coop_host_marker_start = get_time();

    std::string message = std::string(coop_player(index)->name) + ": look here!";
    display_print((char*)message.c_str());
    debug_printf("COOP HOST: marker at tile %d by player %d\n", tile, index + 1);
}

static void coop_host_draw_banner(const char* text)
{
    int width = gSdlSurface->w;
    SDL_Surface* banner = SDL_CreateRGBSurface(0, width, COOP_HOST_BANNER_HEIGHT, 8, 0, 0, 0, 0);
    if (banner == NULL) {
        return;
    }

    SDL_SetSurfacePalette(banner, gSdlSurface->format->palette);

    unsigned char* pixels = (unsigned char*)banner->pixels;
    for (int row = 0; row < COOP_HOST_BANNER_HEIGHT; row++) {
        memset(pixels + row * banner->pitch, colorTable[0], width);
    }

    int oldFont = text_curr();
    text_font(101);
    text_to_buf(pixels + 2 * banner->pitch + 4, text, width - 8, banner->pitch, colorTable[992]);
    text_font(oldFont);

    SDL_BlitSurface(banner, NULL, gSdlTextureSurface, NULL);
    SDL_FreeSurface(banner);
}

// Shares the screen while a menu or combat runs; player 1's free roam
// frames are not shared (player 2 gets their own view).
static void coop_host_present()
{
    bool freeRoam = coop_host_free_roam_present;
    coop_host_free_roam_present = false;

    // The marker: last frame's off the texture, this frame's on it (and in
    // the shared frame), and gSdlSurface clean again before anything else
    // draws there.
    if (coop_host_marker_on_texture) {
        SDL_BlitSurface(gSdlSurface, &coop_host_marker_shown, gSdlTextureSurface, &coop_host_marker_shown);
        coop_host_marker_on_texture = false;
    }
    coop_host_marker_draw();
    if (coop_host_marker_drawn) {
        coop_host_marker_shown = coop_host_marker_rect;
        SDL_BlitSurface(gSdlSurface, &coop_host_marker_shown, gSdlTextureSurface, &coop_host_marker_shown);
        coop_host_marker_on_texture = true;
    }
    struct MarkerRestore {
        ~MarkerRestore() { coop_host_marker_restore(); }
    } markerRestore;

    // Shared frames at most ~30 per second: map loading presents after
    // every few KB read, and encoding each would slow loading down a lot.
    // (To every player.)
    static unsigned int lastShared = 0;
    if (!freeRoam && elapsed_time(lastShared) >= 33) {
        int selected = coop_net_selected();
        for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
            coop_net_select(slot);
            coop_net_send_current_screen();
        }
        coop_net_select(selected);
        lastShared = get_time();
    }

    int focus = coop_host_focus_slot();
    if (coop_host_chat_typing[0] && coop_net_clients_connected() != 0) {
        coop_host_draw_banner(coop_host_chat_line(0).c_str());
        coop_host_banner_shown = true;
    } else if (!freeRoam && focus != 0) {
        char text[80];
        snprintf(text, sizeof(text), isInCombat() ? "Player %d's turn." : "Player %d is busy - please wait.", focus + 1);
        coop_host_draw_banner(text);
        coop_host_banner_shown = true;
    } else if (coop_host_banner_shown) {
        // Back to what is really there.
        SDL_Rect rect = { 0, 0, gSdlSurface->w, COOP_HOST_BANNER_HEIGHT };
        SDL_BlitSurface(gSdlSurface, &rect, gSdlTextureSurface, &rect);
        coop_host_banner_shown = false;
    }
}

// The current view (of `currentObj`, a player critter) into `view`.
static void coop_host_view_save(CoopView* view, Object* currentObj)
{
    view->initialized = true;
    view->cameraTile = tile_center_tile;
    view->followedTile = currentObj->tile;
    mouse_get_context(&(view->mouse));
    view->cursor = gmouse_get_cursor();
    view->mode = gmouse_3d_get_mode();
}

// Makes `view` (of `incoming`) the current one.
static void coop_host_view_load(CoopView* view, Object* incoming, const CoopView* current)
{
    if (!view->initialized) {
        view->initialized = true;
        view->cameraTile = incoming->tile;
        view->followedTile = incoming->tile;
        view->mouse = current->mouse;
        view->mouse.buttons = 0;
        view->mouse.rawButtons = 0;
        view->mouse.lastButtons = 0;
        view->cursor = MOUSE_CURSOR_NONE;
        view->mode = GAME_MOUSE_MODE_MOVE;
    }

    // The camera follows its player when they move, like player 1's does.
    if (incoming->tile != view->followedTile) {
        view->cameraTile = incoming->tile;
        view->followedTile = incoming->tile;
    }

    // Scroll limits count from the player the view is for. tile_set_center()
    // measures from obj_dude, still player 1 here: it refused to follow a
    // player some 15 hexes or more from player 1, who then walked off their
    // own screen.
    Object* dude = obj_dude;
    obj_dude = incoming;
    tile_set_center(view->cameraTile, 0);
    obj_dude = dude;
    mouse_set_context(&(view->mouse));
    gmouse_3d_set_mode(view->mode);
    gmouse_set_cursor(view->cursor);

    // Changing the cursor shape keeps its hotspot in place by moving the
    // mouse by the difference of the two hotspots, here the other player's
    // and this one's: twice a frame that slid player 1's mouse away (to the
    // top left with the hex cursor). Put the position back as it was saved.
    mouse_set_context(&(view->mouse));

    // Roofs open over the incoming player when they are inside.
    obj_follow_viewer(incoming);
}

// Player `slot` + 1's part of a free roam frame: their input, then their
// view (the client selected; their view current, see coop_host_frame()).
static void coop_host_player_turn(int slot)
{
    PlayerState* player2 = coop_player(slot);

    coop_host_turn_slot = slot;
    coop_host_turn_menu = false;
    coop_host_view_centers[slot] = tile_center_tile;
    coop_host_view_part_count[slot]++;

    {
        ActivePlayerScope scope(player2);
        coop_set_viewer(player2);

        inject_set_exclusive(true);
        coop_host_feed_input();

        // The map still shows the previous view; a screen the input opens
        // over part of it (the skilldex) must have this player's map and
        // HUD behind it.
        if (inject_pending()) {
            intface_update_items(false);
            intface_redraw();
            tile_refresh_display();
        }

        while (inject_pending() && coop_player_count() > 1) {
            int keyCode = get_injected_input();
            if (keyCode != -1) {
                game_handle_input(keyCode, false);
            }
        }

        // Hover: 3D cursor for player 2's mouse.
        gmouse_bk_process();

        coop_host_update_status();

        // Their view, when their client takes a frame now: online, frames
        // wait for acknowledgements, and a view nobody gets would cost a
        // full redraw for nothing (player 1's view is redrawn at the end of
        // the frame anyway).
        if (coop_net_can_send_frame()) {
            unsigned int drawStart = get_time();
            tile_refresh_display();
            intface_update_items(false);
            intface_redraw();
            coop_host_draw_ms += elapsed_time(drawStart);
            coop_host_marker_draw();
            unsigned int sendStart = get_time();
            coop_net_send_current_screen();
            coop_host_send_ms += elapsed_time(sendStart);
            coop_host_marker_restore();
        } else {
            coop_host_views_skipped++;
        }
    }

    coop_set_viewer(NULL);

    coop_host_turn_slot = 0;
    coop_host_turn_menu = false;
    inject_set_exclusive(false);
}

// Player `slot` + 1 joins (the next player of the game): with the
// character they brought (1.9), or else character creation, driven by the
// client (selected).
static void coop_host_player_joins(int slot)
{
    static unsigned int joinsSeen[COOP_MAX_PLAYERS];
    static unsigned int joinedAt[COOP_MAX_PLAYERS];
    if (coop_net_client_joins() != joinsSeen[slot]) {
        joinsSeen[slot] = coop_net_client_joins();
        joinedAt[slot] = get_time();
    }

    if (coop_net_client_brings_character()) {
        std::vector<unsigned char> data;
        if (coop_net_take_client_character(&data)) {
            std::string error;
            if (coop_import_player(data, &error) != NULL) {
                coop_request_character_copy();
                return;
            }
            debug_printf("\nCOOP HOST: player %d's character could not be used: %s\n", slot + 1, error.c_str());
            char message[200];
            snprintf(message, sizeof(message), "Player %d's character could not be used (%s); they make a new one.", slot + 1, error.c_str());
            display_print(message);
        } else if (elapsed_time(joinedAt[slot]) < 10000) {
            // It is on the way (it follows the hello).
            return;
        }
    }

    if (coop_create_player() != NULL) {
        coop_request_character_copy();
    }
}

// Now and then, and after saving, each player gets a copy of their
// character to keep (old clients ignore it).
static void coop_host_send_character_copy(int slot, bool requested)
{
    static unsigned int lastCopy[COOP_MAX_PLAYERS];
    if (!requested && elapsed_time(lastCopy[slot]) < 120000) {
        return;
    }
    lastCopy[slot] = get_time();

    std::vector<unsigned char> data;
    if (coop_export_player(slot, &data)) {
        coop_net_send_character(data);
    }
}

int coop_host_view_center(int slot)
{
    return slot >= 0 && slot < COOP_MAX_PLAYERS ? coop_host_view_centers[slot] : -1;
}

unsigned int coop_host_view_parts(int slot)
{
    return slot >= 0 && slot < COOP_MAX_PLAYERS ? coop_host_view_part_count[slot] : 0;
}

void coop_host_frame()
{
    if (!coop_host_on) {
        return;
    }

    coop_net_host_pump();

    // The online game code in the message window when the game starts and
    // whenever someone leaves (while there is room), so player 1 can pass it
    // on.
    static bool codeShown = false;
    bool full = coop_net_clients_connected() >= coop_net_max_clients_value();
    if (full) {
        codeShown = false;
    } else if (!codeShown && coop_net_relay_code()[0] != '\0') {
        std::string message = std::string("Online game code: ") + coop_net_relay_code();
        display_print((char*)message.c_str());
        codeShown = true;
    }

    unsigned int frameStart = get_time();
    bool copyRequested = coop_take_character_copy_request();
    int turns[COOP_MAX_PLAYERS];
    int turnCount = 0;
    int selected = coop_net_selected();
    for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        coop_net_select(slot);
        bool connected = coop_net_client_connected();
        if (!connected) {
            // Whatever they were typing went with them.
            coop_host_chat_typing[slot] = false;
        }

        // A player away (no client): their character is hidden, so nothing
        // can kill them (which would end the game) and nobody trips over
        // them; back next to player 1 when the client returns. Not in the
        // middle of a fight: their turns just pass until it ends.
        if (slot < coop_player_count() && !isInCombat()) {
            Object* player = coop_player(slot)->obj;
            bool hidden = (player->flags & OBJECT_HIDDEN) != 0;
            if (!connected && !hidden && !critter_is_dead(player)) {
                Rect rect;
                obj_turn_off(player, &rect);
                tile_refresh_rect(&rect, player->elevation);
                debug_printf("\nCOOP HOST: player %d away (hidden)\n", slot + 1);
            } else if (connected && hidden) {
                Object* player1 = coop_player(0)->obj;
                Rect rect;
                obj_turn_on(player, &rect);
                obj_attempt_placement(player, player1->tile, player1->elevation, 2);
                tile_refresh_display();
                debug_printf("\nCOOP HOST: player %d back\n", slot + 1);
            }
        }

        if (connected && !isInCombat()) {
            if (coop_player_count() == slot) {
                coop_host_player_joins(slot);
            } else if (slot < coop_player_count()) {
                // Their part (input and view) when their client takes a
                // frame now: online, frames wait for acknowledgements, and a
                // part whose view nobody gets costs two full redraws for
                // nothing (theirs, and player 1's again after it). Their
                // input waits until then.
                if (coop_net_can_send_frame()) {
                    turns[turnCount++] = slot;
                } else {
                    coop_host_views_skipped++;
                }
            }
        }
    }

    // The players' parts, one view after the other, and player 1's view
    // back once at the end.
    if (turnCount > 0) {
        Object* player1 = coop_player(0)->obj;
        CoopView player1View;
        mouse_hide();
        coop_host_view_save(&player1View, player1);
        const CoopView* previous = &player1View;
        for (int index = 0; index < turnCount && index < coop_player_count(); index++) {
            int slot = turns[index];
            if (slot >= coop_player_count()) {
                continue;
            }
            Object* player = coop_player(slot)->obj;
            coop_host_view_load(&(coop_host_views[slot]), player, previous);
            mouse_show();
            coop_net_select(slot);
            coop_host_player_turn(slot);
            coop_host_send_character_copy(slot, copyRequested);
            mouse_hide();
            coop_host_view_save(&(coop_host_views[slot]), player);
            previous = &(coop_host_views[slot]);
        }
        coop_host_view_load(&player1View, player1, previous);
        mouse_show();

        gmouse_bk_process();
        intface_update_items(false);
        tile_refresh_display();
        intface_redraw();
    }
    coop_net_select(selected);

    // How fast the host keeps up, for the debug log.
    static unsigned int statsSince = 0;
    static unsigned int statsFrames = 0;
    static unsigned int statsBusy = 0;
    statsBusy += elapsed_time(frameStart);
    statsFrames++;
    if (elapsed_time(statsSince) >= 10000) {
        if (coop_net_clients_connected() > 0) {
            debug_printf("\nCOOP HOST: %u frames in %u ms, %u ms of them for the other players' parts (drawing %u, sending %u; %u views not needed)\n",
                statsFrames, elapsed_time(statsSince), statsBusy, coop_host_draw_ms, coop_host_send_ms, coop_host_views_skipped);
        }
        statsSince = get_time();
        statsFrames = 0;
        statsBusy = 0;
        coop_host_draw_ms = 0;
        coop_host_send_ms = 0;
        coop_host_views_skipped = 0;
    }

    coop_host_free_roam_present = true;
}

} // namespace fallout
