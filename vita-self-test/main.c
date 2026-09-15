#include "vitacheat/menu_activation.h"
#include "vitacheat/search.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdint.h>
#include <string.h>

#include "debugScreen.h"

#define SNAPSHOT_BYTES 64u
#define RESULT_CAPACITY 16u

typedef enum screen_state {
    SCREEN_HOME = 0,
    SCREEN_MENU,
    SCREEN_RESULT
} screen_state;

typedef struct self_test_result {
    unsigned int checks;
    unsigned int failures;
    size_t initial_matches;
    size_t changed_matches;
    size_t increased_matches;
    size_t decreased_matches;
    size_t unchanged_matches;
} self_test_result;

static uint8_t previous_snapshot[SNAPSHOT_BYTES];
static uint8_t current_snapshot[SNAPSHOT_BYTES];
static uint32_t candidates[RESULT_CAPACITY];
static uint32_t refined[RESULT_CAPACITY];

static void clear_screen(void)
{
    psvDebugScreenPuts("\x1b[H\x1b[2J");
}

static void write_u32_le(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8);
    bytes[offset + 2u] = (uint8_t)(value >> 16);
    bytes[offset + 3u] = (uint8_t)(value >> 24);
}

static void check(self_test_result *report, int passed)
{
    ++report->checks;
    if (!passed)
        ++report->failures;
}

static int offsets_equal(const uint32_t *actual,
                         size_t count,
                         const uint32_t *expected,
                         size_t expected_count)
{
    return count == expected_count &&
           memcmp(actual, expected, count * sizeof(*actual)) == 0;
}

static vc_status refine(vc_relation relation,
                        uint32_t *output,
                        size_t capacity,
                        size_t *written,
                        size_t *total)
{
    vc_query query;
    memset(&query, 0, sizeof(query));
    query.type = VC_SCALAR_U32;
    query.relation = relation;
    query.stride = 4u;
    return vc_search_refine(previous_snapshot, current_snapshot,
                            sizeof(current_snapshot), candidates, 3u,
                            &query, output, capacity, written, total);
}

static self_test_result run_scanner_self_test(void)
{
    static const uint32_t expected_initial[] = {4u, 20u, 36u};
    static const uint32_t expected_changed[] = {4u, 36u};
    static const uint32_t expected_increased[] = {4u};
    static const uint32_t expected_decreased[] = {36u};
    static const uint32_t expected_unchanged[] = {20u};
    self_test_result report;
    vc_query query;
    vc_status status;
    size_t written = 0u;
    size_t total = 0u;
    uint32_t in_place[3];

    memset(&report, 0, sizeof(report));
    memset(previous_snapshot, 0, sizeof(previous_snapshot));
    write_u32_le(previous_snapshot, 4u, 100u);
    write_u32_le(previous_snapshot, 20u, 100u);
    write_u32_le(previous_snapshot, 36u, 100u);

    memset(&query, 0, sizeof(query));
    query.type = VC_SCALAR_U32;
    query.relation = VC_RELATION_EQUAL;
    query.value_bits = 100u;
    query.stride = 4u;
    written = 0u;
    total = 0u;
    status = vc_search_initial(previous_snapshot, sizeof(previous_snapshot),
                               &query, candidates, RESULT_CAPACITY,
                               &written, &total);
    report.initial_matches = total;
    check(&report, status == VC_STATUS_OK && total == 3u && written == 3u &&
                       offsets_equal(candidates, written, expected_initial,
                                     3u));

    memcpy(current_snapshot, previous_snapshot, sizeof(current_snapshot));
    write_u32_le(current_snapshot, 4u, 120u);
    write_u32_le(current_snapshot, 36u, 80u);

    written = 0u;
    total = 0u;
    status = refine(VC_RELATION_CHANGED, refined, RESULT_CAPACITY,
                    &written, &total);
    report.changed_matches = total;
    check(&report, status == VC_STATUS_OK && total == 2u &&
                       offsets_equal(refined, written, expected_changed, 2u));

    written = 0u;
    total = 0u;
    status = refine(VC_RELATION_INCREASED, refined, RESULT_CAPACITY,
                    &written, &total);
    report.increased_matches = total;
    check(&report, status == VC_STATUS_OK && total == 1u &&
                       offsets_equal(refined, written, expected_increased, 1u));

    memcpy(in_place, candidates, sizeof(in_place));
    written = 0u;
    total = 0u;
    status = vc_search_refine(previous_snapshot, current_snapshot,
                              sizeof(current_snapshot), in_place, 3u,
                              &(vc_query){
                                  .type = VC_SCALAR_U32,
                                  .relation = VC_RELATION_DECREASED,
                                  .value_bits = 0u,
                                  .stride = 4u,
                              },
                              in_place, 3u, &written, &total);
    report.decreased_matches = total;
    check(&report, status == VC_STATUS_OK && total == 1u &&
                       offsets_equal(in_place, written,
                                     expected_decreased, 1u));

    written = 0u;
    total = 0u;
    status = refine(VC_RELATION_UNCHANGED, refined, RESULT_CAPACITY,
                    &written, &total);
    report.unchanged_matches = total;
    check(&report, status == VC_STATUS_OK && total == 1u &&
                       offsets_equal(refined, written, expected_unchanged, 1u));

    written = 0u;
    total = 0u;
    status = vc_search_initial(previous_snapshot, sizeof(previous_snapshot),
                               &query, refined, 2u, &written, &total);
    check(&report, status == VC_STATUS_TRUNCATED && written == 2u &&
                       total == 3u && refined[0] == 4u && refined[1] == 20u);
    return report;
}

