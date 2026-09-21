#include "vitacheat/quick_menu_launcher.h"

#include <string.h>

enum {
    VC_QUICK_MENU_LAUNCHER_MARKER = 0x5643514du
};

_Static_assert(VC_LAUNCH_ABI_VERSION == UINT16_C(1),
               "Quick Menu launcher supports only launch ABI v1");
_Static_assert(VC_LAUNCH_REQUEST_WIRE_SIZE == UINT16_C(64),
               "Quick Menu launcher request size changed");
_Static_assert(VC_LAUNCH_RESPONSE_WIRE_SIZE == UINT16_C(72),
               "Quick Menu launcher response size changed");

static bool vc_quick_menu_dependencies_valid(
    const vc_quick_menu_launcher_dependencies *dependencies)
{
    return dependencies != NULL &&
           dependencies->get_api_version != NULL &&
           ((dependencies->register_texture == NULL &&
             dependencies->unregister_texture == NULL) ||
            (dependencies->register_texture != NULL &&
             dependencies->unregister_texture != NULL)) &&
           dependencies->register_label != NULL &&
           dependencies->register_widget != NULL &&
           dependencies->register_callback != NULL &&
           dependencies->start_worker != NULL &&
           dependencies->update_status != NULL &&
           dependencies->signal_worker != NULL &&
           dependencies->stop_worker != NULL &&
           dependencies->unregister_callback != NULL &&
           dependencies->unregister_widget != NULL &&
           dependencies->unregister_label != NULL &&
           dependencies->get_foreground != NULL &&
           dependencies->get_time != NULL &&
           dependencies->transport != NULL &&
           dependencies->expected_api_version != 0;
}

static bool vc_quick_menu_launcher_valid(
    const vc_quick_menu_launcher *launcher)
{
    return launcher != NULL &&
           launcher->marker == (uint32_t)VC_QUICK_MENU_LAUNCHER_MARKER;
}

static vc_quick_menu_launcher_result vc_quick_menu_enter(
    vc_quick_menu_launcher *launcher)
{
    if (!vc_quick_menu_launcher_valid(launcher)) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_NOT_INITIALIZED;
    }
    if (atomic_load_explicit(&launcher->adapter_active,
                             memory_order_acquire) != 0u) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_BUSY;
    }
    if (atomic_exchange_explicit(&launcher->transaction_busy, 1u,
                                 memory_order_acquire) != 0u) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_BUSY;
    }
    if (atomic_load_explicit(&launcher->adapter_active,
                             memory_order_acquire) != 0u) {
        atomic_store_explicit(&launcher->transaction_busy, 0u,
                              memory_order_release);
        return VC_QUICK_MENU_LAUNCHER_RESULT_BUSY;
    }
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

static void vc_quick_menu_leave(vc_quick_menu_launcher *launcher)
{
    atomic_store_explicit(&launcher->transaction_busy, 0u,
                          memory_order_release);
}

static void vc_quick_menu_adapter_begin(
    vc_quick_menu_launcher *launcher)
{
    atomic_store_explicit(&launcher->adapter_active, 1u,
                          memory_order_release);
    atomic_store_explicit(&launcher->transaction_busy, 0u,
                          memory_order_release);
}

static void vc_quick_menu_adapter_end(
    vc_quick_menu_launcher *launcher)
{
    (void)atomic_exchange_explicit(&launcher->transaction_busy, 1u,
                                   memory_order_acquire);
    atomic_store_explicit(&launcher->adapter_active, 0u,
                          memory_order_release);
}

static void vc_quick_menu_set_status(
    vc_quick_menu_launcher *launcher,
    vc_quick_menu_launcher_status status)
{
    atomic_store_explicit(&launcher->status, (unsigned int)status,
                          memory_order_release);
}

static bool vc_quick_menu_next_generation(
    vc_quick_menu_launcher *launcher)
{
    if (launcher->generation == UINT64_MAX) {
        return false;
    }
    ++launcher->generation;
    if (launcher->generation == 0) {
        return false;
    }
    return true;
}

static void vc_quick_menu_clear_action(
    vc_quick_menu_launcher *launcher)
{
    memset(&launcher->action_snapshot, 0,
           sizeof(launcher->action_snapshot));
    memset(launcher->request_wire, 0,
           sizeof(launcher->request_wire));
    launcher->request_now_ms = 0;
    launcher->action = VC_QUICK_MENU_LAUNCHER_ACTION_NONE;
    launcher->work_pending = false;
    launcher->in_flight = false;
    launcher->request_wire_valid = false;
}

