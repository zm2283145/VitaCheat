#include "vitacheat/foreign_target_gate.h"

#include <limits.h>

enum {
    VC_FTG_SERVICE_MARKER = 0x56434647u
};

static void vc_ftg_wipe(void *value, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)value;

    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void vc_ftg_copy_bytes(
    void *destination,
    const void *source,
    size_t size)
{
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;

    while (size != 0u) {
        *output++ = *input++;
        --size;
    }
}

static bool vc_ftg_bytes_zero(const void *value, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)value;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

static bool vc_ftg_bytes_equal(
    const void *left_value,
    const void *right_value,
    size_t size)
{
    const uint8_t *left = (const uint8_t *)left_value;
    const uint8_t *right = (const uint8_t *)right_value;

    while (size != 0u) {
        if (*left++ != *right++) {
            return false;
        }
        --size;
    }
    return true;
}

static size_t vc_ftg_text_size(
    const uint8_t *text,
    size_t capacity)
{
    size_t size = 0u;

    while (size < capacity && text[size] != 0u) {
        ++size;
    }
    return size;
}

static bool vc_ftg_exact_text(
    const uint8_t *actual,
    const uint8_t *expected,
    size_t capacity)
{
    size_t index;
    bool saw_nul = false;

    for (index = 0u; index < capacity; ++index) {
        if (actual[index] != expected[index]) {
            return false;
        }
        if (actual[index] == 0u) {
            saw_nul = true;
            break;
        }
    }
    if (!saw_nul) {
        return false;
    }
    for (++index; index < capacity; ++index) {
        if (actual[index] != 0u || expected[index] != 0u) {
            return false;
        }
    }
    return true;
}

static bool vc_ftg_dependencies_valid(
    const vc_ftg_dependencies *dependencies)
{
    return dependencies != NULL &&
           dependencies->get_caller_pid != NULL &&
           dependencies->get_time_us != NULL &&
           dependencies->get_title_id != NULL &&
           dependencies->get_main_module != NULL &&
           dependencies->copy_from_user != NULL &&
           dependencies->copy_to_user != NULL &&
           dependencies->read_process != NULL;
}

static bool vc_ftg_config_text_valid(
    const uint8_t *text,
    size_t capacity,
    size_t exact_size)
{
    const size_t size = vc_ftg_text_size(text, capacity);

    return size == exact_size && size < capacity &&
           vc_ftg_bytes_zero(
               text + size + 1u, capacity - size - 1u);
}

static bool vc_ftg_config_valid(const vc_ftg_config *config)
{
    return config != NULL &&
           config->timeout_us == VC_FTG_DEFAULT_TIMEOUT_US &&
           vc_ftg_config_text_valid(
               config->target_title_id,
               VC_FTG_TITLE_ID_CAPACITY,
               VC_FTG_TITLE_ID_LENGTH) &&
           vc_ftg_config_text_valid(
               config->controller_title_id,
               VC_FTG_TITLE_ID_CAPACITY,
               VC_FTG_TITLE_ID_LENGTH) &&
           !vc_ftg_bytes_equal(
               config->target_title_id,
               config->controller_title_id,
               VC_FTG_TITLE_ID_CAPACITY) &&
           vc_ftg_text_size(
               config->target_module_name,
               VC_FTG_MODULE_NAME_CAPACITY) != 0u &&
           vc_ftg_text_size(
               config->target_module_name,
               VC_FTG_MODULE_NAME_CAPACITY) <
               VC_FTG_MODULE_NAME_CAPACITY &&
           vc_ftg_bytes_zero(
               config->target_module_name +
                   vc_ftg_text_size(
                       config->target_module_name,
                       VC_FTG_MODULE_NAME_CAPACITY) +
                   1u,
               VC_FTG_MODULE_NAME_CAPACITY -
                   vc_ftg_text_size(
                       config->target_module_name,
                       VC_FTG_MODULE_NAME_CAPACITY) -
                   1u);
}

static bool vc_ftg_service_valid(const vc_ftg_service *service)
{
    return service != NULL &&
           service->marker == (uint32_t)VC_FTG_SERVICE_MARKER;
}

static void vc_ftg_scrub_registry(vc_ftg_service *service)
{
    vc_ftg_wipe(&service->registry, sizeof(service->registry));
}

static void vc_ftg_scrub_session(vc_ftg_service *service)
{
    vc_ftg_wipe(&service->session, sizeof(service->session));
}

static void vc_ftg_trip_fail_closed(
    vc_ftg_service *service,
    vc_ftg_diagnostic_stage stage)
{
    unsigned int expected = VC_FTG_DIAGNOSTIC_NONE;

    (void)atomic_compare_exchange_strong_explicit(
        &service->fail_closed_stage,
        &expected,
        (unsigned int)stage,
        memory_order_release,
        memory_order_relaxed);
}

static void vc_ftg_increment_counter(
    atomic_uint *counter,
    atomic_uint *saturation_flags,
    uint32_t saturation_flag)
{
    unsigned int value = atomic_load_explicit(
        counter, memory_order_relaxed);

    for (;;) {
        if (value == UINT_MAX) {
            (void)atomic_fetch_or_explicit(
                saturation_flags,
                saturation_flag,
                memory_order_relaxed);
            return;
        }
        if (atomic_compare_exchange_weak_explicit(
                counter,
                &value,
                value + 1u,
                memory_order_relaxed,
                memory_order_relaxed)) {
            return;
        }
    }
}

static void vc_ftg_record_lifecycle(
    vc_ftg_service *service,
    vc_ftg_process_event event,
    vc_ftg_result result,
    vc_ftg_diagnostic_stage stage)
{
    atomic_store_explicit(
        &service->last_lifecycle_event,
        (unsigned int)event,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->last_lifecycle_result,
        (int)result,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->last_lifecycle_stage,
        (unsigned int)stage,
        memory_order_release);
}

static void vc_ftg_reset_lifecycle_diagnostics(
    vc_ftg_service *service)
{
    atomic_store_explicit(
        &service->create_callback_count, 0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->start_callback_count, 0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->start_revalidation_count, 0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->ignored_non_target_count, 0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->target_create_match_count, 0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->target_create_authorized_count, 0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->counter_saturation_flags, 0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->last_lifecycle_event,
        0u,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->last_lifecycle_result,
        VC_FTG_RESULT_OK,
        memory_order_relaxed);
    atomic_store_explicit(
        &service->last_lifecycle_stage,
        VC_FTG_DIAGNOSTIC_NONE,
        memory_order_release);
}

static bool vc_ftg_is_fail_closed(
    const vc_ftg_service *service)
{
    return atomic_load_explicit(
               &service->fail_closed_stage,
               memory_order_acquire) !=
           VC_FTG_DIAGNOSTIC_NONE;
}

