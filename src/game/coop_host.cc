#include "game/coop_host.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <string>
#include <vector>

#include "game/art.h"
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
#include "game/light.h"
#include "game/map.h"
#include "plib/gnw/gnw.h"
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
    // The 3D cursor shows (the mouse is on the map).
    bool cursorOn;
    // What it shows when the mouse rests over something.
    GmouseHoverState hover;
} CoopView;

static void coop_host_poll();
static void coop_host_present();
static void coop_host_set_frame_shift(int slot);
static bool coop_host_feed_input(int slot);
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
// ... and in their input (with a redraw before it, for a screen it opens),
// switching between the views, and player 1's view drawn again after them.
static unsigned int coop_host_input_ms = 0;
static unsigned int coop_host_switch_ms = 0;
static unsigned int coop_host_restore_ms = 0;
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

// Player 1's view back after the others' parts without a full redraw:
// their map window as it was, when those parts changed nothing on the map
// but the 3D cursor (most frames: what walks around does so in the main
// loop, drawn in player 1's view there), then their cursor put back where
// their mouse is (redrawing where it was and where it goes, as single
// player does). A full redraw otherwise. COOP_HOST_RESTORE=0 turns it off;
// COOP_HOST_RESTORE_CHECK=1 redraws the whole map after each restore and
// compares (for tests).
static int coop_host_restore_mode = -1;
static bool coop_host_restore_check_on = false;
static std::vector<unsigned char> coop_host_restore_saved;
// The others' parts redrew some of the map for a change to it.
static bool coop_host_restore_changed = false;
// Inside a view's own full redraw (not a change to the map).
static bool coop_host_own_draw = false;
// For the debug log: player 1's view restored or redrawn, and (checking)
// restores that differed from a full redraw.
static unsigned int coop_host_restores = 0;
static unsigned int coop_host_redraws = 0;
static unsigned int coop_host_restore_mismatches = 0;
static unsigned int coop_host_restore_mismatches_logged = 0;
static unsigned int coop_host_changes_logged = 0;
// ... in the whole session.
static unsigned int coop_host_restores_total = 0;
static unsigned int coop_host_redraws_total = 0;
static unsigned int coop_host_restore_mismatches_total = 0;
static int coop_host_restore_mismatch_most = 0;

static int coop_host_restore_setting()
{
    if (coop_host_restore_mode == -1) {
        const char* setting = getenv("COOP_HOST_RESTORE");
        coop_host_restore_mode = setting != NULL ? atoi(setting) : 1;
        coop_host_restore_check_on = getenv("COOP_HOST_RESTORE_CHECK") != NULL;
    }
    return coop_host_restore_mode;
}

// Inside the other players' parts of the frame.
static bool coop_host_in_parts = false;

// A changed part of the map window (in screen coordinates of the camera at
// the time) and where a reference tile was on screen then: screen
// positions move by the same offset for every tile between two cameras.
typedef struct CoopChange {
    Rect rect;
    int referenceX;
    int referenceY;
} CoopChange;

#define COOP_HOST_REFERENCE_TILE 20100

// More changes than this since a player's last picture: a full redraw.
#define COOP_HOST_PICTURE_MAX_CHANGES 48

// Each other player's map window as last drawn for them. Their next one is
// that, moved as far as their camera moved, with what came into view and
// what changed on the map since redrawn (as single player scrolls and
// redraws what changes), and a full redraw after a change of map or floor,
// a roof opening or closing over them, or too many changes.
// COOP_HOST_VIEW_CHANGES=0 turns it off; COOP_HOST_VIEW_CHECK=1 redraws the
// whole map after each such picture and compares (for tests).
typedef struct CoopPicture {
    std::vector<unsigned char> pixels;
    bool valid;
    unsigned char* buffer;
    int width;
    int height;
    int map;
    int elevation;
    // The reference tile's screen position then.
    int referenceX;
    int referenceY;
    // Their character's tile and roof square (-1: none) then.
    int tile;
    int roof;
    // The egg (see-through walls around them) and their 3D cursor then.
    Rect egg;
    Rect cursor[2];
    int cursorCount;
    // Where the map's edges were (what is drawn stays inside them).
    Rect bounds;
    // What changed on the map since.
    std::vector<CoopChange> changes;
    bool changedAll;
} CoopPicture;

static CoopPicture coop_host_pictures[COOP_MAX_PLAYERS];
static int coop_host_view_changes_mode = -1;
static bool coop_host_view_check_on = false;
static std::vector<unsigned char> coop_host_view_check;
// For the debug log: the others' pictures drawn whole or from the last one,
// and (checking) how many of the latter differed from a full redraw.
static unsigned int coop_host_pictures_whole = 0;
static unsigned int coop_host_pictures_changed = 0;
static unsigned int coop_host_picture_mismatches = 0;
static unsigned int coop_host_picture_mismatches_logged = 0;
static unsigned int coop_host_pictures_whole_total = 0;
static unsigned int coop_host_pictures_changed_total = 0;
static unsigned int coop_host_picture_mismatches_total = 0;
static int coop_host_picture_mismatch_most = 0;

