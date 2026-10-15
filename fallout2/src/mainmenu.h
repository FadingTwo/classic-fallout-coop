#ifndef FALLOUT_MAINMENU_H_
#define FALLOUT_MAINMENU_H_

namespace fallout {

typedef enum MainMenuOption {
    MAIN_MENU_INTRO,
    MAIN_MENU_NEW_GAME,
    MAIN_MENU_LOAD_GAME,
    MAIN_MENU_SCREENSAVER,
    MAIN_MENU_TIMEOUT,
    MAIN_MENU_CREDITS,
    MAIN_MENU_QUOTES,
    MAIN_MENU_EXIT,
    MAIN_MENU_SELFRUN,
    MAIN_MENU_OPTIONS,
    // CE: co-op.
    MAIN_MENU_MULTIPLAYER,
} MainMenuOption;

int mainMenuWindowInit();
void mainMenuWindowFree();
void mainMenuWindowHide(bool animate);
void mainMenuWindowUnhide(bool animate);
int _main_menu_is_enabled();
int mainMenuWindowHandleEvents();

// CE: sub-menus in the main menu's button panel (co-op). Shows `count` (at
// most 7) buttons labelled `labels`, with hot keys `keys`, and returns the
// index picked, or -1 for Escape. The main menu's own buttons come back
// with main_menu_restore().
int main_menu_choose(const char* const* labels, const int* keys, int count);

// CE: text entry in the panel, with `label` as its title. Returns false if
// cancelled.
bool main_menu_input(const char* label, const char* hint, char* text, int maxLength);

// CE: a line of text in the panel's bottom slot ("" clears it). Only for
// button sets that leave that slot free.
void main_menu_status(const char* text);

// CE: shows a button set without waiting for a choice.
void main_menu_show_buttons(const char* const* labels, const int* keys, int count);

void main_menu_restore();

} // namespace fallout

#endif /* FALLOUT_MAINMENU_H_ */
