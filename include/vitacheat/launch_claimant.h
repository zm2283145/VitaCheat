#ifndef VITACHEAT_LAUNCH_CLAIMANT_H
#define VITACHEAT_LAUNCH_CLAIMANT_H

#include "vitacheat/launch_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_LAUNCH_CLAIMANT_DEFAULT_AUTHORIZATION_TTL_MS UINT64_C(1000)
#define VC_LAUNCH_CLAIMANT_MAX_AUTHORIZATION_TTL_MS UINT64_C(5000)
#define VC_LAUNCH_CLAIMANT_INITIALIZER {0}

typedef enum vc_launch_claimant_result {
    VC_LAUNCH_CLAIMANT_RESULT_OK = 0,
    VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION = 1,
    VC_LAUNCH_CLAIMANT_RESULT_RETRY = 2,
    VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT = -1,
    VC_LAUNCH_CLAIMANT_RESULT_NOT_INITIALIZED = -2,
    VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING = -3,
    VC_LAUNCH_CLAIMANT_RESULT_BUSY = -4,
    VC_LAUNCH_CLAIMANT_RESULT_UNAVAILABLE = -5,
    VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY = -6,
    VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION = -7,
    VC_LAUNCH_CLAIMANT_RESULT_TRANSPORT_FAILURE = -8,
    VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE = -9,
    VC_LAUNCH_CLAIMANT_RESULT_CLOCK_ROLLBACK = -10,
    VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION = -11,
    VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED = -12,
    VC_LAUNCH_CLAIMANT_RESULT_CLEANUP_FAILED = -13
} vc_launch_claimant_result;

typedef enum vc_launch_claimant_status {
    VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE = 0,
    VC_LAUNCH_CLAIMANT_STATUS_WAITING_REQUEST = 1,
    VC_LAUNCH_CLAIMANT_STATUS_WAITING_OVERLAY_CLOSE = 2,
    VC_LAUNCH_CLAIMANT_STATUS_WAITING_PRESENTATION = 3,
    VC_LAUNCH_CLAIMANT_STATUS_CLAIMING = 4,
    VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED = 5,
    VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED = 6,
    VC_LAUNCH_CLAIMANT_STATUS_OPEN = 7,
    VC_LAUNCH_CLAIMANT_STATUS_EXPIRED = 8,
    VC_LAUNCH_CLAIMANT_STATUS_STALE = 9,
    VC_LAUNCH_CLAIMANT_STATUS_CANCELLED = 10,
    VC_LAUNCH_CLAIMANT_STATUS_ERROR = 11
} vc_launch_claimant_status;

typedef enum vc_launch_overlay_state {
    VC_LAUNCH_OVERLAY_UNKNOWN = 0,
    VC_LAUNCH_OVERLAY_OPEN = 1,
    VC_LAUNCH_OVERLAY_CLOSING = 2,
    VC_LAUNCH_OVERLAY_CLOSED = 3
} vc_launch_overlay_state;

typedef enum vc_launch_presentation_state {
    VC_LAUNCH_PRESENTATION_UNAVAILABLE = 0,
    VC_LAUNCH_PRESENTATION_PROBING = 1,
    VC_LAUNCH_PRESENTATION_READY = 2,
    VC_LAUNCH_PRESENTATION_LOST = 3
} vc_launch_presentation_state;

typedef enum vc_launch_claimant_transport_result {
    VC_LAUNCH_CLAIMANT_TRANSPORT_OK = 0,
    /* No service mutation occurred; rebuild with fresh trusted time. */
    VC_LAUNCH_CLAIMANT_TRANSPORT_BUSY = 1,
    /* A service result journal exists; retry the exact request bytes. */
    VC_LAUNCH_CLAIMANT_TRANSPORT_RETRY = 2,
    VC_LAUNCH_CLAIMANT_TRANSPORT_UNAVAILABLE = 3,
    VC_LAUNCH_CLAIMANT_TRANSPORT_FAILED = 4
} vc_launch_claimant_transport_result;

typedef enum vc_launch_claimant_phase {
    VC_LAUNCH_CLAIMANT_PHASE_UNINITIALIZED = 0,
    VC_LAUNCH_CLAIMANT_PHASE_INITIALIZED = 1,
    VC_LAUNCH_CLAIMANT_PHASE_RUNNING = 2,
    VC_LAUNCH_CLAIMANT_PHASE_STOPPING = 3,
    VC_LAUNCH_CLAIMANT_PHASE_STOPPED = 4
} vc_launch_claimant_phase;