static void vc_quick_menu_clear_request(
    vc_quick_menu_launcher *launcher)
{
    vc_quick_menu_clear_action(launcher);
    memset(&launcher->request_snapshot, 0,
           sizeof(launcher->request_snapshot));
    launcher->request_id = 0;
    launcher->request_deadline_ms = 0;
}

static bool vc_quick_menu_bytes_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static vc_quick_menu_launcher_result vc_quick_menu_normalize_snapshot(
    const vc_launch_foreground_snapshot *snapshot,
    vc_launch_foreground_snapshot *normalized)
{
    if (snapshot == NULL || normalized == NULL ||
        snapshot->sequence == 0 ||
        snapshot->present > 1u ||
        snapshot->title_id_size > VC_LAUNCH_SERVICE_TITLE_ID_MAX) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->sequence = snapshot->sequence;
    normalized->present = snapshot->present;
    if (snapshot->present == 0u) {
        if (snapshot->target_process_id != 0 ||
            snapshot->target_generation != 0 ||
            snapshot->title_id_size != 0 ||
            !vc_quick_menu_bytes_zero(
                snapshot->title_id, sizeof(snapshot->title_id))) {
            return VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET;
        }
        return VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE;
    }
    if (snapshot->target_process_id == 0 ||
        snapshot->target_generation == 0 ||
        snapshot->title_id_size == 0) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET;
    }

    normalized->target_process_id = snapshot->target_process_id;
    normalized->target_generation = snapshot->target_generation;
    normalized->title_id_size = snapshot->title_id_size;
    memcpy(normalized->title_id, snapshot->title_id,
           snapshot->title_id_size);
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

static bool vc_quick_menu_snapshot_equal(
    const vc_launch_foreground_snapshot *left,
    const vc_launch_foreground_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->present == right->present &&
           left->target_process_id == right->target_process_id &&
           left->target_generation == right->target_generation &&
           left->title_id_size == right->title_id_size &&
           memcmp(left->title_id, right->title_id,
                  sizeof(left->title_id)) == 0;
}

static vc_quick_menu_launcher_result vc_quick_menu_capture_snapshot(
    vc_quick_menu_launcher *launcher,
    vc_launch_foreground_snapshot *snapshot)
{
    vc_launch_foreground_snapshot raw;
    bool success;

    memset(&raw, 0, sizeof(raw));
    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.get_foreground(
        launcher->dependencies.context, &raw);
    vc_quick_menu_adapter_end(launcher);
    if (!success) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE;
    }
    return vc_quick_menu_normalize_snapshot(&raw, snapshot);
}

static vc_quick_menu_launcher_result vc_quick_menu_capture_time(
    vc_quick_menu_launcher *launcher,
    uint64_t *now_ms)
{
    bool success;

    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.get_time(
        launcher->dependencies.context, now_ms);
    vc_quick_menu_adapter_end(launcher);
    if (!success) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    }
    if (launcher->has_time && *now_ms < launcher->last_now_ms) {
        launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_CLOCK;
        return VC_QUICK_MENU_LAUNCHER_RESULT_CLOCK_ROLLBACK;
    }
    launcher->has_time = true;
    launcher->last_now_ms = *now_ms;
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

static bool vc_quick_menu_publish_status(
    vc_quick_menu_launcher *launcher,
    vc_quick_menu_launcher_status status)
{
    bool success;

    vc_quick_menu_set_status(launcher, status);
    if (launcher->label_token == 0) {
        return true;
    }
    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.update_status(
        launcher->dependencies.context, launcher->label_token, status);
    vc_quick_menu_adapter_end(launcher);
    return success;
}

static bool vc_quick_menu_register_texture(
    vc_quick_menu_launcher *launcher)
{
    uint64_t token = 0;
    bool success;

    if (launcher->dependencies.register_texture == NULL) {
        return true;
    }
    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.register_texture(
        launcher->dependencies.context, &token);
    vc_quick_menu_adapter_end(launcher);
    if (!success || token == 0) {
        return false;
    }
    launcher->texture_token = token;
    return true;
}

static bool vc_quick_menu_register_label(
    vc_quick_menu_launcher *launcher)
{
    uint64_t token = 0;
    bool success;

    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.register_label(
        launcher->dependencies.context, &token);
    vc_quick_menu_adapter_end(launcher);
    if (!success || token == 0) {
        return false;
    }
    launcher->label_token = token;
    return true;
}

static bool vc_quick_menu_register_widget(
    vc_quick_menu_launcher *launcher)
{
    uint64_t token = 0;
    bool success;

    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.register_widget(
        launcher->dependencies.context,
        launcher->texture_token, launcher->label_token, &token);
    vc_quick_menu_adapter_end(launcher);
    if (!success || token == 0) {
        return false;
    }
    launcher->widget_token = token;
    return true;
}

