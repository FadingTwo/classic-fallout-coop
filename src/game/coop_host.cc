#include "game/coop_host.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <string>
#include <vector>

#include "game/combat.h"
#include "game/coop.h"
#include "game/coop_lobby.h"
#include "game/critter.h"
#include "game/display.h"
#include "game/coop_net.h"
#include "game/game.h"
#include "game/gmouse.h"
#include "game/gsound.h"
#include "game/intface.h"
#include "game/map.h"
#include "game/object.h"
#include "game/protinst.h"
#include "game/tile.h"
#include "plib/color/color.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/inject.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/input.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

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
static void coop_host_swap_view(CoopView* view, Object* current, Object* incoming);
static void coop_host_player2_turn();
static void coop_host_update_status();
static void coop_host_player2_joins();
static void coop_host_send_character_copy();
static void coop_host_marker_place(int index);
static void coop_host_marker_draw();
static void coop_host_marker_restore();

static bool coop_host_on = false;

// Inside the main loop's own get_input() call.
static bool coop_host_main_input = false;

// Inside player 2's part of the frame.
static bool coop_host_in_player2_turn = false;

// Set by coop_host_frame() for the main loop's following renderPresent():
// that one shows player 1's own view and is not streamed.
static bool coop_host_free_roam_present = false;

// Player 2's view while player 1's is current, and vice versa.
static CoopView coop_host_other_view;

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

static bool coop_host_player2_has_focus()
{
    return coop_player_count() > 1 && coop_active_player() == coop_player(1);
}