static void coop_host_note_refresh(Rect* rect, int elevation, bool whole)
{
    if (coop_host_own_draw || gmouse_3d_refreshing_cursor()) {
        return;
    }

    // Player 1's view needs a full redraw after the parts.
    if (coop_host_in_parts) {
        if (coop_host_restore_check_on && !coop_host_restore_changed && coop_host_changes_logged < 20) {
            coop_host_changes_logged++;
            debug_printf("\nCOOP HOST: player %d's part redrew %d,%d-%d,%d%s\n", coop_host_turn_slot + 1,
                rect->ulx, rect->uly, rect->lrx, rect->lry, whole ? " (all of the map)" : "");
        }
        coop_host_restore_changed = true;
    }

    // The others' pictures.
    bool referenced = false;
    int referenceX = 0;
    int referenceY = 0;
    for (int slot = 1; slot < COOP_MAX_PLAYERS; slot++) {
        CoopPicture* picture = &(coop_host_pictures[slot]);
        if (!picture->valid || picture->changedAll) {
            continue;
        }

        if (whole || picture->changes.size() >= COOP_HOST_PICTURE_MAX_CHANGES) {
            picture->changedAll = true;
            continue;
        }

        if (!referenced) {
            tile_coord(COOP_HOST_REFERENCE_TILE, &referenceX, &referenceY, elevation);
            referenced = true;
        }

        // A pixel more each way: another player's outline (only the other
        // players see it) goes a pixel beyond the redraw for them.
        CoopChange change;
        change.rect.ulx = rect->ulx - 1;
        change.rect.uly = rect->uly - 1;
        change.rect.lrx = rect->lrx + 1;
        change.rect.lry = rect->lry + 1;
        change.referenceX = referenceX;
        change.referenceY = referenceY;
        picture->changes.push_back(change);
    }
}

static bool coop_host_view_changes_on()
{
    if (coop_host_view_changes_mode == -1) {
        const char* setting = getenv("COOP_HOST_VIEW_CHANGES");
        coop_host_view_changes_mode = setting != NULL ? atoi(setting) : 1;
        coop_host_view_check_on = getenv("COOP_HOST_VIEW_CHECK") != NULL;
    }
    return coop_host_view_changes_mode != 0;
}

// The roof square over `obj` (-1: none), as obj_update_roof() sees it.
static int coop_host_roof_square(Object* obj)
{
    if (obj->tile < 0 || obj->elevation < 0 || obj->elevation >= ELEVATION_COUNT || square[obj->elevation] == NULL) {
        return -1;
    }

    int roofX = obj->tile % 200 / 2;
    int roofY = obj->tile / 200 / 2;
    int roof = square[obj->elevation]->field_0[roofX + 100 * roofY];
    if (art_id(OBJ_TYPE_TILE, (roof >> 16) & 0xFFF, 0, 0, 0) == art_id(OBJ_TYPE_TILE, 1, 0, 0, 0)) {
        return -1;
    }
    return roofX + 100 * roofY;
}

// Where the map's edges are on screen now: what is drawn stays inside them,
// darkened along them (CE's bounds_render()).
static Rect coop_host_map_bounds()
{
    Rect bounds = { -1000000, -1000000, 1000000, 1000000 };
    tile_inside_bound(&bounds);
    return bounds;
}

// Adds `rect` (clipped to the map window) to `rects`; false when there are
// too many.
static bool coop_host_add_rect(std::vector<Rect>& rects, Rect rect, int width, int height, int* area)
{
    rect.ulx = std::max(rect.ulx, 0);
    rect.uly = std::max(rect.uly, 0);
    rect.lrx = std::min(rect.lrx, width - 1);
    rect.lry = std::min(rect.lry, height - 1);
    if (rect.ulx > rect.lrx || rect.uly > rect.lry) {
        return true;
    }

    rects.push_back(rect);
    *area += (rect.lrx - rect.ulx + 1) * (rect.lry - rect.uly + 1);
    return rects.size() <= COOP_HOST_PICTURE_MAX_CHANGES + 8;
}