static void vc_ftg_consume_fail_closed(
    vc_ftg_service *service)
{
    const unsigned int stage = atomic_load_explicit(
        &service->fail_closed_stage, memory_order_acquire);

    if (stage != VC_FTG_DIAGNOSTIC_NONE) {
        vc_ftg_scrub_session(service);
        vc_ftg_scrub_registry(service);
        service->last_result =
            VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
        service->diagnostic_stage = stage;
    }
}

static vc_ftg_result vc_ftg_enter(
    vc_ftg_service *service,
    bool allow_fail_closed,
    bool require_running,
    bool require_full_gate)
{
    if (!vc_ftg_service_valid(service)) {
        return VC_FTG_RESULT_INVALID_ARGUMENT;
    }
    if (atomic_load_explicit(
            &service->callback_active,
            memory_order_acquire) != 0u ||
        atomic_load_explicit(
            &service->operation_active,
            memory_order_acquire) != 0u) {
        return VC_FTG_RESULT_BUSY;
    }
    if (atomic_exchange_explicit(
            &service->transaction_busy,
            1u,
            memory_order_acquire) != 0u) {
        return VC_FTG_RESULT_BUSY;
    }
    if (atomic_load_explicit(
            &service->callback_active,
            memory_order_acquire) != 0u ||
        atomic_load_explicit(
            &service->operation_active,
            memory_order_acquire) != 0u) {
        atomic_store_explicit(
            &service->transaction_busy,
            0u,
            memory_order_release);
        return VC_FTG_RESULT_BUSY;
    }
    vc_ftg_consume_fail_closed(service);
    if (!allow_fail_closed && vc_ftg_is_fail_closed(service)) {
        atomic_store_explicit(
            &service->transaction_busy,
            0u,
            memory_order_release);
        return VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
    }
    if (require_running && !service->running) {
        const vc_ftg_result result =
            service->last_result == VC_FTG_RESULT_STOPPED
                ? VC_FTG_RESULT_STOPPED
                : VC_FTG_RESULT_NOT_RUNNING;

        atomic_store_explicit(
            &service->transaction_busy,
            0u,
            memory_order_release);
        return result;
    }
    if (require_full_gate && !service->config.enabled) {
        atomic_store_explicit(
            &service->transaction_busy,
            0u,
            memory_order_release);
        return VC_FTG_RESULT_DISABLED;
    }
    if (require_full_gate &&
        (!service->config.api_available ||
         !service->config.foreign_lifecycle_enabled)) {
        atomic_store_explicit(
            &service->transaction_busy,
            0u,
            memory_order_release);
        return VC_FTG_RESULT_DIAGNOSTIC_ONLY;
    }
    return VC_FTG_RESULT_OK;
}

static void vc_ftg_leave(vc_ftg_service *service)
{
    atomic_store_explicit(
        &service->transaction_busy,
        0u,
        memory_order_release);
}

static void vc_ftg_operation_begin(vc_ftg_service *service)
{
    atomic_store_explicit(
        &service->operation_active, 1u, memory_order_release);
    vc_ftg_leave(service);
}

static void vc_ftg_operation_lock(vc_ftg_service *service)
{
    (void)atomic_exchange_explicit(
        &service->transaction_busy, 1u, memory_order_acquire);
}

static void vc_ftg_operation_finish(vc_ftg_service *service)
{
    atomic_store_explicit(
        &service->operation_active, 0u, memory_order_release);
    vc_ftg_leave(service);
}

static bool vc_ftg_get_caller(
    vc_ftg_service *service,
    uint32_t *process_id)
{
    *process_id = 0u;
    return service->dependencies.get_caller_pid(
               service->dependencies.context, process_id) &&
           *process_id != 0u &&
           *process_id <= INT32_MAX;
}

static bool vc_ftg_get_time(
    vc_ftg_service *service,
    uint64_t *now_us)
{
    *now_us = 0u;
    return service->dependencies.get_time_us(
        service->dependencies.context, now_us);
}

