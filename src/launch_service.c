#include "vitacheat/launch_service.h"

#include <string.h>

enum {
    VC_LAUNCH_SERVICE_MARKER = 0x56434c53u
};

_Static_assert(VC_LAUNCH_ABI_VERSION == UINT16_C(1),
               "launch service supports only ABI v1");
_Static_assert(VC_LAUNCH_SERVICE_V1_CAPABILITIES ==
                   VC_LAUNCH_CAPABILITY_ALL,
               "launch service v1 capability mask changed");

static bool vc_launch_dependencies_equal(
    const vc_launch_service_dependencies *left,
    const vc_launch_service_dependencies *right)
{
    return left->copy_from_user == right->copy_from_user &&
           left->copy_to_user == right->copy_to_user &&
           left->attest_caller == right->attest_caller &&
           left->get_foreground == right->get_foreground &&
           left->cleanup == right->cleanup &&
           left->context == right->context;
}

static bool vc_launch_dependencies_valid(
    const vc_launch_service_dependencies *dependencies)
{
    return dependencies != NULL &&
           dependencies->copy_from_user != NULL &&
           dependencies->copy_to_user != NULL &&
           dependencies->attest_caller != NULL &&
           dependencies->get_foreground != NULL;
}

static bool vc_launch_service_valid(const vc_launch_service *service)
{
    return service != NULL &&
           service->marker == (uint32_t)VC_LAUNCH_SERVICE_MARKER;
}