// Player `slot` + 1's map window drawn from their last picture (their view
// current): false (nothing drawn) when it needs a full redraw.
static bool coop_host_draw_picture(int slot, Object* player)
{
    CoopPicture* picture = &(coop_host_pictures[slot]);
    unsigned char* buffer = win_get_buf(display_win);
    if (!coop_host_view_changes_on() || !picture->valid || picture->changedAll || buffer == NULL) {
        return false;
    }

    int width = win_width(display_win);
    int height = win_height(display_win);
    if (buffer != picture->buffer || width != picture->width || height != picture->height
        || map_get_index_number() != picture->map || map_elevation != picture->elevation
        || coop_host_roof_square(player) != picture->roof) {
        return false;
    }

    // How far the picture moves with the camera.
    int referenceX;
    int referenceY;
    tile_coord(COOP_HOST_REFERENCE_TILE, &referenceX, &referenceY, map_elevation);
    int dx = referenceX - picture->referenceX;
    int dy = referenceY - picture->referenceY;
    if (abs(dx) >= width / 2 || abs(dy) >= height / 2) {
        return false;
    }

    // What to redraw: what comes into view, what changed, their cursor and
    // egg where they were (moved with the picture) and where they are.
    std::vector<Rect> rects;
    int area = 0;
    bool fits = true;
    if (dx > 0) {
        fits = fits && coop_host_add_rect(rects, { 0, 0, dx - 1, height - 1 }, width, height, &area);
    } else if (dx < 0) {
        fits = fits && coop_host_add_rect(rects, { width + dx, 0, width - 1, height - 1 }, width, height, &area);
    }
    if (dy > 0) {
        fits = fits && coop_host_add_rect(rects, { 0, 0, width - 1, dy - 1 }, width, height, &area);
    } else if (dy < 0) {
        fits = fits && coop_host_add_rect(rects, { 0, height + dy, width - 1, height - 1 }, width, height, &area);
    }
    for (const CoopChange& change : picture->changes) {
        Rect rect = change.rect;
        int moveX = referenceX - change.referenceX;
        int moveY = referenceY - change.referenceY;
        rect.ulx += moveX;
        rect.lrx += moveX;
        rect.uly += moveY;
        rect.lry += moveY;
        fits = fits && coop_host_add_rect(rects, rect, width, height, &area);
    }
    for (int index = 0; index < picture->cursorCount; index++) {
        Rect rect = picture->cursor[index];
        rect.ulx += dx;
        rect.lrx += dx;
        rect.uly += dy;
        rect.lry += dy;
        fits = fits && coop_host_add_rect(rects, rect, width, height, &area);
    }
    if (gmouse_3d_is_on()) {
        Rect rect;
        obj_bound(obj_mouse, &rect);
        fits = fits && coop_host_add_rect(rects, rect, width, height, &area);
        obj_bound(obj_mouse_flat, &rect);
        fits = fits && coop_host_add_rect(rects, rect, width, height, &area);
    }
    // The map's edges snap to the camera's 32 by 24 pixel steps: moved by a
    // hex they can move another way than the map (by 0 or 32 pixels where
    // the map moved 16). Then what lies along them, darkened, is redrawn.
    Rect bounds = coop_host_map_bounds();
    Rect oldBounds = picture->bounds;
    oldBounds.ulx += dx;
    oldBounds.lrx += dx;
    oldBounds.uly += dy;
    oldBounds.lry += dy;
    const int edge = 16 + 2;
    if (bounds.ulx != oldBounds.ulx) {
        fits = fits && coop_host_add_rect(rects, { std::min(bounds.ulx, oldBounds.ulx) - 2, 0, std::max(bounds.ulx, oldBounds.ulx) + edge, height - 1 }, width, height, &area);
    }
    if (bounds.uly != oldBounds.uly) {
        fits = fits && coop_host_add_rect(rects, { 0, std::min(bounds.uly, oldBounds.uly) - 2, width - 1, std::max(bounds.uly, oldBounds.uly) + edge }, width, height, &area);
    }
    if (bounds.lrx != oldBounds.lrx) {
        fits = fits && coop_host_add_rect(rects, { std::min(bounds.lrx, oldBounds.lrx) - edge, 0, std::max(bounds.lrx, oldBounds.lrx) + 2, height - 1 }, width, height, &area);
    }
    if (bounds.lry != oldBounds.lry) {
        fits = fits && coop_host_add_rect(rects, { 0, std::min(bounds.lry, oldBounds.lry) - edge, width - 1, std::max(bounds.lry, oldBounds.lry) + 2 }, width, height, &area);
    }

    // (The egg moves within a hex too, mid-step.)
    Rect oldEgg = picture->egg;
    oldEgg.ulx += dx;
    oldEgg.lrx += dx;
    oldEgg.uly += dy;
    oldEgg.lry += dy;
    Rect egg;
    obj_bound(obj_egg, &egg);
    if (player->tile != picture->tile || memcmp(&oldEgg, &egg, sizeof(egg)) != 0) {
        fits = fits && coop_host_add_rect(rects, oldEgg, width, height, &area);
        fits = fits && coop_host_add_rect(rects, egg, width, height, &area);
    }
    // Most of the window: a full redraw is as quick.
    if (!fits || area > width * height * 3 / 4) {
        return false;
    }

    // The last picture, moved.
    int copyWidth = width - abs(dx);
    int copyHeight = height - abs(dy);
    for (int row = 0; row < copyHeight; row++) {
        int toY = dy > 0 ? row + dy : row;
        int fromY = dy > 0 ? row : row - dy;
        int toX = dx > 0 ? dx : 0;
        int fromX = dx > 0 ? 0 : -dx;
        memcpy(buffer + (size_t)toY * width + toX, picture->pixels.data() + (size_t)fromY * width + fromX, copyWidth);
    }

    for (Rect& rect : rects) {
        tile_refresh_rect(&rect, map_elevation);
    }
    win_draw(display_win);
    return true;
}

