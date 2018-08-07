/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <SDL.h>
#include <stdbool.h>
#include "common/common.h"
#include "osdep/atomic.h"
#include "common/msg.h"
#include "input.h"
#include "input/keycodes.h"

struct priv {
    atomic_bool cancel_requested;
};

#define INVALID_KEY -1

static const int button_map[][2] = {
    { SDL_CONTROLLER_BUTTON_A, MP_KEY_GAMEPAD_ACTION_DOWN },
    { SDL_CONTROLLER_BUTTON_B, MP_KEY_GAMEPAD_ACTION_RIGHT },
    { SDL_CONTROLLER_BUTTON_X, MP_KEY_GAMEPAD_ACTION_LEFT },
    { SDL_CONTROLLER_BUTTON_Y,  MP_KEY_GAMEPAD_ACTION_UP },
    { SDL_CONTROLLER_BUTTON_BACK, MP_KEY_GAMEPAD_BACK },
    { SDL_CONTROLLER_BUTTON_GUIDE, MP_KEY_GAMEPAD_MENU },
    { SDL_CONTROLLER_BUTTON_START, MP_KEY_GAMEPAD_START },
    { SDL_CONTROLLER_BUTTON_LEFTSTICK, MP_KEY_GAMEPAD_LEFT_STICK },
    { SDL_CONTROLLER_BUTTON_RIGHTSTICK, MP_KEY_GAMEPAD_RIGHT_STICK },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, MP_KEY_GAMEPAD_LEFT_SHOULDER },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, MP_KEY_GAMEPAD_RIGHT_SHOULDER },
    { SDL_CONTROLLER_BUTTON_DPAD_UP, MP_KEY_GAMEPAD_DPAD_UP },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN, MP_KEY_GAMEPAD_DPAD_DOWN },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, MP_KEY_GAMEPAD_DPAD_LEFT },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, MP_KEY_GAMEPAD_DPAD_RIGHT },
};

static const int axis_map[][4] = {
    // 0 -> sdl enum
    // 1 -> negative state
    // 2 -> neutral state
    // 3 -> positive state
    { SDL_CONTROLLER_AXIS_LEFTX,
        MP_KEY_GAMEPAD_LEFT_STICK_LEFT | MP_KEY_STATE_DOWN,
        MP_KEY_GAMEPAD_LEFT_STICK_LEFT |
            MP_KEY_GAMEPAD_LEFT_STICK_RIGHT |
            MP_KEY_STATE_UP,
        MP_KEY_GAMEPAD_LEFT_STICK_RIGHT | MP_KEY_STATE_DOWN },

    { SDL_CONTROLLER_AXIS_LEFTY,
        MP_KEY_GAMEPAD_LEFT_STICK_UP | MP_KEY_STATE_DOWN,
        MP_KEY_GAMEPAD_LEFT_STICK_UP |
            MP_KEY_GAMEPAD_LEFT_STICK_DOWN |
            MP_KEY_STATE_UP,
        MP_KEY_GAMEPAD_LEFT_STICK_DOWN | MP_KEY_STATE_DOWN },

    { SDL_CONTROLLER_AXIS_RIGHTX,
        MP_KEY_GAMEPAD_RIGHT_STICK_LEFT | MP_KEY_STATE_DOWN,
        MP_KEY_GAMEPAD_RIGHT_STICK_LEFT |
            MP_KEY_GAMEPAD_RIGHT_STICK_RIGHT |
            MP_KEY_STATE_UP,
        MP_KEY_GAMEPAD_RIGHT_STICK_RIGHT | MP_KEY_STATE_DOWN },

    { SDL_CONTROLLER_AXIS_RIGHTY,
        MP_KEY_GAMEPAD_RIGHT_STICK_UP | MP_KEY_STATE_DOWN,
        MP_KEY_GAMEPAD_RIGHT_STICK_UP |
            MP_KEY_GAMEPAD_RIGHT_STICK_DOWN |
            MP_KEY_STATE_UP,
        MP_KEY_GAMEPAD_RIGHT_STICK_DOWN | MP_KEY_STATE_DOWN },

    { SDL_CONTROLLER_AXIS_TRIGGERLEFT,
        INVALID_KEY,
        MP_KEY_GAMEPAD_LEFT_TRIGGER | MP_KEY_STATE_UP,
        MP_KEY_GAMEPAD_LEFT_TRIGGER | MP_KEY_STATE_DOWN },

    { SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
        INVALID_KEY,
        MP_KEY_GAMEPAD_RIGHT_TRIGGER | MP_KEY_STATE_UP,
        MP_KEY_GAMEPAD_RIGHT_TRIGGER | MP_KEY_STATE_DOWN },
};

