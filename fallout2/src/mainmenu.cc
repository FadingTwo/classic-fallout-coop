#include "mainmenu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "art.h"
#include "character_editor.h"
#include "color.h"
#include "draw.h"
#include "game.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "mouse.h"
#include "palette.h"
#include "preferences.h"
#include "sfall_config.h"
#include "svga.h"
#include "text_font.h"
#include "version.h"
#include "window_manager.h"

namespace fallout {

#define MAIN_MENU_WINDOW_WIDTH 640
#define MAIN_MENU_WINDOW_HEIGHT 480

typedef enum MainMenuButton {
    MAIN_MENU_BUTTON_INTRO,
    MAIN_MENU_BUTTON_NEW_GAME,
    MAIN_MENU_BUTTON_LOAD_GAME,
    // CE: co-op.
    MAIN_MENU_BUTTON_MULTIPLAYER,
    MAIN_MENU_BUTTON_OPTIONS,
    MAIN_MENU_BUTTON_CREDITS,
    MAIN_MENU_BUTTON_EXIT,
    MAIN_MENU_BUTTON_COUNT,
} MainMenuButton;

static int main_menu_fatal_error();
static void main_menu_play_sound(const char* fileName);
static void main_menu_draw_buttons(const char* const* labels, const int* keys, int count);
static void main_menu_add_plate();

// CE: the panel has a plate per main menu button (a seventh one is added
// for MULTIPLAYER), reused by the co-op sub-menus.
#define MAIN_MENU_PANEL_SLOTS MAIN_MENU_BUTTON_COUNT

// misc.msg labels of the buttons (-1: MULTIPLAYER, not in misc.msg).
static const int gMainMenuLabelIds[MAIN_MENU_BUTTON_COUNT] = { 9, 10, 11, -1, 12, 13, 14 };

static char gMainMenuLabels[MAIN_MENU_BUTTON_COUNT][64];

// The window without buttons and labels, and the sfall offsets.
static unsigned char* gMainMenuWindowClean = nullptr;
static int gMainMenuOffsetX = 0;
static int gMainMenuOffsetY = 0;
static int gMainMenuFontColor = 0;

// 0x5194F0
static int gMainMenuWindow = -1;

// 0x5194F4
static unsigned char* gMainMenuWindowBuffer = nullptr;

// 0x519504
static bool _in_main_menu = false;

// 0x519508
static bool gMainMenuWindowInitialized = false;

// 0x51950C
static unsigned int gMainMenuScreensaverDelay = 120000;

// 0x519510
static const int gMainMenuButtonKeyBindings[MAIN_MENU_BUTTON_COUNT] = {
    KEY_LOWERCASE_I, // intro
    KEY_LOWERCASE_N, // new game
    KEY_LOWERCASE_L, // load game
    KEY_LOWERCASE_M, // multiplayer (CE)
    KEY_LOWERCASE_O, // options
    KEY_LOWERCASE_C, // credits
    KEY_LOWERCASE_E, // exit
};

// 0x519528
static const int _return_values[MAIN_MENU_BUTTON_COUNT] = {
    MAIN_MENU_INTRO,
    MAIN_MENU_NEW_GAME,
    MAIN_MENU_LOAD_GAME,
    MAIN_MENU_MULTIPLAYER,
    MAIN_MENU_OPTIONS,
    MAIN_MENU_CREDITS,
    MAIN_MENU_EXIT,
};

// 0x614840
static int gMainMenuButtons[MAIN_MENU_BUTTON_COUNT];

// 0x614858
static bool gMainMenuWindowHidden;

static FrmImage _mainMenuBackgroundFrmImage;
static FrmImage _mainMenuButtonNormalFrmImage;
static FrmImage _mainMenuButtonPressedFrmImage;

// 0x481650
int mainMenuWindowInit()
{
    int fid;
    MessageListItem msg;
    int len;

    if (gMainMenuWindowInitialized) {
        return 0;
    }

    colorPaletteLoad("color.pal");

    int mainMenuWindowX = (screenGetWidth() - MAIN_MENU_WINDOW_WIDTH) / 2;
    int mainMenuWindowY = (screenGetHeight() - MAIN_MENU_WINDOW_HEIGHT) / 2;
    gMainMenuWindow = windowCreate(mainMenuWindowX,
        mainMenuWindowY,
        MAIN_MENU_WINDOW_WIDTH,
        MAIN_MENU_WINDOW_HEIGHT,
        0,
        WINDOW_HIDDEN | WINDOW_MOVE_ON_TOP);
    if (gMainMenuWindow == -1) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    gMainMenuWindowBuffer = windowGetBuffer(gMainMenuWindow);

    // mainmenu.frm
    int backgroundFid = buildFid(OBJ_TYPE_INTERFACE, 140, 0, 0, 0);
    if (!_mainMenuBackgroundFrmImage.lock(backgroundFid)) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    blitBufferToBuffer(_mainMenuBackgroundFrmImage.getData(), 640, 480, 640, gMainMenuWindowBuffer, 640);
    _mainMenuBackgroundFrmImage.unlock();

    int oldFont = fontGetCurrent();
    fontSetCurrent(100);

    // SFALL: Allow to change font color/flags of copyright/version text
    //        It's the last byte ('3C' by default) that picks the colour used. The first byte supplies additional flags for this option
    //        0x010000 - change the color for version string only
    //        0x020000 - underline text (only for the version string)
    //        0x040000 - monospace font (only for the version string)
    int fontSettings = _colorTable[21091], fontSettingsSFall = 0;
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_MAIN_MENU_FONT_COLOR_KEY, &fontSettingsSFall);
    if (fontSettingsSFall && !(fontSettingsSFall & 0x010000))
        fontSettings = fontSettingsSFall & 0xFF;