// For LAN searches and the server browser.
static void coop_host_pump_lobby()
{
    std::string name = "Wanderer";
    if (coop_player_count() > 0 && coop_player(0)->obj != NULL) {
        name = critter_name(coop_player(0)->obj);
    }
    coop_lobby_host_pump(name, coop_net_client_connected() ? 2 : 1);
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

    // A menu or combat turn: the active player has the focus.
    static bool player2HadFocus = false;
    bool player2Focus = coop_host_player2_has_focus();
    if (player2Focus != player2HadFocus) {
        debug_printf("\nCOOP HOST: focus: player %d\n", player2Focus ? 2 : 1);
        player2HadFocus = player2Focus;
    }

    if (player2Focus && !coop_net_client_connected()) {
        // Player 2 left in the middle of a menu or turn: close it (Escape)
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
    } else if (player2Focus) {
        inject_set_exclusive(true);
        coop_host_feed_input();
    } else {
        inject_set_exclusive(false);

        // Player 2 watches; their clicks do nothing meanwhile. Keep where
        // their mouse is, though, and let them chat.
        CoopNetInput input;
        while (coop_net_next_input(&input)) {
            if (input.kind == COOP_NET_INPUT_MOUSE) {
                coop_host_other_view.mouse.x = input.x;
                coop_host_other_view.mouse.y = input.y;
            } else if (input.kind == COOP_NET_INPUT_KEY && input.value > 0) {
                ActivePlayerScope scope(coop_player(1));
                coop_host_chat_key(input.value);
            }
        }
    }

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
    if (!coop_net_client_connected() || coop_player_count() < 2) {
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
    if (!coop_net_client_connected()) {
        return;
    }

    // Player 2's own view and focus need no explanation; otherwise player 2
    // is looking at player 1's screen.
    const char* status = "";
    std::string chat;
    if (coop_player_count() > 1 && coop_host_chat_typing[1]) {
        chat = coop_host_chat_line(1);
        status = chat.c_str();
    } else if (coop_player_count() < 2) {
        status = "Waiting for player 1 to start or load a game.";
    } else if (!coop_host_in_player2_turn && !coop_host_player2_has_focus()) {
        status = isInCombat() ? "Player 1's turn." : "Player 1 is busy - please wait.";
    }

    coop_net_send_status(status);
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
    if (index == 0 || coop_host_in_player2_turn) {
        mouse_get_position(&x, &y);
    } else {
        // Player 2 watching player 1's screen.
        x = coop_host_other_view.mouse.x;
        y = coop_host_other_view.mouse.y;
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
    static unsigned int lastShared = 0;
    if (!freeRoam && elapsed_time(lastShared) >= 33) {
        coop_net_send_current_screen();
        lastShared = get_time();
    }

    if (coop_host_chat_typing[0] && coop_net_client_connected()) {
        coop_host_draw_banner(coop_host_chat_line(0).c_str());
        coop_host_banner_shown = true;
    } else if (!freeRoam && coop_host_player2_has_focus()) {
        coop_host_draw_banner(isInCombat() ? "Player 2's turn." : "Player 2 is busy - please wait.");
        coop_host_banner_shown = true;
    } else if (coop_host_banner_shown) {
        // Back to what is really there.
        SDL_Rect rect = { 0, 0, gSdlSurface->w, COOP_HOST_BANNER_HEIGHT };
        SDL_BlitSurface(gSdlSurface, &rect, gSdlTextureSurface, &rect);
        coop_host_banner_shown = false;
    }
}

// Exchanges the current view (of `current`, a player critter) with `view`
// (of `incoming`).
static void coop_host_swap_view(CoopView* view, Object* currentObj, Object* incoming)
{
    mouse_hide();

    CoopView current;
    current.initialized = true;
    current.cameraTile = tile_center_tile;
    current.followedTile = currentObj->tile;
    mouse_get_context(&(current.mouse));
    current.cursor = gmouse_get_cursor();
    current.mode = gmouse_3d_get_mode();

    if (!view->initialized) {
        view->initialized = true;
        view->cameraTile = incoming->tile;
        view->followedTile = incoming->tile;
        view->mouse = current.mouse;
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

    tile_set_center(view->cameraTile, 0);
    mouse_set_context(&(view->mouse));
    gmouse_3d_set_mode(view->mode);
    gmouse_set_cursor(view->cursor);

    // Changing the cursor shape keeps its hotspot in place by moving the
    // mouse by the difference of the two hotspots, here the other player's
    // and this one's: twice a frame that slid player 1's mouse away (to the
    // top left with the hex cursor). Put the position back as it was saved.
    mouse_set_context(&(view->mouse));

    *view = current;

    mouse_show();
}

// Player 2's part of a free roam frame: their input, then their view.
static void coop_host_player2_turn()
{
    PlayerState* player1 = coop_player(0);
    PlayerState* player2 = coop_player(1);

    coop_host_in_player2_turn = true;

    {
        ActivePlayerScope scope(player2);
        coop_set_viewer(player2);
        coop_host_swap_view(&coop_host_other_view, player1->obj, player2->obj);

        // Player 2's HUD, also behind any screen their input opens.
        intface_update_items(false);
        intface_redraw();

        inject_set_exclusive(true);
        coop_host_feed_input();

        // The map still shows player 1's view; a screen the input opens over
        // part of it (the skilldex) must have player 2's behind it.
        if (inject_pending()) {
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

        tile_refresh_display();
        intface_update_items(false);
        intface_redraw();
        coop_host_update_status();
        coop_host_marker_draw();
        coop_net_send_current_screen();
        coop_host_marker_restore();
    }

    coop_set_viewer(NULL);

    // Player 1's view again.
    coop_host_swap_view(&coop_host_other_view, player2->obj, player1->obj);

    coop_host_in_player2_turn = false;
    inject_set_exclusive(false);

    gmouse_bk_process();
    intface_update_items(false);
    tile_refresh_display();
    intface_redraw();
}

// Player 2 joins a game without a player 2: with the character they
// brought (1.9), or else character creation, driven by the client.
static void coop_host_player2_joins()
{
    static unsigned int joinsSeen = 0;
    static unsigned int joinedAt = 0;
    if (coop_net_client_joins() != joinsSeen) {
        joinsSeen = coop_net_client_joins();
        joinedAt = get_time();
    }

    if (coop_net_client_brings_character()) {
        std::vector<unsigned char> data;
        if (coop_net_take_client_character(&data)) {
            std::string error;
            if (coop_import_player2(data, &error) != NULL) {
                coop_request_character_copy();
                return;
            }
            debug_printf("\nCOOP HOST: player 2's character could not be used: %s\n", error.c_str());
            std::string message = "Player 2's character could not be used (" + error + "); they make a new one.";
            display_print((char*)message.c_str());
        } else if (elapsed_time(joinedAt) < 10000) {
            // It is on the way (it follows the hello).
            return;
        }
    }

    if (coop_create_player2() != NULL) {
        coop_request_character_copy();
    }
}

// Now and then, and after saving, player 2 gets a copy of their character
// to keep (old clients ignore it).
static void coop_host_send_character_copy()
{
    static unsigned int lastCopy = 0;
    if (!coop_take_character_copy_request() && elapsed_time(lastCopy) < 120000) {
        return;
    }
    lastCopy = get_time();

    std::vector<unsigned char> data;
    if (coop_export_player2(&data)) {
        coop_net_send_character(data);
    }
}

void coop_host_frame()
{
    if (!coop_host_on) {
        return;
    }

    coop_net_host_pump();

    // The online game code in the message window when the game starts and
    // whenever player 2 leaves, so player 1 can pass it on.
    static bool codeShown = false;
    bool connected = coop_net_client_connected();
    if (!connected) {
        // Whatever player 2 was typing went with them.
        coop_host_chat_typing[1] = false;
    }
    if (connected) {
        codeShown = false;
    } else if (!codeShown && coop_net_relay_code()[0] != '\0') {
        std::string message = std::string("Online game code: ") + coop_net_relay_code();
        display_print((char*)message.c_str());
        codeShown = true;
    }

    // Player 2 away (no client): their character is hidden, so nothing can
    // kill them (which would end player 1's game) and nobody trips over
    // them; back next to player 1 when the client returns. Not in the
    // middle of a fight: their turns just pass until it ends.
    if (coop_player_count() > 1 && !isInCombat()) {
        Object* player2 = coop_player(1)->obj;
        bool away = !coop_net_client_connected();
        bool hidden = (player2->flags & OBJECT_HIDDEN) != 0;
        if (away && !hidden && !critter_is_dead(player2)) {
            Rect rect;
            obj_turn_off(player2, &rect);
            tile_refresh_rect(&rect, player2->elevation);
            debug_printf("\nCOOP HOST: player 2 away (hidden)\n");
        } else if (!away && hidden) {
            Object* player1 = coop_player(0)->obj;
            Rect rect;
            obj_turn_on(player2, &rect);
            obj_attempt_placement(player2, player1->tile, player1->elevation, 2);
            tile_refresh_display();
            debug_printf("\nCOOP HOST: player 2 back\n");
        }
    }

    if (coop_net_client_connected() && !isInCombat()) {
        if (coop_player_count() == 1) {
            coop_host_player2_joins();
        } else {
            coop_host_player2_turn();
            coop_host_send_character_copy();
        }
    }

    coop_host_free_roam_present = true;
}

} // namespace fallout
