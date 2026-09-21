#ifndef VITACHEAT_PAUSE_H
#define VITACHEAT_PAUSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_PAUSE_MAX_THREADS 64u
#define VC_PAUSE_MAX_DURATION_MS UINT64_C(600000)

typedef int32_t vc_thread_id;

typedef enum vc_pause_status {
    VC_PAUSE_STATUS_OK = 0,
    VC_PAUSE_STATUS_EXPIRED = 1,
    VC_PAUSE_STATUS_INVALID_ARGUMENT = -1,
    VC_PAUSE_STATUS_INVALID_THREADS = -2,
    VC_PAUSE_STATUS_ALREADY_ACTIVE = -3,
    VC_PAUSE_STATUS_NOT_ACTIVE = -4,
    VC_PAUSE_STATUS_STALE_TARGET = -5,
    VC_PAUSE_STATUS_SUSPEND_FAILED = -6,
    VC_PAUSE_STATUS_ROLLBACK_FAILED = -7,
    VC_PAUSE_STATUS_RESUME_FAILED = -8
} vc_pause_status;

/*
 * Success means exactly one suspend count was acquired or released. Failure
 * must not partially change the thread's suspend count. Callbacks must not
 * reenter this pause state.
 */
typedef int (*vc_pause_thread_operation)(void *context, vc_thread_id thread_id);

typedef struct vc_pause_operations {
    vc_pause_thread_operation suspend_thread;
    vc_pause_thread_operation resume_thread;
    void *context;
} vc_pause_operations;

typedef struct vc_pause_state {
    uint64_t target_generation;
    uint64_t started_ms;
    uint64_t deadline_ms;
    uint64_t owned_mask;
    vc_thread_id thread_ids[VC_PAUSE_MAX_THREADS];
    size_t thread_count;
    vc_thread_id failed_thread_id;
    bool active;
} vc_pause_state;

void vc_pause_state_init(vc_pause_state *state);

/*
 * Suspends a strictly increasing allowlist of gameplay thread IDs. The caller
 * must exclude the hook, input, menu-render, and cleanup threads needed while
 * the menu is open. On failure, every suspension already owned by this
 * transaction is rolled back in reverse order.
 */
vc_pause_status vc_pause_begin(vc_pause_state *state,
                               uint64_t target_generation,
                               uint64_t now_ms,
                               uint64_t max_pause_ms,
                               const vc_thread_id *thread_ids,
                               size_t thread_count,
                               const vc_pause_operations *operations);

/*
 * Resumes only suspensions owned by this transaction, in reverse order.
 * Failed resumes remain recorded so the same call can safely retry cleanup.
 */
vc_pause_status vc_pause_end(vc_pause_state *state,
                             uint64_t target_generation,
                             const vc_pause_operations *operations);

/*
 * Enforces the pause deadline. A watchdog execution path that is not itself
 * paused must call this regularly. A clock rollback is treated as expiry.
 * VC_PAUSE_STATUS_EXPIRED means cleanup completed and the menu must close.
 */
vc_pause_status vc_pause_tick(vc_pause_state *state,
                              uint64_t target_generation,
                              uint64_t now_ms,
                              const vc_pause_operations *operations);

/*
 * Clears retained ownership without resuming threads. The adapter may call
 * this only after independently proving that this exact process generation no
 * longer exists and its thread IDs cannot still refer to the old target.
 */
vc_pause_status vc_pause_abandon(vc_pause_state *state,
                                 uint64_t target_generation);

#ifdef __cplusplus
}
#endif

#endif
