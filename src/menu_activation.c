#include "vitacheat/menu_activation.h"

#include <string.h>

void vc_menu_activation_reset(vc_menu_activation_state *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

vc_menu_activation_event vc_menu_activation_update(vc_menu_activation_state *state,
                                                    bool select_down,
                                                    uint64_t now_ms)
{
    if (state == NULL) {
        return VC_MENU_ACTIVATION_NONE;
    }

    if (!select_down) {
        state->pressed_since_ms = 0;
        state->last_timestamp_ms = now_ms;
        state->pressed = false;
        state->latched = false;
        return VC_MENU_ACTIVATION_NONE;
    }

    if (!state->pressed) {
        state->pressed_since_ms = now_ms;
        state->pressed = true;
        state->latched = false;
    } else if (now_ms < state->last_timestamp_ms && !state->latched) {
        state->pressed_since_ms = now_ms;
    }

    state->last_timestamp_ms = now_ms;
    if (!state->latched && now_ms - state->pressed_since_ms >= VC_MENU_SELECT_HOLD_MS) {
        state->latched = true;
        return VC_MENU_ACTIVATION_TRIGGERED;
    }

    return VC_MENU_ACTIVATION_NONE;
}
