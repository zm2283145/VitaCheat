#include "vitacheat/pause.h"

#include <string.h>

static void vc_pause_clear(vc_pause_state *state)
{
    memset(state, 0, sizeof(*state));
}

void vc_pause_state_init(vc_pause_state *state)
{
    if (state != NULL) {
        vc_pause_clear(state);
    }
}

static vc_pause_status vc_pause_validate_threads(const vc_thread_id *thread_ids,
                                                 size_t thread_count)
{
    size_t index;

    if (thread_ids == NULL || thread_count == 0 ||
        thread_count > VC_PAUSE_MAX_THREADS) {
        return VC_PAUSE_STATUS_INVALID_THREADS;
    }

    for (index = 0; index < thread_count; ++index) {
        if (thread_ids[index] <= 0 ||
            (index != 0 && thread_ids[index - 1] >= thread_ids[index])) {
            return VC_PAUSE_STATUS_INVALID_THREADS;
        }
    }

    return VC_PAUSE_STATUS_OK;
}

static bool vc_pause_validate_boundary(
    const vc_pause_validations *validations,
    vc_pause_boundary_validation validation)
{
    return validations == NULL || validation == NULL ||
           validation(validations->context);
}

static bool vc_pause_resume_owned(
    vc_pause_state *state,
    size_t index,
    const vc_pause_operations *operations,
    const vc_pause_validations *validations,
    bool *validation_failed)
{
    if (!vc_pause_validate_boundary(
            validations,
            validations == NULL ? NULL : validations->validate_resume)) {
        *validation_failed = true;
        return false;
    }
    if (operations->resume_thread(operations->context,
                                  state->thread_ids[index]) != 0) {
        state->failed_thread_id = state->thread_ids[index];
        return false;
    }
    state->owned_mask &= ~(UINT64_C(1) << index);
    if (!vc_pause_validate_boundary(
            validations,
            validations == NULL ? NULL : validations->validate_resume)) {
        *validation_failed = true;
        return false;
    }
    return true;
}

vc_pause_status vc_pause_begin_checked(
    vc_pause_state *state,
    uint64_t target_generation,
    uint64_t now_ms,
    uint64_t max_pause_ms,
    const vc_thread_id *thread_ids,
    size_t thread_count,
    const vc_pause_operations *operations,
    const vc_pause_validations *validations)
{
    vc_thread_id validated_thread_ids[VC_PAUSE_MAX_THREADS];
    size_t index;
    vc_pause_status status;

    if (state == NULL || operations == NULL ||
        operations->suspend_thread == NULL || operations->resume_thread == NULL ||
        target_generation == 0 || max_pause_ms == 0 ||
        max_pause_ms > VC_PAUSE_MAX_DURATION_MS ||
        now_ms > UINT64_MAX - max_pause_ms) {
        return VC_PAUSE_STATUS_INVALID_ARGUMENT;
    }
    if (state->active || state->owned_mask != 0) {
        return VC_PAUSE_STATUS_ALREADY_ACTIVE;
    }

    status = vc_pause_validate_threads(thread_ids, thread_count);
    if (status != VC_PAUSE_STATUS_OK) {
        return status;
    }

    memcpy(validated_thread_ids, thread_ids,
           thread_count * sizeof(validated_thread_ids[0]));
    vc_pause_clear(state);
    memcpy(state->thread_ids, validated_thread_ids,
           thread_count * sizeof(state->thread_ids[0]));
    state->target_generation = target_generation;
    state->started_ms = now_ms;
    state->deadline_ms = now_ms + max_pause_ms;
    state->thread_count = thread_count;
    state->active = true;

    for (index = 0; index < thread_count; ++index) {
        if (!vc_pause_validate_boundary(
                validations,
                validations == NULL
                    ? NULL
                    : validations->validate_suspend)) {
            bool rollback_failed = false;
            bool validation_failed = false;

            state->failed_thread_id = state->thread_ids[index];
            while (index != 0) {
                --index;
                if ((state->owned_mask & (UINT64_C(1) << index)) == 0) {
                    continue;
                }
                if (!vc_pause_resume_owned(
                        state, index, operations, validations,
                        &validation_failed)) {
                    rollback_failed = true;
                    if (validation_failed) {
                        break;
                    }
                }
            }
            if (state->owned_mask == 0) {
                vc_pause_clear(state);
                return VC_PAUSE_STATUS_VALIDATION_FAILED;
            }
            return rollback_failed ? VC_PAUSE_STATUS_ROLLBACK_FAILED
                                   : VC_PAUSE_STATUS_VALIDATION_FAILED;
        }
        if (operations->suspend_thread(operations->context,
                                       state->thread_ids[index]) != 0) {
            bool rollback_failed = false;
            bool validation_failed = false;

            state->failed_thread_id = state->thread_ids[index];
            while (index != 0) {
                --index;
                if ((state->owned_mask & (UINT64_C(1) << index)) == 0) {
                    continue;
                }
                if (!vc_pause_resume_owned(
                        state, index, operations, validations,
                        &validation_failed)) {
                    rollback_failed = true;
                    if (validation_failed) {
                        break;
                    }
                }
            }

            if (state->owned_mask == 0) {
                vc_pause_clear(state);
                return VC_PAUSE_STATUS_SUSPEND_FAILED;
            }
            return rollback_failed ? VC_PAUSE_STATUS_ROLLBACK_FAILED
                                   : VC_PAUSE_STATUS_SUSPEND_FAILED;
        }

        state->owned_mask |= UINT64_C(1) << index;
        if (!vc_pause_validate_boundary(
                validations,
                validations == NULL
                    ? NULL
                    : validations->validate_suspend)) {
            bool rollback_failed = false;
            bool validation_failed = false;
            size_t rollback_index = index + 1u;

            state->failed_thread_id = state->thread_ids[index];
            while (rollback_index != 0) {
                --rollback_index;
                if ((state->owned_mask &
                     (UINT64_C(1) << rollback_index)) == 0) {
                    continue;
                }
                if (!vc_pause_resume_owned(
                        state, rollback_index, operations, validations,
                        &validation_failed)) {
                    rollback_failed = true;
                    if (validation_failed) {
                        break;
                    }
                }
            }
            if (state->owned_mask == 0) {
                vc_pause_clear(state);
                return VC_PAUSE_STATUS_VALIDATION_FAILED;
            }
            return rollback_failed ? VC_PAUSE_STATUS_ROLLBACK_FAILED
                                   : VC_PAUSE_STATUS_VALIDATION_FAILED;
        }
    }

    state->failed_thread_id = 0;
    return VC_PAUSE_STATUS_OK;
}