static void draw_home(uint64_t held_ms)
{
    unsigned int seconds = (unsigned int)(held_ms / 1000u);
    unsigned int tenths = (unsigned int)((held_ms % 1000u) / 100u);
    clear_screen();
    psvDebugScreenPrintf("VitaCheat safe self-test\n");
    psvDebugScreenPrintf(
        "Ordinary user mode - no privileged or game access\n\n");
    psvDebugScreenPrintf("Hold SELECT for 5 seconds to open the menu.\n");
    psvDebugScreenPrintf("Hold progress: %u.%u / 5.0 seconds\n\n",
                         seconds, tenths);
    psvDebugScreenPrintf("Circle: exit safely\n");
}

static void draw_menu(void)
{
    clear_screen();
    psvDebugScreenPrintf("VitaCheat test menu\n\n");
    psvDebugScreenPrintf("X       Run owned-buffer scanner checks\n");
    psvDebugScreenPrintf("Circle  Close menu\n");
    psvDebugScreenPrintf("Triangle Exit safely\n\n");
    psvDebugScreenPrintf("This build cannot inspect or write another app.\n");
}

static void draw_result(const self_test_result *report)
{
    clear_screen();
    psvDebugScreenPrintf("VitaCheat scanner self-test\n\n");
    psvDebugScreenPrintf("[%s] %u checks, %u failures\n\n",
                         report->failures == 0u ? "PASS" : "FAIL",
                         report->checks, report->failures);
    psvDebugScreenPrintf("Initial exact matches : %u (expected 3)\n",
                         (unsigned int)report->initial_matches);
    psvDebugScreenPrintf("Changed refinements   : %u (expected 2)\n",
                         (unsigned int)report->changed_matches);
    psvDebugScreenPrintf("Increased refinements : %u (expected 1)\n",
                         (unsigned int)report->increased_matches);
    psvDebugScreenPrintf("Decreased refinements : %u (expected 1)\n",
                         (unsigned int)report->decreased_matches);
    psvDebugScreenPrintf("Unchanged refinements : %u (expected 1)\n\n",
                         (unsigned int)report->unchanged_matches);
    psvDebugScreenPrintf("Circle: return to menu    Triangle: exit\n");
}

int main(void)
{
    vc_menu_activation_state activation;
    self_test_result result;
    screen_state screen = SCREEN_HOME;
    uint32_t previous_buttons = 0u;
    uint64_t last_draw_bucket = UINT64_MAX;
    int exit_requested = 0;

    memset(&result, 0, sizeof(result));
    vc_menu_activation_reset(&activation);
    if (psvDebugScreenInit() < 0) {
        sceKernelExitProcess(1);
        return 1;
    }
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    draw_home(0u);

    while (!exit_requested) {
        SceCtrlData pad;
        uint32_t buttons = 0u;
        uint32_t pressed;
        uint64_t now_ms = (uint64_t)sceKernelGetSystemTimeWide() / 1000u;

        memset(&pad, 0, sizeof(pad));
        if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0) {
            /* Missing input is not a physical button release. Preserve edge
             * state, but fail closed by restarting an unlatched HOME hold. */
            if (screen == SCREEN_HOME && activation.pressed &&
                !activation.latched) {
                vc_menu_activation_reset(&activation);
                last_draw_bucket = UINT64_MAX;
                draw_home(0u);
            }
            sceKernelDelayThread(16000);
            continue;
        }
        buttons = pad.buttons;
        pressed = buttons & ~previous_buttons;
        previous_buttons = buttons;

        /* Holds are timed only on the home screen, but a Select release must
         * always rearm the one-shot latch. Otherwise a release and re-press
         * performed entirely inside the menu/result screens would be lost. */
        if (screen != SCREEN_HOME &&
            (buttons & SCE_CTRL_SELECT) == 0u && activation.pressed)
            (void)vc_menu_activation_update(&activation, false, now_ms);

        if (screen == SCREEN_HOME) {
            const int select_down = (buttons & SCE_CTRL_SELECT) != 0u;
            vc_menu_activation_event event = vc_menu_activation_update(
                &activation, select_down != 0, now_ms);
            uint64_t held_ms = 0u;
            uint64_t bucket;
            if (activation.pressed && now_ms >= activation.pressed_since_ms)
                held_ms = now_ms - activation.pressed_since_ms;
            if (held_ms > VC_MENU_SELECT_HOLD_MS)
                held_ms = VC_MENU_SELECT_HOLD_MS;
            bucket = held_ms / 100u;
            if (bucket != last_draw_bucket) {
                draw_home(held_ms);
                last_draw_bucket = bucket;
            }
            if (event == VC_MENU_ACTIVATION_TRIGGERED) {
                screen = SCREEN_MENU;
                draw_menu();
            } else if ((pressed & SCE_CTRL_CIRCLE) != 0u) {
                exit_requested = 1;
            }
        } else if (screen == SCREEN_MENU) {
            if ((pressed & SCE_CTRL_CROSS) != 0u) {
                result = run_scanner_self_test();
                screen = SCREEN_RESULT;
                draw_result(&result);
            } else if ((pressed & SCE_CTRL_CIRCLE) != 0u) {
                last_draw_bucket = UINT64_MAX;
                screen = SCREEN_HOME;
                draw_home(0u);
            } else if ((pressed & SCE_CTRL_TRIANGLE) != 0u) {
                exit_requested = 1;
            }
        } else {
            if ((pressed & SCE_CTRL_CIRCLE) != 0u) {
                screen = SCREEN_MENU;
                draw_menu();
            } else if ((pressed & SCE_CTRL_TRIANGLE) != 0u) {
                exit_requested = 1;
            }
        }
        sceKernelDelayThread(16000);
    }

    psvDebugScreenFinish();
    sceKernelExitProcess(result.failures == 0u ? 0 : 1);
    return 0;
}