static void vc_quick_menu_button_bridge(void *context,
                                        uint64_t generation)
{
    (void)vc_quick_menu_launcher_handle_button(
        (vc_quick_menu_launcher *)context, generation);
}

static void vc_quick_menu_worker_bridge(void *context,
                                        uint64_t generation)
{
    (void)vc_quick_menu_launcher_worker_step(
        (vc_quick_menu_launcher *)context, generation);
}

static bool vc_quick_menu_register_callback(
    vc_quick_menu_launcher *launcher)
{
    uint64_t token = 0;
    bool success;

    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.register_callback(
        launcher->dependencies.context,
        launcher->widget_token,
        vc_quick_menu_button_bridge,
        launcher,
        launcher->generation,
        &token);
    vc_quick_menu_adapter_end(launcher);
    if (!success || token == 0) {
        return false;
    }
    launcher->callback_token = token;
    return true;
}

static bool vc_quick_menu_start_worker(
    vc_quick_menu_launcher *launcher)
{
    uint64_t token = 0;
    bool success;

    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.start_worker(
        launcher->dependencies.context,
        vc_quick_menu_worker_bridge,
        launcher,
        launcher->generation,
        &token);
    vc_quick_menu_adapter_end(launcher);
    if (!success || token == 0) {
        return false;
    }
    launcher->worker_token = token;
    return true;
}

static bool vc_quick_menu_cleanup_token(
    vc_quick_menu_launcher *launcher,
    vc_quick_menu_unregister_fn cleanup,
    uint64_t *token)
{
    bool success;

    if (*token == 0) {
        return true;
    }
    vc_quick_menu_adapter_begin(launcher);
    success = cleanup(launcher->dependencies.context, *token);
    vc_quick_menu_adapter_end(launcher);
    if (success) {
        *token = 0;
    }
    return success;
}

static bool vc_quick_menu_cleanup_resources(
    vc_quick_menu_launcher *launcher)
{
    bool success = true;

    if (!vc_quick_menu_cleanup_token(
            launcher, launcher->dependencies.stop_worker,
            &launcher->worker_token)) {
        success = false;
    }
    if (!vc_quick_menu_cleanup_token(
            launcher, launcher->dependencies.unregister_callback,
            &launcher->callback_token)) {
        success = false;
    }
    if (!vc_quick_menu_cleanup_token(
            launcher, launcher->dependencies.unregister_widget,
            &launcher->widget_token)) {
        success = false;
    }
    if (!vc_quick_menu_cleanup_token(
            launcher, launcher->dependencies.unregister_label,
            &launcher->label_token)) {
        success = false;
    }
    if (!vc_quick_menu_cleanup_token(
            launcher,
            launcher->dependencies.unregister_texture,
            &launcher->texture_token)) {
        success = false;
    }
    return success;
}

static bool vc_quick_menu_has_resources(
    const vc_quick_menu_launcher *launcher)
{
    return launcher->worker_token != 0 ||
           launcher->callback_token != 0 ||
           launcher->widget_token != 0 ||
           launcher->label_token != 0 ||
           launcher->texture_token != 0;
}

static vc_quick_menu_launcher_result vc_quick_menu_fail_action(
    vc_quick_menu_launcher *launcher,
    vc_quick_menu_launcher_result result,
    vc_quick_menu_launcher_status status,
    vc_quick_menu_launcher_error error)
{
    vc_quick_menu_clear_request(launcher);
    launcher->last_error = (uint32_t)error;
    vc_quick_menu_set_status(launcher, status);
    return result;
}

static vc_quick_menu_launcher_result vc_quick_menu_signal_action(
    vc_quick_menu_launcher *launcher)
{
    vc_launch_foreground_snapshot after_signal;
    bool success;
    vc_quick_menu_launcher_result result;

    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.signal_worker(
        launcher->dependencies.context, launcher->worker_token);
    vc_quick_menu_adapter_end(launcher);
    if (!success) {
        return vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE,
            VC_QUICK_MENU_LAUNCHER_STATUS_ERROR,
            VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM);
    }

    result = vc_quick_menu_capture_snapshot(launcher, &after_signal);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK ||
        !vc_quick_menu_snapshot_equal(
            &launcher->action_snapshot, &after_signal)) {
        return vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET,
            VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE,
            VC_QUICK_MENU_LAUNCHER_ERROR_NONE);
    }
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

