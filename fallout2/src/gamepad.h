#ifndef FALLOUT_GAMEPAD_H_
#define FALLOUT_GAMEPAD_H_

#include <SDL.h>

namespace fallout {

// CE: game controller support (Xbox and PlayStation pads, Steam Deck):
//
//   left stick     mouse cursor          A / B          left / right click
//   X              inventory (I)         Y              Pip-Boy (P)
//   View/Back      character (C)         Menu/Start     Escape
//   LB             skilldex (S)          RB             switch hand (B)
//   LT             attack mode (A)       RT             end turn (Space)
//   D-pad, right stick    scroll the map (arrow keys)
//   L3             Enter                 R3             co-op marker (G)
//
// The keys go through the keyboard code, the cursor and clicks through the
// mouse code, so the game sees an ordinary mouse and keyboard.

typedef void(GamepadKeyProc)(int scancode, bool down);

void gamepad_init();
void gamepad_exit();

// Handles a controller event from the event loop; keys are passed to
// `sendKey`. False for events that aren't the controller's.
bool gamepad_handle_event(const SDL_Event* event, GamepadKeyProc* sendKey);

// Cursor movement (pixels) since the last call, and the mouse buttons the
// controller holds (MOUSE_STATE_*).
void gamepad_mouse(int* dx, int* dy, int* buttons);

} // namespace fallout

#endif /* FALLOUT_GAMEPAD_H_ */
