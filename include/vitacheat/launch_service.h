#ifndef VITACHEAT_LAUNCH_SERVICE_H
#define VITACHEAT_LAUNCH_SERVICE_H

#include "vitacheat/launch_broker.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_LAUNCH_SERVICE_TITLE_ID_MAX UINT32_C(16)
#define VC_LAUNCH_SERVICE_V1_CAPABILITIES VC_LAUNCH_CAPABILITY_ALL
#define VC_LAUNCH_SERVICE_INITIALIZER {0}

typedef enum vc_launch_service_status {
    VC_LAUNCH_SERVICE_STATUS_OK = 0,
    VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT = -1,
    VC_LAUNCH_SERVICE_STATUS_INVALID_INPUT_SIZE = -2,
    VC_LAUNCH_SERVICE_STATUS_OUTPUT_TOO_SMALL = -3,
    VC_LAUNCH_SERVICE_STATUS_COPY_FROM_USER_FAILED = -4,
    VC_LAUNCH_SERVICE_STATUS_COPY_TO_USER_FAILED = -5,
    VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER = -6,
    VC_LAUNCH_SERVICE_STATUS_UNAUTHORIZED_CALLER = -7,
    VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT = -8,
    VC_LAUNCH_SERVICE_STATUS_NOT_INITIALIZED = -9,
    VC_LAUNCH_SERVICE_STATUS_STOPPED = -10,
    VC_LAUNCH_SERVICE_STATUS_BUSY = -11,
    VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE = -12,
    VC_LAUNCH_SERVICE_STATUS_RESULT_PENDING = -13
} vc_launch_service_status;

typedef enum vc_launch_attestation_result {
    VC_LAUNCH_ATTESTATION_ACCEPTED = 0,
    VC_LAUNCH_ATTESTATION_UNKNOWN = 1,
    VC_LAUNCH_ATTESTATION_FAILED = 2
} vc_launch_attestation_result;

typedef enum vc_launch_service_phase {
    VC_LAUNCH_SERVICE_PHASE_UNINITIALIZED = 0,
    VC_LAUNCH_SERVICE_PHASE_INITIALIZED = 1,
    VC_LAUNCH_SERVICE_PHASE_RUNNING = 2,
    VC_LAUNCH_SERVICE_PHASE_STOPPED = 3
} vc_launch_service_phase;

/*
 * This identity is produced only by a privileged adapter. module_generation is
 * an opaque, nonzero loader/module-instance identity; it is not a name or a
 * caller-provided hash.
 */
typedef struct vc_launch_trusted_caller {
    uint16_t role;
    uint16_t reserved0;
    uint32_t process_id;
    uint64_t process_generation;
    uint64_t module_generation;
} vc_launch_trusted_caller;

/*
 * sequence is a nonzero, monotonically increasing adapter observation number.
 * A present target has a PID, nonzero process generation, and a bounded opaque
 * title identifier. An absent target has all identity fields zero.
 */
typedef struct vc_launch_foreground_snapshot {
    uint64_t sequence;
    uint64_t target_generation;
    uint32_t target_process_id;
    uint32_t title_id_size;
    uint32_t present;
    uint8_t title_id[VC_LAUNCH_SERVICE_TITLE_ID_MAX];
} vc_launch_foreground_snapshot;

/*
 * Copy callbacks must implement exact all-or-failure transfers. Returning false
 * after a partial transfer is permitted and is treated as a copy fault.
 */
typedef bool (*vc_launch_copy_from_user_fn)(void *context,
                                           void *destination,
                                           const void *user_source,
                                           size_t size);

typedef bool (*vc_launch_copy_to_user_fn)(void *context,
                                         void *user_destination,
                                         const void *source,
                                         size_t size);

/*
 * The attestation callback must derive role, process generation, and module
 * generation from privileged process/loader identity, never from wire fields
 * or a process/module name alone.
 */
typedef vc_launch_attestation_result (*vc_launch_attest_caller_fn)(
    void *context,
    const void *caller_context,
    vc_launch_trusted_caller *caller);

typedef bool (*vc_launch_get_foreground_fn)(
    void *context,
    vc_launch_foreground_snapshot *snapshot);

typedef bool (*vc_launch_platform_cleanup_fn)(void *context);

typedef struct vc_launch_service_dependencies {
    vc_launch_copy_from_user_fn copy_from_user;
    vc_launch_copy_to_user_fn copy_to_user;
    vc_launch_attest_caller_fn attest_caller;
    vc_launch_get_foreground_fn get_foreground;
    vc_launch_platform_cleanup_fn cleanup;
    void *context;
} vc_launch_service_dependencies;

typedef struct vc_launch_service_result_journal {
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground;
    uint8_t request[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint64_t committed_now_ms;
    int32_t operation_status;
    bool foreground_bound;
    bool valid;
} vc_launch_service_result_journal;

typedef struct vc_launch_service {
    vc_launch_service_dependencies dependencies;
    vc_launch_broker broker;
    vc_launch_foreground_snapshot foreground;
    vc_launch_service_result_journal journal;
    atomic_uint transaction_busy;
    uint64_t initial_request_id;
    uint32_t phase;
    uint32_t marker;
} vc_launch_service;

/*
 * The service object must start as VC_LAUNCH_SERVICE_INITIALIZER. Repeating
 * init with the identical dependency set and request-ID seed is idempotent.
 * Initialization is a construction operation and must not race another call.
 */
vc_launch_service_status vc_launch_service_init(
    vc_launch_service *service,
    const vc_launch_service_dependencies *dependencies,
    uint64_t first_request_id);

vc_launch_service_status vc_launch_service_start(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status);

/*
 * stop revokes and scrubs broker authority before invoking optional platform
 * cleanup. Cleanup failure is returned after the service is already stopped.
 */
vc_launch_service_status vc_launch_service_stop(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status);

vc_launch_service_status vc_launch_service_reset(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status);

vc_launch_service_status vc_launch_service_tick(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status);

vc_launch_service_status vc_launch_service_foreground_changed(
    vc_launch_service *service,
    const vc_launch_foreground_snapshot *snapshot,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status);

vc_launch_service_status vc_launch_service_process_exit(
    vc_launch_service *service,
    uint32_t target_process_id,
    uint64_t target_generation,
    uint64_t snapshot_sequence,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status);

vc_launch_service_status vc_launch_service_plugin_unload(
    vc_launch_service *service,
    uint32_t target_process_id,
    uint64_t target_generation,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status);

/*
 * All callbacks run while the service owns a nonblocking transaction
 * reservation, but never while an internal mutex is held. Callbacks must not
 * reenter the service; attempted reentry returns BUSY. Concurrent lifecycle
 * events that receive BUSY must be retried before the platform acknowledges
 * the event.
 *
 * request_size must be exactly VC_LAUNCH_REQUEST_WIRE_SIZE. response_capacity
 * may be larger than VC_LAUNCH_RESPONSE_WIRE_SIZE, but exactly that many bytes
 * are initialized and copied out. Input and output user ranges may alias.
 *
 * A copy-out failure journals the exact initialized response after dispatch.
 * Until an exact request from the same attested caller and target retrieves
 * that result, unrelated dispatch/tick calls return RESULT_PENDING. Target
 * lifecycle changes, reset, and stop revoke the journal.
 */
vc_launch_service_status vc_launch_service_dispatch(
    vc_launch_service *service,
    const void *caller_context,
    const void *request_user,
    size_t request_size,
    void *response_user,
    size_t response_capacity,
    uint64_t now_ms,
    size_t *response_size,
    vc_launch_status *operation_status);

#ifdef __cplusplus
}
#endif

#endif