// Testing: what can change what player 1's view shows without a redraw
// (where things are, their looks and lights, the egg, the camera), as one
// line, to tell what changed when a restored view differs from a full
// redraw.
static std::string coop_host_view_state()
{
    char line[160];
    std::string state;
    snprintf(line, sizeof(line), "camera %d; egg %d %d,%d", tile_center_tile, obj_egg->tile, obj_egg->x, obj_egg->y);
    state += line;
    // Player 1's 3D cursor: the arrow (or X) and the hex.
    snprintf(line, sizeof(line), "; cursor %d %d,%d fid %x frame %d flags %x, hex %d fid %x frame %d flags %x",
        obj_mouse->tile, obj_mouse->x, obj_mouse->y, obj_mouse->fid, obj_mouse->frame, obj_mouse->flags,
        obj_mouse_flat->tile, obj_mouse_flat->fid, obj_mouse_flat->frame, obj_mouse_flat->flags);
    state += line;
    for (int index = 0; index < coop_player_count(); index++) {
        Object* obj = coop_player(index)->obj;
        snprintf(line, sizeof(line), "; p%d %d %d,%d fid %x frame %d light %d/%d flags %x lit %d", index + 1, obj->tile, obj->x, obj->y,
            obj->fid, obj->frame, obj->lightDistance, obj->lightIntensity, obj->flags, light_get_tile(obj->elevation, obj->tile));
        state += line;
    }
    return state;
}

static std::string coop_host_restore_state;
// ... and at the end of the frame before (player 1's view drawn or
// restored, checked).
static std::string coop_host_end_state;

// The last comparison's difference was the biggest so far (by twice).
static bool coop_host_compare_record = false;

// A line longer than the debug log takes (260 characters), in pieces.
static void coop_host_log_long(const char* label, const std::string& text)
{
    for (size_t at = 0; at == 0 || at < text.size(); at += 200) {
        debug_printf("COOP HOST:   %s%s\n", at == 0 ? label : "  ... ", text.substr(at, 200).c_str());
    }
}

// Testing: redraws the whole map window and compares it with what it held
// (`what`, for the log); how many pixels differed.
static int coop_host_compare_full_redraw(const char* what, unsigned int* logged, bool ignoreCursor = false)
{
    unsigned char* buffer = win_get_buf(display_win);
    if (buffer == NULL) {
        return 0;
    }

    int width = win_width(display_win);
    size_t size = (size_t)width * win_height(display_win);
    coop_host_view_check.assign(buffer, buffer + size);
    bool ownDraw = coop_host_own_draw;
    coop_host_own_draw = true;
    tile_refresh_display();
    coop_host_own_draw = ownDraw;
    // Not where the 3D cursor is, when asked (a redraw of its part draws it
    // where a full one doesn't, at the map's edge).
    if (ignoreCursor && gmouse_3d_is_on()) {
        Object* cursors[2] = { obj_mouse, obj_mouse_flat };
        for (Object* cursor : cursors) {
            Rect rect;
            obj_bound(cursor, &rect);
            int height = (int)(size / width);
            for (int y = std::max(rect.uly, 0); y <= std::min(rect.lry, height - 1); y++) {
                for (int x = std::max(rect.ulx, 0); x <= std::min(rect.lrx, width - 1); x++) {
                    coop_host_view_check[(size_t)y * width + x] = buffer[(size_t)y * width + x];
                }
            }
        }
    }

    if (memcmp(coop_host_view_check.data(), buffer, size) == 0) {
        return 0;
    }

    int left = width;
    int top = -1;
    int right = -1;
    int bottom = -1;
    int pixels = 0;
    for (size_t index = 0; index < size; index++) {
        if (coop_host_view_check[index] != buffer[index]) {
            int x = (int)(index % width);
            int y = (int)(index / width);
            left = std::min(left, x);
            right = std::max(right, x);
            if (top == -1) {
                top = y;
            }
            bottom = y;
            pixels++;
        }
    }
    // The first few, and the first few big ones (a missed change rather
    // than the engine drawing a part a pixel differently than a whole);
    // and the biggest so far, which may come later.
    static unsigned int bigLogged = 0;
    static int biggest = 0;
    coop_host_compare_record = pixels > 1000 && pixels > biggest * 2;
    if (coop_host_compare_record) {
        biggest = pixels;
    }
    if (coop_host_compare_record || (pixels > 50 ? bigLogged++ < 20 : (*logged)++ < 20)) {
        debug_printf("\nCOOP HOST: %s differs from a full redraw in %d pixels in %d,%d-%d,%d\n", what, pixels, left, top, right, bottom);
    }

    // The first few big ones (and a few biggest so far) as pictures:
    // coop-redraw-N-a.bmp as it was, -b.bmp redrawn.
    static int dumps = 0;
    if (pixels > 50 && (dumps < 4 || (coop_host_compare_record && dumps < 8)) && gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        dumps++;
        int height = (int)(size / width);
        for (int which = 0; which < 2; which++) {
            unsigned char* pixelsToSave = which == 0 ? coop_host_view_check.data() : buffer;
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixelsToSave, width, height, 8, width, SDL_PIXELFORMAT_INDEX8);
            if (surface != NULL) {
                SDL_SetSurfacePalette(surface, gSdlSurface->format->palette);
                char name[64];
                snprintf(name, sizeof(name), "coop-redraw-%d-%c.bmp", dumps, which == 0 ? 'a' : 'b');
                SDL_SaveBMP(surface, name);
                SDL_FreeSurface(surface);
                debug_printf("COOP HOST:   saved as %s\n", name);
            }
        }
    }
    return pixels;
}

