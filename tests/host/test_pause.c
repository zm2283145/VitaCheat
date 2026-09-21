#include "vitacheat/pause.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

typedef struct fake_event {
    char operation;
    vc_thread_id thread_id;
} fake_event;

typedef struct fake_threads {
    fake_event events[32];
    size_t event_count;
    vc_thread_id suspend_failure;
    vc_thread_id resume_failure;
    unsigned int resume_failures_remaining;
    unsigned int suspend_validation_calls;
    unsigned int resume_validation_calls;
    unsigned int fail_suspend_validation_call;
    unsigned int fail_resume_validation_call;
} fake_threads;

static void record_event(fake_threads *threads, char operation, vc_thread_id thread_id)
{
    CHECK(threads->event_count < sizeof(threads->events) / sizeof(threads->events[0]));
    if (threads->event_count < sizeof(threads->events) / sizeof(threads->events[0])) {
        threads->events[threads->event_count].operation = operation;
        threads->events[threads->event_count].thread_id = thread_id;
        ++threads->event_count;
    }
}

static int suspend_thread(void *context, vc_thread_id thread_id)
{
    fake_threads *threads = context;

    record_event(threads, 'S', thread_id);
    return thread_id == threads->suspend_failure ? -1 : 0;
}

static int resume_thread(void *context, vc_thread_id thread_id)
{
    fake_threads *threads = context;

    record_event(threads, 'R', thread_id);
    if (thread_id == threads->resume_failure &&
        threads->resume_failures_remaining != 0) {
        --threads->resume_failures_remaining;
        return -1;
    }
    return 0;
}

static vc_pause_operations operations_for(fake_threads *threads)
{
    vc_pause_operations operations;

    operations.suspend_thread = suspend_thread;
    operations.resume_thread = resume_thread;
    operations.context = threads;
    return operations;
}

static bool validate_suspend(void *context)
{
    fake_threads *threads = context;

    ++threads->suspend_validation_calls;
    return threads->fail_suspend_validation_call == 0 ||
           threads->suspend_validation_calls !=
               threads->fail_suspend_validation_call;
}

static bool validate_resume(void *context)
{
    fake_threads *threads = context;

    ++threads->resume_validation_calls;
    return threads->fail_resume_validation_call == 0 ||
           threads->resume_validation_calls !=
               threads->fail_resume_validation_call;
}

static vc_pause_validations validations_for(fake_threads *threads)
{
    vc_pause_validations validations;

    validations.validate_suspend = validate_suspend;
    validations.validate_resume = validate_resume;
    validations.context = threads;
    return validations;
}

static void check_event(const fake_threads *threads,
                        size_t index,
                        char operation,
                        vc_thread_id thread_id)
{
    CHECK(index < threads->event_count);
    if (index < threads->event_count) {
        CHECK(threads->events[index].operation == operation);
        CHECK(threads->events[index].thread_id == thread_id);
    }
}

static void test_successful_pause_and_reverse_resume(void)
{
    const vc_thread_id ids[] = {10, 20, 30};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;

    memset(&threads, 0, sizeof(threads));
    operations = operations_for(&threads);
    vc_pause_state_init(&state);

    CHECK(vc_pause_begin(&state, 7, 1000, 5000, ids, 3, &operations) ==
          VC_PAUSE_STATUS_OK);
    CHECK(state.active);
    CHECK(state.target_generation == 7);
    CHECK(state.started_ms == 1000);
    CHECK(state.deadline_ms == 6000);
    CHECK(state.owned_mask == UINT64_C(7));
    CHECK(threads.event_count == 3);
    check_event(&threads, 0, 'S', 10);
    check_event(&threads, 1, 'S', 20);
    check_event(&threads, 2, 'S', 30);

    CHECK(vc_pause_end(&state, 7, &operations) == VC_PAUSE_STATUS_OK);
    CHECK(!state.active);
    CHECK(state.owned_mask == 0);
    CHECK(threads.event_count == 6);
    check_event(&threads, 3, 'R', 30);
    check_event(&threads, 4, 'R', 20);
    check_event(&threads, 5, 'R', 10);
}