static vc_launch_service_status vc_launch_service_enter(
    vc_launch_service *service,
    bool require_running)
{
    if (!vc_launch_service_valid(service)) {
        return VC_LAUNCH_SERVICE_STATUS_NOT_INITIALIZED;
    }
    if (atomic_exchange_explicit(&service->transaction_busy, 1u,
                                 memory_order_acquire) != 0u) {
        return VC_LAUNCH_SERVICE_STATUS_BUSY;
    }
    if (require_running &&
        service->phase != VC_LAUNCH_SERVICE_PHASE_RUNNING) {
        vc_launch_service_status status =
            service->phase == VC_LAUNCH_SERVICE_PHASE_STOPPED
                ? VC_LAUNCH_SERVICE_STATUS_STOPPED
                : VC_LAUNCH_SERVICE_STATUS_NOT_INITIALIZED;

        atomic_store_explicit(&service->transaction_busy, 0u,
                              memory_order_release);
        return status;
    }
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

static void vc_launch_service_leave(vc_launch_service *service)
{
    atomic_store_explicit(&service->transaction_busy, 0u,
                          memory_order_release);
}

static bool vc_launch_bytes_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static vc_launch_service_status vc_launch_normalize_foreground(
    const vc_launch_foreground_snapshot *snapshot,
    vc_launch_foreground_snapshot *normalized)
{
    if (snapshot == NULL || normalized == NULL ||
        snapshot->sequence == 0 ||
        snapshot->present > 1u ||
        snapshot->title_id_size > VC_LAUNCH_SERVICE_TITLE_ID_MAX) {
        return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->sequence = snapshot->sequence;
    normalized->present = snapshot->present;
    if (snapshot->present == 0u) {
        if (snapshot->target_process_id != 0 ||
            snapshot->target_generation != 0 ||
            snapshot->title_id_size != 0 ||
            !vc_launch_bytes_zero(snapshot->title_id,
                                  sizeof(snapshot->title_id))) {
            return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
        }
        return VC_LAUNCH_SERVICE_STATUS_OK;
    }
    if (snapshot->target_process_id == 0 ||
        snapshot->target_generation == 0 ||
        snapshot->title_id_size == 0) {
        return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
    }

    normalized->target_process_id = snapshot->target_process_id;
    normalized->target_generation = snapshot->target_generation;
    normalized->title_id_size = snapshot->title_id_size;
    memcpy(normalized->title_id, snapshot->title_id,
           snapshot->title_id_size);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

static bool vc_launch_foreground_identity_equal(
    const vc_launch_foreground_snapshot *left,
    const vc_launch_foreground_snapshot *right)
{
    return left->present == right->present &&
           left->target_process_id == right->target_process_id &&
           left->target_generation == right->target_generation &&
           left->title_id_size == right->title_id_size &&
           memcmp(left->title_id, right->title_id,
                  sizeof(left->title_id)) == 0;
}

static bool vc_launch_foreground_equal(
    const vc_launch_foreground_snapshot *left,
    const vc_launch_foreground_snapshot *right)
{
    return left->sequence == right->sequence &&
           vc_launch_foreground_identity_equal(left, right);
}

static bool vc_launch_caller_equal(const vc_launch_trusted_caller *left,
                                   const vc_launch_trusted_caller *right)
{
    return left->role == right->role &&
           left->process_id == right->process_id &&
           left->process_generation == right->process_generation &&
           left->module_generation == right->module_generation;
}

static bool vc_launch_caller_valid(const vc_launch_trusted_caller *caller)
{
    return caller != NULL &&
           (caller->role == VC_LAUNCH_CALLER_SCE_SHELL ||
            caller->role == VC_LAUNCH_CALLER_GAME_PLUGIN) &&
           caller->reserved0 == 0 &&
           caller->process_id != 0 &&
           caller->process_generation != 0 &&
           caller->module_generation != 0;
}

static void vc_launch_service_clear_journal(vc_launch_service *service)
{
    memset(&service->journal, 0, sizeof(service->journal));
}

static void vc_launch_service_scrub_broker(vc_launch_service *service)
{
    const uint64_t next_request_id = service->broker.next_request_id;
    const uint64_t last_now_ms = service->broker.last_now_ms;
    const bool has_time = service->broker.has_time;
    const bool id_exhausted = service->broker.id_exhausted;

    memset(&service->broker, 0, sizeof(service->broker));
    service->broker.next_request_id = next_request_id;
    service->broker.last_now_ms = last_now_ms;
    service->broker.has_time = has_time;
    service->broker.id_exhausted = id_exhausted;
    service->broker.initialized = true;
}

static vc_launch_status vc_launch_service_sync_foreground(
    vc_launch_service *service,
    const vc_launch_foreground_snapshot *snapshot,
    uint64_t now_ms)
{
    vc_launch_status status = VC_LAUNCH_STATUS_OK;
    vc_launch_status second_status = VC_LAUNCH_STATUS_OK;
    const bool had_snapshot = service->foreground.sequence != 0;
    const bool identity_changed =
        !had_snapshot ||
        !vc_launch_foreground_identity_equal(&service->foreground, snapshot);

    if (had_snapshot && snapshot->sequence < service->foreground.sequence) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    if (had_snapshot && snapshot->sequence == service->foreground.sequence &&
        !vc_launch_foreground_equal(&service->foreground, snapshot)) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }

    if (identity_changed && service->foreground.present != 0u) {
        status = vc_launch_broker_process_exit(
            &service->broker,
            service->foreground.target_process_id,
            service->foreground.target_generation,
            now_ms);
    }

    if (snapshot->present != 0u) {
        second_status = vc_launch_broker_foreground_changed(
            &service->broker,
            snapshot->target_process_id,
            snapshot->target_generation,
            now_ms);
    } else if (!identity_changed || !had_snapshot) {
        second_status = vc_launch_broker_tick(&service->broker, now_ms);
    }

    if (identity_changed) {
        vc_launch_service_clear_journal(service);
    }
    service->foreground = *snapshot;

    if (status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK ||
        second_status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        service->broker.foreground_process_id = 0;
        service->broker.foreground_generation = 0;
        return VC_LAUNCH_STATUS_CLOCK_ROLLBACK;
    }
    if (status == VC_LAUNCH_STATUS_STALE_TARGET) {
        return status;
    }
    if (second_status != VC_LAUNCH_STATUS_OK) {
        return second_status;
    }
    return status;
}

static bool vc_launch_role_operation_allowed(
    const vc_launch_trusted_caller *caller,
    const vc_launch_request *request)
{
    if (caller->role == VC_LAUNCH_CALLER_SCE_SHELL) {
        return request->operation == VC_LAUNCH_OPERATION_SUBMIT ||
               request->operation == VC_LAUNCH_OPERATION_STATUS;
    }
    return request->operation == VC_LAUNCH_OPERATION_CLAIM ||
           request->operation == VC_LAUNCH_OPERATION_CANCEL ||
           request->operation == VC_LAUNCH_OPERATION_STATUS;
}

static bool vc_launch_journal_matches(
    const vc_launch_service_result_journal *journal,
    const uint8_t *request,
    const vc_launch_trusted_caller *caller,
    const vc_launch_foreground_snapshot *foreground,
    bool foreground_bound,
    uint64_t now_ms)
{
    if (!journal->valid ||
        journal->foreground_bound != foreground_bound ||
        now_ms < journal->committed_now_ms ||
        !vc_launch_caller_equal(&journal->caller, caller) ||
        memcmp(journal->request, request,
               VC_LAUNCH_REQUEST_WIRE_SIZE) != 0) {
        return false;
    }
    return !foreground_bound ||
           vc_launch_foreground_identity_equal(
               &journal->foreground, foreground);
}

static void vc_launch_store_journal(
    vc_launch_service *service,
    const uint8_t *request,
    const uint8_t *response,
    const vc_launch_trusted_caller *caller,
    const vc_launch_foreground_snapshot *foreground,
    bool foreground_bound,
    uint64_t now_ms,
    vc_launch_status operation_status)
{
    vc_launch_service_clear_journal(service);
    service->journal.caller = *caller;
    if (foreground_bound) {
        service->journal.foreground = *foreground;
    }
    memcpy(service->journal.request, request,
           VC_LAUNCH_REQUEST_WIRE_SIZE);
    memcpy(service->journal.response, response,
           VC_LAUNCH_RESPONSE_WIRE_SIZE);
    service->journal.committed_now_ms = now_ms;
    service->journal.operation_status = operation_status;
    service->journal.foreground_bound = foreground_bound;
    service->journal.valid = true;
}

static vc_launch_service_status vc_launch_copy_journal_response(
    vc_launch_service *service,
    void *response_user,
    size_t *response_size,
    vc_launch_status *operation_status)
{
    *operation_status =
        (vc_launch_status)service->journal.operation_status;
    if (!service->dependencies.copy_to_user(
            service->dependencies.context,
            response_user,
            service->journal.response,
            VC_LAUNCH_RESPONSE_WIRE_SIZE)) {
        return VC_LAUNCH_SERVICE_STATUS_COPY_TO_USER_FAILED;
    }
    *response_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    vc_launch_service_clear_journal(service);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_init(
    vc_launch_service *service,
    const vc_launch_service_dependencies *dependencies,
    uint64_t first_request_id)
{
    if (service == NULL ||
        !vc_launch_dependencies_valid(dependencies) ||
        first_request_id == 0) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    if (service->marker == (uint32_t)VC_LAUNCH_SERVICE_MARKER) {
        if (atomic_exchange_explicit(&service->transaction_busy, 1u,
                                     memory_order_acquire) != 0u) {
            return VC_LAUNCH_SERVICE_STATUS_BUSY;
        }
        if (service->initial_request_id == first_request_id &&
            vc_launch_dependencies_equal(&service->dependencies,
                                         dependencies)) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_OK;
        }
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }

    memset(service, 0, sizeof(*service));
    atomic_init(&service->transaction_busy, 0u);
    service->dependencies = *dependencies;
    service->initial_request_id = first_request_id;
    if (vc_launch_broker_init(&service->broker, first_request_id) !=
        VC_LAUNCH_STATUS_OK) {
        memset(service, 0, sizeof(*service));
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    service->phase = VC_LAUNCH_SERVICE_PHASE_INITIALIZED;
    service->marker = (uint32_t)VC_LAUNCH_SERVICE_MARKER;
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_start(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status)
{
    vc_launch_service_status service_status;

    if (lifecycle_status == NULL) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *lifecycle_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    service_status = vc_launch_service_enter(service, false);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    if (service->phase == VC_LAUNCH_SERVICE_PHASE_RUNNING) {
        *lifecycle_status = VC_LAUNCH_STATUS_OK;
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_OK;
    }

    *lifecycle_status =
        vc_launch_broker_service_reset(&service->broker, now_ms);
    vc_launch_service_scrub_broker(service);
    memset(&service->foreground, 0, sizeof(service->foreground));
    vc_launch_service_clear_journal(service);
    service->phase = VC_LAUNCH_SERVICE_PHASE_RUNNING;
    vc_launch_service_leave(service);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_stop(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status)
{
    vc_launch_service_status service_status;
    vc_launch_platform_cleanup_fn cleanup;
    void *context;
    bool cleanup_ok = true;

    if (lifecycle_status == NULL) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *lifecycle_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    service_status = vc_launch_service_enter(service, false);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    if (service->phase == VC_LAUNCH_SERVICE_PHASE_STOPPED) {
        *lifecycle_status = VC_LAUNCH_STATUS_OK;
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_OK;
    }

    *lifecycle_status =
        vc_launch_broker_service_reset(&service->broker, now_ms);
    vc_launch_service_scrub_broker(service);
    memset(&service->foreground, 0, sizeof(service->foreground));
    vc_launch_service_clear_journal(service);
    service->phase = VC_LAUNCH_SERVICE_PHASE_STOPPED;
    cleanup = service->dependencies.cleanup;
    context = service->dependencies.context;

    if (cleanup != NULL) {
        cleanup_ok = cleanup(context);
    }
    vc_launch_service_leave(service);
    return cleanup_ok ? VC_LAUNCH_SERVICE_STATUS_OK
                      : VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE;
}

vc_launch_service_status vc_launch_service_reset(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status)
{
    vc_launch_service_status service_status;

    if (lifecycle_status == NULL) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *lifecycle_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    service_status = vc_launch_service_enter(service, true);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    *lifecycle_status =
        vc_launch_broker_service_reset(&service->broker, now_ms);
    vc_launch_service_scrub_broker(service);
    memset(&service->foreground, 0, sizeof(service->foreground));
    vc_launch_service_clear_journal(service);
    vc_launch_service_leave(service);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_tick(
    vc_launch_service *service,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status)
{
    vc_launch_service_status service_status;

    if (lifecycle_status == NULL) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *lifecycle_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    service_status = vc_launch_service_enter(service, true);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    if (service->journal.valid) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_RESULT_PENDING;
    }
    *lifecycle_status = vc_launch_broker_tick(&service->broker, now_ms);
    vc_launch_service_leave(service);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_foreground_changed(
    vc_launch_service *service,
    const vc_launch_foreground_snapshot *snapshot,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status)
{
    vc_launch_foreground_snapshot normalized;
    vc_launch_service_status service_status;

    if (lifecycle_status == NULL) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *lifecycle_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    service_status = vc_launch_service_enter(service, true);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    service_status =
        vc_launch_normalize_foreground(snapshot, &normalized);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        vc_launch_service_leave(service);
        return service_status;
    }
    if (service->foreground.sequence != 0 &&
        (normalized.sequence < service->foreground.sequence ||
         (normalized.sequence == service->foreground.sequence &&
          !vc_launch_foreground_equal(&service->foreground, &normalized)))) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
    }
    if (service->journal.valid &&
        vc_launch_foreground_identity_equal(
            &service->foreground, &normalized)) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_RESULT_PENDING;
    }
    *lifecycle_status = vc_launch_service_sync_foreground(
        service, &normalized, now_ms);
    vc_launch_service_leave(service);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_process_exit(
    vc_launch_service *service,
    uint32_t target_process_id,
    uint64_t target_generation,
    uint64_t snapshot_sequence,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status)
{
    vc_launch_service_status service_status;

    if (lifecycle_status == NULL ||
        target_process_id == 0 ||
        target_generation == 0 ||
        snapshot_sequence == 0) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *lifecycle_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    service_status = vc_launch_service_enter(service, true);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    if (service->foreground.present == 0u ||
        service->foreground.target_process_id != target_process_id ||
        service->foreground.target_generation != target_generation ||
        snapshot_sequence <= service->foreground.sequence) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
    }

    *lifecycle_status = vc_launch_broker_process_exit(
        &service->broker, target_process_id, target_generation, now_ms);
    service->broker.foreground_process_id = 0;
    service->broker.foreground_generation = 0;
    memset(&service->foreground, 0, sizeof(service->foreground));
    service->foreground.sequence = snapshot_sequence;
    vc_launch_service_clear_journal(service);
    vc_launch_service_leave(service);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_plugin_unload(
    vc_launch_service *service,
    uint32_t target_process_id,
    uint64_t target_generation,
    uint64_t now_ms,
    vc_launch_status *lifecycle_status)
{
    vc_launch_service_status service_status;

    if (lifecycle_status == NULL ||
        target_process_id == 0 ||
        target_generation == 0) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *lifecycle_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    service_status = vc_launch_service_enter(service, true);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    if (service->foreground.present == 0u ||
        service->foreground.target_process_id != target_process_id ||
        service->foreground.target_generation != target_generation) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
    }

    *lifecycle_status = vc_launch_broker_plugin_unload(
        &service->broker, target_process_id, target_generation, now_ms);
    vc_launch_service_clear_journal(service);
    vc_launch_service_leave(service);
    return VC_LAUNCH_SERVICE_STATUS_OK;
}

vc_launch_service_status vc_launch_service_dispatch(
    vc_launch_service *service,
    const void *caller_context,
    const void *request_user,
    size_t request_size,
    void *response_user,
    size_t response_capacity,
    uint64_t now_ms,
    size_t *response_size,
    vc_launch_status *operation_status)
{
    uint8_t request_local[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_local[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground_raw;
    vc_launch_foreground_snapshot foreground;
    vc_launch_request request;
    vc_launch_attestation_result attestation;
    vc_launch_service_status service_status;
    vc_launch_status decode_status;
    vc_launch_status dispatch_status;
    vc_launch_status sync_status = VC_LAUNCH_STATUS_OK;
    size_t encoded_size = 0;
    bool foreground_bound = false;
    bool foreground_identity_changed = false;

    memset(&foreground, 0, sizeof(foreground));
    if (response_size == NULL || operation_status == NULL) {
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    *response_size = 0;
    *operation_status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;

    service_status = vc_launch_service_enter(service, true);
    if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
        return service_status;
    }
    if (request_size != VC_LAUNCH_REQUEST_WIRE_SIZE) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_INVALID_INPUT_SIZE;
    }
    if (request_user == NULL || response_user == NULL) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT;
    }
    if (response_capacity < VC_LAUNCH_RESPONSE_WIRE_SIZE) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_OUTPUT_TOO_SMALL;
    }

    memset(request_local, 0, sizeof(request_local));
    if (!service->dependencies.copy_from_user(
            service->dependencies.context,
            request_local,
            request_user,
            sizeof(request_local))) {
        memset(request_local, 0, sizeof(request_local));
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_COPY_FROM_USER_FAILED;
    }

    memset(&caller, 0, sizeof(caller));
    attestation = service->dependencies.attest_caller(
        service->dependencies.context, caller_context, &caller);
    if (attestation == VC_LAUNCH_ATTESTATION_FAILED) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE;
    }
    if (attestation != VC_LAUNCH_ATTESTATION_ACCEPTED ||
        !vc_launch_caller_valid(&caller)) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER;
    }

    decode_status = vc_launch_request_decode(
        request_local, sizeof(request_local), &request);
    if (decode_status == VC_LAUNCH_STATUS_OK) {
        if (request.caller_role != caller.role) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER;
        }
        if (!vc_launch_role_operation_allowed(&caller, &request)) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_UNAUTHORIZED_CALLER;
        }
        if (caller.role == VC_LAUNCH_CALLER_GAME_PLUGIN &&
            (request.target_process_id != caller.process_id ||
             request.target_generation != caller.process_generation)) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER;
        }

        memset(&foreground_raw, 0, sizeof(foreground_raw));
        if (!service->dependencies.get_foreground(
                service->dependencies.context, &foreground_raw)) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE;
        }
        service_status = vc_launch_normalize_foreground(
            &foreground_raw, &foreground);
        if (service_status != VC_LAUNCH_SERVICE_STATUS_OK) {
            vc_launch_service_leave(service);
            return service_status;
        }
        if (service->foreground.sequence != 0 &&
            (foreground.sequence < service->foreground.sequence ||
             (foreground.sequence == service->foreground.sequence &&
              !vc_launch_foreground_equal(&service->foreground,
                                          &foreground)))) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
        }

        foreground_bound = true;
        foreground_identity_changed =
            service->foreground.sequence == 0 ||
            !vc_launch_foreground_identity_equal(
                &service->foreground, &foreground);
        if (service->journal.valid &&
            vc_launch_foreground_identity_equal(
                &service->journal.foreground, &foreground)) {
            if (vc_launch_journal_matches(
                    &service->journal, request_local, &caller,
                    &foreground, true, now_ms)) {
                service_status = vc_launch_copy_journal_response(
                    service, response_user, response_size,
                    operation_status);
                vc_launch_service_leave(service);
                return service_status;
            }
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_RESULT_PENDING;
        }
        if (!foreground_identity_changed &&
            (foreground.present == 0u ||
             request.target_process_id != foreground.target_process_id ||
             request.target_generation != foreground.target_generation)) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
        }
        if (!foreground_identity_changed && request.now_ms != now_ms) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
        }

        sync_status = vc_launch_service_sync_foreground(
            service, &foreground, now_ms);
        if (foreground.present == 0u ||
            request.target_process_id != foreground.target_process_id ||
            request.target_generation != foreground.target_generation) {
            vc_launch_service_leave(service);
            return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
        }
    }

    if (service->journal.valid) {
        if (vc_launch_journal_matches(
                &service->journal, request_local, &caller,
                &foreground, foreground_bound, now_ms)) {
            service_status = vc_launch_copy_journal_response(
                service, response_user, response_size, operation_status);
            vc_launch_service_leave(service);
            return service_status;
        }
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_RESULT_PENDING;
    }

    if (decode_status == VC_LAUNCH_STATUS_OK &&
        request.now_ms != now_ms) {
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT;
    }

    memset(response_local, 0, sizeof(response_local));
    dispatch_status = vc_launch_broker_dispatch_wire(
        &service->broker,
        request_local,
        sizeof(request_local),
        response_local,
        sizeof(response_local),
        &encoded_size);
    if (encoded_size != VC_LAUNCH_RESPONSE_WIRE_SIZE) {
        memset(response_local, 0, sizeof(response_local));
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE;
    }
    if (decode_status == VC_LAUNCH_STATUS_OK &&
        sync_status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK &&
        dispatch_status != VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        memset(response_local, 0, sizeof(response_local));
        vc_launch_service_leave(service);
        return VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE;
    }

    vc_launch_store_journal(
        service, request_local, response_local, &caller,
        &foreground, foreground_bound, now_ms, dispatch_status);
    service_status = vc_launch_copy_journal_response(
        service, response_user, response_size, operation_status);
    vc_launch_service_leave(service);
    return service_status;
}