// Testing: player `slot` + 1's picture from their last one against a full
// redraw.
static void coop_host_check_picture(int slot)
{
    char what[64];
    snprintf(what, sizeof(what), "player %d's picture from the last one", slot + 1);
    int pixels = coop_host_compare_full_redraw(what, &coop_host_picture_mismatches_logged, true);
    static unsigned int cursorsLogged = 0;
    if (pixels > 50 && cursorsLogged++ < 20) {
        // What the cursor was then and is now (the picture's own record
        // is still the last one's).
        CoopPicture* picture = &(coop_host_pictures[slot]);
        Rect mouseRect;
        Rect flatRect;
        obj_bound(obj_mouse, &mouseRect);
        obj_bound(obj_mouse_flat, &flatRect);
        debug_printf("COOP HOST:   cursor then: %d rects %d,%d-%d,%d %d,%d-%d,%d; now: %s %d,%d-%d,%d %d,%d-%d,%d\n", picture->cursorCount,
            picture->cursor[0].ulx, picture->cursor[0].uly, picture->cursor[0].lrx, picture->cursor[0].lry,
            picture->cursor[1].ulx, picture->cursor[1].uly, picture->cursor[1].lrx, picture->cursor[1].lry,
            gmouse_3d_is_on() ? "shown" : "hidden", mouseRect.ulx, mouseRect.uly, mouseRect.lrx, mouseRect.lry,
            flatRect.ulx, flatRect.uly, flatRect.lrx, flatRect.lry);
    }
    if (pixels != 0) {
        coop_host_picture_mismatches++;
        coop_host_picture_mismatches_total++;
        coop_host_picture_mismatch_most = std::max(coop_host_picture_mismatch_most, pixels);
    }
}

// Player `slot` + 1's map window as it is now (their view current), for
// their next picture.
static void coop_host_keep_picture(int slot, Object* player)
{
    CoopPicture* picture = &(coop_host_pictures[slot]);
    unsigned char* buffer = win_get_buf(display_win);
    if (!coop_host_view_changes_on() || buffer == NULL) {
        picture->valid = false;
        return;
    }

    picture->width = win_width(display_win);
    picture->height = win_height(display_win);
    picture->buffer = buffer;
    picture->pixels.assign(buffer, buffer + (size_t)picture->width * picture->height);
    picture->map = map_get_index_number();
    picture->elevation = map_elevation;
    tile_coord(COOP_HOST_REFERENCE_TILE, &(picture->referenceX), &(picture->referenceY), map_elevation);
    picture->tile = player->tile;
    picture->roof = coop_host_roof_square(player);
    obj_bound(obj_egg, &(picture->egg));
    picture->cursorCount = 0;
    if (gmouse_3d_is_on()) {
        obj_bound(obj_mouse, &(picture->cursor[0]));
        obj_bound(obj_mouse_flat, &(picture->cursor[1]));
        picture->cursorCount = 2;
    }
    picture->bounds = coop_host_map_bounds();
    picture->changes.clear();
    picture->changedAll = false;
    picture->valid = true;
}

// Testing: the camera's tile in each player's last part of a frame, and
// how many parts they had.
static int coop_host_view_centers[COOP_MAX_PLAYERS];
static unsigned int coop_host_view_part_count[COOP_MAX_PLAYERS];
// ... and how many of those parts ended with their cursor showing what's
// under it.
static unsigned int coop_host_view_hover_count[COOP_MAX_PLAYERS];