static void test_suspend_failure_rolls_back(void)
{
    const vc_thread_id ids[] = {10, 20, 30};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;

    memset(&threads, 0, sizeof(threads));
    threads.suspend_failure = 20;
    operations = operations_for(&threads);
    vc_pause_state_init(&state);

    CHECK(vc_pause_begin(&state, 9, 100, 1000, ids, 3, &operations) ==
          VC_PAUSE_STATUS_SUSPEND_FAILED);
    CHECK(!state.active);
    CHECK(state.owned_mask == 0);
    CHECK(threads.event_count == 3);
    check_event(&threads, 0, 'S', 10);
    check_event(&threads, 1, 'S', 20);
    check_event(&threads, 2, 'R', 10);
}

static void test_failed_rollback_retains_only_owned_suspensions(void)
{
    const vc_thread_id ids[] = {10, 20, 30};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;

    memset(&threads, 0, sizeof(threads));
    threads.suspend_failure = 30;
    threads.resume_failure = 20;
    threads.resume_failures_remaining = 1;
    operations = operations_for(&threads);
    vc_pause_state_init(&state);

    CHECK(vc_pause_begin(&state, 11, 100, 1000, ids, 3, &operations) ==
          VC_PAUSE_STATUS_ROLLBACK_FAILED);
    CHECK(state.active);
    CHECK(state.owned_mask == UINT64_C(2));
    CHECK(state.failed_thread_id == 20);
    CHECK(threads.event_count == 5);
    check_event(&threads, 3, 'R', 20);
    check_event(&threads, 4, 'R', 10);

    CHECK(vc_pause_end(&state, 11, &operations) == VC_PAUSE_STATUS_OK);
    CHECK(!state.active);
    CHECK(threads.event_count == 6);
    check_event(&threads, 5, 'R', 20);
}

static void test_resume_failure_is_retryable(void)
{
    const vc_thread_id ids[] = {10, 20, 30};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;

    memset(&threads, 0, sizeof(threads));
    threads.resume_failure = 20;
    threads.resume_failures_remaining = 1;
    operations = operations_for(&threads);
    vc_pause_state_init(&state);

    CHECK(vc_pause_begin(&state, 13, 100, 1000, ids, 3, &operations) ==
          VC_PAUSE_STATUS_OK);
    CHECK(vc_pause_end(&state, 13, &operations) ==
          VC_PAUSE_STATUS_RESUME_FAILED);
    CHECK(state.active);
    CHECK(state.owned_mask == UINT64_C(2));
    CHECK(state.failed_thread_id == 20);

    CHECK(vc_pause_end(&state, 13, &operations) == VC_PAUSE_STATUS_OK);
    CHECK(!state.active);
    CHECK(threads.event_count == 7);
    check_event(&threads, 3, 'R', 30);
    check_event(&threads, 4, 'R', 20);
    check_event(&threads, 5, 'R', 10);
    check_event(&threads, 6, 'R', 20);
}

static void test_generation_binding_and_confirmed_abandon(void)
{
    const vc_thread_id ids[] = {10, 20};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;
    size_t event_count;

    memset(&threads, 0, sizeof(threads));
    operations = operations_for(&threads);
    vc_pause_state_init(&state);

    CHECK(vc_pause_begin(&state, 15, 100, 1000, ids, 2, &operations) ==
          VC_PAUSE_STATUS_OK);
    event_count = threads.event_count;
    CHECK(vc_pause_end(&state, 16, &operations) ==
          VC_PAUSE_STATUS_STALE_TARGET);
    CHECK(threads.event_count == event_count);
    CHECK(vc_pause_abandon(&state, 16) == VC_PAUSE_STATUS_STALE_TARGET);
    CHECK(state.active);
    CHECK(vc_pause_abandon(&state, 15) == VC_PAUSE_STATUS_OK);
    CHECK(!state.active);
    CHECK(threads.event_count == event_count);
}

