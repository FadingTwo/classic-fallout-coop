#include "gamepad.h"

#include <math.h>
#include <stdlib.h>

#include "debug.h"
#include "mouse.h"

namespace fallout {

// Stick values below this are noise.
#define GAMEPAD_DEADZONE 7000
// A stick or trigger counts as pressed past this.
#define GAMEPAD_PRESS 16000
// Cursor speed at full tilt, in pixels per second; slower near the middle
// (squared), for aiming at a hex.
#define GAMEPAD_CURSOR_SPEED 700.0

static SDL_GameController* gamepad_controller = nullptr;
static unsigned int gamepad_last_ticks = 0;
static double gamepad_carry_x = 0;
static double gamepad_carry_y = 0;
static int gamepad_buttons = 0;

// Keys held by sticks and triggers (scancode, or 0).
static int gamepad_scroll_x_key = 0;
static int gamepad_scroll_y_key = 0;
static bool gamepad_left_trigger = false;
static bool gamepad_right_trigger = false;

static void gamepad_open(int index)
{
    if (gamepad_controller != nullptr || !SDL_IsGameController(index)) {
        return;
    }

    gamepad_controller = SDL_GameControllerOpen(index);
    if (gamepad_controller != nullptr) {
        debugPrint("\nGAMEPAD: %s\n", SDL_GameControllerName(gamepad_controller));
    }
}

void gamepad_init()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        debugPrint("\nGAMEPAD: no controller support (%s)\n", SDL_GetError());
        return;
    }

    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        gamepad_open(index);
    }
}

void gamepad_exit()
{
    if (gamepad_controller != nullptr) {
        SDL_GameControllerClose(gamepad_controller);
        gamepad_controller = nullptr;
    }

    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
}

// The key a button stands for (see gamepad.h), or 0.
static int gamepad_button_key(int button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_X:
        return SDL_SCANCODE_I;
    case SDL_CONTROLLER_BUTTON_Y:
        return SDL_SCANCODE_P;
    case SDL_CONTROLLER_BUTTON_BACK:
        return SDL_SCANCODE_C;
    case SDL_CONTROLLER_BUTTON_START:
        return SDL_SCANCODE_ESCAPE;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return SDL_SCANCODE_S;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return SDL_SCANCODE_B;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return SDL_SCANCODE_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return SDL_SCANCODE_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return SDL_SCANCODE_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return SDL_SCANCODE_RIGHT;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        return SDL_SCANCODE_RETURN;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        return SDL_SCANCODE_G;
    }
    return 0;
}

// Holds `*held` while `want` (a scancode or 0), releasing the previous one.
static void gamepad_hold(int* held, int want, GamepadKeyProc* sendKey)
{
    if (*held == want) {
        return;
    }
    if (*held != 0) {
        sendKey(*held, false);
    }
    *held = want;
    if (want != 0) {
        sendKey(want, true);
    }
}

bool gamepad_handle_event(const SDL_Event* event, GamepadKeyProc* sendKey)
{
    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        gamepad_open(event->cdevice.which);
        return true;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (gamepad_controller != nullptr
            && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad_controller)) == event->cdevice.which) {
            SDL_GameControllerClose(gamepad_controller);
            gamepad_controller = nullptr;
            gamepad_buttons = 0;
        }
        return true;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        if (true) {
            bool down = event->type == SDL_CONTROLLERBUTTONDOWN;
            int button = event->cbutton.button;
            int mouseButton = button == SDL_CONTROLLER_BUTTON_A ? MOUSE_STATE_LEFT_BUTTON_DOWN
                : button == SDL_CONTROLLER_BUTTON_B           ? MOUSE_STATE_RIGHT_BUTTON_DOWN
                                                              : 0;
            if (mouseButton != 0) {
                gamepad_buttons = down ? (gamepad_buttons | mouseButton) : (gamepad_buttons & ~mouseButton);
            } else if (int key = gamepad_button_key(button)) {
                sendKey(key, down);
            }
        }
        return true;
    case SDL_CONTROLLERAXISMOTION:
        if (true) {
            int value = event->caxis.value;
            switch (event->caxis.axis) {
            case SDL_CONTROLLER_AXIS_RIGHTX:
                gamepad_hold(&gamepad_scroll_x_key, value > GAMEPAD_PRESS ? SDL_SCANCODE_RIGHT : value < -GAMEPAD_PRESS ? SDL_SCANCODE_LEFT : 0, sendKey);
                break;
            case SDL_CONTROLLER_AXIS_RIGHTY:
                gamepad_hold(&gamepad_scroll_y_key, value > GAMEPAD_PRESS ? SDL_SCANCODE_DOWN : value < -GAMEPAD_PRESS ? SDL_SCANCODE_UP : 0, sendKey);
                break;
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                if ((value > GAMEPAD_PRESS) != gamepad_left_trigger) {
                    gamepad_left_trigger = value > GAMEPAD_PRESS;
                    sendKey(SDL_SCANCODE_A, gamepad_left_trigger);
                }
                break;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                if ((value > GAMEPAD_PRESS) != gamepad_right_trigger) {
                    gamepad_right_trigger = value > GAMEPAD_PRESS;
                    sendKey(SDL_SCANCODE_SPACE, gamepad_right_trigger);
                }
                break;
            }
        }
        return true;
    }

    return false;
}

// -1..1 past the dead zone, squared (keeping the sign).
static double gamepad_curve(int value)
{
    int magnitude = abs(value);
    if (magnitude <= GAMEPAD_DEADZONE) {
        return 0;
    }
    double n = (double)(magnitude - GAMEPAD_DEADZONE) / (32767 - GAMEPAD_DEADZONE);
    if (n > 1) {
        n = 1;
    }
    return (value < 0 ? -1 : 1) * n * n;
}

void gamepad_mouse(int* dx, int* dy, int* buttons)
{
    *dx = 0;
    *dy = 0;
    *buttons = gamepad_buttons;

    unsigned int now = SDL_GetTicks();
    unsigned int elapsed = gamepad_last_ticks == 0 ? 0 : now - gamepad_last_ticks;
    gamepad_last_ticks = now;
    if (gamepad_controller == nullptr) {
        gamepad_carry_x = 0;
        gamepad_carry_y = 0;
        return;
    }

    // (Long gaps, like a loading map, don't fling the cursor.)
    if (elapsed > 100) {
        elapsed = 100;
    }

    double seconds = elapsed / 1000.0;
    gamepad_carry_x += gamepad_curve(SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_LEFTX)) * GAMEPAD_CURSOR_SPEED * seconds;
    gamepad_carry_y += gamepad_curve(SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_LEFTY)) * GAMEPAD_CURSOR_SPEED * seconds;

    *dx = (int)gamepad_carry_x;
    *dy = (int)gamepad_carry_y;
    gamepad_carry_x -= *dx;
    gamepad_carry_y -= *dy;
}

} // namespace fallout
