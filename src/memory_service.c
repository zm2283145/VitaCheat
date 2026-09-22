#include "vitacheat/memory_service.h"

#include <limits.h>
#include <string.h>

enum {
    VC_MEMORY_SERVICE_MARKER = 0x56434d53u
};

_Static_assert(VC_MEMORY_READ_ABI_VERSION == UINT16_C(1),
               "memory service supports only ABI v1");
_Static_assert(VC_MEMORY_READ_MAX_PAYLOAD == UINT32_C(256),
               "memory service payload boundary changed");

static bool vc_memory_dependencies_valid(
    const vc_memory_service_dependencies *dependencies)
{
    return dependencies != NULL &&
           dependencies->copy_from_user != NULL &&
           dependencies->copy_to_user != NULL &&
           dependencies->attest_caller != NULL &&
           dependencies->get_foreground != NULL &&
           dependencies->read_target != NULL;
}

static bool vc_memory_dependencies_equal(
    const vc_memory_service_dependencies *left,
    const vc_memory_service_dependencies *right)
{
    return left->copy_from_user == right->copy_from_user &&
           left->copy_to_user == right->copy_to_user &&
           left->attest_caller == right->attest_caller &&
           left->get_foreground == right->get_foreground &&
           left->read_target == right->read_target &&
           left->cleanup == right->cleanup &&
           left->context == right->context;
}

static bool vc_memory_service_valid(
    const vc_memory_service *service)
{
    return service != NULL &&
           service->marker == (uint32_t)VC_MEMORY_SERVICE_MARKER;
}

static vc_memory_service_result vc_memory_enter(
    vc_memory_service *service,
    bool require_running)
{
    if (!vc_memory_service_valid(service)) {
        return VC_MEMORY_SERVICE_RESULT_NOT_INITIALIZED;
    }
    if (atomic_load_explicit(
            &service->adapter_active,
            memory_order_acquire) != 0u) {
        return VC_MEMORY_SERVICE_RESULT_BUSY;
    }
    if (atomic_exchange_explicit(
            &service->transaction_busy, 1u,
            memory_order_acquire) != 0u) {
        return VC_MEMORY_SERVICE_RESULT_BUSY;
    }
    if (atomic_load_explicit(
            &service->adapter_active,
            memory_order_acquire) != 0u) {
        atomic_store_explicit(
            &service->transaction_busy, 0u,
            memory_order_release);
        return VC_MEMORY_SERVICE_RESULT_BUSY;
    }
    if (require_running &&
        service->phase != VC_MEMORY_SERVICE_PHASE_RUNNING) {
        vc_memory_service_result result =
            service->phase == VC_MEMORY_SERVICE_PHASE_STOPPED
                ? VC_MEMORY_SERVICE_RESULT_STOPPED
                : VC_MEMORY_SERVICE_RESULT_NOT_INITIALIZED;

        atomic_store_explicit(
            &service->transaction_busy, 0u,
            memory_order_release);
        return result;
    }
    return VC_MEMORY_SERVICE_RESULT_OK;
}

static void vc_memory_leave(vc_memory_service *service)
{
    atomic_store_explicit(
        &service->transaction_busy, 0u,
        memory_order_release);
}

static void vc_memory_adapter_begin(vc_memory_service *service)
{
    atomic_store_explicit(
        &service->adapter_active, 1u,
        memory_order_release);
    atomic_store_explicit(
        &service->transaction_busy, 0u,
        memory_order_release);
}

static void vc_memory_adapter_end(vc_memory_service *service)
{
    (void)atomic_exchange_explicit(
        &service->transaction_busy, 1u,
        memory_order_acquire);
    atomic_store_explicit(
        &service->adapter_active, 0u,
        memory_order_release);
}

static bool vc_memory_caller_valid(
    const vc_launch_trusted_caller *caller)
{
    return caller != NULL &&
           caller->role == VC_LAUNCH_CALLER_GAME_PLUGIN &&
           caller->reserved0 == 0 &&
           caller->process_id != 0 &&
           caller->process_generation != 0 &&
           caller->module_generation != 0;
}

static bool vc_memory_caller_equal(
    const vc_launch_trusted_caller *left,
    const vc_launch_trusted_caller *right)
{
    return left->role == right->role &&
           left->process_id == right->process_id &&
           left->process_generation == right->process_generation &&
           left->module_generation == right->module_generation;
}