typedef enum vc_launch_claimant_action {
    VC_LAUNCH_CLAIMANT_ACTION_NONE = 0,
    VC_LAUNCH_CLAIMANT_ACTION_DISCOVER = 1,
    VC_LAUNCH_CLAIMANT_ACTION_CLAIM = 2,
    VC_LAUNCH_CLAIMANT_ACTION_CANCEL = 3
} vc_launch_claimant_action;

typedef struct vc_launch_claimant_identity_snapshot {
    uint64_t sequence;
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground;
    uint32_t title_id_size;
    uint8_t title_id[VC_LAUNCH_SERVICE_TITLE_ID_MAX];
} vc_launch_claimant_identity_snapshot;

typedef struct vc_launch_overlay_snapshot {
    uint64_t sequence;
    uint64_t generation;
    uint32_t state;
    uint32_t reserved0;
} vc_launch_overlay_snapshot;

typedef struct vc_launch_presentation_snapshot {
    uint64_t sequence;
    uint64_t process_generation;
    uint64_t module_generation;
    uint32_t process_id;
    uint32_t state;
} vc_launch_presentation_snapshot;

typedef struct vc_launch_claimant_observation {
    vc_launch_claimant_identity_snapshot identity;
    vc_launch_overlay_snapshot overlay;
    vc_launch_presentation_snapshot presentation;
} vc_launch_claimant_observation;

/*
 * The token is opaque to its consumer. Its values identify only controller-
 * local state and never contain the service request ID or target identity.
 */
typedef struct vc_launch_open_authorization {
    uint64_t value;
    uint64_t lifecycle_generation;
} vc_launch_open_authorization;

/*
 * A prospective owner may inspect this controller-local snapshot to validate
 * all of its own prerequisites before consuming the one-shot authorization.
 * Inspection does not reserve or consume authority; the later consumed token
 * must still byte-match authorization.
 */
typedef struct vc_launch_open_authorization_snapshot {
    vc_launch_claimant_observation observation;
    vc_launch_open_authorization authorization;
    uint64_t deadline_ms;
} vc_launch_open_authorization_snapshot;

/*
 * get_observation must atomically or coherently report authoritative injected
 * module identity, the matching foreground title, overlay state, and renderer
 * compatibility. Identity and title fields must come from trusted platform
 * metadata, never menu or cheat data.
 */
typedef bool (*vc_launch_claimant_get_observation_fn)(
    void *context,
    vc_launch_claimant_observation *observation);

typedef bool (*vc_launch_claimant_get_time_fn)(void *context,
                                                uint64_t *now_ms);

typedef vc_launch_claimant_transport_result
(*vc_launch_claimant_transport_fn)(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size);

/*
 * The native adapter maps this notification to the launch service's
 * plugin-unload lifecycle for the exact attested module instance. It must be
 * idempotent because an unload cleanup failure may be retried.
 */
typedef bool (*vc_launch_claimant_plugin_unload_fn)(
    void *context,
    const vc_launch_claimant_identity_snapshot *identity,
    uint64_t now_ms);

typedef struct vc_launch_claimant_dependencies {
    vc_launch_claimant_get_observation_fn get_observation;
    vc_launch_claimant_get_time_fn get_time;
    vc_launch_claimant_transport_fn transport;
    vc_launch_claimant_plugin_unload_fn plugin_unload;
    void *context;
} vc_launch_claimant_dependencies;

typedef struct vc_launch_claimant {
    vc_launch_claimant_dependencies dependencies;
    vc_launch_claimant_observation observation;
    vc_launch_claimant_observation action_observation;
    vc_launch_claimant_observation authorization_observation;
    vc_launch_claimant_identity_snapshot unload_identity;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint64_t request_id;
    uint64_t request_deadline_ms;
    uint64_t request_now_ms;
    uint64_t authorization_deadline_ms;
    uint64_t authorization_id;
    uint64_t next_authorization_id;
    uint64_t lifecycle_generation;
    uint64_t action_lifecycle_generation;
    uint64_t first_closed_sequence;
    uint64_t last_now_ms;
    uint64_t unload_now_ms;
    uint64_t authorization_ttl_ms;
    atomic_uint transaction_busy;
    atomic_uint adapter_active;
    atomic_uint status;
    uint32_t phase;
    uint32_t action;
    uint32_t marker;
    bool identity_bound;
    bool overlay_closed_stable;
    bool claim_requested;
    bool cancel_requested;
    bool request_wire_valid;
    bool in_flight;
    bool server_claimed;
    bool authorization_available;
    bool authorization_consumed;
    bool menu_open;
    bool has_time;
    bool unload_pending;
} vc_launch_claimant;