void coop_host_init()
{
    if (!coop_is_enabled() || coop_net_mode() != COOP_NET_HOST) {
        return;
    }

    if (!coop_net_host_start()) {
        return;
    }
    coop_net_cpu_mark();

    input_poll_hook = coop_host_poll;
    svga_present_hook = coop_host_present;
    gsound_event_hook = coop_net_send_sound;
    tile_refresh_observer = coop_host_note_refresh;
    for (int slot = 0; slot < COOP_MAX_PLAYERS; slot++) {
        coop_host_pictures[slot].valid = false;
    }
    coop_host_on = true;

    // Found by LAN searches; the menu may restart this with a public listing.
    coop_lobby_host_start("", false);
}

void coop_host_exit()
{
    if (!coop_host_on) {
        return;
    }

    coop_net_cpu_report("host");
    if (coop_host_restores_total + coop_host_redraws_total > 0) {
        debug_printf("\nCOOP HOST: player 1's view restored %u times, redrawn %u times", coop_host_restores_total, coop_host_redraws_total);
        if (coop_host_restore_check_on) {
            debug_printf("; %u restores differed from a full redraw (%d pixels at most)", coop_host_restore_mismatches_total, coop_host_restore_mismatch_most);
        }
        debug_printf("\n");
    }
    if (coop_host_pictures_whole_total + coop_host_pictures_changed_total > 0) {
        debug_printf("\nCOOP HOST: the other players' pictures drawn whole %u times, from the last one %u times", coop_host_pictures_whole_total, coop_host_pictures_changed_total);
        if (coop_host_view_check_on) {
            debug_printf("; %u of these differed from a full redraw (%d pixels at most)", coop_host_picture_mismatches_total, coop_host_picture_mismatch_most);
        }
        debug_printf("\n");
    }
    tile_refresh_observer = NULL;
    for (int slot = 0; slot < COOP_MAX_PLAYERS; slot++) {
        coop_host_pictures[slot].valid = false;
        coop_host_pictures[slot].pixels.clear();
        coop_host_pictures[slot].pixels.shrink_to_fit();
        coop_host_pictures[slot].changes.clear();
    }
    coop_lobby_host_stop();
    input_poll_hook = NULL;
    svga_present_hook = NULL;
    gsound_event_hook = NULL;
    inject_set_exclusive(false);
    coop_net_exit();
    coop_host_on = false;
}