    // SFALL: Allow to move copyright text
    int offsetX = 0, offsetY = 0;
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_MAIN_MENU_CREDITS_OFFSET_X_KEY, &offsetX);
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_MAIN_MENU_CREDITS_OFFSET_Y_KEY, &offsetY);

    // Copyright.
    msg.num = 20;
    if (messageListGetItem(&gMiscMessageList, &msg)) {
        windowDrawText(gMainMenuWindow, msg.text, 0, offsetX + 15, offsetY + 460, fontSettings | 0x06000000);
    }

    // SFALL: Make sure font settings are applied when using 0x010000 flag
    if (fontSettingsSFall)
        fontSettings = fontSettingsSFall;

    // TODO: Allow to move version text
    // Version.
    char version[VERSION_MAX];
    versionGetVersion(version, sizeof(version));
    len = fontGetStringWidth(version);
    windowDrawText(gMainMenuWindow, version, 0, 615 - len, 460, fontSettings | 0x06000000);

    // menuup.frm
    fid = buildFid(OBJ_TYPE_INTERFACE, 299, 0, 0, 0);
    if (!_mainMenuButtonNormalFrmImage.lock(fid)) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    // menudown.frm
    fid = buildFid(OBJ_TYPE_INTERFACE, 300, 0, 0, 0);
    if (!_mainMenuButtonPressedFrmImage.lock(fid)) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        gMainMenuButtons[index] = -1;
    }

    // SFALL: Allow to move menu buttons
    gMainMenuOffsetX = 0;
    gMainMenuOffsetY = 0;
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_MAIN_MENU_OFFSET_X_KEY, &gMainMenuOffsetX);
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_MAIN_MENU_OFFSET_Y_KEY, &gMainMenuOffsetY);

    // SFALL: Allow to change font color of buttons
    gMainMenuFontColor = _colorTable[21091];
    fontSettingsSFall = 0;
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_MAIN_MENU_BIG_FONT_COLOR_KEY, &fontSettingsSFall);
    if (fontSettingsSFall)
        gMainMenuFontColor = fontSettingsSFall & 0xFF;

    // CE: a seventh plate for MULTIPLAYER; the window without buttons and
    // labels is kept for the co-op sub-menus.
    main_menu_add_plate();

    gMainMenuWindowClean = (unsigned char*)malloc(MAIN_MENU_WINDOW_WIDTH * MAIN_MENU_WINDOW_HEIGHT);
    if (gMainMenuWindowClean == nullptr) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }
    memcpy(gMainMenuWindowClean, gMainMenuWindowBuffer, MAIN_MENU_WINDOW_WIDTH * MAIN_MENU_WINDOW_HEIGHT);

    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        gMainMenuLabels[index][0] = '\0';
        if (gMainMenuLabelIds[index] == -1) {
            strcpy(gMainMenuLabels[index], "MULTIPLAYER");
        } else {
            msg.num = gMainMenuLabelIds[index];
            if (messageListGetItem(&gMiscMessageList, &msg)) {
                snprintf(gMainMenuLabels[index], sizeof(gMainMenuLabels[index]), "%s", msg.text);
            }
        }
    }

    main_menu_restore();
    if (gMainMenuButtons[0] == -1) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    fontSetCurrent(oldFont);

    gMainMenuWindowInitialized = true;
    gMainMenuWindowHidden = true;

    return 0;
}

