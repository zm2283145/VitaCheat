#ifndef VITACHEAT_QUICK_MENU_LAUNCHER_H
#define VITACHEAT_QUICK_MENU_LAUNCHER_H

#include "vitacheat/launch_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_QUICK_MENU_LAUNCHER_ADAPTER_API_VERSION UINT32_C(1)
#define VC_QUICK_MENU_LAUNCHER_INITIALIZER {0}

typedef enum vc_quick_menu_launcher_result {
    VC_QUICK_MENU_LAUNCHER_RESULT_OK = 0,
    VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION = 1,
    VC_QUICK_MENU_LAUNCHER_RESULT_RETRY = 2,
    VC_QUICK_MENU_LAUNCHER_RESULT_INVALID_ARGUMENT = -1,
    VC_QUICK_MENU_LAUNCHER_RESULT_NOT_INITIALIZED = -2,
    VC_QUICK_MENU_LAUNCHER_RESULT_NOT_RUNNING = -3,
    VC_QUICK_MENU_LAUNCHER_RESULT_BUSY = -4,
    VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE = -5,
    VC_QUICK_MENU_LAUNCHER_RESULT_INCOMPATIBLE = -6,
    VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE = -7,
    VC_QUICK_MENU_LAUNCHER_RESULT_TRANSPORT_FAILURE = -8,
    VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE = -9,
    VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET = -10,
    VC_QUICK_MENU_LAUNCHER_RESULT_CLOCK_ROLLBACK = -11,
    VC_QUICK_MENU_LAUNCHER_RESULT_CLEANUP_PENDING = -12
} vc_quick_menu_launcher_result;

typedef enum vc_quick_menu_launcher_status {
    VC_QUICK_MENU_LAUNCHER_STATUS_STOPPED = 0,
    VC_QUICK_MENU_LAUNCHER_STATUS_UNAVAILABLE = 1,
    VC_QUICK_MENU_LAUNCHER_STATUS_INCOMPATIBLE = 2,
    VC_QUICK_MENU_LAUNCHER_STATUS_READY = 3,
    VC_QUICK_MENU_LAUNCHER_STATUS_QUEUED = 4,
    VC_QUICK_MENU_LAUNCHER_STATUS_SUBMITTING = 5,
    VC_QUICK_MENU_LAUNCHER_STATUS_PENDING_CLAIM = 6,
    VC_QUICK_MENU_LAUNCHER_STATUS_EXPIRED = 7,
    VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE = 8,
    VC_QUICK_MENU_LAUNCHER_STATUS_CONSUMED = 9,
    VC_QUICK_MENU_LAUNCHER_STATUS_BUSY = 10,
    VC_QUICK_MENU_LAUNCHER_STATUS_ERROR = 11
} vc_quick_menu_launcher_status;

typedef enum vc_quick_menu_launcher_error {
    VC_QUICK_MENU_LAUNCHER_ERROR_NONE = 0,
    VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM = 1,
    VC_QUICK_MENU_LAUNCHER_ERROR_TRANSPORT = 2,
    VC_QUICK_MENU_LAUNCHER_ERROR_PROTOCOL = 3,
    VC_QUICK_MENU_LAUNCHER_ERROR_CLOCK = 4,
    VC_QUICK_MENU_LAUNCHER_ERROR_CLEANUP = 5
} vc_quick_menu_launcher_error;

typedef enum vc_quick_menu_transport_result {
    VC_QUICK_MENU_TRANSPORT_OK = 0,
    /* No service mutation occurred; rebuild with fresh trusted time. */
    VC_QUICK_MENU_TRANSPORT_BUSY = 1,
    /* A service result journal exists; retry the exact request bytes. */
    VC_QUICK_MENU_TRANSPORT_RETRY = 2,
    VC_QUICK_MENU_TRANSPORT_UNAVAILABLE = 3,
    VC_QUICK_MENU_TRANSPORT_FAILED = 4
} vc_quick_menu_transport_result;

typedef enum vc_quick_menu_launcher_phase {
    VC_QUICK_MENU_LAUNCHER_PHASE_UNINITIALIZED = 0,
    VC_QUICK_MENU_LAUNCHER_PHASE_INITIALIZED = 1,
    VC_QUICK_MENU_LAUNCHER_PHASE_STARTING = 2,
    VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING = 3,
    VC_QUICK_MENU_LAUNCHER_PHASE_STOPPING = 4,
    VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED = 5
} vc_quick_menu_launcher_phase;

typedef enum vc_quick_menu_launcher_action {
    VC_QUICK_MENU_LAUNCHER_ACTION_NONE = 0,
    VC_QUICK_MENU_LAUNCHER_ACTION_SUBMIT = 1,
    VC_QUICK_MENU_LAUNCHER_ACTION_STATUS = 2
} vc_quick_menu_launcher_action;

typedef void (*vc_quick_menu_button_callback_fn)(void *context,
                                                  uint64_t generation);
typedef void (*vc_quick_menu_worker_callback_fn)(void *context,
                                                  uint64_t generation);

typedef bool (*vc_quick_menu_get_api_version_fn)(void *context,
                                                  uint32_t *version);
typedef bool (*vc_quick_menu_register_texture_fn)(void *context,
                                                   uint64_t *token);
typedef bool (*vc_quick_menu_register_label_fn)(void *context,
                                                 uint64_t *token);
typedef bool (*vc_quick_menu_register_widget_fn)(void *context,
                                                  uint64_t texture_token,
                                                  uint64_t label_token,
                                                  uint64_t *token);
