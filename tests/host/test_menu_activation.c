#include "vitacheat/menu_activation.h"

#include <stdio.h>

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void test_exact_hold_and_release_rearm(void)
{
    vc_menu_activation_state state;

    vc_menu_activation_reset(&state);
    CHECK(vc_menu_activation_update(&state, true, 1000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 5999) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 6000) == VC_MENU_ACTIVATION_TRIGGERED);
    CHECK(vc_menu_activation_update(&state, true, 9000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, false, 9001) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 10000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 15000) == VC_MENU_ACTIVATION_TRIGGERED);
}

static void test_taps_and_clock_rollback(void)
{
    vc_menu_activation_state state;

    vc_menu_activation_reset(&state);
    CHECK(vc_menu_activation_update(&state, true, 100) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, false, 1000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 2000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 6000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 1500) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 6499) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 6500) == VC_MENU_ACTIVATION_TRIGGERED);
}

static void test_clock_rollback_cannot_retrigger_latched_hold(void)
{
    vc_menu_activation_state state;

    vc_menu_activation_reset(&state);
    CHECK(vc_menu_activation_update(&state, true, 1000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 6000) == VC_MENU_ACTIVATION_TRIGGERED);
    CHECK(vc_menu_activation_update(&state, true, 50) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 10000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, false, 10001) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 11000) == VC_MENU_ACTIVATION_NONE);
    CHECK(vc_menu_activation_update(&state, true, 16000) == VC_MENU_ACTIVATION_TRIGGERED);
}

static void test_null_is_safe(void)
{
    CHECK(vc_menu_activation_update(NULL, true, 0) == VC_MENU_ACTIVATION_NONE);
    vc_menu_activation_reset(NULL);
}

int main(void)
{
    test_exact_hold_and_release_rearm();
    test_taps_and_clock_rollback();
    test_clock_rollback_cannot_retrigger_latched_hold();
    test_null_is_safe();

    if (failures != 0) {
        fprintf(stderr, "%d menu activation test(s) failed\n", failures);
        return 1;
    }
    puts("VitaCheat menu activation tests passed");
    return 0;
}
