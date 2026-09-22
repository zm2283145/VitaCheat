#ifndef VITACHEAT_MEMORY_SERVICE_H
#define VITACHEAT_MEMORY_SERVICE_H

#include "vitacheat/memory_read.h"
#include "vitacheat/menu_coordinator.h"
#include "vitacheat/target_attestation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_MEMORY_SERVICE_DEFAULT_BYTE_BUDGET UINT64_C(4096)
#define VC_MEMORY_SERVICE_MAX_BYTE_BUDGET UINT64_C(65536)
#define VC_MEMORY_SERVICE_DEFAULT_OPERATION_BUDGET UINT32_C(32)
#define VC_MEMORY_SERVICE_MAX_OPERATION_BUDGET UINT32_C(256)
#define VC_MEMORY_SERVICE_MAX_DURATION_MS UINT64_C(60000)
#define VC_MEMORY_SERVICE_INITIALIZER {0}

typedef enum vc_memory_service_result {
    VC_MEMORY_SERVICE_RESULT_OK = 0,
    VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT = -1,
    VC_MEMORY_SERVICE_RESULT_INVALID_INPUT_SIZE = -2,
    VC_MEMORY_SERVICE_RESULT_OUTPUT_TOO_SMALL = -3,
    VC_MEMORY_SERVICE_RESULT_COPY_FROM_USER_FAILED = -4,
    VC_MEMORY_SERVICE_RESULT_COPY_TO_USER_FAILED = -5,
    VC_MEMORY_SERVICE_RESULT_UNAUTHENTICATED_CALLER = -6,
    VC_MEMORY_SERVICE_RESULT_UNAUTHORIZED_CALLER = -7,
    VC_MEMORY_SERVICE_RESULT_STALE_TRUSTED_STATE = -8,
    VC_MEMORY_SERVICE_RESULT_NOT_INITIALIZED = -9,
    VC_MEMORY_SERVICE_RESULT_STOPPED = -10,
    VC_MEMORY_SERVICE_RESULT_BUSY = -11,
    VC_MEMORY_SERVICE_RESULT_PLATFORM_FAILURE = -12,
    VC_MEMORY_SERVICE_RESULT_RESULT_PENDING = -13,
    VC_MEMORY_SERVICE_RESULT_ID_EXHAUSTED = -14,
    VC_MEMORY_SERVICE_RESULT_BOUNDED_ERROR = -15
} vc_memory_service_result;

typedef enum vc_memory_service_phase {
    VC_MEMORY_SERVICE_PHASE_UNINITIALIZED = 0,
    VC_MEMORY_SERVICE_PHASE_INITIALIZED = 1,
    VC_MEMORY_SERVICE_PHASE_RUNNING = 2,
    VC_MEMORY_SERVICE_PHASE_STOPPED = 3
} vc_memory_service_phase;

typedef struct vc_memory_target_read {
    uint32_t target_process_id;
    uint32_t address;
    uint64_t target_generation;
    uint64_t attestation_revision;
    uint32_t length;
    uint32_t reserved0;
} vc_memory_target_read;

typedef struct vc_memory_target_read_observation {
    uint64_t target_generation;
    uint64_t attestation_revision;
    size_t bytes_read;
} vc_memory_target_read_observation;

typedef bool (*vc_memory_copy_from_user_fn)(
    void *context,
    void *destination,
    const void *user_source,
    size_t size);

typedef bool (*vc_memory_copy_to_user_fn)(
    void *context,
    void *user_destination,
    const void *source,
    size_t size);

typedef vc_launch_attestation_result (*vc_memory_attest_caller_fn)(
    void *context,
    const void *caller_context,
    vc_launch_trusted_caller *caller);

typedef bool (*vc_memory_get_foreground_fn)(
    void *context,
    vc_launch_foreground_snapshot *snapshot);

/*
 * The adapter performs exactly one bounded read from the already validated
 * target address. It must report the exact observed target generation,
 * attestation revision, and byte count even when returning false.
 */
typedef bool (*vc_memory_read_target_fn)(
    void *context,
    const vc_memory_target_read *request,
    uint8_t *destination,
    size_t destination_capacity,
    vc_memory_target_read_observation *observation);

typedef bool (*vc_memory_platform_cleanup_fn)(void *context);

