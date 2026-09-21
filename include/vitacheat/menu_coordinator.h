#ifndef VITACHEAT_MENU_COORDINATOR_H
#define VITACHEAT_MENU_COORDINATOR_H

#include "vitacheat/launch_claimant.h"
#include "vitacheat/pause.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_MENU_COORDINATOR_MAX_ACK_MS UINT64_C(1000)
#define VC_MENU_COORDINATOR_INITIALIZER {0}

typedef enum vc_menu_coordinator_result {
    VC_MENU_COORDINATOR_RESULT_OK = 0,
    VC_MENU_COORDINATOR_RESULT_NO_ACTION = 1,
    VC_MENU_COORDINATOR_RESULT_EXPIRED = 2,
    VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT = -1,
    VC_MENU_COORDINATOR_RESULT_NOT_INITIALIZED = -2,
    VC_MENU_COORDINATOR_RESULT_NOT_RUNNING = -3,
    VC_MENU_COORDINATOR_RESULT_BUSY = -4,
    VC_MENU_COORDINATOR_RESULT_INVALID_TARGET = -5,
    VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST = -6,
    VC_MENU_COORDINATOR_RESULT_OWNERSHIP_FAILED = -7,
    VC_MENU_COORDINATOR_RESULT_NO_AUTHORIZATION = -8,
    VC_MENU_COORDINATOR_RESULT_AUTHORIZATION_FAILED = -9,
    VC_MENU_COORDINATOR_RESULT_PAUSE_FAILED = -10,
    VC_MENU_COORDINATOR_RESULT_STALE = -11,
    VC_MENU_COORDINATOR_RESULT_WRONG_LEASE = -12,
    VC_MENU_COORDINATOR_RESULT_ACK_FAILED = -13,
    VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING = -14,
    VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK = -15,
    VC_MENU_COORDINATOR_RESULT_ID_EXHAUSTED = -16
} vc_menu_coordinator_result;

typedef enum vc_menu_coordinator_status {
    VC_MENU_COORDINATOR_STATUS_STOPPED = 0,
    VC_MENU_COORDINATOR_STATUS_READY = 1,
    VC_MENU_COORDINATOR_STATUS_VALIDATING = 2,
    VC_MENU_COORDINATOR_STATUS_SUSPENDING = 3,
    VC_MENU_COORDINATOR_STATUS_AWAITING_OPEN_ACK = 4,
    VC_MENU_COORDINATOR_STATUS_OPEN = 5,
    VC_MENU_COORDINATOR_STATUS_CLOSING = 6,
    VC_MENU_COORDINATOR_STATUS_RESUME_PENDING = 7,
    VC_MENU_COORDINATOR_STATUS_EXPIRED = 8,
    VC_MENU_COORDINATOR_STATUS_STALE = 9,
    VC_MENU_COORDINATOR_STATUS_ERROR = 10
} vc_menu_coordinator_status;

typedef enum vc_menu_coordinator_phase {
    VC_MENU_COORDINATOR_PHASE_UNINITIALIZED = 0,
    VC_MENU_COORDINATOR_PHASE_INITIALIZED = 1,
    VC_MENU_COORDINATOR_PHASE_RUNNING = 2,
    VC_MENU_COORDINATOR_PHASE_STOPPED = 3
} vc_menu_coordinator_phase;

typedef struct vc_menu_open_lease {
    uint64_t opaque[2];
} vc_menu_open_lease;

typedef struct vc_menu_protected_threads {
    vc_thread_id plugin_control_worker;
    vc_thread_id input_hook;
    vc_thread_id renderer_present_hook;
    vc_thread_id watchdog_worker;
    vc_thread_id cleanup_worker;
    vc_thread_id current_thread;
} vc_menu_protected_threads;

typedef struct vc_menu_thread_allowlist {
    uint32_t process_id;
    uint64_t process_generation;
    uint64_t module_generation;
    uint64_t revision;
    const vc_thread_id *thread_ids;
    size_t thread_count;
    vc_menu_protected_threads protected_threads;
} vc_menu_thread_allowlist;

/*
 * The adapter must report one coherent trusted launch observation plus the
 * revision of the explicit gameplay-thread allowlist source. This is not a
 * thread-discovery interface.
 */
typedef struct vc_menu_runtime_snapshot {
    vc_launch_claimant_observation observation;
    uint64_t allowlist_revision;
} vc_menu_runtime_snapshot;

typedef bool (*vc_menu_get_runtime_snapshot_fn)(
    void *context,
    vc_menu_runtime_snapshot *snapshot);

/*
 * Verifies that every supplied thread ID is currently owned by the exact
 * process generation. It must not infer ownership by name, priority, index, or
 * a hard-coded ID.
 */
typedef bool (*vc_menu_verify_thread_ownership_fn)(
    void *context,
    const vc_launch_claimant_identity_snapshot *target,
    const vc_thread_id *thread_ids,
    size_t thread_count);