static int lookup_button_mp_key(int sdl_key) {
    for (int i = 0; i < MP_ARRAY_SIZE(button_map); i++) {
        if (button_map[i][0] == sdl_key) {
            return button_map[i][1];
        }
    }
    return INVALID_KEY;
}

static int lookup_axis_mp_key(int sdl_key, int16_t value) {
    const int sdl_axis_max = 32767;
    const int negative = 1;
    const int neutral = 2;
    const int positive = 3;

    const float threshold = (sdl_axis_max * 0.01);

    int state = neutral;

    if (value >= sdl_axis_max - threshold) {
        state = positive;
    }

    if (value <= threshold - sdl_axis_max) {
        state = negative;
    }

    for (int i = 0; i < MP_ARRAY_SIZE(axis_map); i++) {
        if (axis_map[i][0] == sdl_key) {
            return axis_map[i][state];
        }
    }

    return INVALID_KEY;
}


static void request_cancel(struct mp_input_src *src)
{
    struct priv *p = src->priv;

    MP_VERBOSE(src, "exiting...\n");
    atomic_store(&p->cancel_requested, true);
}

static void uninit(struct mp_input_src *src)
{
    MP_VERBOSE(src, "exited.\n");
}

#define GUID_LEN 33

static void read_gamepad_thread(struct mp_input_src *src, void *param)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)) {
        MP_ERR(src, "SDL_Init failed\n");
        mp_input_src_init_done(src);
        return;
    };

    SDL_GameController *controller;
    struct priv *p = talloc_zero(src, struct priv);
    char guid[GUID_LEN];

    if (SDL_NumJoysticks() <= 0) {
        MP_VERBOSE(src, "no joysticks found");
        mp_input_src_init_done(src);
        return;
    }

    MP_VERBOSE(src, "connected controllers: %i\n", SDL_NumJoysticks());

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            SDL_JoystickID id = SDL_JoystickInstanceID(
                SDL_GameControllerGetJoystick(controller));

            SDL_JoystickGetGUIDString(
                SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(controller)),
                guid, GUID_LEN);

            if (controller) {
                MP_VERBOSE(
                    src, "detected controller #%i: %s, guid: %s\n",
                    id, SDL_GameControllerName(controller), guid);

                // stop at first controller apparently SDL can't open more
                // than one controller anyway?
                break;
            }
        }
    }

    atomic_store(&p->cancel_requested, false);
    src->priv = p;
    src->cancel = request_cancel;
    src->uninit = uninit;
    mp_input_src_init_done(src);

    SDL_Event ev;

    while (!atomic_load(&p->cancel_requested)) {
        while (SDL_PollEvent(&ev) != 0) {
            switch (ev.type) {
                case SDL_CONTROLLERBUTTONDOWN: {
                    const int key = lookup_button_mp_key(ev.cbutton.button);
                    if (key != INVALID_KEY) {
                        mp_input_put_key(src->input_ctx, key | MP_KEY_STATE_DOWN);
                    }
                    continue;
                }
                case SDL_CONTROLLERBUTTONUP: {
                    const int key = lookup_button_mp_key(ev.cbutton.button);
                    if (key != INVALID_KEY) {
                        mp_input_put_key(src->input_ctx, key | MP_KEY_STATE_UP);
                    }
                    continue;
                }
                case SDL_CONTROLLERAXISMOTION: {
                    const int key =
                        lookup_axis_mp_key(ev.caxis.axis, ev.caxis.value);
                    if (key != INVALID_KEY) {
                        mp_input_put_key(src->input_ctx, key);
                    }
                    continue;
                }
            }
        }
    }

    if (controller != NULL) {
        SDL_GameControllerClose(controller);
    }

    // must be called on the same thread of SDL_InitSubSystem, so uninit
    // callback can't be used for this
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void mp_input_gamepad_add(struct input_ctx *ictx)
{
    mp_input_add_thread_src(ictx, NULL, read_gamepad_thread);
}