// 0x481968
void mainMenuWindowFree()
{
    if (!gMainMenuWindowInitialized) {
        return;
    }

    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        // FIXME: Why it tries to free only invalid buttons?
        if (gMainMenuButtons[index] == -1) {
            buttonDestroy(gMainMenuButtons[index]);
        }
    }

    if (gMainMenuWindowClean != nullptr) {
        free(gMainMenuWindowClean);
        gMainMenuWindowClean = nullptr;
    }

    _mainMenuButtonPressedFrmImage.unlock();
    _mainMenuButtonNormalFrmImage.unlock();

    if (gMainMenuWindow != -1) {
        windowDestroy(gMainMenuWindow);
    }

    gMainMenuWindowInitialized = false;
}

// 0x481A00
void mainMenuWindowHide(bool animate)
{
    if (!gMainMenuWindowInitialized) {
        return;
    }

    if (gMainMenuWindowHidden) {
        return;
    }

    soundContinueAll();

    if (animate) {
        paletteFadeTo(gPaletteBlack);
        soundContinueAll();
    }

    windowHide(gMainMenuWindow);

    gMainMenuWindowHidden = true;
}

// 0x481A48
void mainMenuWindowUnhide(bool animate)
{
    if (!gMainMenuWindowInitialized) {
        return;
    }

    if (!gMainMenuWindowHidden) {
        return;
    }

    windowShow(gMainMenuWindow);

    if (animate) {
        colorPaletteLoad("color.pal");
        paletteFadeTo(_cmap);
    }

    gMainMenuWindowHidden = false;
}

// 0x481AA8
int _main_menu_is_enabled()
{
    return 1;
}

