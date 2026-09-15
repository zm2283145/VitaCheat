#ifndef VITACHEAT_MENU_ACTIVATION_H
#define VITACHEAT_MENU_ACTIVATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_MENU_SELECT_HOLD_MS UINT64_C(5000)

typedef enum vc_menu_activation_event {
    VC_MENU_ACTIVATION_NONE = 0,
    VC_MENU_ACTIVATION_TRIGGERED = 1
} vc_menu_activation_event;

typedef struct vc_menu_activation_state {
    uint64_t pressed_since_ms;
    uint64_t last_timestamp_ms;
    bool pressed;
    bool latched;
} vc_menu_activation_state;

void vc_menu_activation_reset(vc_menu_activation_state *state);

/*
 * Feed monotonic timestamps and the current Select-button state. One event is
 * emitted after a continuous five-second hold. It cannot repeat until Select
 * is released. A clock rollback safely restarts the hold interval.
 */
vc_menu_activation_event vc_menu_activation_update(vc_menu_activation_state *state,
                                                    bool select_down,
                                                    uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