static bool vc_memory_bytes_zero(
    const uint8_t *bytes,
    size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool vc_memory_normalize_foreground(
    const vc_launch_foreground_snapshot *snapshot,
    vc_launch_foreground_snapshot *normalized)
{
    if (snapshot == NULL || normalized == NULL ||
        snapshot->sequence == 0 ||
        snapshot->present > 1u ||
        snapshot->title_id_size >
            VC_LAUNCH_SERVICE_TITLE_ID_MAX) {
        return false;
    }
    memset(normalized, 0, sizeof(*normalized));
    normalized->sequence = snapshot->sequence;
    normalized->present = snapshot->present;
    if (snapshot->present == 0u) {
        return snapshot->target_process_id == 0 &&
               snapshot->target_generation == 0 &&
               snapshot->title_id_size == 0 &&
               vc_memory_bytes_zero(
                   snapshot->title_id,
                   sizeof(snapshot->title_id));
    }
    if (snapshot->target_process_id == 0 ||
        snapshot->target_generation == 0 ||
        snapshot->title_id_size == 0) {
        return false;
    }
    normalized->target_process_id =
        snapshot->target_process_id;
    normalized->target_generation =
        snapshot->target_generation;
    normalized->title_id_size = snapshot->title_id_size;
    memcpy(normalized->title_id, snapshot->title_id,
           snapshot->title_id_size);
    return true;
}

static bool vc_memory_foreground_equal(
    const vc_launch_foreground_snapshot *left,
    const vc_launch_foreground_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->present == right->present &&
           left->target_process_id ==
               right->target_process_id &&
           left->target_generation ==
               right->target_generation &&
           left->title_id_size == right->title_id_size &&
           memcmp(left->title_id, right->title_id,
                  sizeof(left->title_id)) == 0;
}

static bool vc_memory_identity_matches_foreground(
    const vc_launch_claimant_identity_snapshot *identity,
    const vc_launch_foreground_snapshot *foreground)
{
    return identity->caller.process_id ==
               foreground->target_process_id &&
           identity->caller.process_generation ==
               foreground->target_generation &&
           identity->foreground.sequence ==
               foreground->sequence &&
           identity->foreground.present == 1u &&
           vc_memory_foreground_equal(
               &identity->foreground, foreground);
}

static bool vc_memory_lineage_equal(
    const vc_menu_open_lineage *left,
    const vc_menu_open_lineage *right)
{
    return vc_memory_caller_equal(
               &left->target.caller,
               &right->target.caller) &&
           vc_memory_foreground_equal(
               &left->target.foreground,
               &right->target.foreground) &&
           left->target.sequence == right->target.sequence &&
           left->target.title_id_size ==
               right->target.title_id_size &&
           memcmp(left->target.title_id,
                  right->target.title_id,
                  sizeof(left->target.title_id)) == 0 &&
           left->authorization.value ==
               right->authorization.value &&
           left->authorization.lifecycle_generation ==
               right->authorization.lifecycle_generation &&
           memcmp(&left->lease, &right->lease,
                  sizeof(left->lease)) == 0 &&
           left->target_snapshot_revision ==
               right->target_snapshot_revision &&
           left->allowlist_revision ==
               right->allowlist_revision &&
           left->coordinator_lifecycle_generation ==
               right->coordinator_lifecycle_generation &&
           left->pause_generation ==
               right->pause_generation &&
           left->started_ms == right->started_ms &&
           left->deadline_ms == right->deadline_ms;
}

static void vc_memory_clear_journal(
    vc_memory_service *service)
{
    memset(&service->journal, 0,
           sizeof(service->journal));
}

static void vc_memory_revoke_session(
    vc_memory_service *service)
{
    memset(&service->session, 0,
           sizeof(service->session));
    vc_memory_clear_journal(service);
}

static bool vc_memory_advance_lifecycle(
    vc_memory_service *service)
{
    if (service->lifecycle_generation == UINT64_MAX) {
        return false;
    }
    ++service->lifecycle_generation;
    return true;
}

static vc_memory_read_status vc_memory_record_time(
    vc_memory_service *service,
    uint64_t now_ms)
{
    if (service->has_time && now_ms < service->last_now_ms) {
        vc_memory_revoke_session(service);
        return VC_MEMORY_READ_STATUS_CLOCK_ROLLBACK;
    }
    service->last_now_ms = now_ms;
    service->has_time = true;
    if (service->session.active &&
        now_ms >= service->session.deadline_ms) {
        vc_memory_revoke_session(service);
        return VC_MEMORY_READ_STATUS_EXPIRED;
    }
    return VC_MEMORY_READ_STATUS_OK;
}

static bool vc_memory_capture_foreground(
    vc_memory_service *service,
    vc_launch_foreground_snapshot *foreground)
{
    vc_launch_foreground_snapshot raw;
    bool captured;

    memset(&raw, 0, sizeof(raw));
    vc_memory_adapter_begin(service);
    captured = service->dependencies.get_foreground(
        service->dependencies.context, &raw);
    vc_memory_adapter_end(service);
    return captured &&
           vc_memory_normalize_foreground(&raw, foreground);
}

static vc_memory_service_result vc_memory_attest(
    vc_memory_service *service,
    const void *caller_context,
    vc_launch_trusted_caller *caller)
{
    vc_launch_attestation_result result;

    memset(caller, 0, sizeof(*caller));
    vc_memory_adapter_begin(service);
    result = service->dependencies.attest_caller(
        service->dependencies.context,
        caller_context, caller);
    vc_memory_adapter_end(service);
    if (result == VC_LAUNCH_ATTESTATION_FAILED) {
        return VC_MEMORY_SERVICE_RESULT_PLATFORM_FAILURE;
    }
    if (result != VC_LAUNCH_ATTESTATION_ACCEPTED ||
        !vc_memory_caller_valid(caller)) {
        return VC_MEMORY_SERVICE_RESULT_UNAUTHENTICATED_CALLER;
    }
    return VC_MEMORY_SERVICE_RESULT_OK;
}

static vc_menu_coordinator_result vc_memory_inspect_lineage(
    vc_memory_service *service,
    uint64_t now_ms,
    vc_menu_open_lineage *lineage)
{
    vc_menu_coordinator_result result;

    vc_memory_adapter_begin(service);
    result = vc_menu_coordinator_inspect_open_lineage(
        service->menu_coordinator, now_ms, lineage);
    vc_memory_adapter_end(service);
    return result;
}

static vc_menu_coordinator_result vc_memory_validate_lineage(
    vc_memory_service *service,
    uint64_t now_ms,
    const vc_menu_open_lineage *lineage)
{
    vc_menu_coordinator_result result;

    vc_memory_adapter_begin(service);
    result = vc_menu_coordinator_validate_open_lineage(
        service->menu_coordinator, now_ms, lineage);
    vc_memory_adapter_end(service);
    return result;
}

static vc_target_status vc_memory_get_snapshot(
    vc_memory_service *service,
    vc_target_snapshot *snapshot)
{
    vc_target_status status;

    vc_memory_adapter_begin(service);
    status = vc_target_attestation_get_snapshot(
        service->target_attestation, snapshot);
    vc_memory_adapter_end(service);
    return status;
}

static vc_target_status vc_memory_resolve_range(
    vc_memory_service *service,
    const vc_memory_read_request *request,
    vc_target_range *range)
{
    vc_target_status status;

    vc_memory_adapter_begin(service);
    status = vc_target_attestation_resolve_segment_range(
        service->target_attestation,
        request->attestation_revision,
        request->module_id,
        request->module_load_generation,
        request->segment_index,
        request->segment_offset,
        request->length,
        VC_TARGET_PERMISSION_READ,
        range);
    vc_memory_adapter_end(service);
    return status;
}

static vc_target_status vc_memory_find_segment(
    vc_memory_service *service,
    const vc_memory_read_request *request,
    vc_target_segment *segment)
{
    vc_target_status status;

    vc_memory_adapter_begin(service);
    status = vc_target_attestation_find_segment(
        service->target_attestation,
        request->attestation_revision,
        request->module_id,
        request->module_load_generation,
        request->segment_index,
        segment);
    vc_memory_adapter_end(service);
    return status;
}

static bool vc_memory_target_snapshot_matches(
    const vc_target_snapshot *snapshot,
    const vc_memory_service_session *session)
{
    return snapshot->revision ==
               session->target_attestation_revision &&
           snapshot->lifecycle_generation ==
               session->target_attestation_lifecycle_generation &&
           snapshot->identity.process_id ==
               session->foreground.target_process_id &&
           snapshot->identity.process_generation ==
               session->foreground.target_generation &&
           snapshot->identity.foreground_sequence ==
               session->foreground.sequence;
}

static vc_memory_read_status vc_memory_validate_trusted_state(
    vc_memory_service *service,
    uint64_t now_ms,
    vc_launch_foreground_snapshot *foreground,
    vc_target_snapshot *snapshot)
{
    vc_menu_coordinator_result menu_result;
    vc_target_status target_status;

    if (!service->session.active) {
        return VC_MEMORY_READ_STATUS_NO_AUTHORIZATION;
    }
    menu_result = vc_memory_validate_lineage(
        service, now_ms, &service->session.lineage);
    if (menu_result != VC_MENU_COORDINATOR_RESULT_OK) {
        vc_memory_revoke_session(service);
        return menu_result ==
                       VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK
                   ? VC_MEMORY_READ_STATUS_CLOCK_ROLLBACK
                   : VC_MEMORY_READ_STATUS_NO_AUTHORIZATION;
    }
    memset(foreground, 0, sizeof(*foreground));
    if (!vc_memory_capture_foreground(service, foreground) ||
        !vc_memory_foreground_equal(
            foreground, &service->session.foreground)) {
        vc_memory_revoke_session(service);
        return VC_MEMORY_READ_STATUS_STALE_TARGET;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    target_status = vc_memory_get_snapshot(service, snapshot);
    if (target_status != VC_TARGET_STATUS_OK ||
        !vc_memory_target_snapshot_matches(
            snapshot, &service->session)) {
        vc_memory_revoke_session(service);
        return target_status == VC_TARGET_STATUS_BUSY
                   ? VC_MEMORY_READ_STATUS_BUSY
                   : VC_MEMORY_READ_STATUS_STALE_SNAPSHOT;
    }
    return VC_MEMORY_READ_STATUS_OK;
}

static vc_memory_read_status vc_memory_map_range_status(
    vc_target_status status)
{
    switch (status) {
    case VC_TARGET_STATUS_OK:
        return VC_MEMORY_READ_STATUS_OK;
    case VC_TARGET_STATUS_PERMISSION_DENIED:
        return VC_MEMORY_READ_STATUS_PERMISSION_DENIED;
    case VC_TARGET_STATUS_INVALID_MODULE:
        return VC_MEMORY_READ_STATUS_STALE_MODULE;
    case VC_TARGET_STATUS_STALE_SNAPSHOT:
    case VC_TARGET_STATUS_UNAVAILABLE:
        return VC_MEMORY_READ_STATUS_STALE_SNAPSHOT;
    case VC_TARGET_STATUS_INVALID_SEGMENT:
    case VC_TARGET_STATUS_OVERFLOW:
    case VC_TARGET_STATUS_UNMAPPED_RANGE:
    case VC_TARGET_STATUS_CROSS_SEGMENT_RANGE:
        return VC_MEMORY_READ_STATUS_RANGE_DENIED;
    case VC_TARGET_STATUS_BUSY:
        return VC_MEMORY_READ_STATUS_BUSY;
    default:
        return VC_MEMORY_READ_STATUS_BOUNDED_ERROR;
    }
}

static void vc_memory_response_init(
    vc_memory_read_response *response,
    const vc_memory_read_request *request,
    vc_memory_read_status status,
    const vc_memory_service *service)
{
    memset(response, 0, sizeof(*response));
    response->version = VC_MEMORY_READ_ABI_VERSION;
    response->header_size =
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE;
    response->total_size =
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE;
    response->status = status;
    if (request == NULL) {
        response->operation = VC_MEMORY_READ_OPERATION_NONE;
        response->state =
            status == VC_MEMORY_READ_STATUS_EXPIRED
                ? VC_MEMORY_READ_STATE_EXPIRED
                : VC_MEMORY_READ_STATE_ERROR;
        return;
    }
    response->operation = request->operation;
    response->capabilities =
        VC_MEMORY_READ_CAPABILITY_READ_TARGET;
    response->request_id = request->request_id;
    response->state = service->session.active
                          ? VC_MEMORY_READ_STATE_ACTIVE
                          : (status == VC_MEMORY_READ_STATUS_EXPIRED
                                 ? VC_MEMORY_READ_STATE_EXPIRED
                                 : VC_MEMORY_READ_STATE_REVOKED);
    if (service->session.active) {
        response->session_id = service->session.session_id;
        response->remaining_bytes =
            service->session.remaining_bytes;
        response->remaining_operations =
            service->session.remaining_operations;
    }
}

static bool vc_memory_journal_matches(
    const vc_memory_service *service,
    const uint8_t *request,
    const vc_launch_trusted_caller *caller,
    const vc_launch_foreground_snapshot *foreground,
    uint64_t now_ms)
{
    const vc_memory_service_journal *journal =
        &service->journal;

    return journal->valid &&
           journal->service_lifecycle_generation ==
               service->lifecycle_generation &&
           now_ms >= journal->committed_now_ms &&
           vc_memory_caller_equal(&journal->caller, caller) &&
           vc_memory_foreground_equal(
               &journal->foreground, foreground) &&
           memcmp(journal->request, request,
                  VC_MEMORY_READ_REQUEST_WIRE_SIZE) == 0 &&
           (!journal->authorization_bound ||
            (service->session.active &&
             vc_memory_lineage_equal(
                 &journal->lineage,
                 &service->session.lineage)));
}

static void vc_memory_store_journal(
    vc_memory_service *service,
    const uint8_t *request,
    const uint8_t *response,
    size_t response_size,
    const vc_launch_trusted_caller *caller,
    const vc_launch_foreground_snapshot *foreground,
    uint64_t now_ms,
    vc_memory_read_status operation_status,
    bool authorization_bound)
{
    vc_memory_clear_journal(service);
    service->journal.caller = *caller;
    service->journal.foreground = *foreground;
    if (authorization_bound) {
        service->journal.lineage =
            service->session.lineage;
    }
    memcpy(service->journal.request, request,
           VC_MEMORY_READ_REQUEST_WIRE_SIZE);
    memcpy(service->journal.response, response,
           response_size);
    service->journal.response_size = response_size;
    service->journal.committed_now_ms = now_ms;
    service->journal.service_lifecycle_generation =
        service->lifecycle_generation;
    service->journal.operation_status = operation_status;
    service->journal.authorization_bound =
        authorization_bound;
    service->journal.valid = true;
}

static vc_memory_service_result vc_memory_copy_journal(
    vc_memory_service *service,
    void *response_user,
    size_t response_capacity,
    size_t *response_size,
    vc_memory_read_status *operation_status)
{
    bool copied;

    if (response_capacity < service->journal.response_size) {
        return VC_MEMORY_SERVICE_RESULT_OUTPUT_TOO_SMALL;
    }
    *operation_status =
        (vc_memory_read_status)service->journal.operation_status;
    vc_memory_adapter_begin(service);
    copied = service->dependencies.copy_to_user(
        service->dependencies.context,
        response_user,
        service->journal.response,
        service->journal.response_size);
    vc_memory_adapter_end(service);
    if (!copied) {
        return VC_MEMORY_SERVICE_RESULT_COPY_TO_USER_FAILED;
    }
    *response_size = service->journal.response_size;
    vc_memory_clear_journal(service);
    return VC_MEMORY_SERVICE_RESULT_OK;
}

static vc_memory_service_result vc_memory_publish_response(
    vc_memory_service *service,
    const uint8_t *request_wire,
    const vc_memory_read_request *request,
    vc_memory_read_response *response,
    const vc_launch_trusted_caller *caller,
    const vc_launch_foreground_snapshot *foreground,
    uint64_t now_ms,
    bool authorization_bound,
    void *response_user,
    size_t response_capacity,
    size_t *response_size,
    vc_memory_read_status *operation_status)
{
    uint8_t wire[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    vc_memory_read_status status;
    size_t encoded_size = 0;

    (void)request;
    memset(wire, 0, sizeof(wire));
    status = vc_memory_read_response_encode(
        response, wire, response->total_size, &encoded_size);
    if (status != VC_MEMORY_READ_STATUS_OK ||
        encoded_size != response->total_size) {
        memset(wire, 0, sizeof(wire));
        memset(response, 0, sizeof(*response));
        return VC_MEMORY_SERVICE_RESULT_BOUNDED_ERROR;
    }
    if (response_capacity < encoded_size) {
        memset(wire, 0, sizeof(wire));
        memset(response, 0, sizeof(*response));
        return VC_MEMORY_SERVICE_RESULT_OUTPUT_TOO_SMALL;
    }
    *operation_status =
        (vc_memory_read_status)response->status;
    vc_memory_store_journal(
        service, request_wire, wire, encoded_size,
        caller, foreground, now_ms,
        *operation_status, authorization_bound);
    memset(wire, 0, sizeof(wire));
    memset(response, 0, sizeof(*response));
    return vc_memory_copy_journal(
        service, response_user, response_capacity,
        response_size, operation_status);
}

static vc_memory_service_result vc_memory_publish_once(
    vc_memory_service *service,
    vc_memory_read_response *response,
    void *response_user,
    size_t response_capacity,
    size_t *response_size,
    vc_memory_read_status *operation_status)
{
    uint8_t wire[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    size_t encoded_size = 0;
    bool copied;

    memset(wire, 0, sizeof(wire));
    if (vc_memory_read_response_encode(
            response, wire, response->total_size,
            &encoded_size) != VC_MEMORY_READ_STATUS_OK ||
        encoded_size != response->total_size) {
        memset(wire, 0, sizeof(wire));
        memset(response, 0, sizeof(*response));
        return VC_MEMORY_SERVICE_RESULT_BOUNDED_ERROR;
    }
    if (response_capacity < encoded_size) {
        memset(wire, 0, sizeof(wire));
        memset(response, 0, sizeof(*response));
        return VC_MEMORY_SERVICE_RESULT_OUTPUT_TOO_SMALL;
    }
    *operation_status =
        (vc_memory_read_status)response->status;
    vc_memory_adapter_begin(service);
    copied = service->dependencies.copy_to_user(
        service->dependencies.context,
        response_user, wire, encoded_size);
    vc_memory_adapter_end(service);
    memset(wire, 0, sizeof(wire));
    memset(response, 0, sizeof(*response));
    if (!copied) {
        return VC_MEMORY_SERVICE_RESULT_COPY_TO_USER_FAILED;
    }
    *response_size = encoded_size;
    return VC_MEMORY_SERVICE_RESULT_OK;
}

vc_memory_service_result vc_memory_service_init(
    vc_memory_service *service,
    vc_target_attestation *target_attestation,
    vc_menu_coordinator *menu_coordinator,
    const vc_memory_service_dependencies *dependencies,
    uint64_t first_session_id)
{
    if (service == NULL || target_attestation == NULL ||
        menu_coordinator == NULL ||
        !vc_memory_dependencies_valid(dependencies) ||
        first_session_id == 0) {
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    if (service->marker == (uint32_t)VC_MEMORY_SERVICE_MARKER) {
        vc_memory_service_result result =
            vc_memory_enter(service, false);

        if (result != VC_MEMORY_SERVICE_RESULT_OK) {
            return result;
        }
        if (service->target_attestation ==
                target_attestation &&
            service->menu_coordinator == menu_coordinator &&
            service->initial_session_id ==
                first_session_id &&
            vc_memory_dependencies_equal(
                &service->dependencies, dependencies)) {
            vc_memory_leave(service);
            return VC_MEMORY_SERVICE_RESULT_OK;
        }
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }

    memset(service, 0, sizeof(*service));
    atomic_init(&service->transaction_busy, 0u);
    atomic_init(&service->adapter_active, 0u);
    service->dependencies = *dependencies;
    service->target_attestation = target_attestation;
    service->menu_coordinator = menu_coordinator;
    service->next_session_id = first_session_id;
    service->initial_session_id = first_session_id;
    service->phase = VC_MEMORY_SERVICE_PHASE_INITIALIZED;
    service->marker = (uint32_t)VC_MEMORY_SERVICE_MARKER;
    return VC_MEMORY_SERVICE_RESULT_OK;
}

vc_memory_service_result vc_memory_service_start(
    vc_memory_service *service,
    uint64_t now_ms)
{
    vc_memory_service_result result =
        vc_memory_enter(service, false);

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    if (service->phase == VC_MEMORY_SERVICE_PHASE_RUNNING) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    if (!vc_memory_advance_lifecycle(service)) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_BOUNDED_ERROR;
    }
    vc_memory_revoke_session(service);
    service->last_now_ms = now_ms;
    service->has_time = true;
    service->phase = VC_MEMORY_SERVICE_PHASE_RUNNING;
    vc_memory_leave(service);
    return VC_MEMORY_SERVICE_RESULT_OK;
}

vc_memory_service_result vc_memory_service_stop(
    vc_memory_service *service,
    uint64_t now_ms)
{
    vc_memory_service_result result =
        vc_memory_enter(service, false);
    vc_memory_platform_cleanup_fn cleanup;
    void *context;
    bool cleanup_ok = true;

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    if (service->phase == VC_MEMORY_SERVICE_PHASE_STOPPED) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    (void)vc_memory_record_time(service, now_ms);
    if (!vc_memory_advance_lifecycle(service)) {
        vc_memory_revoke_session(service);
        service->phase = VC_MEMORY_SERVICE_PHASE_STOPPED;
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_BOUNDED_ERROR;
    }
    vc_memory_revoke_session(service);
    service->phase = VC_MEMORY_SERVICE_PHASE_STOPPED;
    cleanup = service->dependencies.cleanup;
    context = service->dependencies.context;
    if (cleanup != NULL) {
        vc_memory_adapter_begin(service);
        cleanup_ok = cleanup(context);
        vc_memory_adapter_end(service);
    }
    vc_memory_leave(service);
    return cleanup_ok ? VC_MEMORY_SERVICE_RESULT_OK
                      : VC_MEMORY_SERVICE_RESULT_PLATFORM_FAILURE;
}

vc_memory_service_result vc_memory_service_reset(
    vc_memory_service *service,
    uint64_t now_ms)
{
    vc_memory_service_result result =
        vc_memory_enter(service, true);

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    (void)vc_memory_record_time(service, now_ms);
    if (!vc_memory_advance_lifecycle(service)) {
        vc_memory_revoke_session(service);
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_BOUNDED_ERROR;
    }
    vc_memory_revoke_session(service);
    vc_memory_leave(service);
    return VC_MEMORY_SERVICE_RESULT_OK;
}

vc_memory_service_result vc_memory_service_tick(
    vc_memory_service *service,
    uint64_t now_ms)
{
    vc_launch_foreground_snapshot foreground;
    vc_target_snapshot snapshot;
    vc_memory_read_status status;
    vc_memory_service_result result =
        vc_memory_enter(service, true);

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    status = vc_memory_record_time(service, now_ms);
    if (status != VC_MEMORY_READ_STATUS_OK) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    if (service->session.active) {
        status = vc_memory_validate_trusted_state(
            service, now_ms, &foreground, &snapshot);
        memset(&foreground, 0, sizeof(foreground));
        memset(&snapshot, 0, sizeof(snapshot));
        if (status != VC_MEMORY_READ_STATUS_OK) {
            vc_memory_leave(service);
            return VC_MEMORY_SERVICE_RESULT_OK;
        }
    }
    result = service->journal.valid
                 ? VC_MEMORY_SERVICE_RESULT_RESULT_PENDING
                 : VC_MEMORY_SERVICE_RESULT_OK;
    vc_memory_leave(service);
    return result;
}

vc_memory_service_result vc_memory_service_activate(
    vc_memory_service *service,
    uint64_t now_ms,
    uint64_t byte_budget,
    uint32_t operation_budget,
    uint64_t *session_id,
    vc_memory_read_status *authorization_status)
{
    vc_menu_open_lineage lineage;
    vc_launch_foreground_snapshot foreground;
    vc_target_snapshot snapshot;
    vc_menu_coordinator_result menu_result;
    vc_target_status target_status;
    vc_memory_service_result result =
        vc_memory_enter(service, true);
    uint64_t maximum_deadline;

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    if (session_id == NULL || authorization_status == NULL ||
        byte_budget == 0 ||
        byte_budget > VC_MEMORY_SERVICE_MAX_BYTE_BUDGET ||
        operation_budget == 0 ||
        operation_budget >
            VC_MEMORY_SERVICE_MAX_OPERATION_BUDGET ||
        now_ms > UINT64_MAX -
            VC_MEMORY_SERVICE_MAX_DURATION_MS) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    *session_id = 0;
    *authorization_status =
        VC_MEMORY_READ_STATUS_NO_AUTHORIZATION;
    if (vc_memory_record_time(service, now_ms) !=
        VC_MEMORY_READ_STATUS_OK) {
        *authorization_status =
            VC_MEMORY_READ_STATUS_CLOCK_ROLLBACK;
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    if (service->session.active || service->journal.valid) {
        *authorization_status =
            VC_MEMORY_READ_STATUS_STALE_SESSION;
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_BUSY;
    }
    if (service->session_id_exhausted ||
        service->next_session_id == 0) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_ID_EXHAUSTED;
    }

    memset(&lineage, 0, sizeof(lineage));
    menu_result = vc_memory_inspect_lineage(
        service, now_ms, &lineage);
    if (menu_result != VC_MENU_COORDINATOR_RESULT_OK) {
        *authorization_status =
            menu_result ==
                    VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK
                ? VC_MEMORY_READ_STATUS_CLOCK_ROLLBACK
                : VC_MEMORY_READ_STATUS_NO_AUTHORIZATION;
        memset(&lineage, 0, sizeof(lineage));
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    memset(&foreground, 0, sizeof(foreground));
    if (!vc_memory_capture_foreground(service, &foreground) ||
        !vc_memory_caller_valid(&lineage.target.caller) ||
        !vc_memory_identity_matches_foreground(
            &lineage.target, &foreground)) {
        *authorization_status =
            VC_MEMORY_READ_STATUS_STALE_TARGET;
        memset(&lineage, 0, sizeof(lineage));
        memset(&foreground, 0, sizeof(foreground));
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    target_status = vc_memory_get_snapshot(service, &snapshot);
    if (target_status != VC_TARGET_STATUS_OK ||
        snapshot.revision !=
            lineage.target_snapshot_revision ||
        snapshot.identity.process_id !=
            foreground.target_process_id ||
        snapshot.identity.process_generation !=
            foreground.target_generation ||
        snapshot.identity.foreground_sequence !=
            foreground.sequence) {
        *authorization_status =
            VC_MEMORY_READ_STATUS_STALE_SNAPSHOT;
        memset(&lineage, 0, sizeof(lineage));
        memset(&foreground, 0, sizeof(foreground));
        memset(&snapshot, 0, sizeof(snapshot));
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OK;
    }

    service->session.caller = lineage.target.caller;
    service->session.foreground = foreground;
    service->session.lineage = lineage;
    service->session.session_id =
        service->next_session_id;
    service->session.service_lifecycle_generation =
        service->lifecycle_generation;
    service->session.target_attestation_revision =
        snapshot.revision;
    service->session
        .target_attestation_lifecycle_generation =
        snapshot.lifecycle_generation;
    maximum_deadline =
        now_ms + VC_MEMORY_SERVICE_MAX_DURATION_MS;
    service->session.deadline_ms =
        lineage.deadline_ms < maximum_deadline
            ? lineage.deadline_ms
            : maximum_deadline;
    service->session.remaining_bytes = byte_budget;
    service->session.remaining_operations =
        operation_budget;
    service->session.active = true;
    *session_id = service->session.session_id;
    *authorization_status = VC_MEMORY_READ_STATUS_OK;
    if (service->next_session_id == UINT64_MAX) {
        service->next_session_id = 0;
        service->session_id_exhausted = true;
    } else {
        ++service->next_session_id;
    }
    memset(&lineage, 0, sizeof(lineage));
    memset(&foreground, 0, sizeof(foreground));
    memset(&snapshot, 0, sizeof(snapshot));
    vc_memory_leave(service);
    return VC_MEMORY_SERVICE_RESULT_OK;
}

static vc_memory_service_result vc_memory_revoke_event(
    vc_memory_service *service)
{
    vc_memory_service_result result =
        vc_memory_enter(service, true);

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    vc_memory_revoke_session(service);
    vc_memory_leave(service);
    return VC_MEMORY_SERVICE_RESULT_OK;
}

vc_memory_service_result vc_memory_service_menu_closed(
    vc_memory_service *service)
{
    return vc_memory_revoke_event(service);
}

vc_memory_service_result vc_memory_service_target_changed(
    vc_memory_service *service)
{
    return vc_memory_revoke_event(service);
}

vc_memory_service_result vc_memory_service_overlay_changed(
    vc_memory_service *service,
    vc_launch_overlay_state state)
{
    if ((uint32_t)state > (uint32_t)VC_LAUNCH_OVERLAY_CLOSED) {
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    if (state == VC_LAUNCH_OVERLAY_CLOSED) {
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    return vc_memory_revoke_event(service);
}

vc_memory_service_result vc_memory_service_presentation_changed(
    vc_memory_service *service,
    vc_launch_presentation_state state)
{
    if ((uint32_t)state > (uint32_t)VC_LAUNCH_PRESENTATION_LOST) {
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    if (state == VC_LAUNCH_PRESENTATION_READY) {
        return VC_MEMORY_SERVICE_RESULT_OK;
    }
    return vc_memory_revoke_event(service);
}

vc_memory_service_result vc_memory_service_process_exit(
    vc_memory_service *service,
    uint32_t process_id,
    uint64_t process_generation)
{
    vc_memory_service_result result =
        vc_memory_enter(service, true);

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    if (process_id == 0 || process_generation == 0) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    if (service->session.active &&
        (service->session.caller.process_id != process_id ||
         service->session.caller.process_generation !=
             process_generation)) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_STALE_TRUSTED_STATE;
    }
    vc_memory_revoke_session(service);
    vc_memory_leave(service);
    return VC_MEMORY_SERVICE_RESULT_OK;
}

vc_memory_service_result vc_memory_service_plugin_unload(
    vc_memory_service *service,
    uint32_t process_id,
    uint64_t process_generation,
    uint64_t module_generation)
{
    vc_memory_service_result result =
        vc_memory_enter(service, true);

    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    if (process_id == 0 || process_generation == 0 ||
        module_generation == 0) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    if (service->session.active &&
        (service->session.caller.process_id != process_id ||
         service->session.caller.process_generation !=
             process_generation ||
         service->session.caller.module_generation !=
             module_generation)) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_STALE_TRUSTED_STATE;
    }
    vc_memory_revoke_session(service);
    vc_memory_leave(service);
    return VC_MEMORY_SERVICE_RESULT_OK;
}

static vc_memory_read_status vc_memory_perform_read(
    vc_memory_service *service,
    const vc_memory_read_request *request,
    vc_memory_read_response *response,
    uint64_t now_ms)
{
    vc_target_range range;
    vc_target_segment segment;
    vc_memory_target_read target_read;
    vc_memory_target_read_observation observation;
    vc_launch_foreground_snapshot after_foreground;
    vc_target_snapshot after_snapshot;
    vc_target_status target_status;
    vc_memory_read_status status;
    bool read_ok;

    memset(&range, 0, sizeof(range));
    target_status = vc_memory_resolve_range(
        service, request, &range);
    status = vc_memory_map_range_status(target_status);
    if (status != VC_MEMORY_READ_STATUS_OK) {
        return status;
    }
    memset(&segment, 0, sizeof(segment));
    target_status = vc_memory_find_segment(
        service, request, &segment);
    status = vc_memory_map_range_status(target_status);
    if (status != VC_MEMORY_READ_STATUS_OK ||
        segment.base >
            UINT32_MAX - request->segment_offset) {
        memset(&range, 0, sizeof(range));
        memset(&segment, 0, sizeof(segment));
        return status != VC_MEMORY_READ_STATUS_OK
                   ? status
                   : VC_MEMORY_READ_STATUS_RANGE_DENIED;
    }

    service->session.remaining_bytes -= request->length;

    memset(&target_read, 0, sizeof(target_read));
    target_read.target_process_id =
        service->session.foreground.target_process_id;
    target_read.address =
        segment.base + request->segment_offset;
    target_read.target_generation =
        service->session.foreground.target_generation;
    target_read.attestation_revision =
        service->session.target_attestation_revision;
    target_read.length = request->length;
    memset(&observation, 0, sizeof(observation));
    vc_memory_adapter_begin(service);
    read_ok = service->dependencies.read_target(
        service->dependencies.context, &target_read,
        response->payload, sizeof(response->payload),
        &observation);
    vc_memory_adapter_end(service);
    memset(&target_read, 0, sizeof(target_read));
    memset(&range, 0, sizeof(range));
    memset(&segment, 0, sizeof(segment));

    if (!read_ok) {
        memset(response->payload, 0,
               sizeof(response->payload));
        memset(&observation, 0, sizeof(observation));
        return VC_MEMORY_READ_STATUS_READ_FAULT;
    }
    if (observation.bytes_read != request->length) {
        status = observation.bytes_read > request->length
                     ? VC_MEMORY_READ_STATUS_READ_FAULT
                     : VC_MEMORY_READ_STATUS_SHORT_READ;
        memset(response->payload, 0,
               sizeof(response->payload));
        memset(&observation, 0, sizeof(observation));
        return status;
    }
    if (observation.target_generation !=
            service->session.foreground.target_generation ||
        observation.attestation_revision !=
            service->session.target_attestation_revision) {
        memset(response->payload, 0,
               sizeof(response->payload));
        memset(&observation, 0, sizeof(observation));
        vc_memory_revoke_session(service);
        return VC_MEMORY_READ_STATUS_MUTATED_TARGET;
    }
    memset(&after_foreground, 0, sizeof(after_foreground));
    memset(&after_snapshot, 0, sizeof(after_snapshot));
    status = vc_memory_validate_trusted_state(
        service, now_ms, &after_foreground, &after_snapshot);
    memset(&after_foreground, 0, sizeof(after_foreground));
    memset(&after_snapshot, 0, sizeof(after_snapshot));
    memset(&observation, 0, sizeof(observation));
    if (status != VC_MEMORY_READ_STATUS_OK) {
        memset(response->payload, 0,
               sizeof(response->payload));
        return status == VC_MEMORY_READ_STATUS_BUSY
                   ? status
                   : VC_MEMORY_READ_STATUS_MUTATED_TARGET;
    }
    response->payload_size = request->length;
    response->total_size =
        (uint16_t)(
            VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE +
            request->length);
    return VC_MEMORY_READ_STATUS_OK;
}

vc_memory_service_result vc_memory_service_dispatch(
    vc_memory_service *service,
    const void *caller_context,
    const void *request_user,
    size_t request_size,
    void *response_user,
    size_t response_capacity,
    uint64_t now_ms,
    size_t *response_size,
    vc_memory_read_status *operation_status)
{
    uint8_t request_wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground;
    vc_target_snapshot snapshot;
    vc_memory_read_request request;
    vc_memory_read_response response;
    vc_memory_service_result result;
    vc_memory_read_status status;
    size_t required_response_size;
    bool copied;

    if (response_size == NULL || operation_status == NULL) {
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    *response_size = 0;
    *operation_status = VC_MEMORY_READ_STATUS_INVALID_ARGUMENT;
    result = vc_memory_enter(service, true);
    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        return result;
    }
    if (request_size != VC_MEMORY_READ_REQUEST_WIRE_SIZE) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_INVALID_INPUT_SIZE;
    }
    if (request_user == NULL || response_user == NULL) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT;
    }
    if (response_capacity <
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE) {
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OUTPUT_TOO_SMALL;
    }

    memset(request_wire, 0, sizeof(request_wire));
    vc_memory_adapter_begin(service);
    copied = service->dependencies.copy_from_user(
        service->dependencies.context,
        request_wire, request_user,
        sizeof(request_wire));
    vc_memory_adapter_end(service);
    if (!copied) {
        memset(request_wire, 0, sizeof(request_wire));
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_COPY_FROM_USER_FAILED;
    }
    result = vc_memory_attest(
        service, caller_context, &caller);
    if (result != VC_MEMORY_SERVICE_RESULT_OK) {
        memset(request_wire, 0, sizeof(request_wire));
        memset(&caller, 0, sizeof(caller));
        vc_memory_leave(service);
        return result;
    }
    memset(&foreground, 0, sizeof(foreground));
    if (!vc_memory_capture_foreground(
            service, &foreground)) {
        memset(request_wire, 0, sizeof(request_wire));
        memset(&caller, 0, sizeof(caller));
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_PLATFORM_FAILURE;
    }

    status = vc_memory_record_time(service, now_ms);
    if (status != VC_MEMORY_READ_STATUS_OK) {
        memset(&request, 0, sizeof(request));
        memset(&response, 0, sizeof(response));
        vc_memory_response_init(
            &response, NULL, status, service);
        result = vc_memory_publish_once(
            service, &response, response_user,
            response_capacity, response_size,
            operation_status);
        memset(request_wire, 0, sizeof(request_wire));
        memset(&caller, 0, sizeof(caller));
        memset(&foreground, 0, sizeof(foreground));
        vc_memory_leave(service);
        return result;
    }

    if (service->journal.valid) {
        if (service->journal.authorization_bound) {
            memset(&snapshot, 0, sizeof(snapshot));
            status = vc_memory_validate_trusted_state(
                service, now_ms, &foreground, &snapshot);
            memset(&snapshot, 0, sizeof(snapshot));
            if (status != VC_MEMORY_READ_STATUS_OK) {
                memset(request_wire, 0,
                       sizeof(request_wire));
                memset(&caller, 0, sizeof(caller));
                memset(&foreground, 0,
                       sizeof(foreground));
                vc_memory_leave(service);
                return VC_MEMORY_SERVICE_RESULT_STALE_TRUSTED_STATE;
            }
        }
        if (!vc_memory_journal_matches(
                service, request_wire, &caller,
                &foreground, now_ms)) {
            memset(request_wire, 0, sizeof(request_wire));
            memset(&caller, 0, sizeof(caller));
            memset(&foreground, 0, sizeof(foreground));
            vc_memory_leave(service);
            return VC_MEMORY_SERVICE_RESULT_RESULT_PENDING;
        }
        result = vc_memory_copy_journal(
            service, response_user, response_capacity,
            response_size, operation_status);
        memset(request_wire, 0, sizeof(request_wire));
        memset(&caller, 0, sizeof(caller));
        memset(&foreground, 0, sizeof(foreground));
        vc_memory_leave(service);
        return result;
    }

    memset(&request, 0, sizeof(request));
    status = vc_memory_read_request_decode(
        request_wire, sizeof(request_wire), &request);
    if (status != VC_MEMORY_READ_STATUS_OK) {
        memset(&response, 0, sizeof(response));
        vc_memory_response_init(
            &response, NULL, status, service);
        result = vc_memory_publish_response(
            service, request_wire, NULL, &response,
            &caller, &foreground, now_ms, false,
            response_user, response_capacity,
            response_size, operation_status);
        memset(request_wire, 0, sizeof(request_wire));
        memset(&caller, 0, sizeof(caller));
        memset(&foreground, 0, sizeof(foreground));
        vc_memory_leave(service);
        return result;
    }
    if (request.caller_role != caller.role) {
        memset(request_wire, 0, sizeof(request_wire));
        memset(&caller, 0, sizeof(caller));
        memset(&foreground, 0, sizeof(foreground));
        memset(&request, 0, sizeof(request));
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_UNAUTHENTICATED_CALLER;
    }
    required_response_size =
        (size_t)VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE +
        (request.operation == VC_MEMORY_READ_OPERATION_READ
             ? request.length
             : 0u);
    if (response_capacity < required_response_size) {
        memset(request_wire, 0, sizeof(request_wire));
        memset(&caller, 0, sizeof(caller));
        memset(&foreground, 0, sizeof(foreground));
        memset(&request, 0, sizeof(request));
        vc_memory_leave(service);
        return VC_MEMORY_SERVICE_RESULT_OUTPUT_TOO_SMALL;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    status = vc_memory_validate_trusted_state(
        service, now_ms, &foreground, &snapshot);
    if (status == VC_MEMORY_READ_STATUS_OK &&
        (!vc_memory_caller_equal(
             &caller, &service->session.caller) ||
         !vc_memory_identity_matches_foreground(
             &service->session.lineage.target,
             &foreground))) {
        status = VC_MEMORY_READ_STATUS_UNAUTHENTICATED_CALLER;
    }
    if (status == VC_MEMORY_READ_STATUS_OK &&
        (request.session_id !=
             service->session.session_id ||
         request.request_id <=
             service->session.last_request_id)) {
        status = VC_MEMORY_READ_STATUS_STALE_SESSION;
    }
    if (status == VC_MEMORY_READ_STATUS_OK &&
        (request.target_process_id !=
             service->session.foreground.target_process_id ||
         request.target_generation !=
             service->session.foreground.target_generation)) {
        status = VC_MEMORY_READ_STATUS_STALE_TARGET;
    }
    if (status == VC_MEMORY_READ_STATUS_OK &&
        request.attestation_revision !=
            service->session.target_attestation_revision) {
        status = VC_MEMORY_READ_STATUS_STALE_SNAPSHOT;
    }
    if (status == VC_MEMORY_READ_STATUS_OK &&
        (service->session.remaining_operations == 0 ||
         (request.operation ==
              VC_MEMORY_READ_OPERATION_READ &&
          request.length >
              service->session.remaining_bytes))) {
        status = VC_MEMORY_READ_STATUS_QUOTA_EXHAUSTED;
    }

    vc_memory_response_init(
        &response, &request, status, service);
    if (status == VC_MEMORY_READ_STATUS_OK) {
        --service->session.remaining_operations;
        service->session.last_request_id =
            request.request_id;
        if (request.operation ==
            VC_MEMORY_READ_OPERATION_READ) {
            status = vc_memory_perform_read(
                service, &request, &response, now_ms);
        }
        response.status = status;
        response.state = service->session.active
                             ? VC_MEMORY_READ_STATE_ACTIVE
                             : VC_MEMORY_READ_STATE_REVOKED;
        response.session_id =
            service->session.active
                ? service->session.session_id
                : 0;
        response.remaining_bytes =
            service->session.active
                ? service->session.remaining_bytes
                : 0;
        response.remaining_operations =
            service->session.active
                ? service->session.remaining_operations
                : 0;
        if (status != VC_MEMORY_READ_STATUS_OK) {
            response.payload_size = 0;
            response.total_size =
                VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE;
            memset(response.payload, 0,
                   sizeof(response.payload));
        }
    }
    memset(&snapshot, 0, sizeof(snapshot));
    if (status == VC_MEMORY_READ_STATUS_MUTATED_TARGET ||
        status == VC_MEMORY_READ_STATUS_CLOCK_ROLLBACK ||
        status == VC_MEMORY_READ_STATUS_EXPIRED) {
        result = vc_memory_publish_once(
            service, &response, response_user,
            response_capacity, response_size,
            operation_status);
    } else {
        result = vc_memory_publish_response(
            service, request_wire, &request, &response,
            &caller, &foreground, now_ms,
            service->session.active,
            response_user, response_capacity,
            response_size, operation_status);
    }
    memset(request_wire, 0, sizeof(request_wire));
    memset(&caller, 0, sizeof(caller));
    memset(&foreground, 0, sizeof(foreground));
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    vc_memory_leave(service);
    return result;
}

size_t vc_memory_service_format_status(
    const vc_memory_service *service,
    char *buffer,
    size_t capacity)
{
    const char *text;
    size_t length;
    size_t copy_size;

    if (!vc_memory_service_valid(service) ||
        service->phase == VC_MEMORY_SERVICE_PHASE_STOPPED) {
        text = "Memory read service stopped";
    } else if (service->phase !=
               VC_MEMORY_SERVICE_PHASE_RUNNING) {
        text = "Memory read service initialized";
    } else if (service->journal.valid) {
        text = "Memory read result retry required";
    } else if (service->session.active) {
        text = "Memory read session active";
    } else {
        text = "Memory read service ready";
    }

    length = strlen(text);
    if (buffer == NULL || capacity == 0) {
        return length;
    }
    copy_size = length;
    if (copy_size >= capacity) {
        copy_size = capacity - 1u;
    }
    memcpy(buffer, text, copy_size);
    buffer[copy_size] = '\0';
    return length;
}