// 0x481AEC
int mainMenuWindowHandleEvents()
{
    _in_main_menu = true;

    bool oldCursorIsHidden = cursorIsHidden();
    if (oldCursorIsHidden) {
        mouseShowCursor();
    }

    unsigned int tick = getTicks();

    int rc = -1;
    while (rc == -1) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();

        for (int buttonIndex = 0; buttonIndex < MAIN_MENU_BUTTON_COUNT; buttonIndex++) {
            if (keyCode == gMainMenuButtonKeyBindings[buttonIndex] || keyCode == toupper(gMainMenuButtonKeyBindings[buttonIndex])) {
                // NOTE: Uninline.
                main_menu_play_sound("nmselec1");

                rc = _return_values[buttonIndex];

                if (buttonIndex == MAIN_MENU_BUTTON_CREDITS && (gPressedPhysicalKeys[SDL_SCANCODE_RSHIFT] != KEY_STATE_UP || gPressedPhysicalKeys[SDL_SCANCODE_LSHIFT] != KEY_STATE_UP)) {
                    rc = MAIN_MENU_QUOTES;
                }

                break;
            }
        }

        if (rc == -1) {
            if (keyCode == KEY_CTRL_R) {
                rc = MAIN_MENU_SELFRUN;
                continue;
            } else if (keyCode == KEY_PLUS || keyCode == KEY_EQUAL) {
                brightnessIncrease();
            } else if (keyCode == KEY_MINUS || keyCode == KEY_UNDERSCORE) {
                brightnessDecrease();
            } else if (keyCode == KEY_UPPERCASE_D || keyCode == KEY_LOWERCASE_D) {
                rc = MAIN_MENU_SCREENSAVER;
                continue;
            } else if (keyCode == 1111) {
                if (!(mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_REPEAT)) {
                    // NOTE: Uninline.
                    main_menu_play_sound("nmselec0");
                }
                continue;
            }
        }

        if (keyCode == KEY_ESCAPE || _game_user_wants_to_quit == 3) {
            rc = MAIN_MENU_EXIT;

            // NOTE: Uninline.
            main_menu_play_sound("nmselec1");
            break;
        } else if (_game_user_wants_to_quit == 2) {
            _game_user_wants_to_quit = 0;
        } else {
            if (getTicksSince(tick) >= gMainMenuScreensaverDelay) {
                rc = MAIN_MENU_TIMEOUT;
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (oldCursorIsHidden) {
        mouseHideCursor();
    }

    _in_main_menu = false;

    return rc;
}

// CE: copies the last plate one slot down (the background has six).
static void main_menu_add_plate()
{
    const int pitch = 41;
    const int left = 14;
    const int right = 210;
    const int top = 217 + gMainMenuOffsetY;

    for (int y = 0; y < pitch; y++) {
        int from = top + y;
        int to = top + pitch + y;
        if (to >= MAIN_MENU_WINDOW_HEIGHT || left + gMainMenuOffsetX < 0 || right + gMainMenuOffsetX > MAIN_MENU_WINDOW_WIDTH) {
            return;
        }
        memcpy(gMainMenuWindowBuffer + to * MAIN_MENU_WINDOW_WIDTH + left + gMainMenuOffsetX,
            gMainMenuWindowBuffer + from * MAIN_MENU_WINDOW_WIDTH + left + gMainMenuOffsetX,
            right - left);
    }
}

static void main_menu_draw_buttons(const char* const* labels, const int* keys, int count)
{
    for (int index = 0; index < MAIN_MENU_PANEL_SLOTS; index++) {
        if (gMainMenuButtons[index] != -1) {
            buttonDestroy(gMainMenuButtons[index]);
            gMainMenuButtons[index] = -1;
        }
    }

    memcpy(gMainMenuWindowBuffer, gMainMenuWindowClean, MAIN_MENU_WINDOW_WIDTH * MAIN_MENU_WINDOW_HEIGHT);

    if (count > MAIN_MENU_PANEL_SLOTS) {
        count = MAIN_MENU_PANEL_SLOTS;
    }

    for (int index = 0; index < count; index++) {
        gMainMenuButtons[index] = buttonCreate(gMainMenuWindow,
            gMainMenuOffsetX + 30,
            gMainMenuOffsetY + 19 + index * 42 - index,
            26,
            26,
            -1,
            -1,
            1111,
            keys[index],
            _mainMenuButtonNormalFrmImage.getData(),
            _mainMenuButtonPressedFrmImage.getData(),
            nullptr,
            BUTTON_FLAG_TRANSPARENT);
        if (gMainMenuButtons[index] != -1) {
            buttonSetMask(gMainMenuButtons[index], _mainMenuButtonNormalFrmImage.getData());
        }
    }

    int oldFont = fontGetCurrent();
    fontSetCurrent(104);

    for (int index = 0; index < count; index++) {
        // Centered on the plate; a long label (MULTIPLAYER) starts after
        // the button instead.
        int len = fontGetStringWidth(labels[index]);
        int x = std::max(126 - len / 2, 60);
        fontDrawText(gMainMenuWindowBuffer + gMainMenuOffsetX + 640 * (gMainMenuOffsetY + 42 * index - index + 20) + x,
            labels[index],
            640 - x - 1,
            640,
            gMainMenuFontColor);
    }

    fontSetCurrent(oldFont);

    windowRefresh(gMainMenuWindow);
}

void main_menu_show_buttons(const char* const* labels, const int* keys, int count)
{
    main_menu_draw_buttons(labels, keys, count);
}

void main_menu_restore()
{
    const char* labels[MAIN_MENU_BUTTON_COUNT];
    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        labels[index] = gMainMenuLabels[index];
    }

    main_menu_draw_buttons(labels, gMainMenuButtonKeyBindings, MAIN_MENU_BUTTON_COUNT);
}

int main_menu_choose(const char* const* labels, const int* keys, int count)
{
    main_menu_draw_buttons(labels, keys, count);

    bool oldCursorIsHidden = cursorIsHidden();
    if (oldCursorIsHidden) {
        mouseShowCursor();
    }

    int rc = -2;
    while (rc == -2) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();

        for (int index = 0; index < count; index++) {
            if (keyCode == keys[index] || keyCode == toupper(keys[index])) {
                // NOTE: Uninline.
                main_menu_play_sound("nmselec1");
                rc = index;
                break;
            }
        }

        if (keyCode == 1111) {
            if (!(mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_REPEAT)) {
                // NOTE: Uninline.
                main_menu_play_sound("nmselec0");
            }
        } else if (keyCode == KEY_ESCAPE || _game_user_wants_to_quit != 0) {
            // NOTE: Uninline.
            main_menu_play_sound("nmselec1");
            rc = -1;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (oldCursorIsHidden) {
        mouseHideCursor();
    }

    return rc;
}

// The bottom plate of the panel, for text.
#define MAIN_MENU_TEXT_X 62
#define MAIN_MENU_TEXT_Y (19 + 6 * 41 + 9)
#define MAIN_MENU_TEXT_WIDTH 140

void main_menu_status(const char* text)
{
    int x0 = MAIN_MENU_TEXT_X + gMainMenuOffsetX;
    int y0 = MAIN_MENU_TEXT_Y + gMainMenuOffsetY;

    // Clean slot.
    for (int row = 0; row < 26; row++) {
        int offset = MAIN_MENU_WINDOW_WIDTH * (y0 - 4 + row) + x0;
        memcpy(gMainMenuWindowBuffer + offset, gMainMenuWindowClean + offset, MAIN_MENU_TEXT_WIDTH);
    }

    if (text[0] != '\0') {
        int oldFont = fontGetCurrent();
        fontSetCurrent(101);
        int len = fontGetStringWidth(text);
        int x = x0 + (len < MAIN_MENU_TEXT_WIDTH ? (MAIN_MENU_TEXT_WIDTH - len) / 2 : 0);
        fontDrawText(gMainMenuWindowBuffer + MAIN_MENU_WINDOW_WIDTH * y0 + x, text, MAIN_MENU_TEXT_WIDTH, MAIN_MENU_WINDOW_WIDTH, _colorTable[992]);
        fontSetCurrent(oldFont);
    }

    windowRefresh(gMainMenuWindow);
    renderPresent();
}

bool main_menu_input(const char* label, const char* hint, char* text, int maxLength)
{
    // Only the label: no buttons while typing.
    main_menu_draw_buttons(nullptr, nullptr, 0);

    int oldFont = fontGetCurrent();
    fontSetCurrent(104);
    int len = fontGetStringWidth(label);
    fontDrawText(gMainMenuWindowBuffer + gMainMenuOffsetX + MAIN_MENU_WINDOW_WIDTH * (gMainMenuOffsetY + 20) + 126 - (len / 2), label, MAIN_MENU_WINDOW_WIDTH - (126 - (len / 2)) - 1, MAIN_MENU_WINDOW_WIDTH, gMainMenuFontColor);

    fontSetCurrent(101);
    len = fontGetStringWidth(hint);
    fontDrawText(gMainMenuWindowBuffer + gMainMenuOffsetX + MAIN_MENU_WINDOW_WIDTH * (gMainMenuOffsetY + 19 + 2 * 41 + 9) + 112 - (len / 2), hint, MAIN_MENU_WINDOW_WIDTH - (112 - (len / 2)) - 1, MAIN_MENU_WINDOW_WIDTH, _colorTable[992]);

    // Black field across the second plate.
    int fieldX = gMainMenuOffsetX + 30;
    int fieldY = gMainMenuOffsetY + 19 + 41 + 9;
    bufferFill(gMainMenuWindowBuffer + MAIN_MENU_WINDOW_WIDTH * (fieldY - 3) + fieldX - 4, 172, fontGetLineHeight() + 6, MAIN_MENU_WINDOW_WIDTH, _colorTable[0]);
    windowRefresh(gMainMenuWindow);

    // Large enough cancel code that no key produces it.
    bool ok = _get_input_str(gMainMenuWindow, 0x7FFF0000, text, maxLength, fieldX, fieldY, _colorTable[992], _colorTable[0], 0) == 0;
    fontSetCurrent(oldFont);

    // Trailing blank the editor's input adds.
    size_t length = strlen(text);
    while (length > 0 && text[length - 1] == ' ') {
        text[--length] = '\0';
    }

    return ok;
}

// NOTE: Inlined.
//
// 0x481C88
static int main_menu_fatal_error()
{
    mainMenuWindowFree();

    return -1;
}

// NOTE: Inlined.
//
// 0x481C94
static void main_menu_play_sound(const char* fileName)
{
    soundPlayFile(fileName);
}

} // namespace fallout