static void test_watchdog_expiry_and_clock_rollback(void)
{
    const vc_thread_id ids[] = {10, 20};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;
    size_t event_count;

    memset(&threads, 0, sizeof(threads));
    operations = operations_for(&threads);
    vc_pause_state_init(&state);

    CHECK(vc_pause_begin(&state, 17, 100, 50, ids, 2, &operations) ==
          VC_PAUSE_STATUS_OK);
    event_count = threads.event_count;
    CHECK(vc_pause_tick(&state, 17, 149, &operations) ==
          VC_PAUSE_STATUS_OK);
    CHECK(threads.event_count == event_count);
    CHECK(vc_pause_tick(&state, 17, 150, &operations) ==
          VC_PAUSE_STATUS_EXPIRED);
    CHECK(!state.active);

    CHECK(vc_pause_begin(&state, 18, 100, 50, ids, 2, &operations) ==
          VC_PAUSE_STATUS_OK);
    CHECK(vc_pause_tick(&state, 18, 99, &operations) ==
          VC_PAUSE_STATUS_EXPIRED);
    CHECK(!state.active);
}

static void test_validation_is_side_effect_free(void)
{
    vc_thread_id too_many[VC_PAUSE_MAX_THREADS + 1u];
    const vc_thread_id valid[] = {10, 20};
    const vc_thread_id duplicate[] = {10, 10};
    const vc_thread_id zero[] = {0};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;
    size_t index;

    for (index = 0; index < VC_PAUSE_MAX_THREADS + 1u; ++index) {
        too_many[index] = (vc_thread_id)(index + 1u);
    }
    memset(&threads, 0, sizeof(threads));
    operations = operations_for(&threads);
    vc_pause_state_init(&state);

    CHECK(vc_pause_begin(&state, 1, 0, 1, NULL, 0, &operations) ==
          VC_PAUSE_STATUS_INVALID_THREADS);
    CHECK(vc_pause_begin(&state, 1, 0, 1, duplicate, 2, &operations) ==
          VC_PAUSE_STATUS_INVALID_THREADS);
    CHECK(vc_pause_begin(&state, 1, 0, 1, zero, 1, &operations) ==
          VC_PAUSE_STATUS_INVALID_THREADS);
    CHECK(vc_pause_begin(&state, 1, 0, 1, too_many,
                         VC_PAUSE_MAX_THREADS + 1u, &operations) ==
          VC_PAUSE_STATUS_INVALID_THREADS);
    CHECK(vc_pause_begin(&state, 0, 0, 1, valid, 2, &operations) ==
          VC_PAUSE_STATUS_INVALID_ARGUMENT);
    CHECK(vc_pause_begin(&state, 1, 0, VC_PAUSE_MAX_DURATION_MS + 1u,
                         valid, 2, &operations) ==
          VC_PAUSE_STATUS_INVALID_ARGUMENT);
    CHECK(vc_pause_begin(&state, 1, UINT64_MAX - 5u, 10,
                         valid, 2, &operations) ==
          VC_PAUSE_STATUS_INVALID_ARGUMENT);
    CHECK(threads.event_count == 0);
    CHECK(!state.active);

    CHECK(vc_pause_begin(&state, 1, 0, 100, valid, 2, &operations) ==
          VC_PAUSE_STATUS_OK);
    CHECK(vc_pause_begin(&state, 1, 0, 100, valid, 2, &operations) ==
          VC_PAUSE_STATUS_ALREADY_ACTIVE);
    CHECK(threads.event_count == 2);
    CHECK(vc_pause_end(&state, 1, &operations) == VC_PAUSE_STATUS_OK);
    CHECK(vc_pause_end(&state, 1, &operations) ==
          VC_PAUSE_STATUS_NOT_ACTIVE);
}

