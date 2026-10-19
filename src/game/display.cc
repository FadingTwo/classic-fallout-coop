#include "game/display.h"

#include <stdio.h>
#include <string.h>

#include "game/art.h"
#include "game/combat.h"
#include "game/coop.h"
#include "game/gmouse.h"
#include "game/gsound.h"
#include "game/intface.h"
#include "plib/color/color.h"
#include "plib/gnw/button.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/memory.h"
#include "plib/gnw/rect.h"
#include "plib/gnw/text.h"

namespace fallout {

// The maximum number of lines display monitor can hold. Once this value
// is reached earlier messages are thrown away.
#define DISPLAY_MONITOR_LINES_CAPACITY 100

// The maximum length of a string in display monitor (in characters).
#define DISPLAY_MONITOR_LINE_LENGTH 80

#define DISPLAY_MONITOR_X 23
#define DISPLAY_MONITOR_Y 24
#define DISPLAY_MONITOR_WIDTH 167
#define DISPLAY_MONITOR_HEIGHT 60

#define DISPLAY_MONITOR_HALF_HEIGHT (DISPLAY_MONITOR_HEIGHT / 2)

#define DISPLAY_MONITOR_FONT 101

#define DISPLAY_MONITOR_BEEP_DELAY 500U

// 0x504F0C
static bool disp_init = false;

// The rectangle that display monitor occupies in the main interface window.
//
// 0x504F10
static Rect disp_rect = {
    DISPLAY_MONITOR_X,
    DISPLAY_MONITOR_Y,
    DISPLAY_MONITOR_X + DISPLAY_MONITOR_WIDTH - 1,
    DISPLAY_MONITOR_Y + DISPLAY_MONITOR_HEIGHT - 1,
};

// 0x504F20
static int dn_bid = -1;

// 0x504F24
static int up_bid = -1;

// 0x56C38C
//
// Co-op: one monitor per player: everyone's messages go to all, a player's
// own (what they look at) only to theirs; each player's screen shows theirs.
static char disp_strs[COOP_MAX_PLAYERS][DISPLAY_MONITOR_LINES_CAPACITY][DISPLAY_MONITOR_LINE_LENGTH];

// 0x56E2CC
static unsigned char* disp_buf;

// 0x56E2D0
static int max_disp_ptr;

// 0x56E2D4
static bool display_enabled;

// 0x56E2D8
static int disp_currs[COOP_MAX_PLAYERS];

// 0x56E2DC
static int intface_full_wid;

// 0x56E2DE0
static int max_ptr;

// 0x56E2DE4
static int disp_starts[COOP_MAX_PLAYERS];

// Co-op: the monitor the code below works on, and the one a message goes
// to alone (-1: everyone's).
static int disp_ring = 0;
static int disp_private = -1;

#define disp_str (disp_strs[disp_ring])
#define disp_curr (disp_currs[disp_ring])
#define disp_start (disp_starts[disp_ring])

static void display_print_ring(char* str);

// 0x42BBE0
int display_init()
{
    if (!disp_init) {
        int oldFont = text_curr();
        text_font(DISPLAY_MONITOR_FONT);

        max_ptr = DISPLAY_MONITOR_LINES_CAPACITY;
        max_disp_ptr = DISPLAY_MONITOR_HEIGHT / text_height();
        for (int index = 0; index < COOP_MAX_PLAYERS; index++) {
            disp_starts[index] = 0;
            disp_currs[index] = 0;
        }
        text_font(oldFont);

        disp_buf = (unsigned char*)mem_malloc(DISPLAY_MONITOR_WIDTH * DISPLAY_MONITOR_HEIGHT);
        if (disp_buf == NULL) {
            return -1;
        }

        CacheEntry* backgroundFrmHandle;
        int backgroundFid = art_id(OBJ_TYPE_INTERFACE, 16, 0, 0, 0);
        Art* backgroundFrm = art_ptr_lock(backgroundFid, &backgroundFrmHandle);
        if (backgroundFrm == NULL) {
            mem_free(disp_buf);
            return -1;
        }

        unsigned char* backgroundFrmData = art_frame_data(backgroundFrm, 0, 0);
        intface_full_wid = art_frame_width(backgroundFrm, 0, 0);
        buf_to_buf(backgroundFrmData + intface_full_wid * DISPLAY_MONITOR_Y + DISPLAY_MONITOR_X,
            DISPLAY_MONITOR_WIDTH,
            DISPLAY_MONITOR_HEIGHT,
            intface_full_wid,
            disp_buf,
            DISPLAY_MONITOR_WIDTH);

        art_ptr_unlock(backgroundFrmHandle);

        up_bid = win_register_button(interfaceWindow,
            DISPLAY_MONITOR_X,
            DISPLAY_MONITOR_Y,
            DISPLAY_MONITOR_WIDTH,
            DISPLAY_MONITOR_HALF_HEIGHT,
            -1,
            -1,
            -1,
            -1,
            NULL,
            NULL,
            NULL,
            0);
        if (up_bid != -1) {
            win_register_button_func(up_bid,
                display_arrow_up,
                display_arrow_restore,
                display_scroll_up,
                NULL);
        }

        dn_bid = win_register_button(interfaceWindow,
            DISPLAY_MONITOR_X,
            DISPLAY_MONITOR_Y + DISPLAY_MONITOR_HALF_HEIGHT,
            DISPLAY_MONITOR_WIDTH,
            DISPLAY_MONITOR_HEIGHT - DISPLAY_MONITOR_HALF_HEIGHT,
            -1,
            -1,
            -1,
            -1,
            NULL,
            NULL,
            NULL,
            0);
        if (dn_bid != -1) {
            win_register_button_func(dn_bid,
                display_arrow_down,
                display_arrow_restore,
                display_scroll_down,
                NULL);
        }

        display_enabled = true;
        disp_init = true;

        // NOTE: Uninline.
        display_clear();
    }

    return 0;
}

// 0x42BDD0
int display_reset()
{
    // NOTE: Uninline.
    display_clear();

    return 0;
}

// 0x42BE1C
void display_exit()
{
    if (disp_init) {
        mem_free(disp_buf);
        disp_init = false;
    }
}

// 0x42BE3C
void display_print(char* str)
{
    // 0x56E2E8
    static unsigned int lastTime;

    if (!disp_init) {
        return;
    }

    if (!isInCombat()) {
        unsigned int now = get_bk_time();
        if (elapsed_tocks(now, lastTime) >= DISPLAY_MONITOR_BEEP_DELAY) {
            lastTime = now;
            gsound_play_sfx_file("monitor");
        }
    }

    // Co-op: to every player's monitor (a copy each: wrapping changes it),
    // or to one's alone.
    for (int index = 0; index < COOP_MAX_PLAYERS; index++) {
        if (disp_private != -1 && index != disp_private) {
            continue;
        }
        char copy[1024];
        snprintf(copy, sizeof(copy), "%s", str);
        disp_ring = index;
        display_print_ring(copy);
        disp_curr = disp_start;
    }

    display_redraw();
}

void display_print_private(int player, char* str)
{
    int oldPrivate = disp_private;
    disp_private = player >= 0 && player < COOP_MAX_PLAYERS ? player : -1;
    display_print(str);
    disp_private = oldPrivate;
}

void display_set_private(int player)
{
    disp_private = player >= 0 && player < COOP_MAX_PLAYERS ? player : -1;
}

const char* display_last_line(int player)
{
    if (!disp_init || player < 0 || player >= COOP_MAX_PLAYERS) {
        return "";
    }
    return disp_strs[player][(disp_starts[player] + max_ptr - 1) % max_ptr];
}

// One player's monitor (disp_ring) takes `str`.
static void display_print_ring(char* str)
{
    int oldFont = text_curr();
    text_font(DISPLAY_MONITOR_FONT);

    char knob = '\x95';

    char knobString[2];
    knobString[0] = knob;
    knobString[1] = '\0';
    int knobWidth = text_width(knobString);

    // TODO: Refactor these two loops.
    char* v1 = NULL;
    while (true) {
        while (text_width(str) < DISPLAY_MONITOR_WIDTH - max_disp_ptr - knobWidth) {
            char* temp = disp_str[disp_start];
            int length;
            if (knob != '\0') {
                *temp++ = knob;
                length = DISPLAY_MONITOR_LINE_LENGTH - 2;
                knob = '\0';
                knobWidth = 0;
            } else {
                length = DISPLAY_MONITOR_LINE_LENGTH - 1;
            }
            strncpy(temp, str, length);
            disp_str[disp_start][DISPLAY_MONITOR_LINE_LENGTH - 1] = '\0';
            disp_start = (disp_start + 1) % max_ptr;

            if (v1 == NULL) {
                text_font(oldFont);
                return;
            }

            str = v1 + 1;
            *v1 = ' ';
            v1 = NULL;
        }

        char* space = strrchr(str, ' ');
        if (space == NULL) {
            break;
        }

        if (v1 != NULL) {
            *v1 = ' ';
        }

        v1 = space;
        *space = '\0';
    }

    char* temp = disp_str[disp_start];
    int length;
    if (knob != '\0') {
        temp++;
        disp_str[disp_start][0] = knob;
        length = DISPLAY_MONITOR_LINE_LENGTH - 2;
        knob = '\0';
    } else {
        length = DISPLAY_MONITOR_LINE_LENGTH - 1;
    }
    strncpy(temp, str, length);

    disp_str[disp_start][DISPLAY_MONITOR_LINE_LENGTH - 1] = '\0';
    disp_start = (disp_start + 1) % max_ptr;

    text_font(oldFont);
}

// 0x42BFF4
void display_clear()
{
    int index;

    if (disp_init) {
        for (disp_ring = 0; disp_ring < COOP_MAX_PLAYERS; disp_ring++) {
            for (index = 0; index < max_ptr; index++) {
                disp_str[index][0] = '\0';
            }

            disp_start = 0;
            disp_curr = 0;
        }
        display_redraw();
    }
}

// 0x42C040
void display_redraw()
{
    if (!disp_init) {
        return;
    }

    unsigned char* buf = win_get_buf(interfaceWindow);
    if (buf == NULL) {
        return;
    }

    // Co-op: the monitor of the player whose screen this is.
    disp_ring = coop_viewer_index();

    buf += intface_full_wid * DISPLAY_MONITOR_Y + DISPLAY_MONITOR_X;
    buf_to_buf(disp_buf,
        DISPLAY_MONITOR_WIDTH,
        DISPLAY_MONITOR_HEIGHT,
        DISPLAY_MONITOR_WIDTH,
        buf,
        intface_full_wid);

    int oldFont = text_curr();
    text_font(DISPLAY_MONITOR_FONT);

    for (int index = 0; index < max_disp_ptr; index++) {
        int stringIndex = (disp_curr + max_ptr + index - max_disp_ptr) % max_ptr;
        text_to_buf(buf + index * intface_full_wid * text_height(), disp_str[stringIndex], DISPLAY_MONITOR_WIDTH, intface_full_wid, colorTable[992]);

        // Even though the display monitor is rectangular, it's graphic is not.
        // To give a feel of depth it's covered by some metal canopy and
        // considered inclined outwards. This way earlier messages appear a
        // little bit far from player's perspective. To implement this small
        // detail the destination buffer is incremented by 1.
        buf++;
    }

    win_draw_rect(interfaceWindow, &disp_rect);
    text_font(oldFont);
}

// 0x42C138
void display_scroll_up(int btn, int keyCode)
{
    disp_ring = coop_viewer_index();
    if ((max_ptr + disp_curr - 1) % max_ptr != disp_start) {
        disp_curr = (max_ptr + disp_curr - 1) % max_ptr;
        display_redraw();
    }
}

// 0x42C164
void display_scroll_down(int btn, int keyCode)
{
    disp_ring = coop_viewer_index();
    if (disp_curr != disp_start) {
        disp_curr = (disp_curr + 1) % max_ptr;
        display_redraw();
    }
}

// 0x42C190
void display_arrow_up(int btn, int keyCode)
{
    gmouse_set_cursor(MOUSE_CURSOR_SMALL_ARROW_UP);
}

// 0x42C19C
void display_arrow_down(int btn, int keyCode)
{
    gmouse_set_cursor(MOUSE_CURSOR_SMALL_ARROW_DOWN);
}

// 0x42C1A8
void display_arrow_restore(int btn, int keyCode)
{
    gmouse_set_cursor(MOUSE_CURSOR_ARROW);
}

// 0x42C1B4
void display_disable()
{
    if (display_enabled) {
        win_disable_button(dn_bid);
        win_disable_button(up_bid);
        display_enabled = false;
    }
}

// 0x42C1DC
void display_enable()
{
    if (!display_enabled) {
        win_enable_button(dn_bid);
        win_enable_button(up_bid);
        display_enabled = true;
    }
}

} // namespace fallout