typedef bool (*vc_quick_menu_register_callback_fn)(
    void *context,
    uint64_t widget_token,
    vc_quick_menu_button_callback_fn callback,
    void *callback_context,
    uint64_t generation,
    uint64_t *token);
typedef bool (*vc_quick_menu_start_worker_fn)(
    void *context,
    vc_quick_menu_worker_callback_fn callback,
    void *callback_context,
    uint64_t generation,
    uint64_t *token);
typedef bool (*vc_quick_menu_update_status_fn)(
    void *context,
    uint64_t label_token,
    vc_quick_menu_launcher_status status);
typedef bool (*vc_quick_menu_signal_worker_fn)(void *context,
                                                uint64_t worker_token);
typedef bool (*vc_quick_menu_unregister_fn)(void *context, uint64_t token);
typedef bool (*vc_quick_menu_get_foreground_fn)(
    void *context,
    vc_launch_foreground_snapshot *snapshot);
typedef bool (*vc_quick_menu_get_time_fn)(void *context, uint64_t *now_ms);
typedef vc_quick_menu_transport_result (*vc_quick_menu_transport_fn)(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size);

typedef struct vc_quick_menu_launcher_dependencies {
    vc_quick_menu_get_api_version_fn get_api_version;
    /* Texture registration is optional for text-only adapters. */
    vc_quick_menu_register_texture_fn register_texture;
    vc_quick_menu_register_label_fn register_label;
    vc_quick_menu_register_widget_fn register_widget;
    vc_quick_menu_register_callback_fn register_callback;
    vc_quick_menu_start_worker_fn start_worker;
    vc_quick_menu_update_status_fn update_status;
    vc_quick_menu_signal_worker_fn signal_worker;
    vc_quick_menu_unregister_fn stop_worker;
    vc_quick_menu_unregister_fn unregister_callback;
    vc_quick_menu_unregister_fn unregister_widget;
    vc_quick_menu_unregister_fn unregister_label;
    /* Must be NULL when register_texture is NULL. */
    vc_quick_menu_unregister_fn unregister_texture;
    vc_quick_menu_get_foreground_fn get_foreground;
    vc_quick_menu_get_time_fn get_time;
    vc_quick_menu_transport_fn transport;
    void *context;
    uint32_t expected_api_version;
} vc_quick_menu_launcher_dependencies;

typedef struct vc_quick_menu_launcher {
    vc_quick_menu_launcher_dependencies dependencies;
    vc_launch_foreground_snapshot action_snapshot;
    vc_launch_foreground_snapshot request_snapshot;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint64_t request_id;
    uint64_t request_deadline_ms;
    uint64_t request_now_ms;
    uint64_t generation;
    uint64_t texture_token;
    uint64_t label_token;
    uint64_t widget_token;
    uint64_t callback_token;
    uint64_t worker_token;
    uint64_t last_now_ms;
    atomic_uint transaction_busy;
    atomic_uint adapter_active;
    atomic_uint status;
    uint32_t phase;
    uint32_t action;
    uint32_t last_error;
    uint32_t marker;
    bool work_pending;
    bool in_flight;
    bool request_wire_valid;
    bool has_time;
    bool callbacks_enabled;
} vc_quick_menu_launcher;

/*
 * The object must start as VC_QUICK_MENU_LAUNCHER_INITIALIZER. Adapter
 * callbacks may block or attempt reentry: the controller never invokes one
 * while holding its transaction gate, and every reentrant public mutation
 * returns BUSY. Adapter callbacks must not execute the supplied button or
 * worker callbacks inline. Every cleanup callback must have a bounded,
 * nonblocking implementation; the foreground and worker-signal callbacks used
 * by the button path must also be short and nonblocking.
 */
vc_quick_menu_launcher_result vc_quick_menu_launcher_init(
    vc_quick_menu_launcher *launcher,
    const vc_quick_menu_launcher_dependencies *dependencies);

vc_quick_menu_launcher_result vc_quick_menu_launcher_start(
    vc_quick_menu_launcher *launcher);

vc_quick_menu_launcher_result vc_quick_menu_launcher_handle_button(
    vc_quick_menu_launcher *launcher,
    uint64_t generation);

/*
 * One worker step performs at most one launch-service transport operation.
 * RETRY preserves the exact encoded request for service-journal recovery.
 */
vc_quick_menu_launcher_result vc_quick_menu_launcher_worker_step(
    vc_quick_menu_launcher *launcher,
    uint64_t generation);

vc_quick_menu_launcher_result vc_quick_menu_launcher_refresh_status(
    vc_quick_menu_launcher *launcher);

/* Clears controller-local action/request state; it grants no service action. */
vc_quick_menu_launcher_result vc_quick_menu_launcher_reset(
    vc_quick_menu_launcher *launcher);

/*
 * Stop revokes callbacks and local work before cleanup. Failed cleanup tokens
 * are retained for a later stop/destroy retry, while the phase stays STOPPED.
 */
vc_quick_menu_launcher_result vc_quick_menu_launcher_stop(
    vc_quick_menu_launcher *launcher);

vc_quick_menu_launcher_result vc_quick_menu_launcher_destroy(
    vc_quick_menu_launcher *launcher);

vc_quick_menu_launcher_status vc_quick_menu_launcher_get_status(
    const vc_quick_menu_launcher *launcher);

/*
 * Returns the complete text length excluding the terminator. When buffer is
 * non-NULL and capacity is nonzero, output is always NUL terminated. Text
 * never includes target, request, process, module, generation, or kernel
 * identity.
 */
size_t vc_quick_menu_launcher_format_status(
    const vc_quick_menu_launcher *launcher,
    char *buffer,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