static vc_quick_menu_launcher_result vc_quick_menu_build_request(
    vc_quick_menu_launcher *launcher)
{
    vc_launch_foreground_snapshot after_time;
    vc_launch_request request;
    vc_launch_status encode_status;
    vc_quick_menu_launcher_result result;
    uint64_t now_ms = 0;
    size_t encoded_size = 0;

    result = vc_quick_menu_capture_time(launcher, &now_ms);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    result = vc_quick_menu_capture_snapshot(launcher, &after_time);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK ||
        !vc_quick_menu_snapshot_equal(
            &launcher->action_snapshot, &after_time)) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET;
    }

    vc_launch_request_init(
        &request,
        launcher->action == VC_QUICK_MENU_LAUNCHER_ACTION_SUBMIT
            ? VC_LAUNCH_OPERATION_SUBMIT
            : VC_LAUNCH_OPERATION_STATUS,
        VC_LAUNCH_CALLER_SCE_SHELL);
    request.target_process_id =
        launcher->action_snapshot.target_process_id;
    request.target_generation =
        launcher->action_snapshot.target_generation;
    request.now_ms = now_ms;
    if (launcher->action == VC_QUICK_MENU_LAUNCHER_ACTION_SUBMIT) {
        request.capabilities = VC_LAUNCH_CAPABILITY_LAUNCH;
        request.ttl_ms = VC_LAUNCH_DEFAULT_TTL_MS;
    } else {
        request.capabilities = VC_LAUNCH_CAPABILITY_STATUS;
        request.request_id = launcher->request_id;
    }

    encode_status = vc_launch_request_encode(
        &request, launcher->request_wire,
        sizeof(launcher->request_wire), &encoded_size);
    if (encode_status != VC_LAUNCH_STATUS_OK ||
        encoded_size != sizeof(launcher->request_wire)) {
        memset(launcher->request_wire, 0,
               sizeof(launcher->request_wire));
        return VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE;
    }
    launcher->request_now_ms = now_ms;
    launcher->request_wire_valid = true;
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

static bool vc_quick_menu_response_status_matches(
    const vc_launch_response *response)
{
    switch ((vc_launch_state)response->launch_state) {
    case VC_LAUNCH_STATE_ABSENT:
        return response->status == VC_LAUNCH_STATUS_ABSENT;
    case VC_LAUNCH_STATE_PENDING:
        return response->status == VC_LAUNCH_STATUS_PENDING;
    case VC_LAUNCH_STATE_CLAIMED:
        return response->status == VC_LAUNCH_STATUS_CLAIMED;
    case VC_LAUNCH_STATE_EXPIRED:
        return response->status == VC_LAUNCH_STATUS_EXPIRED;
    case VC_LAUNCH_STATE_CANCELLED:
        return response->status == VC_LAUNCH_STATUS_CANCELLED;
    case VC_LAUNCH_STATE_STALE_TARGET:
        return response->status == VC_LAUNCH_STATUS_STALE_TARGET;
    case VC_LAUNCH_STATE_CLOCK_ROLLBACK:
        return response->status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK;
    default:
        return false;
    }
}