static void test_state_thread_list_can_be_reused(void)
{
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;

    memset(&threads, 0, sizeof(threads));
    operations = operations_for(&threads);
    vc_pause_state_init(&state);
    state.thread_ids[0] = 10;
    state.thread_ids[1] = 20;

    CHECK(vc_pause_begin(&state, 21, 100, 1000,
                         state.thread_ids, 2, &operations) ==
          VC_PAUSE_STATUS_OK);
    CHECK(state.thread_ids[0] == 10);
    CHECK(state.thread_ids[1] == 20);
    CHECK(vc_pause_end(&state, 21, &operations) == VC_PAUSE_STATUS_OK);
}

static void test_checked_callback_boundaries(void)
{
    const vc_thread_id ids[] = {10, 20};
    vc_pause_state state;
    fake_threads threads;
    vc_pause_operations operations;
    vc_pause_validations validations;

    memset(&threads, 0, sizeof(threads));
    threads.fail_suspend_validation_call = 2;
    operations = operations_for(&threads);
    validations = validations_for(&threads);
    vc_pause_state_init(&state);
    CHECK(vc_pause_begin_checked(
              &state, 23, 100, 1000, ids, 2,
              &operations, &validations) ==
          VC_PAUSE_STATUS_VALIDATION_FAILED);
    CHECK(!state.active);
    CHECK(threads.event_count == 2);
    check_event(&threads, 0, 'S', 10);
    check_event(&threads, 1, 'R', 10);

    memset(&threads, 0, sizeof(threads));
    threads.fail_suspend_validation_call = 2;
    threads.resume_failure = 10;
    threads.resume_failures_remaining = 1;
    operations = operations_for(&threads);
    validations = validations_for(&threads);
    vc_pause_state_init(&state);
    CHECK(vc_pause_begin_checked(
              &state, 24, 100, 1000, ids, 2,
              &operations, &validations) ==
          VC_PAUSE_STATUS_ROLLBACK_FAILED);
    CHECK(state.active);
    CHECK(state.owned_mask == UINT64_C(1));
    threads.fail_suspend_validation_call = 0;
    CHECK(vc_pause_end_checked(
              &state, 24, &operations, &validations) ==
          VC_PAUSE_STATUS_OK);

    memset(&threads, 0, sizeof(threads));
    operations = operations_for(&threads);
    validations = validations_for(&threads);
    vc_pause_state_init(&state);
    CHECK(vc_pause_begin_checked(
              &state, 25, 100, 1000, ids, 2,
              &operations, &validations) ==
          VC_PAUSE_STATUS_OK);
    threads.fail_resume_validation_call = 1;
    CHECK(vc_pause_end_checked(
              &state, 25, &operations, &validations) ==
          VC_PAUSE_STATUS_VALIDATION_FAILED);
    CHECK(state.active);
    CHECK(state.owned_mask == UINT64_C(3));
    threads.fail_resume_validation_call = 0;
    CHECK(vc_pause_end_checked(
              &state, 25, &operations, &validations) ==
          VC_PAUSE_STATUS_OK);
}

int main(void)
{
    test_successful_pause_and_reverse_resume();
    test_suspend_failure_rolls_back();
    test_failed_rollback_retains_only_owned_suspensions();
    test_resume_failure_is_retryable();
    test_generation_binding_and_confirmed_abandon();
    test_watchdog_expiry_and_clock_rollback();
    test_validation_is_side_effect_free();
    test_state_thread_list_can_be_reused();
    test_checked_callback_boundaries();

    if (failures != 0) {
        fprintf(stderr, "%d pause lifecycle test(s) failed\n", failures);
        return 1;
    }

    puts("VitaCheat pause lifecycle tests passed");
    return 0;
}