bool coop_host_in_other_part()
{
    return coop_host_on && coop_host_turn_slot != 0;
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
        coop_host_feed_input(focus);
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
// Each player's mouse buttons as last fed.
static int coop_host_fed_buttons[COOP_MAX_PLAYERS];

// Feeds player `slot` + 1's input; whether any of it can do something (a
// key, a mouse button pressed or let go) rather than only move the mouse.
static bool coop_host_feed_input(int slot)
{
    bool acts = false;
    CoopNetInput input;
    while (coop_net_next_input(&input)) {
        if (input.kind == COOP_NET_INPUT_MOUSE) {
            if (input.value != coop_host_fed_buttons[slot]) {
                acts = true;
                coop_host_fed_buttons[slot] = input.value;
            }
            inject_mouse(input.x, input.y, input.value);
        } else if (input.kind == COOP_NET_INPUT_KEY) {
            acts = true;
            // Quitting is player 1's (the client uses these keys to leave).
            if (input.value != KEY_F10 && input.value != KEY_CTRL_Q && input.value != KEY_CTRL_X) {
                inject_key(input.value);
            }
        }
    }
    return acts;
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

// The camera of each player's last picture of their own view (-1: none
// yet, or a shared picture since).
static int coop_host_sent_center[COOP_MAX_PLAYERS] = { -1, -1, -1, -1 };
static int coop_host_sent_elevation[COOP_MAX_PLAYERS];

// Player `slot` + 1's view is about to be sent: how far the map moved
// since their last picture (coop_net_set_frame_shift), from the camera's
// two centers on the screen.
static void coop_host_set_frame_shift(int slot)
{
    int last = coop_host_sent_center[slot];
    if (last != -1 && last != tile_center_tile && coop_host_sent_elevation[slot] == map_elevation) {
        int lastX;
        int lastY;
        int x;
        int y;
        if (tile_coord(last, &lastX, &lastY, map_elevation) == 0 && tile_coord(tile_center_tile, &x, &y, map_elevation) == 0) {
            coop_net_set_frame_shift(lastX - x, lastY - y, 0, 0, screenGetWidth(), screenGetHeight() - INTERFACE_BAR_HEIGHT);
        }
    }
    coop_host_sent_center[slot] = tile_center_tile;
    coop_host_sent_elevation[slot] = map_elevation;
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
            // Not a view of theirs: the next one moves nothing.
            coop_host_sent_center[slot] = -1;
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
    view->cursorOn = gmouse_3d_is_on();
    gmouse_3d_save_hover(&(view->hover));
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
        view->cursorOn = true;
    }

    // The camera follows its player when they move, like player 1's does,
    // as far as the map's scroll limits let it. They count from the player
    // the view is for: tile_set_center() measures from obj_dude, still
    // player 1 here, and refused to follow a player some 15 hexes or more
    // from player 1, who then walked off their own screen.
    bool centered = false;
    if (incoming->tile != view->followedTile) {
        view->followedTile = incoming->tile;
        Object* dude = obj_dude;
        obj_dude = incoming;
        if (tile_set_center(incoming->tile, 0) == 0) {
            view->cameraTile = incoming->tile;
            centered = true;
        }
        obj_dude = dude;
    }

    // Otherwise exactly where it was: the limits measure how far a camera
    // moves away from the player, from where it is now - another player's
    // view - and refused to put a player's own camera back, so they saw
    // someone else's part of the map.
    if (!centered) {
        tile_set_center(view->cameraTile, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
    }
    mouse_set_context(&(view->mouse));
    gmouse_3d_set_mode(view->mode);
    gmouse_set_cursor(view->cursor);

    // Changing the cursor shape keeps its hotspot in place by moving the
    // mouse by the difference of the two hotspots, here the other player's
    // and this one's: twice a frame that slid player 1's mouse away (to the
    // top left with the hex cursor). Put the position back as it was saved.
    mouse_set_context(&(view->mouse));

    // The 3D cursor shows while this player's mouse is on the map: with
    // theirs over the interface bar it showed where the other player
    // pointed.
    if (view->cursorOn != gmouse_3d_is_on()) {
        if (view->cursorOn) {
            gmouse_3d_on();
        } else {
            gmouse_3d_off();
        }
    }

    // What this player's cursor showed, and how long their mouse rested.
    gmouse_3d_load_hover(&(view->hover));

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
        unsigned int inputStart = get_time();
        bool acts = coop_host_feed_input(slot);

        // The map still shows the previous view; a screen the input opens
        // over part of it (the skilldex) must have this player's map and
        // HUD behind it. (Not for a mouse that only moved: it opens nothing,
        // and this full redraw came on top of the one for their picture
        // nearly every frame.)
        if (acts && inject_pending()) {
            coop_host_own_draw = true;
            svga_screen_only = true;
            intface_update_items(false);
            intface_redraw();
            tile_refresh_display();
            svga_screen_only = false;
            coop_host_own_draw = false;
        }

        while (inject_pending() && coop_player_count() > 1) {
            int keyCode = get_injected_input();
            if (keyCode != -1) {
                game_handle_input(keyCode, false);
            }
        }

        // Hover: 3D cursor for player 2's mouse.
        gmouse_bk_process();
        coop_host_input_ms += elapsed_time(inputStart);

        coop_host_update_status();

        // Their view, when their client takes a frame now: online, frames
        // wait for acknowledgements, and a view nobody gets would cost a
        // full redraw for nothing (player 1's view is redrawn at the end of
        // the frame anyway).
        if (coop_net_can_send_frame()) {
            unsigned int drawStart = get_time();
            // Not shown on this screen: player 1's view comes back after
            // the parts.
            svga_screen_only = true;
            coop_host_own_draw = true;
            if (coop_host_draw_picture(slot, player2->obj)) {
                coop_host_pictures_changed++;
                coop_host_pictures_changed_total++;
                if (coop_host_view_check_on) {
                    coop_host_check_picture(slot);
                }
            } else {
                tile_refresh_display();
                coop_host_pictures_whole++;
                coop_host_pictures_whole_total++;
            }
            coop_host_keep_picture(slot, player2->obj);
            coop_host_own_draw = false;
            intface_update_items(false);
            intface_redraw();
            svga_screen_only = false;
            coop_host_draw_ms += elapsed_time(drawStart);
            coop_host_marker_draw();
            unsigned int sendStart = get_time();
            coop_host_set_frame_shift(slot);
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

unsigned int coop_host_view_hover_parts(int slot)
{
    return slot >= 0 && slot < COOP_MAX_PLAYERS ? coop_host_view_hover_count[slot] : 0;
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

        // Player 1's map window as it is, and whether the parts change it.
        unsigned char* mapBuffer = win_get_buf(display_win);
        size_t mapSize = mapBuffer != NULL ? (size_t)win_width(display_win) * win_height(display_win) : 0;
        int mapCenter = tile_center_tile;
        int mapIndex = map_get_index_number();
        int mapElevation = map_elevation;
        bool restore = coop_host_restore_setting() != 0 && mapBuffer != NULL;
        if (restore) {
            coop_host_restore_saved.assign(mapBuffer, mapBuffer + mapSize);
            if (coop_host_restore_check_on) {
                coop_host_restore_state = coop_host_view_state();
            }
        }
        coop_host_restore_changed = false;
        coop_host_in_parts = true;

        mouse_hide();
        coop_host_view_save(&player1View, player1);
        const CoopView* previous = &player1View;
        for (int index = 0; index < turnCount && index < coop_player_count(); index++) {
            int slot = turns[index];
            if (slot >= coop_player_count()) {
                continue;
            }
            Object* player = coop_player(slot)->obj;
            unsigned int switchStart = get_time();
            coop_host_view_load(&(coop_host_views[slot]), player, previous);
            coop_host_switch_ms += elapsed_time(switchStart);
            mouse_show();
            coop_net_select(slot);
            coop_host_player_turn(slot);
            coop_host_send_character_copy(slot, copyRequested);
            mouse_hide();
            coop_host_view_save(&(coop_host_views[slot]), player);
            if (coop_host_views[slot].hover.hoverTest) {
                coop_host_view_hover_count[slot]++;
            }
            previous = &(coop_host_views[slot]);
        }
        unsigned int restoreStart = get_time();
        coop_host_view_load(&player1View, player1, previous);
        coop_host_in_parts = false;
        mouse_show();

        restore = restore
            && !coop_host_restore_changed
            && win_get_buf(display_win) == mapBuffer
            && (size_t)win_width(display_win) * win_height(display_win) == mapSize
            && map_get_index_number() == mapIndex
            && map_elevation == mapElevation
            && tile_center_tile == mapCenter;
        if (restore) {
            // As it was; the cursor back where player 1's mouse is.
            memcpy(mapBuffer, coop_host_restore_saved.data(), mapSize);
            win_draw(display_win);
            gmouse_bk_process();
            intface_update_items(false);
            intface_redraw();
            coop_host_restores++;
            coop_host_restores_total++;
        } else {
            gmouse_bk_process();
            intface_update_items(false);
            coop_host_own_draw = true;
            tile_refresh_display();
            coop_host_own_draw = false;
            intface_redraw();
            coop_host_redraws++;
            coop_host_redraws_total++;
        }
        coop_host_restore_ms += elapsed_time(restoreStart);

        if (restore && coop_host_restore_check_on) {
            // The same as a full redraw? (Not where player 1's 3D cursor is:
            // at the map's edge the cursor's own redraw draws it, a full one
            // doesn't, as for the others' pictures.)
            int pixels = coop_host_compare_full_redraw("restored view", &coop_host_restore_mismatches_logged, true);
            static unsigned int statesLogged = 0;
            if (pixels > 50 && (statesLogged++ < 20 || coop_host_compare_record)) {
                coop_host_log_long("frame before: ", coop_host_end_state);
                coop_host_log_long("when taken:   ", coop_host_restore_state);
                coop_host_log_long("now:          ", coop_host_view_state());
            }
            if (pixels != 0) {
                coop_host_restore_mismatches++;
                coop_host_restore_mismatches_total++;
                coop_host_restore_mismatch_most = std::max(coop_host_restore_mismatch_most, pixels);
            }
        }
        if (coop_host_restore_check_on) {
            coop_host_end_state = coop_host_view_state();
        }
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
            debug_printf("\nCOOP HOST: %u frames in %u ms, %u ms of them for the other players' parts (drawing %u, sending %u, input %u, switching %u, player 1's view again %u: restored %u times, redrawn %u times; %u views not needed)\n",
                statsFrames, elapsed_time(statsSince), statsBusy, coop_host_draw_ms, coop_host_send_ms, coop_host_input_ms, coop_host_switch_ms, coop_host_restore_ms,
                coop_host_restores, coop_host_redraws, coop_host_views_skipped);
            debug_printf("COOP HOST: the other players' pictures drawn whole %u times, from the last one %u times\n", coop_host_pictures_whole, coop_host_pictures_changed);
            if (coop_host_restore_check_on) {
                debug_printf("COOP HOST: %u restored views differed from a full redraw\n", coop_host_restore_mismatches);
            }
            if (coop_host_view_check_on) {
                debug_printf("COOP HOST: %u pictures from the last one differed from a full redraw\n", coop_host_picture_mismatches);
            }
        }
        statsSince = get_time();
        statsFrames = 0;
        statsBusy = 0;
        coop_host_draw_ms = 0;
        coop_host_send_ms = 0;
        coop_host_input_ms = 0;
        coop_host_switch_ms = 0;
        coop_host_restore_ms = 0;
        coop_host_restores = 0;
        coop_host_redraws = 0;
        coop_host_restore_mismatches = 0;
        coop_host_pictures_whole = 0;
        coop_host_pictures_changed = 0;
        coop_host_picture_mismatches = 0;
        coop_host_views_skipped = 0;
    }

    coop_host_free_roam_present = true;
}

} // namespace fallout