static vc_quick_menu_launcher_result vc_quick_menu_accept_response(
    vc_quick_menu_launcher *launcher,
    const uint8_t *response_wire,
    size_t response_size)
{
    vc_launch_response response;
    const uint16_t expected_operation =
        launcher->action == VC_QUICK_MENU_LAUNCHER_ACTION_SUBMIT
            ? (uint16_t)VC_LAUNCH_OPERATION_SUBMIT
            : (uint16_t)VC_LAUNCH_OPERATION_STATUS;
    const uint32_t expected_capability =
        launcher->action == VC_QUICK_MENU_LAUNCHER_ACTION_SUBMIT
            ? VC_LAUNCH_CAPABILITY_LAUNCH
            : VC_LAUNCH_CAPABILITY_STATUS;

    if (response_size != VC_LAUNCH_RESPONSE_WIRE_SIZE ||
        vc_launch_response_decode(
            response_wire, response_size, &response) !=
            VC_LAUNCH_STATUS_OK ||
        response.operation != expected_operation ||
        response.capabilities != expected_capability ||
        response.target_process_id !=
            launcher->action_snapshot.target_process_id ||
        response.target_generation !=
            launcher->action_snapshot.target_generation ||
        response.observed_ms != launcher->request_now_ms ||
        !vc_quick_menu_response_status_matches(&response)) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE;
    }

    if (launcher->action == VC_QUICK_MENU_LAUNCHER_ACTION_SUBMIT) {
        if (response.launch_state != VC_LAUNCH_STATE_PENDING ||
            response.status != VC_LAUNCH_STATUS_PENDING ||
            response.request_id == 0) {
            return VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE;
        }
        launcher->request_id = response.request_id;
        launcher->request_deadline_ms = response.deadline_ms;
        launcher->request_snapshot = launcher->action_snapshot;
        vc_quick_menu_clear_action(launcher);
        launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_NONE;
        return vc_quick_menu_publish_status(
                   launcher,
                   VC_QUICK_MENU_LAUNCHER_STATUS_PENDING_CLAIM)
                   ? VC_QUICK_MENU_LAUNCHER_RESULT_OK
                   : VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    }

    if (response.request_id != launcher->request_id) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE;
    }

    vc_quick_menu_clear_action(launcher);
    launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_NONE;
    switch ((vc_launch_state)response.launch_state) {
    case VC_LAUNCH_STATE_PENDING:
        return vc_quick_menu_publish_status(
                   launcher,
                   VC_QUICK_MENU_LAUNCHER_STATUS_PENDING_CLAIM)
                   ? VC_QUICK_MENU_LAUNCHER_RESULT_OK
                   : VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    case VC_LAUNCH_STATE_CLAIMED:
        launcher->request_id = 0;
        launcher->request_deadline_ms = 0;
        memset(&launcher->request_snapshot, 0,
               sizeof(launcher->request_snapshot));
        return vc_quick_menu_publish_status(
                   launcher,
                   VC_QUICK_MENU_LAUNCHER_STATUS_CONSUMED)
                   ? VC_QUICK_MENU_LAUNCHER_RESULT_OK
                   : VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    case VC_LAUNCH_STATE_EXPIRED:
        launcher->request_id = 0;
        launcher->request_deadline_ms = 0;
        memset(&launcher->request_snapshot, 0,
               sizeof(launcher->request_snapshot));
        return vc_quick_menu_publish_status(
                   launcher,
                   VC_QUICK_MENU_LAUNCHER_STATUS_EXPIRED)
                   ? VC_QUICK_MENU_LAUNCHER_RESULT_OK
                   : VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    case VC_LAUNCH_STATE_STALE_TARGET:
        vc_quick_menu_clear_request(launcher);
        return vc_quick_menu_publish_status(
                   launcher,
                   VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE)
                   ? VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET
                   : VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    case VC_LAUNCH_STATE_CLOCK_ROLLBACK:
        vc_quick_menu_clear_request(launcher);
        launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_CLOCK;
        (void)vc_quick_menu_publish_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
        return VC_QUICK_MENU_LAUNCHER_RESULT_CLOCK_ROLLBACK;
    case VC_LAUNCH_STATE_ABSENT:
    case VC_LAUNCH_STATE_CANCELLED:
    default:
        return VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE;
    }
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_init(
    vc_quick_menu_launcher *launcher,
    const vc_quick_menu_launcher_dependencies *dependencies)
{
    if (launcher == NULL ||
        !vc_quick_menu_dependencies_valid(dependencies)) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_INVALID_ARGUMENT;
    }
    if (launcher->marker == (uint32_t)VC_QUICK_MENU_LAUNCHER_MARKER) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_INVALID_ARGUMENT;
    }

    memset(launcher, 0, sizeof(*launcher));
    atomic_init(&launcher->transaction_busy, 0u);
    atomic_init(&launcher->adapter_active, 0u);
    atomic_init(&launcher->status,
                (unsigned int)VC_QUICK_MENU_LAUNCHER_STATUS_STOPPED);
    launcher->dependencies = *dependencies;
    launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_INITIALIZED;
    launcher->marker = (uint32_t)VC_QUICK_MENU_LAUNCHER_MARKER;
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_start(
    vc_quick_menu_launcher *launcher)
{
    vc_quick_menu_launcher_result result;
    uint32_t api_version = 0;
    bool success;

    result = vc_quick_menu_enter(launcher);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    if (launcher->phase == VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
    }
    if (launcher->phase == VC_QUICK_MENU_LAUNCHER_PHASE_STARTING ||
        launcher->phase == VC_QUICK_MENU_LAUNCHER_PHASE_STOPPING) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_BUSY;
    }
    if (vc_quick_menu_has_resources(launcher)) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_CLEANUP_PENDING;
    }
    if (!vc_quick_menu_next_generation(launcher)) {
        launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM;
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    }

    launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STARTING;
    launcher->callbacks_enabled = false;
    vc_quick_menu_clear_request(launcher);
    launcher->has_time = false;
    launcher->last_now_ms = 0;
    launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_NONE;

    vc_quick_menu_adapter_begin(launcher);
    success = launcher->dependencies.get_api_version(
        launcher->dependencies.context, &api_version);
    vc_quick_menu_adapter_end(launcher);
    if (!success) {
        launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED;
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_UNAVAILABLE);
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE;
    }
    if (api_version != launcher->dependencies.expected_api_version) {
        launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED;
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_INCOMPATIBLE);
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_INCOMPATIBLE;
    }

    if (!vc_quick_menu_register_texture(launcher) ||
        !vc_quick_menu_register_label(launcher) ||
        !vc_quick_menu_register_widget(launcher) ||
        !vc_quick_menu_register_callback(launcher) ||
        !vc_quick_menu_start_worker(launcher)) {
        launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPING;
        (void)vc_quick_menu_next_generation(launcher);
        success = vc_quick_menu_cleanup_resources(launcher);
        launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED;
        launcher->last_error = success
                                   ? VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM
                                   : VC_QUICK_MENU_LAUNCHER_ERROR_CLEANUP;
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
        vc_quick_menu_leave(launcher);
        return success ? VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE
                       : VC_QUICK_MENU_LAUNCHER_RESULT_CLEANUP_PENDING;
    }

    launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING;
    launcher->callbacks_enabled = true;
    if (!vc_quick_menu_publish_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_READY)) {
        launcher->callbacks_enabled = false;
        launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPING;
        (void)vc_quick_menu_next_generation(launcher);
        success = vc_quick_menu_cleanup_resources(launcher);
        launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED;
        launcher->last_error = success
                                   ? VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM
                                   : VC_QUICK_MENU_LAUNCHER_ERROR_CLEANUP;
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
        vc_quick_menu_leave(launcher);
        return success ? VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE
                       : VC_QUICK_MENU_LAUNCHER_RESULT_CLEANUP_PENDING;
    }

    vc_quick_menu_leave(launcher);
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_handle_button(
    vc_quick_menu_launcher *launcher,
    uint64_t generation)
{
    vc_launch_foreground_snapshot snapshot;
    vc_quick_menu_launcher_result result =
        vc_quick_menu_enter(launcher);

    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    if (launcher->phase != VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING ||
        !launcher->callbacks_enabled ||
        generation == 0 ||
        generation != launcher->generation) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NOT_RUNNING;
    }
    if (launcher->action != VC_QUICK_MENU_LAUNCHER_ACTION_NONE ||
        launcher->work_pending || launcher->in_flight ||
        launcher->request_id != 0 ||
        vc_quick_menu_launcher_get_status(launcher) !=
            VC_QUICK_MENU_LAUNCHER_STATUS_READY) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION;
    }

    result = vc_quick_menu_capture_snapshot(launcher, &snapshot);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        vc_quick_menu_set_status(
            launcher,
            result == VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE
                ? VC_QUICK_MENU_LAUNCHER_STATUS_UNAVAILABLE
                : VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE);
        vc_quick_menu_leave(launcher);
        return result;
    }

    launcher->action_snapshot = snapshot;
    launcher->action = VC_QUICK_MENU_LAUNCHER_ACTION_SUBMIT;
    launcher->work_pending = true;
    launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_NONE;
    vc_quick_menu_set_status(
        launcher, VC_QUICK_MENU_LAUNCHER_STATUS_QUEUED);
    result = vc_quick_menu_signal_action(launcher);
    vc_quick_menu_leave(launcher);
    return result;
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_refresh_status(
    vc_quick_menu_launcher *launcher)
{
    vc_launch_foreground_snapshot snapshot;
    vc_quick_menu_launcher_result result =
        vc_quick_menu_enter(launcher);

    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    if (launcher->phase != VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NOT_RUNNING;
    }
    if (launcher->request_id == 0) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION;
    }
    if (launcher->action != VC_QUICK_MENU_LAUNCHER_ACTION_NONE ||
        launcher->work_pending || launcher->in_flight) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION;
    }

    result = vc_quick_menu_capture_snapshot(launcher, &snapshot);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK ||
        !vc_quick_menu_snapshot_equal(
            &launcher->request_snapshot, &snapshot)) {
        result = vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET,
            VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE,
            VC_QUICK_MENU_LAUNCHER_ERROR_NONE);
        vc_quick_menu_leave(launcher);
        return result;
    }

    launcher->action_snapshot = snapshot;
    launcher->action = VC_QUICK_MENU_LAUNCHER_ACTION_STATUS;
    launcher->work_pending = true;
    vc_quick_menu_set_status(
        launcher, VC_QUICK_MENU_LAUNCHER_STATUS_QUEUED);
    result = vc_quick_menu_signal_action(launcher);
    vc_quick_menu_leave(launcher);
    return result;
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_worker_step(
    vc_quick_menu_launcher *launcher,
    uint64_t generation)
{
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    vc_launch_foreground_snapshot before_transport;
    vc_launch_foreground_snapshot after_transport;
    vc_quick_menu_transport_result transport_result;
    vc_quick_menu_launcher_result result =
        vc_quick_menu_enter(launcher);
    size_t response_size = 0;

    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    if (launcher->phase != VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING ||
        generation == 0 ||
        generation != launcher->generation) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NOT_RUNNING;
    }
    if (!launcher->work_pending ||
        launcher->action == VC_QUICK_MENU_LAUNCHER_ACTION_NONE) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION;
    }

    launcher->work_pending = false;
    launcher->in_flight = true;
    if (!vc_quick_menu_publish_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_SUBMITTING)) {
        result = vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE,
            VC_QUICK_MENU_LAUNCHER_STATUS_ERROR,
            VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM);
        vc_quick_menu_leave(launcher);
        return result;
    }

    result = vc_quick_menu_capture_snapshot(
        launcher, &before_transport);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK ||
        !vc_quick_menu_snapshot_equal(
            &launcher->action_snapshot, &before_transport)) {
        result = vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET,
            VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE,
            VC_QUICK_MENU_LAUNCHER_ERROR_NONE);
        vc_quick_menu_leave(launcher);
        return result;
    }

    if (!launcher->request_wire_valid) {
        result = vc_quick_menu_build_request(launcher);
        if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
            result = vc_quick_menu_fail_action(
                launcher, result,
                result == VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET
                    ? VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE
                    : VC_QUICK_MENU_LAUNCHER_STATUS_ERROR,
                result == VC_QUICK_MENU_LAUNCHER_RESULT_CLOCK_ROLLBACK
                    ? VC_QUICK_MENU_LAUNCHER_ERROR_CLOCK
                    : VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM);
            vc_quick_menu_leave(launcher);
            return result;
        }
    }

    memset(response_wire, 0, sizeof(response_wire));
    vc_quick_menu_adapter_begin(launcher);
    transport_result = launcher->dependencies.transport(
        launcher->dependencies.context,
        launcher->request_wire,
        sizeof(launcher->request_wire),
        response_wire,
        sizeof(response_wire),
        &response_size);
    vc_quick_menu_adapter_end(launcher);

    result = vc_quick_menu_capture_snapshot(
        launcher, &after_transport);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK ||
        !vc_quick_menu_snapshot_equal(
            &launcher->action_snapshot, &after_transport)) {
        memset(response_wire, 0, sizeof(response_wire));
        result = vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET,
            VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE,
            VC_QUICK_MENU_LAUNCHER_ERROR_NONE);
        vc_quick_menu_leave(launcher);
        return result;
    }

    if (transport_result == VC_QUICK_MENU_TRANSPORT_BUSY ||
        transport_result == VC_QUICK_MENU_TRANSPORT_RETRY) {
        launcher->in_flight = false;
        launcher->work_pending = true;
        if (transport_result == VC_QUICK_MENU_TRANSPORT_BUSY) {
            memset(launcher->request_wire, 0,
                   sizeof(launcher->request_wire));
            launcher->request_wire_valid = false;
            launcher->request_now_ms = 0;
        }
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_QUEUED);
        memset(response_wire, 0, sizeof(response_wire));
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_RETRY;
    }
    if (transport_result == VC_QUICK_MENU_TRANSPORT_UNAVAILABLE) {
        memset(response_wire, 0, sizeof(response_wire));
        result = vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE,
            VC_QUICK_MENU_LAUNCHER_STATUS_UNAVAILABLE,
            VC_QUICK_MENU_LAUNCHER_ERROR_TRANSPORT);
        vc_quick_menu_leave(launcher);
        return result;
    }
    if (transport_result != VC_QUICK_MENU_TRANSPORT_OK) {
        memset(response_wire, 0, sizeof(response_wire));
        result = vc_quick_menu_fail_action(
            launcher, VC_QUICK_MENU_LAUNCHER_RESULT_TRANSPORT_FAILURE,
            VC_QUICK_MENU_LAUNCHER_STATUS_ERROR,
            VC_QUICK_MENU_LAUNCHER_ERROR_TRANSPORT);
        vc_quick_menu_leave(launcher);
        return result;
    }

    result = vc_quick_menu_accept_response(
        launcher, response_wire, response_size);
    memset(response_wire, 0, sizeof(response_wire));
    if (result == VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE) {
        result = vc_quick_menu_fail_action(
            launcher, result,
            VC_QUICK_MENU_LAUNCHER_STATUS_ERROR,
            VC_QUICK_MENU_LAUNCHER_ERROR_PROTOCOL);
    } else if (result == VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE) {
        launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM;
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
    }
    vc_quick_menu_leave(launcher);
    return result;
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_reset(
    vc_quick_menu_launcher *launcher)
{
    vc_quick_menu_launcher_result result =
        vc_quick_menu_enter(launcher);

    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    if (launcher->phase != VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING) {
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_NOT_RUNNING;
    }
    vc_quick_menu_clear_request(launcher);
    launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_NONE;
    if (!vc_quick_menu_publish_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_READY)) {
        launcher->last_error = VC_QUICK_MENU_LAUNCHER_ERROR_PLATFORM;
        vc_quick_menu_set_status(
            launcher, VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
        vc_quick_menu_leave(launcher);
        return VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE;
    }
    vc_quick_menu_leave(launcher);
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_stop(
    vc_quick_menu_launcher *launcher)
{
    vc_quick_menu_launcher_result result =
        vc_quick_menu_enter(launcher);
    bool cleanup_ok;

    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    launcher->callbacks_enabled = false;
    launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPING;
    (void)vc_quick_menu_next_generation(launcher);
    vc_quick_menu_clear_request(launcher);
    launcher->has_time = false;
    launcher->last_now_ms = 0;
    vc_quick_menu_set_status(
        launcher, VC_QUICK_MENU_LAUNCHER_STATUS_STOPPED);
    cleanup_ok = vc_quick_menu_cleanup_resources(launcher);
    launcher->phase = VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED;
    launcher->last_error = cleanup_ok
                               ? VC_QUICK_MENU_LAUNCHER_ERROR_NONE
                               : VC_QUICK_MENU_LAUNCHER_ERROR_CLEANUP;
    vc_quick_menu_leave(launcher);
    return cleanup_ok ? VC_QUICK_MENU_LAUNCHER_RESULT_OK
                      : VC_QUICK_MENU_LAUNCHER_RESULT_CLEANUP_PENDING;
}

vc_quick_menu_launcher_result vc_quick_menu_launcher_destroy(
    vc_quick_menu_launcher *launcher)
{
    vc_quick_menu_launcher_result result;

    if (launcher == NULL) {
        return VC_QUICK_MENU_LAUNCHER_RESULT_INVALID_ARGUMENT;
    }
    if (!vc_quick_menu_launcher_valid(launcher)) {
        memset(launcher, 0, sizeof(*launcher));
        return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
    }
    result = vc_quick_menu_launcher_stop(launcher);
    if (result != VC_QUICK_MENU_LAUNCHER_RESULT_OK) {
        return result;
    }
    memset(launcher, 0, sizeof(*launcher));
    return VC_QUICK_MENU_LAUNCHER_RESULT_OK;
}

vc_quick_menu_launcher_status vc_quick_menu_launcher_get_status(
    const vc_quick_menu_launcher *launcher)
{
    unsigned int status;

    if (!vc_quick_menu_launcher_valid(launcher)) {
        return VC_QUICK_MENU_LAUNCHER_STATUS_STOPPED;
    }
    status = atomic_load_explicit(&launcher->status,
                                  memory_order_acquire);
    if (status > (unsigned int)VC_QUICK_MENU_LAUNCHER_STATUS_ERROR) {
        return VC_QUICK_MENU_LAUNCHER_STATUS_ERROR;
    }
    return (vc_quick_menu_launcher_status)status;
}

size_t vc_quick_menu_launcher_format_status(
    const vc_quick_menu_launcher *launcher,
    char *buffer,
    size_t capacity)
{
    const char *text;
    size_t length;
    size_t copy_size;

    switch (vc_quick_menu_launcher_get_status(launcher)) {
    case VC_QUICK_MENU_LAUNCHER_STATUS_STOPPED:
        text = "Launcher stopped";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_UNAVAILABLE:
        text = "VitaCheat unavailable";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_INCOMPATIBLE:
        text = "Quick Menu incompatible";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_READY:
        text = "Ready to request";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_QUEUED:
        text = "Request queued";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_SUBMITTING:
        text = "Submitting request";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_PENDING_CLAIM:
        text = "Request pending; waiting for game claim";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_EXPIRED:
        text = "Request expired";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE:
        text = "Title changed; request discarded";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_CONSUMED:
        text = "Request claimed by game";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_BUSY:
        text = "Launcher busy";
        break;
    case VC_QUICK_MENU_LAUNCHER_STATUS_ERROR:
    default:
        text = "Request failed";
        break;
    }

    length = strlen(text);
    if (capacity == 0 || buffer == NULL) {
        return length;
    }
    copy_size = length;
    if (copy_size >= capacity) {
        copy_size = capacity - 1u;
    }
    if (copy_size != 0) {
        memcpy(buffer, text, copy_size);
    }
    buffer[copy_size] = '\0';
    return length;
}