typedef struct vc_menu_coordinator_dependencies {
    vc_menu_get_runtime_snapshot_fn get_runtime_snapshot;
    vc_menu_verify_thread_ownership_fn verify_thread_ownership;
    vc_pause_thread_operation suspend_thread;
    vc_pause_thread_operation resume_thread;
    void *context;
} vc_menu_coordinator_dependencies;

typedef struct vc_menu_coordinator_state {
    uint32_t phase;
    vc_menu_coordinator_status status;
    vc_menu_coordinator_result last_result;
    bool cleanup_pending;
    bool pause_owned;
    bool menu_authority_active;
} vc_menu_coordinator_state;

typedef struct vc_menu_coordinator {
    vc_menu_coordinator_dependencies dependencies;
    vc_launch_claimant *claimant;
    vc_launch_claimant_identity_snapshot target;
    vc_launch_open_authorization_snapshot authorization_snapshot;
    vc_launch_open_authorization consumed_authorization;
    vc_pause_state pause;
    vc_thread_id thread_ids[VC_PAUSE_MAX_THREADS];
    vc_menu_protected_threads protected_threads;
    vc_menu_open_lease lease;
    uint64_t allowlist_revision;
    uint64_t lifecycle_generation;
    uint64_t pause_generation;
    uint64_t lease_pause_generation;
    uint64_t lease_lifecycle_generation;
    uint64_t next_lease_id;
    uint64_t started_ms;
    uint64_t acknowledgement_deadline_ms;
    uint64_t deadline_ms;
    uint64_t last_now_ms;
    size_t thread_count;
    atomic_uint transaction_busy;
    atomic_uint adapter_active;
    atomic_uint status;
    atomic_int last_result;
    uint32_t phase;
    uint32_t marker;
    bool target_bound;
    bool allowlist_configured;
    bool has_time;
    bool menu_authority_active;
    bool acknowledged;
    bool clear_configuration_pending;
    uint32_t claimant_cleanup_action;
} vc_menu_coordinator;

/*
 * All mutations are serialized through a nonblocking gate. Adapter and
 * claimant calls execute outside that gate under adapter-active exclusion.
 * They must be bounded and non-reentrant; attempted reentry returns BUSY.
 */
vc_menu_coordinator_result vc_menu_coordinator_init(
    vc_menu_coordinator *coordinator,
    vc_launch_claimant *claimant,
    const vc_menu_coordinator_dependencies *dependencies);

vc_menu_coordinator_result vc_menu_coordinator_start(
    vc_menu_coordinator *coordinator);

vc_menu_coordinator_result vc_menu_coordinator_bind_target(
    vc_menu_coordinator *coordinator,
    const vc_launch_claimant_identity_snapshot *target);

vc_menu_coordinator_result vc_menu_coordinator_configure_allowlist(
    vc_menu_coordinator *coordinator,
    const vc_menu_thread_allowlist *allowlist);

/*
 * All local checks and ownership verification complete before the claimant
 * authorization is consumed. Consumption occurs exactly once immediately
 * before checked pause acquisition. Success returns a provisional lease that
 * is valid only for acknowledgement or close.
 */
vc_menu_coordinator_result vc_menu_coordinator_begin_open(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    uint64_t menu_duration_ms,
    uint64_t acknowledgement_timeout_ms,
    vc_menu_open_lease *lease);

/*
 * Acknowledgement consumes the provisional value and replaces it with the
 * menu-open lease. It never changes either deadline.
 */
vc_menu_coordinator_result vc_menu_coordinator_acknowledge_open(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    vc_menu_open_lease *lease);

vc_menu_coordinator_result vc_menu_coordinator_tick(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_close(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    vc_menu_open_lease *lease);

/* Refreshes trusted target/foreground state and cleans up on any mismatch. */
vc_menu_coordinator_result vc_menu_coordinator_target_changed(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_notify_overlay(
    vc_menu_coordinator *coordinator,
    const vc_launch_overlay_snapshot *snapshot,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_notify_presentation(
    vc_menu_coordinator *coordinator,
    const vc_launch_presentation_snapshot *snapshot,
    uint64_t now_ms);

/*
 * An exact exit notification is the only path that may abandon retained pause
 * ownership without resume.
 */
vc_menu_coordinator_result vc_menu_coordinator_target_exit(
    vc_menu_coordinator *coordinator,
    uint32_t process_id,
    uint64_t process_generation,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_claimant_reset(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_plugin_unload(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_service_stop(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_retry_cleanup(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_stop(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_reset(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_result vc_menu_coordinator_destroy(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms);

vc_menu_coordinator_status vc_menu_coordinator_get_status(
    const vc_menu_coordinator *coordinator);

vc_menu_coordinator_result vc_menu_coordinator_get_state(
    vc_menu_coordinator *coordinator,
    vc_menu_coordinator_state *state);

/*
 * Returns the complete text length excluding NUL. Output is always terminated
 * for a non-NULL, nonzero buffer and never includes identities, thread IDs,
 * generations, authorization values, lease values, or kernel/memory details.
 */
size_t vc_menu_coordinator_format_status(
    const vc_menu_coordinator *coordinator,
    char *buffer,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