vc_pause_status vc_pause_begin(vc_pause_state *state,
                               uint64_t target_generation,
                               uint64_t now_ms,
                               uint64_t max_pause_ms,
                               const vc_thread_id *thread_ids,
                               size_t thread_count,
                               const vc_pause_operations *operations)
{
    return vc_pause_begin_checked(
        state, target_generation, now_ms, max_pause_ms,
        thread_ids, thread_count, operations, NULL);
}

vc_pause_status vc_pause_end_checked(
    vc_pause_state *state,
    uint64_t target_generation,
    const vc_pause_operations *operations,
    const vc_pause_validations *validations)
{
    size_t index;
    bool validation_failed = false;
    bool resume_failed = false;

    if (state == NULL || operations == NULL ||
        operations->resume_thread == NULL || target_generation == 0) {
        return VC_PAUSE_STATUS_INVALID_ARGUMENT;
    }
    if (!state->active) {
        return VC_PAUSE_STATUS_NOT_ACTIVE;
    }
    if (state->target_generation != target_generation) {
        return VC_PAUSE_STATUS_STALE_TARGET;
    }

    state->failed_thread_id = 0;
    index = state->thread_count;
    while (index != 0) {
        --index;
        if ((state->owned_mask & (UINT64_C(1) << index)) == 0) {
            continue;
        }
        if (!vc_pause_resume_owned(
                state, index, operations, validations,
                &validation_failed)) {
            if (validation_failed) {
                break;
            }
            resume_failed = true;
        }
    }

    if (state->owned_mask != 0) {
        return validation_failed ? VC_PAUSE_STATUS_VALIDATION_FAILED
                                 : VC_PAUSE_STATUS_RESUME_FAILED;
    }

    vc_pause_clear(state);
    return validation_failed ? VC_PAUSE_STATUS_VALIDATION_FAILED
                             : resume_failed
                                   ? VC_PAUSE_STATUS_RESUME_FAILED
                                   : VC_PAUSE_STATUS_OK;
}

vc_pause_status vc_pause_end(vc_pause_state *state,
                             uint64_t target_generation,
                             const vc_pause_operations *operations)
{
    return vc_pause_end_checked(
        state, target_generation, operations, NULL);
}

vc_pause_status vc_pause_tick(vc_pause_state *state,
                              uint64_t target_generation,
                              uint64_t now_ms,
                              const vc_pause_operations *operations)
{
    vc_pause_status status;

    if (state == NULL || operations == NULL ||
        operations->resume_thread == NULL || target_generation == 0) {
        return VC_PAUSE_STATUS_INVALID_ARGUMENT;
    }
    if (!state->active) {
        return VC_PAUSE_STATUS_NOT_ACTIVE;
    }
    if (state->target_generation != target_generation) {
        return VC_PAUSE_STATUS_STALE_TARGET;
    }
    if (now_ms >= state->started_ms && now_ms < state->deadline_ms) {
        return VC_PAUSE_STATUS_OK;
    }

    status = vc_pause_end(state, target_generation, operations);
    return status == VC_PAUSE_STATUS_OK ? VC_PAUSE_STATUS_EXPIRED : status;
}

vc_pause_status vc_pause_abandon(vc_pause_state *state,
                                 uint64_t target_generation)
{
    if (state == NULL || target_generation == 0) {
        return VC_PAUSE_STATUS_INVALID_ARGUMENT;
    }
    if (!state->active) {
        return VC_PAUSE_STATUS_NOT_ACTIVE;
    }
    if (state->target_generation != target_generation) {
        return VC_PAUSE_STATUS_STALE_TARGET;
    }

    vc_pause_clear(state);
    return VC_PAUSE_STATUS_OK;
}