static vc_ftg_result vc_ftg_copy_request(
    vc_ftg_service *service,
    uint32_t process_id,
    void *destination,
    const void *request_user,
    size_t size)
{
    if (request_user == NULL ||
        !service->dependencies.copy_from_user(
            service->dependencies.context,
            process_id,
            destination,
            request_user,
            size)) {
        vc_ftg_wipe(destination, size);
        return VC_FTG_RESULT_COPY_FROM_USER_FAILED;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_copy_response(
    vc_ftg_service *service,
    uint32_t process_id,
    void *response_user,
    const void *response,
    size_t size)
{
    if (response_user == NULL ||
        !service->dependencies.copy_to_user(
            service->dependencies.context,
            process_id,
            response_user,
            response,
            size)) {
        return VC_FTG_RESULT_COPY_TO_USER_FAILED;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_validate_status_request(
    const vc_ftg_status_request *request)
{
    if (request->version != VC_FTG_STATUS_VERSION_1 &&
        request->version != VC_FTG_STATUS_VERSION_2) {
        return VC_FTG_RESULT_INVALID_VERSION;
    }
    if (request->struct_size != sizeof(*request)) {
        return VC_FTG_RESULT_INVALID_SIZE;
    }
    if (request->capabilities != 0u) {
        return VC_FTG_RESULT_CAPABILITY_MISMATCH;
    }
    if (request->reserved0 != 0u ||
        request->reserved1 != 0u) {
        return VC_FTG_RESULT_RESERVED_NOT_ZERO;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_validate_open_request(
    const vc_ftg_open_request *request)
{
    if (request->version != VC_FTG_ABI_VERSION) {
        return VC_FTG_RESULT_INVALID_VERSION;
    }
    if (request->struct_size != sizeof(*request)) {
        return VC_FTG_RESULT_INVALID_SIZE;
    }
    if (request->capabilities != 0u) {
        return VC_FTG_RESULT_CAPABILITY_MISMATCH;
    }
    if (request->reserved0 != 0u ||
        request->reserved1 != 0u) {
        return VC_FTG_RESULT_RESERVED_NOT_ZERO;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_validate_session_request(
    const vc_ftg_session_request *request)
{
    if (request->version != VC_FTG_ABI_VERSION) {
        return VC_FTG_RESULT_INVALID_VERSION;
    }
    if (request->struct_size != sizeof(*request)) {
        return VC_FTG_RESULT_INVALID_SIZE;
    }
    if (request->capabilities !=
        VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ) {
        return VC_FTG_RESULT_CAPABILITY_MISMATCH;
    }
    if (request->handle == 0u) {
        return VC_FTG_RESULT_INVALID_HANDLE;
    }
    if (request->reserved0 != 0u) {
        return VC_FTG_RESULT_RESERVED_NOT_ZERO;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_validate_read_request(
    const vc_ftg_read_request *request)
{
    if (request->version != VC_FTG_ABI_VERSION) {
        return VC_FTG_RESULT_INVALID_VERSION;
    }
    if (request->struct_size != sizeof(*request)) {
        return VC_FTG_RESULT_INVALID_SIZE;
    }
    if (request->capabilities !=
        VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ) {
        return VC_FTG_RESULT_CAPABILITY_MISMATCH;
    }
    if (request->handle == 0u) {
        return VC_FTG_RESULT_INVALID_HANDLE;
    }
    if (request->reserved0 != 0u) {
        return VC_FTG_RESULT_RESERVED_NOT_ZERO;
    }
    if (request->length == 0u ||
        request->length > VC_FTG_MAX_READ) {
        return VC_FTG_RESULT_RANGE_DENIED;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_validate_controller(
    const vc_ftg_service *service,
    const uint8_t title_id[VC_FTG_TITLE_ID_CAPACITY])
{
    return vc_ftg_exact_text(
               title_id,
               service->config.controller_title_id,
               VC_FTG_TITLE_ID_CAPACITY)
               ? VC_FTG_RESULT_OK
               : VC_FTG_RESULT_CALLER_TITLE_MISMATCH;
}

static bool vc_ftg_module_equal(
    const vc_ftg_module_snapshot *left,
    const vc_ftg_module_snapshot *right)
{
    uint32_t index;

    if (left->process_id != right->process_id ||
        left->kernel_module_id != right->kernel_module_id ||
        left->process_module_id != right->process_module_id ||
        left->module_fingerprint != right->module_fingerprint ||
        left->segment_count != right->segment_count ||
        !vc_ftg_bytes_equal(
            left->module_name,
            right->module_name,
            sizeof(left->module_name))) {
        return false;
    }
    for (index = 0u; index < left->segment_count; ++index) {
        if (left->segments[index].base !=
                right->segments[index].base ||
            left->segments[index].size !=
                right->segments[index].size ||
            left->segments[index].permissions !=
                right->segments[index].permissions) {
            return false;
        }
    }
    return true;
}

static vc_ftg_result vc_ftg_validate_module(
    const vc_ftg_service *service,
    uint32_t process_id,
    const vc_ftg_module_snapshot *module)
{
    uint32_t left;

    if (module->process_id != process_id ||
        module->kernel_module_id <= 0 ||
        module->process_module_id <= 0 ||
        module->module_fingerprint == 0u ||
        module->segment_count == 0u ||
        module->segment_count > VC_FTG_MAX_SEGMENTS ||
        !vc_ftg_exact_text(
            module->module_name,
            service->config.target_module_name,
            VC_FTG_MODULE_NAME_CAPACITY)) {
        return VC_FTG_RESULT_MODULE_MISMATCH;
    }
    for (left = 0u; left < module->segment_count; ++left) {
        const vc_ftg_segment_snapshot *segment =
            &module->segments[left];
        uint32_t right;

        if (segment->base == (uintptr_t)0 ||
            segment->size == 0u ||
            segment->base > UINTPTR_MAX - segment->size ||
            (segment->permissions &
             ~VC_FTG_PERMISSION_KNOWN_MASK) != 0u) {
            return VC_FTG_RESULT_MODULE_MISMATCH;
        }
        for (right = left + 1u;
             right < module->segment_count;
             ++right) {
            const vc_ftg_segment_snapshot *other =
                &module->segments[right];

            if (other->base == (uintptr_t)0 ||
                other->size == 0u ||
                other->base > UINTPTR_MAX - other->size ||
                (segment->base <
                     other->base + other->size &&
                 other->base <
                     segment->base + segment->size)) {
                return VC_FTG_RESULT_MODULE_MISMATCH;
            }
        }
    }
    for (; left < VC_FTG_MAX_SEGMENTS; ++left) {
        if (!vc_ftg_bytes_zero(
                &module->segments[left],
                sizeof(module->segments[left]))) {
            return VC_FTG_RESULT_MODULE_MISMATCH;
        }
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_advance_revision(
    vc_ftg_service *service,
    uint64_t *revision)
{
    if (service->revision_exhausted) {
        vc_ftg_trip_fail_closed(
            service,
            VC_FTG_DIAGNOSTIC_REVISION_EXHAUSTED);
        return VC_FTG_RESULT_REVISION_EXHAUSTED;
    }
    *revision = service->next_revision;
    if (service->next_revision == UINT64_MAX) {
        service->revision_exhausted = true;
    } else {
        ++service->next_revision;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_assign_generation(
    vc_ftg_service *service,
    uint64_t *generation)
{
    if (service->generation_exhausted) {
        vc_ftg_trip_fail_closed(
            service,
            VC_FTG_DIAGNOSTIC_GENERATION_EXHAUSTED);
        return VC_FTG_RESULT_GENERATION_EXHAUSTED;
    }
    *generation = service->next_generation;
    if (service->next_generation == UINT64_MAX) {
        service->generation_exhausted = true;
    } else {
        ++service->next_generation;
    }
    return VC_FTG_RESULT_OK;
}

static vc_ftg_result vc_ftg_fail_event(
    vc_ftg_service *service,
    vc_ftg_diagnostic_stage stage)
{
    vc_ftg_trip_fail_closed(service, stage);
    vc_ftg_consume_fail_closed(service);
    return VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
}

static bool vc_ftg_full_gate_available(
    const vc_ftg_service *service)
{
    return service->config.enabled &&
           service->config.api_available &&
           service->config.foreign_lifecycle_enabled &&
           service->registered &&
           service->running &&
           !vc_ftg_is_fail_closed(service);
}

vc_ftg_result vc_ftg_service_init(
    vc_ftg_service *service,
    const vc_ftg_config *config,
    const vc_ftg_dependencies *dependencies,
    uint64_t first_generation,
    uint64_t first_revision,
    uint64_t first_handle)
{
    if (service == NULL ||
        !vc_ftg_config_valid(config) ||
        !vc_ftg_dependencies_valid(dependencies) ||
        first_generation == 0u ||
        first_revision == 0u ||
        first_handle == 0u) {
        return VC_FTG_RESULT_INVALID_ARGUMENT;
    }
    vc_ftg_wipe(service, sizeof(*service));
    vc_ftg_copy_bytes(
        &service->config, config, sizeof(service->config));
    vc_ftg_copy_bytes(
        &service->dependencies,
        dependencies,
        sizeof(service->dependencies));
    atomic_init(&service->transaction_busy, 0u);
    atomic_init(&service->operation_active, 0u);
    atomic_init(&service->callback_active, 0u);
    atomic_init(
        &service->fail_closed_stage,
        VC_FTG_DIAGNOSTIC_NONE);
    atomic_init(&service->create_callback_count, 0u);
    atomic_init(&service->start_callback_count, 0u);
    atomic_init(&service->start_revalidation_count, 0u);
    atomic_init(&service->ignored_non_target_count, 0u);
    atomic_init(&service->target_create_match_count, 0u);
    atomic_init(&service->target_create_authorized_count, 0u);
    atomic_init(&service->counter_saturation_flags, 0u);
    atomic_init(&service->last_lifecycle_event, 0u);
    atomic_init(
        &service->last_lifecycle_result,
        VC_FTG_RESULT_OK);
    atomic_init(
        &service->last_lifecycle_stage,
        VC_FTG_DIAGNOSTIC_NONE);
    service->next_generation = first_generation;
    service->next_revision = first_revision;
    service->next_handle = first_handle;
    service->last_result = config->enabled
                               ? VC_FTG_RESULT_NOT_RUNNING
                               : VC_FTG_RESULT_DISABLED;
    service->marker = (uint32_t)VC_FTG_SERVICE_MARKER;
    return VC_FTG_RESULT_OK;
}

vc_ftg_result vc_ftg_service_start(vc_ftg_service *service)
{
    vc_ftg_result result =
        vc_ftg_enter(service, true, false, false);

    if (result != VC_FTG_RESULT_OK) {
        return result;
    }
    vc_ftg_scrub_registry(service);
    vc_ftg_scrub_session(service);
    vc_ftg_reset_lifecycle_diagnostics(service);
    service->running = true;
    service->last_result = service->config.enabled
                               ? VC_FTG_RESULT_OK
                               : VC_FTG_RESULT_DISABLED;
    service->diagnostic_stage =
        VC_FTG_DIAGNOSTIC_INITIAL_RECONCILIATION_UNPROVEN;
    vc_ftg_leave(service);
    return VC_FTG_RESULT_OK;
}

vc_ftg_result vc_ftg_service_set_registered(
    vc_ftg_service *service,
    bool registered,
    vc_ftg_diagnostic_stage failure_stage)
{
    vc_ftg_result result =
        vc_ftg_enter(service, true, true, false);

    if (result != VC_FTG_RESULT_OK) {
        return result;
    }
    service->registered = registered;
    if (registered) {
        service->runtime_unload_blocked = true;
        service->diagnostic_stage =
            service->config.foreign_lifecycle_enabled
                ? VC_FTG_DIAGNOSTIC_REGISTERED
                : VC_FTG_DIAGNOSTIC_BACKGROUND_LIFECYCLE_UNPROVEN;
        service->last_result =
            service->config.foreign_lifecycle_enabled
                ? VC_FTG_RESULT_OK
                : VC_FTG_RESULT_DIAGNOSTIC_ONLY;
    } else {
        service->diagnostic_stage = failure_stage;
        service->last_result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    vc_ftg_leave(service);
    return VC_FTG_RESULT_OK;
}

vc_ftg_result vc_ftg_service_stop(vc_ftg_service *service)
{
    vc_ftg_result result =
        vc_ftg_enter(service, true, false, false);

    if (result != VC_FTG_RESULT_OK) {
        return result;
    }
    vc_ftg_scrub_session(service);
    vc_ftg_scrub_registry(service);
    service->registered = false;
    service->running = false;
    service->last_result = VC_FTG_RESULT_STOPPED;
    vc_ftg_leave(service);
    return VC_FTG_RESULT_OK;
}

vc_ftg_result vc_ftg_service_process_event_with_type(
    vc_ftg_service *service,
    vc_ftg_process_event event,
    uint32_t process_id,
    uint32_t event_type)
{
    uint8_t title_id[VC_FTG_TITLE_ID_CAPACITY];
    vc_ftg_module_snapshot module;
    vc_ftg_result result = VC_FTG_RESULT_OK;
    vc_ftg_diagnostic_stage lifecycle_stage =
        VC_FTG_DIAGNOSTIC_NONE;
    bool is_target = false;
    bool need_title = true;
    bool need_module = false;

    (void)event_type;
    vc_ftg_wipe(title_id, sizeof(title_id));
    vc_ftg_wipe(&module, sizeof(module));
    if (!vc_ftg_service_valid(service) ||
        process_id == 0u ||
        process_id > INT32_MAX ||
        event < VC_FTG_PROCESS_CREATED ||
        event > VC_FTG_PROCESS_KILLED) {
        return VC_FTG_RESULT_INVALID_ARGUMENT;
    }
    if (event == VC_FTG_PROCESS_CREATED) {
        vc_ftg_increment_counter(
            &service->create_callback_count,
            &service->counter_saturation_flags,
            VC_FTG_COUNTER_SAT_CREATE_CALLBACK);
    } else if (event == VC_FTG_PROCESS_STARTED) {
        vc_ftg_increment_counter(
            &service->start_callback_count,
            &service->counter_saturation_flags,
            VC_FTG_COUNTER_SAT_START_CALLBACK);
    }
    if (atomic_exchange_explicit(
            &service->callback_active,
            1u,
            memory_order_acquire) != 0u) {
        lifecycle_stage =
            VC_FTG_DIAGNOSTIC_CALLBACK_CONTENTION;
        vc_ftg_trip_fail_closed(
            service, lifecycle_stage);
        vc_ftg_record_lifecycle(
            service, event, VC_FTG_RESULT_BUSY,
            lifecycle_stage);
        return VC_FTG_RESULT_BUSY;
    }
    if (atomic_load_explicit(
            &service->operation_active,
            memory_order_acquire) != 0u ||
        atomic_exchange_explicit(
            &service->transaction_busy,
            1u,
            memory_order_acquire) != 0u) {
        lifecycle_stage =
            VC_FTG_DIAGNOSTIC_CALLBACK_CONTENTION;
        vc_ftg_trip_fail_closed(
            service, lifecycle_stage);
        vc_ftg_record_lifecycle(
            service, event, VC_FTG_RESULT_BUSY,
            lifecycle_stage);
        atomic_store_explicit(
            &service->callback_active,
            0u,
            memory_order_release);
        return VC_FTG_RESULT_BUSY;
    }
    if (!service->running) {
        result = VC_FTG_RESULT_NOT_RUNNING;
        goto finish;
    }
    if (!service->registered) {
        lifecycle_stage =
            VC_FTG_DIAGNOSTIC_INITIAL_RECONCILIATION_UNPROVEN;
        result = vc_ftg_fail_event(
            service, lifecycle_stage);
        goto finish;
    }
    if ((event == VC_FTG_PROCESS_EXITED ||
         event == VC_FTG_PROCESS_KILLED) &&
        service->registry.state != VC_FTG_TARGET_NONE &&
        service->registry.process_id == process_id) {
        need_title = false;
    }
    need_module =
        event == VC_FTG_PROCESS_CREATED ||
        event == VC_FTG_PROCESS_STARTED;
    if (need_title || need_module) {
        vc_ftg_leave(service);
        if (need_title &&
            !service->dependencies.get_title_id(
                service->dependencies.context,
                process_id,
                title_id)) {
            result = VC_FTG_RESULT_PLATFORM_FAILURE;
        } else if (need_title) {
            is_target = vc_ftg_exact_text(
                title_id,
                service->config.target_title_id,
                VC_FTG_TITLE_ID_CAPACITY);
        }
        if (result == VC_FTG_RESULT_OK &&
            need_module &&
            is_target &&
            !service->dependencies.get_main_module(
                service->dependencies.context,
                process_id,
                &module)) {
            result = VC_FTG_RESULT_MODULE_UNAVAILABLE;
        }
        vc_ftg_operation_lock(service);
    } else {
        is_target = true;
    }
    if (result != VC_FTG_RESULT_OK) {
        lifecycle_stage =
            result == VC_FTG_RESULT_MODULE_UNAVAILABLE
                ? VC_FTG_DIAGNOSTIC_MODULE_QUERY
                : VC_FTG_DIAGNOSTIC_TITLE_QUERY;
        if (service->registry.process_id == process_id ||
            event == VC_FTG_PROCESS_CREATED ||
            event == VC_FTG_PROCESS_STARTED) {
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
        }
        goto finish;
    }
    if (!is_target) {
        if (service->registry.state != VC_FTG_TARGET_NONE &&
            service->registry.process_id == process_id) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_TARGET_IDENTITY_MISMATCH;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            goto finish;
        }
        vc_ftg_increment_counter(
            &service->ignored_non_target_count,
            &service->counter_saturation_flags,
            VC_FTG_COUNTER_SAT_IGNORED_NON_TARGET);
        lifecycle_stage =
            VC_FTG_DIAGNOSTIC_NON_TARGET_IGNORED;
        goto finish;
    }
    switch (event) {
    case VC_FTG_PROCESS_CREATED:
        vc_ftg_increment_counter(
            &service->target_create_match_count,
            &service->counter_saturation_flags,
            VC_FTG_COUNTER_SAT_TARGET_CREATE_MATCH);
        if (service->registry.state != VC_FTG_TARGET_NONE) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_DUPLICATE_EVENT;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        if (vc_ftg_validate_module(
                service, process_id, &module) !=
            VC_FTG_RESULT_OK) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_MODULE_MISMATCH;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        result = vc_ftg_assign_generation(
            service, &service->registry.generation);
        if (result != VC_FTG_RESULT_OK) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_GENERATION_EXHAUSTED;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        result = vc_ftg_advance_revision(
            service, &service->registry.lifecycle_revision);
        if (result != VC_FTG_RESULT_OK) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_REVISION_EXHAUSTED;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        vc_ftg_copy_bytes(
            &service->registry.module,
            &module,
            sizeof(service->registry.module));
        service->registry.process_id = process_id;
        service->registry.state = VC_FTG_TARGET_STARTED;
        service->diagnostic_stage =
            VC_FTG_DIAGNOSTIC_TARGET_STARTED;
        service->last_result = VC_FTG_RESULT_OK;
        vc_ftg_increment_counter(
            &service->target_create_authorized_count,
            &service->counter_saturation_flags,
            VC_FTG_COUNTER_SAT_TARGET_CREATE_AUTHORIZED);
        lifecycle_stage =
            VC_FTG_DIAGNOSTIC_TARGET_CREATE_BOUND;
        break;
    case VC_FTG_PROCESS_STARTED:
        if (service->registry.state !=
                VC_FTG_TARGET_STARTED ||
            service->registry.process_id != process_id) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_OUT_OF_ORDER_EVENT;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        if (vc_ftg_validate_module(
                service, process_id, &module) !=
                VC_FTG_RESULT_OK ||
            !vc_ftg_module_equal(
                &service->registry.module, &module)) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_MODULE_MISMATCH;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        service->diagnostic_stage =
            VC_FTG_DIAGNOSTIC_TARGET_STARTED;
        service->last_result = VC_FTG_RESULT_OK;
        vc_ftg_increment_counter(
            &service->start_revalidation_count,
            &service->counter_saturation_flags,
            VC_FTG_COUNTER_SAT_START_REVALIDATION);
        lifecycle_stage =
            VC_FTG_DIAGNOSTIC_TARGET_START_REVALIDATED;
        break;
    case VC_FTG_PROCESS_EXITED:
    case VC_FTG_PROCESS_KILLED:
        if (service->registry.state == VC_FTG_TARGET_NONE ||
            service->registry.process_id != process_id) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_OUT_OF_ORDER_EVENT;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        if (vc_ftg_advance_revision(
                service,
                &service->registry.lifecycle_revision) !=
            VC_FTG_RESULT_OK) {
            lifecycle_stage =
                VC_FTG_DIAGNOSTIC_REVISION_EXHAUSTED;
            result = vc_ftg_fail_event(
                service, lifecycle_stage);
            break;
        }
        vc_ftg_scrub_session(service);
        vc_ftg_scrub_registry(service);
        service->diagnostic_stage =
            event == VC_FTG_PROCESS_EXITED
                ? VC_FTG_DIAGNOSTIC_TARGET_EXITED
                : VC_FTG_DIAGNOSTIC_TARGET_KILLED;
        service->last_result = VC_FTG_RESULT_OK;
        lifecycle_stage =
            (vc_ftg_diagnostic_stage)
                service->diagnostic_stage;
        break;
    default:
        lifecycle_stage =
            VC_FTG_DIAGNOSTIC_OUT_OF_ORDER_EVENT;
        result = vc_ftg_fail_event(
            service, lifecycle_stage);
        break;
    }

finish:
    vc_ftg_record_lifecycle(
        service, event, result, lifecycle_stage);
    vc_ftg_wipe(title_id, sizeof(title_id));
    vc_ftg_wipe(&module, sizeof(module));
    vc_ftg_leave(service);
    atomic_store_explicit(
        &service->callback_active, 0u, memory_order_release);
    return result;
}

vc_ftg_result vc_ftg_service_process_event(
    vc_ftg_service *service,
    vc_ftg_process_event event,
    uint32_t process_id)
{
    return vc_ftg_service_process_event_with_type(
        service, event, process_id, 0u);
}

bool vc_ftg_service_runtime_unload_allowed(
    const vc_ftg_service *service)
{
    return vc_ftg_service_valid(service) &&
           !service->runtime_unload_blocked &&
           atomic_load_explicit(
               &service->transaction_busy,
               memory_order_acquire) == 0u &&
           atomic_load_explicit(
               &service->operation_active,
               memory_order_acquire) == 0u &&
           atomic_load_explicit(
               &service->callback_active,
               memory_order_acquire) == 0u;
}

vc_ftg_result vc_ftg_service_get_status(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_ftg_status_request request;
    vc_ftg_status_response_v2 response;
    uint8_t caller_title[VC_FTG_TITLE_ID_CAPACITY];
    uint32_t caller_process_id = 0u;
    size_t response_size = sizeof(response.base);
    vc_ftg_result result;

    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    result = vc_ftg_enter(service, true, false, false);
    if (result != VC_FTG_RESULT_OK) {
        return result;
    }
    vc_ftg_operation_begin(service);
    if (!vc_ftg_get_caller(service, &caller_process_id)) {
        result = VC_FTG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_ftg_copy_request(
            service,
            caller_process_id,
            &request,
            request_user,
            sizeof(request));
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_status_request(&request);
    }
    if (result == VC_FTG_RESULT_OK &&
        !service->dependencies.get_title_id(
            service->dependencies.context,
            caller_process_id,
            caller_title)) {
        result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_controller(
            service, caller_title);
    }
    vc_ftg_operation_lock(service);
    vc_ftg_consume_fail_closed(service);
    if (result == VC_FTG_RESULT_OK) {
        response_size =
            request.version == VC_FTG_STATUS_VERSION_2
                ? sizeof(response)
                : sizeof(response.base);
        response.base.version = request.version;
        response.base.struct_size = (uint16_t)response_size;
        response.base.capabilities =
            vc_ftg_full_gate_available(service)
                ? VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ
                : 0u;
        response.base.max_read = VC_FTG_MAX_READ;
        response.base.timeout_ms = VC_FTG_DEFAULT_TIMEOUT_MS;
        response.base.abi_flags =
            request.version == VC_FTG_STATUS_VERSION_2
                ? VC_FTG_ABI_FLAGS
                : VC_FTG_ABI_FLAGS_V1;
        response.base.diagnostic_stage =
            service->diagnostic_stage;
        response.base.last_result = service->last_result;
        response.base.target_state = service->registry.state;
        response.create_callback_count =
            atomic_load_explicit(
                &service->create_callback_count,
                memory_order_relaxed);
        response.start_callback_count =
            atomic_load_explicit(
                &service->start_callback_count,
                memory_order_relaxed);
        response.start_revalidation_count =
            atomic_load_explicit(
                &service->start_revalidation_count,
                memory_order_relaxed);
        response.ignored_non_target_count =
            atomic_load_explicit(
                &service->ignored_non_target_count,
                memory_order_relaxed);
        response.target_create_match_count =
            atomic_load_explicit(
                &service->target_create_match_count,
                memory_order_relaxed);
        response.target_create_authorized_count =
            atomic_load_explicit(
                &service->target_create_authorized_count,
                memory_order_relaxed);
        response.counter_saturation_flags =
            atomic_load_explicit(
                &service->counter_saturation_flags,
                memory_order_relaxed);
        response.last_lifecycle_event =
            atomic_load_explicit(
                &service->last_lifecycle_event,
                memory_order_relaxed);
        response.last_lifecycle_result =
            atomic_load_explicit(
                &service->last_lifecycle_result,
                memory_order_relaxed);
        response.last_lifecycle_stage =
            atomic_load_explicit(
                &service->last_lifecycle_stage,
                memory_order_acquire);
        if (!service->config.enabled) {
            response.base.status = VC_FTG_RUNTIME_DISABLED;
        } else if (!service->running) {
            response.base.status = VC_FTG_RUNTIME_STOPPED;
        } else if (!service->config.api_available ||
                   !service->registered) {
            response.base.status =
                VC_FTG_RUNTIME_API_MISMATCH;
        } else if (vc_ftg_is_fail_closed(service)) {
            response.base.status =
                VC_FTG_RUNTIME_FAIL_CLOSED;
        } else if (!service->config.foreign_lifecycle_enabled) {
            response.base.status =
                VC_FTG_RUNTIME_DIAGNOSTIC_ONLY;
        } else if (service->session.active) {
            response.base.status =
                VC_FTG_RUNTIME_SESSION_OPEN;
        } else if (service->registry.state ==
                   VC_FTG_TARGET_STARTED) {
            response.base.status =
                VC_FTG_RUNTIME_TARGET_AVAILABLE;
        } else {
            response.base.status = VC_FTG_RUNTIME_READY;
        }
        vc_ftg_leave(service);
        result = vc_ftg_copy_response(
            service,
            caller_process_id,
            response_user,
            &response,
            response_size);
        vc_ftg_operation_lock(service);
    }
    if (result != VC_FTG_RESULT_OK) {
        service->last_result = result;
    }
    vc_ftg_operation_finish(service);
    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    return result;
}

vc_ftg_result vc_ftg_service_open_exact_fixture(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_ftg_open_request request;
    vc_ftg_open_response response;
    vc_ftg_registry registry;
    vc_ftg_module_snapshot module;
    uint8_t caller_title[VC_FTG_TITLE_ID_CAPACITY];
    uint8_t target_title[VC_FTG_TITLE_ID_CAPACITY];
    uint32_t caller_process_id = 0u;
    uint64_t now_us = 0u;
    vc_ftg_result result;

    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(&registry, sizeof(registry));
    vc_ftg_wipe(&module, sizeof(module));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    vc_ftg_wipe(target_title, sizeof(target_title));
    result = vc_ftg_enter(service, false, true, true);
    if (result != VC_FTG_RESULT_OK) {
        return result;
    }
    if (service->registry.state != VC_FTG_TARGET_STARTED) {
        result = VC_FTG_RESULT_TARGET_UNAVAILABLE;
    } else if (service->session.active) {
        result = VC_FTG_RESULT_BUSY;
    } else if (service->handle_exhausted) {
        result = VC_FTG_RESULT_HANDLE_EXHAUSTED;
    } else {
        vc_ftg_copy_bytes(
            &registry,
            &service->registry,
            sizeof(registry));
    }
    if (result != VC_FTG_RESULT_OK) {
        service->last_result = result;
        vc_ftg_leave(service);
        return result;
    }
    vc_ftg_operation_begin(service);
    if (!vc_ftg_get_caller(service, &caller_process_id)) {
        result = VC_FTG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_ftg_copy_request(
            service,
            caller_process_id,
            &request,
            request_user,
            sizeof(request));
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_open_request(&request);
    }
    if (result == VC_FTG_RESULT_OK &&
        (!vc_ftg_get_time(service, &now_us) ||
         !service->dependencies.get_title_id(
             service->dependencies.context,
             caller_process_id,
             caller_title) ||
         !service->dependencies.get_title_id(
             service->dependencies.context,
             registry.process_id,
             target_title))) {
        result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_controller(
            service, caller_title);
    }
    if (result == VC_FTG_RESULT_OK &&
        !vc_ftg_exact_text(
            target_title,
            service->config.target_title_id,
            VC_FTG_TITLE_ID_CAPACITY)) {
        result = VC_FTG_RESULT_TARGET_UNAVAILABLE;
    }
    if (result == VC_FTG_RESULT_OK &&
        !service->dependencies.get_main_module(
            service->dependencies.context,
            registry.process_id,
            &module)) {
        result = VC_FTG_RESULT_MODULE_UNAVAILABLE;
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_module(
            service, registry.process_id, &module);
    }
    if (result == VC_FTG_RESULT_OK &&
        !vc_ftg_module_equal(&registry.module, &module)) {
        result = VC_FTG_RESULT_MODULE_MISMATCH;
    }
    if (result == VC_FTG_RESULT_OK &&
        now_us > UINT64_MAX - service->config.timeout_us) {
        result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    vc_ftg_operation_lock(service);
    vc_ftg_consume_fail_closed(service);
    if (vc_ftg_is_fail_closed(service)) {
        result = VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
    }
    if (result == VC_FTG_RESULT_OK &&
        (service->registry.state != VC_FTG_TARGET_STARTED ||
         service->registry.generation !=
             registry.generation ||
         service->registry.lifecycle_revision !=
             registry.lifecycle_revision ||
         !vc_ftg_module_equal(
             &service->registry.module,
             &registry.module))) {
        result = VC_FTG_RESULT_REVISION_MISMATCH;
    }
    if (result == VC_FTG_RESULT_OK) {
        response.version = VC_FTG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.status = VC_FTG_RESULT_OK;
        response.capabilities =
            VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
        response.handle = service->next_handle;
        response.timeout_ms = VC_FTG_DEFAULT_TIMEOUT_MS;
        vc_ftg_leave(service);
        result = vc_ftg_copy_response(
            service,
            caller_process_id,
            response_user,
            &response,
            sizeof(response));
        vc_ftg_operation_lock(service);
        vc_ftg_consume_fail_closed(service);
        if (vc_ftg_is_fail_closed(service)) {
            vc_ftg_wipe(&response, sizeof(response));
            vc_ftg_leave(service);
            (void)vc_ftg_copy_response(
                service,
                caller_process_id,
                response_user,
                &response,
                sizeof(response));
            vc_ftg_operation_lock(service);
            result = VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
        }
    }
    if (result == VC_FTG_RESULT_OK) {
        vc_ftg_copy_bytes(
            &service->session.module,
            &registry.module,
            sizeof(service->session.module));
        service->session.handle = response.handle;
        service->session.issued_at_us = now_us;
        service->session.deadline_us =
            now_us + service->config.timeout_us;
        service->session.target_generation =
            registry.generation;
        service->session.lifecycle_revision =
            registry.lifecycle_revision;
        service->session.caller_process_id =
            caller_process_id;
        service->session.active = true;
        if (service->next_handle == UINT64_MAX) {
            service->handle_exhausted = true;
        } else {
            ++service->next_handle;
        }
    }
    service->last_result = result;
    vc_ftg_operation_finish(service);
    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(&registry, sizeof(registry));
    vc_ftg_wipe(&module, sizeof(module));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    vc_ftg_wipe(target_title, sizeof(target_title));
    return result;
}

vc_ftg_result vc_ftg_service_read_fixture_segment(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_ftg_read_request request;
    vc_ftg_read_response response;
    vc_ftg_registry registry;
    vc_ftg_session session;
    vc_ftg_module_snapshot module_before;
    vc_ftg_module_snapshot module_after;
    uint8_t caller_title[VC_FTG_TITLE_ID_CAPACITY];
    uint8_t target_title[VC_FTG_TITLE_ID_CAPACITY];
    uint8_t bounce[VC_FTG_MAX_READ];
    uint32_t caller_process_id = 0u;
    uint64_t now_us = 0u;
    uintptr_t source_address = (uintptr_t)0;
    vc_ftg_result result;

    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(&registry, sizeof(registry));
    vc_ftg_wipe(&session, sizeof(session));
    vc_ftg_wipe(&module_before, sizeof(module_before));
    vc_ftg_wipe(&module_after, sizeof(module_after));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    vc_ftg_wipe(target_title, sizeof(target_title));
    vc_ftg_wipe(bounce, sizeof(bounce));
    result = vc_ftg_enter(service, false, true, true);
    if (result != VC_FTG_RESULT_OK) {
        return result;
    }
    if (!service->session.active) {
        result = VC_FTG_RESULT_INVALID_HANDLE;
    } else if (service->registry.state !=
               VC_FTG_TARGET_STARTED) {
        result = VC_FTG_RESULT_TARGET_UNAVAILABLE;
    } else {
        vc_ftg_copy_bytes(
            &registry,
            &service->registry,
            sizeof(registry));
        vc_ftg_copy_bytes(
            &session,
            &service->session,
            sizeof(session));
    }
    if (result != VC_FTG_RESULT_OK) {
        service->last_result = result;
        vc_ftg_leave(service);
        return result;
    }
    vc_ftg_operation_begin(service);
    if (!vc_ftg_get_caller(service, &caller_process_id)) {
        result = VC_FTG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_ftg_copy_request(
            service,
            caller_process_id,
            &request,
            request_user,
            sizeof(request));
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_read_request(&request);
    }
    if (result == VC_FTG_RESULT_OK &&
        (!vc_ftg_get_time(service, &now_us) ||
         !service->dependencies.get_title_id(
             service->dependencies.context,
             caller_process_id,
             caller_title) ||
         !service->dependencies.get_title_id(
             service->dependencies.context,
             registry.process_id,
             target_title))) {
        result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_controller(
            service, caller_title);
    }
    if (result == VC_FTG_RESULT_OK &&
        !vc_ftg_exact_text(
            target_title,
            service->config.target_title_id,
            VC_FTG_TITLE_ID_CAPACITY)) {
        result = VC_FTG_RESULT_TARGET_UNAVAILABLE;
    }
    if (result == VC_FTG_RESULT_OK &&
        request.handle != session.handle) {
        result = VC_FTG_RESULT_INVALID_HANDLE;
    }
    if (result == VC_FTG_RESULT_OK &&
        caller_process_id != session.caller_process_id) {
        result = VC_FTG_RESULT_CALLER_TITLE_MISMATCH;
    }
    if (result == VC_FTG_RESULT_OK &&
        (now_us < session.issued_at_us ||
         now_us >= session.deadline_us)) {
        result = VC_FTG_RESULT_EXPIRED;
    }
    if (result == VC_FTG_RESULT_OK &&
        (registry.generation !=
             session.target_generation ||
         registry.lifecycle_revision !=
             session.lifecycle_revision)) {
        result = VC_FTG_RESULT_GENERATION_MISMATCH;
    }
    if (result == VC_FTG_RESULT_OK &&
        !service->dependencies.get_main_module(
            service->dependencies.context,
            registry.process_id,
            &module_before)) {
        result = VC_FTG_RESULT_MODULE_UNAVAILABLE;
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_module(
            service,
            registry.process_id,
            &module_before);
    }
    if (result == VC_FTG_RESULT_OK &&
        (!vc_ftg_module_equal(
             &registry.module, &module_before) ||
         !vc_ftg_module_equal(
             &session.module, &module_before))) {
        result = VC_FTG_RESULT_MODULE_MISMATCH;
    }
    if (result == VC_FTG_RESULT_OK &&
        request.segment_index >=
            module_before.segment_count) {
        result = VC_FTG_RESULT_INVALID_SEGMENT;
    }
    if (result == VC_FTG_RESULT_OK) {
        const vc_ftg_segment_snapshot *segment =
            &module_before.segments[request.segment_index];

        if ((segment->permissions &
             VC_FTG_PERMISSION_USER_READ) == 0u) {
            result = VC_FTG_RESULT_PERMISSION_DENIED;
        } else if (request.offset > segment->size ||
                   request.length >
                       segment->size - request.offset ||
                   segment->base >
                       UINTPTR_MAX - request.offset) {
            result = VC_FTG_RESULT_RANGE_DENIED;
        } else {
            source_address =
                segment->base + request.offset;
        }
    }
    if (result == VC_FTG_RESULT_OK &&
        !service->dependencies.read_process(
            service->dependencies.context,
            registry.process_id,
            bounce,
            source_address,
            request.length)) {
        result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_FTG_RESULT_OK &&
        (!service->dependencies.get_main_module(
             service->dependencies.context,
             registry.process_id,
             &module_after) ||
         !vc_ftg_get_time(service, &now_us))) {
        result = VC_FTG_RESULT_MODULE_UNAVAILABLE;
    }
    if (result == VC_FTG_RESULT_OK &&
        (!vc_ftg_module_equal(
             &module_before, &module_after) ||
         now_us < session.issued_at_us ||
         now_us >= session.deadline_us)) {
        result = VC_FTG_RESULT_MODULE_MISMATCH;
    }
    vc_ftg_operation_lock(service);
    vc_ftg_consume_fail_closed(service);
    if (vc_ftg_is_fail_closed(service)) {
        vc_ftg_wipe(&response, sizeof(response));
        vc_ftg_leave(service);
        (void)vc_ftg_copy_response(
            service,
            caller_process_id,
            response_user,
            &response,
            sizeof(response));
        vc_ftg_operation_lock(service);
        result = VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
    }
    if (result == VC_FTG_RESULT_OK &&
        (!service->session.active ||
         service->session.handle != session.handle)) {
        result = VC_FTG_RESULT_INVALID_HANDLE;
    }
    if (result == VC_FTG_RESULT_OK &&
        service->registry.generation !=
            session.target_generation) {
        result = VC_FTG_RESULT_GENERATION_MISMATCH;
    }
    if (result == VC_FTG_RESULT_OK &&
        service->registry.lifecycle_revision !=
            session.lifecycle_revision) {
        result = VC_FTG_RESULT_REVISION_MISMATCH;
    }
    if (result == VC_FTG_RESULT_OK) {
        response.version = VC_FTG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.status = VC_FTG_RESULT_OK;
        response.length = request.length;
        vc_ftg_copy_bytes(
            response.bytes, bounce, request.length);
        vc_ftg_leave(service);
        result = vc_ftg_copy_response(
            service,
            caller_process_id,
            response_user,
            &response,
            sizeof(response));
        vc_ftg_operation_lock(service);
        vc_ftg_consume_fail_closed(service);
        if (vc_ftg_is_fail_closed(service)) {
            vc_ftg_wipe(&response, sizeof(response));
            vc_ftg_leave(service);
            (void)vc_ftg_copy_response(
                service,
                caller_process_id,
                response_user,
                &response,
                sizeof(response));
            vc_ftg_operation_lock(service);
            result = VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
        }
    }
    if (result == VC_FTG_RESULT_MODULE_MISMATCH ||
        result == VC_FTG_RESULT_GENERATION_MISMATCH ||
        result == VC_FTG_RESULT_REVISION_MISMATCH ||
        result == VC_FTG_RESULT_LIFECYCLE_COMPROMISED ||
        result == VC_FTG_RESULT_EXPIRED) {
        vc_ftg_scrub_session(service);
    }
    service->last_result = result;
    vc_ftg_operation_finish(service);
    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(&registry, sizeof(registry));
    vc_ftg_wipe(&session, sizeof(session));
    vc_ftg_wipe(&module_before, sizeof(module_before));
    vc_ftg_wipe(&module_after, sizeof(module_after));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    vc_ftg_wipe(target_title, sizeof(target_title));
    vc_ftg_wipe(bounce, sizeof(bounce));
    return result;
}

vc_ftg_result vc_ftg_service_close(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_ftg_close_request request;
    vc_ftg_close_response response;
    uint8_t caller_title[VC_FTG_TITLE_ID_CAPACITY];
    uint32_t caller_process_id = 0u;
    vc_ftg_result result;

    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    result = vc_ftg_enter(service, false, true, true);
    if (result != VC_FTG_RESULT_OK) {
        return result;
    }
    vc_ftg_operation_begin(service);
    if (!vc_ftg_get_caller(service, &caller_process_id)) {
        result = VC_FTG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_ftg_copy_request(
            service,
            caller_process_id,
            &request,
            request_user,
            sizeof(request));
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_session_request(&request);
    }
    if (result == VC_FTG_RESULT_OK &&
        !service->dependencies.get_title_id(
            service->dependencies.context,
            caller_process_id,
            caller_title)) {
        result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_FTG_RESULT_OK) {
        result = vc_ftg_validate_controller(
            service, caller_title);
    }
    vc_ftg_operation_lock(service);
    vc_ftg_consume_fail_closed(service);
    if (vc_ftg_is_fail_closed(service)) {
        result = VC_FTG_RESULT_LIFECYCLE_COMPROMISED;
    }
    if (result == VC_FTG_RESULT_OK &&
        (!service->session.active ||
         service->session.handle != request.handle)) {
        result = VC_FTG_RESULT_INVALID_HANDLE;
    }
    if (result == VC_FTG_RESULT_OK) {
        response.version = VC_FTG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.status = VC_FTG_RESULT_OK;
        vc_ftg_leave(service);
        result = vc_ftg_copy_response(
            service,
            caller_process_id,
            response_user,
            &response,
            sizeof(response));
        vc_ftg_operation_lock(service);
    }
    if (result == VC_FTG_RESULT_OK) {
        vc_ftg_scrub_session(service);
    }
    service->last_result = result;
    vc_ftg_operation_finish(service);
    vc_ftg_wipe(&request, sizeof(request));
    vc_ftg_wipe(&response, sizeof(response));
    vc_ftg_wipe(caller_title, sizeof(caller_title));
    return result;
}