typedef struct vc_memory_service_dependencies {
    vc_memory_copy_from_user_fn copy_from_user;
    vc_memory_copy_to_user_fn copy_to_user;
    vc_memory_attest_caller_fn attest_caller;
    vc_memory_get_foreground_fn get_foreground;
    vc_memory_read_target_fn read_target;
    vc_memory_platform_cleanup_fn cleanup;
    void *context;
} vc_memory_service_dependencies;

typedef struct vc_memory_service_session {
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground;
    vc_menu_open_lineage lineage;
    uint64_t session_id;
    uint64_t service_lifecycle_generation;
    uint64_t target_attestation_revision;
    uint64_t target_attestation_lifecycle_generation;
    uint64_t deadline_ms;
    uint64_t last_request_id;
    uint64_t remaining_bytes;
    uint32_t remaining_operations;
    bool active;
} vc_memory_service_session;

typedef struct vc_memory_service_journal {
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground;
    vc_menu_open_lineage lineage;
    uint8_t request[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    uint8_t response[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    size_t response_size;
    uint64_t committed_now_ms;
    uint64_t service_lifecycle_generation;
    int32_t operation_status;
    bool authorization_bound;
    bool valid;
} vc_memory_service_journal;

typedef struct vc_memory_service {
    vc_memory_service_dependencies dependencies;
    vc_target_attestation *target_attestation;
    vc_menu_coordinator *menu_coordinator;
    vc_memory_service_session session;
    vc_memory_service_journal journal;
    atomic_uint transaction_busy;
    atomic_uint adapter_active;
    uint64_t next_session_id;
    uint64_t initial_session_id;
    uint64_t lifecycle_generation;
    uint64_t last_now_ms;
    uint32_t phase;
    uint32_t marker;
    bool has_time;
    bool session_id_exhausted;
} vc_memory_service;

vc_memory_service_result vc_memory_service_init(
    vc_memory_service *service,
    vc_target_attestation *target_attestation,
    vc_menu_coordinator *menu_coordinator,
    const vc_memory_service_dependencies *dependencies,
    uint64_t first_session_id);

vc_memory_service_result vc_memory_service_start(
    vc_memory_service *service,
    uint64_t now_ms);

vc_memory_service_result vc_memory_service_stop(
    vc_memory_service *service,
    uint64_t now_ms);

vc_memory_service_result vc_memory_service_reset(
    vc_memory_service *service,
    uint64_t now_ms);

vc_memory_service_result vc_memory_service_tick(
    vc_memory_service *service,
    uint64_t now_ms);

/*
 * This trusted control-plane operation is the only session-creation path. It
 * succeeds only while the coordinator proves an acknowledged, paused, active
 * menu-open lineage for the exact immutable target-attestation snapshot.
 */
vc_memory_service_result vc_memory_service_activate(
    vc_memory_service *service,
    uint64_t now_ms,
    uint64_t byte_budget,
    uint32_t operation_budget,
    uint64_t *session_id,
    vc_memory_read_status *authorization_status);

vc_memory_service_result vc_memory_service_menu_closed(
    vc_memory_service *service);

vc_memory_service_result vc_memory_service_target_changed(
    vc_memory_service *service);

vc_memory_service_result vc_memory_service_overlay_changed(
    vc_memory_service *service,
    vc_launch_overlay_state state);

vc_memory_service_result vc_memory_service_presentation_changed(
    vc_memory_service *service,
    vc_launch_presentation_state state);

vc_memory_service_result vc_memory_service_process_exit(
    vc_memory_service *service,
    uint32_t process_id,
    uint64_t process_generation);

vc_memory_service_result vc_memory_service_plugin_unload(
    vc_memory_service *service,
    uint32_t process_id,
    uint64_t process_generation,
    uint64_t module_generation);

/*
 * Callbacks execute outside the transaction gate under an adapter-active
 * exclusion. Reentry returns BUSY. Lifecycle BUSY results must be retried
 * before their trusted platform events are acknowledged.
 *
 * A copy-out fault journals the exact initialized response. Only the same
 * request bytes, caller, target, active lineage, and service lifecycle may
 * retry it. Unrelated requests return RESULT_PENDING. Any lifecycle
 * invalidation scrubs the journal and session.
 */
vc_memory_service_result vc_memory_service_dispatch(
    vc_memory_service *service,
    const void *caller_context,
    const void *request_user,
    size_t request_size,
    void *response_user,
    size_t response_capacity,
    uint64_t now_ms,
    size_t *response_size,
    vc_memory_read_status *operation_status);

size_t vc_memory_service_format_status(
    const vc_memory_service *service,
    char *buffer,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
