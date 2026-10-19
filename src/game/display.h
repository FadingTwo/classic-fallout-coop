#ifndef FALLOUT_GAME_DISPLAY_H_
#define FALLOUT_GAME_DISPLAY_H_

namespace fallout {

int display_init();
int display_reset();
void display_exit();
void display_print(char* string);
// Co-op: a message for one player's monitor (index) alone; and all
// messages to it alone until called with -1.
void display_print_private(int player, char* string);
void display_set_private(int player);
// Testing: the newest line on a player's monitor.
const char* display_last_line(int player);
void display_clear();
void display_redraw();
void display_scroll_up(int btn, int keyCode);
void display_scroll_down(int btn, int keyCode);
void display_arrow_up(int btn, int keyCode);
void display_arrow_down(int btn, int keyCode);
void display_arrow_restore(int btn, int keyCode);
void display_disable();
void display_enable();

} // namespace fallout

#endif /* FALLOUT_GAME_DISPLAY_H_ */