/*
 * All public mutations share one nonblocking serialization gate. Adapter
 * callbacks execute outside that gate but under an adapter-active exclusion;
 * reentrant calls return BUSY. Adapter lifecycle callbacks must be bounded and
 * must retry a BUSY observation notification before acknowledging the event.
 */
vc_launch_claimant_result vc_launch_claimant_init(
    vc_launch_claimant *claimant,
    const vc_launch_claimant_dependencies *dependencies,
    uint64_t authorization_ttl_ms);

vc_launch_claimant_result vc_launch_claimant_start(
    vc_launch_claimant *claimant);

vc_launch_claimant_result vc_launch_claimant_bind_identity(
    vc_launch_claimant *claimant);

/* Refreshes trusted observations without service transport. */
vc_launch_claimant_result vc_launch_claimant_observe(
    vc_launch_claimant *claimant);

/* Refreshes observations/time and enforces request/authorization deadlines. */
vc_launch_claimant_result vc_launch_claimant_tick(
    vc_launch_claimant *claimant);

/*
 * These short callback-facing entry points update only trusted observations.
 * CLOSED becomes stable only after a second, strictly newer CLOSED sequence in
 * the same overlay generation.
 */
vc_launch_claimant_result vc_launch_claimant_notify_overlay(
    vc_launch_claimant *claimant,
    const vc_launch_overlay_snapshot *snapshot);

vc_launch_claimant_result vc_launch_claimant_notify_presentation(
    vc_launch_claimant *claimant,
    const vc_launch_presentation_snapshot *snapshot);

/* Marks bounded worker work; it never performs transport. */
vc_launch_claimant_result vc_launch_claimant_request_claim(
    vc_launch_claimant *claimant);

/*
 * One step performs at most one STATUS, CLAIM, or CANCEL transport operation.
 * A journal RETRY preserves exact bytes; BUSY rebuilds with fresh trusted time.
 */
vc_launch_claimant_result vc_launch_claimant_worker_step(
    vc_launch_claimant *claimant);

vc_launch_claimant_result vc_launch_claimant_inspect_open_authorization(
    vc_launch_claimant *claimant,
    vc_launch_open_authorization_snapshot *snapshot);

vc_launch_claimant_result vc_launch_claimant_consume_open_authorization(
    vc_launch_claimant *claimant,
    vc_launch_open_authorization *authorization);

/*
 * Coherently validates the exact consumed or acknowledged authorization
 * lineage without invoking adapters. Only AUTHORIZATION_CONSUMED and OPEN are
 * accepted as expected_status values.
 */
vc_launch_claimant_result vc_launch_claimant_validate_open_authorization(
    vc_launch_claimant *claimant,
    const vc_launch_open_authorization *authorization,
    vc_launch_claimant_status expected_status);

vc_launch_claimant_result vc_launch_claimant_acknowledge_open(
    vc_launch_claimant *claimant,
    const vc_launch_open_authorization *authorization);

vc_launch_claimant_result vc_launch_claimant_cancel(
    vc_launch_claimant *claimant);

vc_launch_claimant_result vc_launch_claimant_close(
    vc_launch_claimant *claimant);

vc_launch_claimant_result vc_launch_claimant_plugin_unload(
    vc_launch_claimant *claimant);

vc_launch_claimant_result vc_launch_claimant_stop(
    vc_launch_claimant *claimant);

vc_launch_claimant_result vc_launch_claimant_reset(
    vc_launch_claimant *claimant);

vc_launch_claimant_result vc_launch_claimant_destroy(
    vc_launch_claimant *claimant);

vc_launch_claimant_status vc_launch_claimant_get_status(
    const vc_launch_claimant *claimant);

/*
 * Returns the full text length excluding NUL. Output is always terminated when
 * buffer and capacity are nonzero, and never includes identity or request data.
 */
size_t vc_launch_claimant_format_status(
    const vc_launch_claimant *claimant,
    char *buffer,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
