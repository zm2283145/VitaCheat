#include "vitacheat/hardware_gate.h"

#include <limits.h>

enum {
    VC_HG_SERVICE_MARKER = 0x56434847u
};

static void vc_hg_wipe(void *value, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)value;

    while (size != 0u) {
        *bytes++ = 0;
        --size;
    }
}

static bool vc_hg_bytes_zero(const void *value, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)value;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

static void vc_hg_copy_bytes(
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

static bool vc_hg_bytes_equal(
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

static bool vc_hg_exact_text(
    const uint8_t *actual,
    const uint8_t *expected,
    size_t capacity)
{
    size_t index;
    bool saw_nul = false;

    for (index = 0; index < capacity; ++index) {
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

static size_t vc_hg_text_size(const uint8_t *text, size_t capacity)
{
    size_t size = 0;

    while (size < capacity && text[size] != 0u) {
        ++size;
    }
    return size;
}

static bool vc_hg_dependencies_valid(
    const vc_hg_dependencies *dependencies)
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

static bool vc_hg_config_valid(const vc_hg_config *config)
{
    size_t title_size;
    size_t module_size;

    if (config == NULL ||
        config->timeout_us != VC_HG_DEFAULT_TIMEOUT_US) {
        return false;
    }
    title_size = vc_hg_text_size(
        config->expected_title_id, VC_HG_TITLE_ID_CAPACITY);
    module_size = vc_hg_text_size(
        config->expected_module_name, VC_HG_MODULE_NAME_CAPACITY);
    return title_size == VC_HG_TITLE_ID_LENGTH &&
           title_size < VC_HG_TITLE_ID_CAPACITY &&
           module_size != 0u &&
           module_size < VC_HG_MODULE_NAME_CAPACITY &&
           vc_hg_bytes_zero(
               config->expected_title_id + title_size + 1u,
               VC_HG_TITLE_ID_CAPACITY - title_size - 1u) &&
           vc_hg_bytes_zero(
               config->expected_module_name + module_size + 1u,
               VC_HG_MODULE_NAME_CAPACITY - module_size - 1u);
}

static bool vc_hg_service_valid(const vc_hg_service *service)
{
    return service != NULL &&
           service->marker == (uint32_t)VC_HG_SERVICE_MARKER;
}

static vc_hg_result vc_hg_enter(
    vc_hg_service *service,
    bool require_running,
    bool require_enabled)
{
    if (!vc_hg_service_valid(service)) {
        return VC_HG_RESULT_INVALID_ARGUMENT;
    }
    if (atomic_load_explicit(
            &service->adapter_active,
            memory_order_acquire) != 0u) {
        return VC_HG_RESULT_BUSY;
    }
    if (atomic_exchange_explicit(
            &service->transaction_busy, 1u,
            memory_order_acquire) != 0u) {
        return VC_HG_RESULT_BUSY;
    }
    if (atomic_load_explicit(
            &service->adapter_active,
            memory_order_acquire) != 0u) {
        atomic_store_explicit(
            &service->transaction_busy, 0u,
            memory_order_release);
        return VC_HG_RESULT_BUSY;
    }
    if (require_running && !service->running) {
        vc_hg_result result =
            service->last_result == VC_HG_RESULT_STOPPED
                ? VC_HG_RESULT_STOPPED
                : VC_HG_RESULT_NOT_RUNNING;

        atomic_store_explicit(
            &service->transaction_busy, 0u,
            memory_order_release);
        return result;
    }
    if (require_enabled && !service->config.enabled) {
        atomic_store_explicit(
            &service->transaction_busy, 0u,
            memory_order_release);
        return VC_HG_RESULT_DISABLED;
    }
    if (require_enabled && !service->config.api_available) {
        atomic_store_explicit(
            &service->transaction_busy, 0u,
            memory_order_release);
        return VC_HG_RESULT_PLATFORM_FAILURE;
    }
    return VC_HG_RESULT_OK;
}

static void vc_hg_leave(vc_hg_service *service)
{
    atomic_store_explicit(
        &service->transaction_busy, 0u,
        memory_order_release);
}

static void vc_hg_adapter_begin(vc_hg_service *service)
{
    atomic_store_explicit(
        &service->adapter_active, 1u,
        memory_order_release);
    vc_hg_leave(service);
}

static void vc_hg_adapter_lock(vc_hg_service *service)
{
    (void)atomic_exchange_explicit(
        &service->transaction_busy, 1u,
        memory_order_acquire);
}

static void vc_hg_adapter_finish(vc_hg_service *service)
{
    atomic_store_explicit(
        &service->adapter_active, 0u,
        memory_order_release);
    vc_hg_leave(service);
}

static void vc_hg_scrub_session(vc_hg_service *service)
{
    vc_hg_wipe(&service->session, sizeof(service->session));
}

static vc_hg_result vc_hg_record_result(
    vc_hg_service *service,
    vc_hg_result result)
{
    service->last_result = result;
    return result;
}

static bool vc_hg_module_equal(
    const vc_hg_module_snapshot *left,
    const vc_hg_module_snapshot *right)
{
    uint32_t index;

    if (left->process_id != right->process_id ||
        left->module_id != right->module_id ||
        left->module_fingerprint != right->module_fingerprint ||
        left->segment_count != right->segment_count ||
        !vc_hg_bytes_equal(
            left->module_name, right->module_name,
            sizeof(left->module_name))) {
        return false;
    }
    for (index = 0; index < left->segment_count; ++index) {
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

static vc_hg_result vc_hg_validate_module(
    const vc_hg_service *service,
    uint32_t process_id,
    const vc_hg_module_snapshot *module)
{
    uint32_t left;

    if (module->process_id != process_id ||
        module->module_id <= 0 ||
        module->module_fingerprint == 0u ||
        module->segment_count == 0u ||
        module->segment_count > VC_HG_MAX_SEGMENTS ||
        !vc_hg_exact_text(
            module->module_name,
            service->config.expected_module_name,
            VC_HG_MODULE_NAME_CAPACITY)) {
        return VC_HG_RESULT_MODULE_MISMATCH;
    }
    for (left = 0; left < module->segment_count; ++left) {
        const vc_hg_segment_snapshot *segment =
            &module->segments[left];
        uint32_t right;

        if (segment->base == (uintptr_t)0 ||
            segment->size == 0u ||
            segment->base > UINTPTR_MAX - segment->size ||
            (segment->permissions &
             ~VC_HG_PERMISSION_KNOWN_MASK) != 0u) {
            return VC_HG_RESULT_MODULE_MISMATCH;
        }
        for (right = left + 1u;
             right < module->segment_count;
             ++right) {
            const vc_hg_segment_snapshot *other =
                &module->segments[right];
            uintptr_t segment_end =
                segment->base + segment->size;
            uintptr_t other_end =
                other->base + other->size;

            if (other->base == (uintptr_t)0 ||
                other->size == 0u ||
                other->base > UINTPTR_MAX - other->size ||
                (segment->base < other_end &&
                 other->base < segment_end)) {
                return VC_HG_RESULT_MODULE_MISMATCH;
            }
        }
    }
    for (; left < VC_HG_MAX_SEGMENTS; ++left) {
        if (!vc_hg_bytes_zero(
                &module->segments[left],
                sizeof(module->segments[left]))) {
            return VC_HG_RESULT_MODULE_MISMATCH;
        }
    }
    return VC_HG_RESULT_OK;
}

static vc_hg_result vc_hg_validate_title(
    const vc_hg_service *service,
    const uint8_t title_id[VC_HG_TITLE_ID_CAPACITY])
{
    return vc_hg_exact_text(
               title_id,
               service->config.expected_title_id,
               VC_HG_TITLE_ID_CAPACITY)
               ? VC_HG_RESULT_OK
               : VC_HG_RESULT_TITLE_MISMATCH;
}

static vc_hg_result vc_hg_validate_status_request(
    const vc_hg_status_request *request)
{
    if (request->version != VC_HG_ABI_VERSION) {
        return VC_HG_RESULT_INVALID_VERSION;
    }
    if (request->struct_size != sizeof(*request)) {
        return VC_HG_RESULT_INVALID_SIZE;
    }
    if (request->capabilities != 0u) {
        return VC_HG_RESULT_CAPABILITY_MISMATCH;
    }
    if (request->reserved0 != 0u ||
        request->reserved1 != 0u) {
        return VC_HG_RESULT_RESERVED_NOT_ZERO;
    }
    return VC_HG_RESULT_OK;
}

static vc_hg_result vc_hg_validate_session_request(
    const vc_hg_session_request *request)
{
    if (request->version != VC_HG_ABI_VERSION) {
        return VC_HG_RESULT_INVALID_VERSION;
    }
    if (request->struct_size != sizeof(*request)) {
        return VC_HG_RESULT_INVALID_SIZE;
    }
    if (request->capabilities !=
        VC_HG_CAPABILITY_SELF_SEGMENT_READ) {
        return VC_HG_RESULT_CAPABILITY_MISMATCH;
    }
    if (request->reserved0 != 0u) {
        return VC_HG_RESULT_RESERVED_NOT_ZERO;
    }
    if (request->handle == 0u) {
        return VC_HG_RESULT_INVALID_HANDLE;
    }
    return VC_HG_RESULT_OK;
}

static vc_hg_result vc_hg_validate_read_request(
    const vc_hg_read_request *request)
{
    if (request->version != VC_HG_ABI_VERSION) {
        return VC_HG_RESULT_INVALID_VERSION;
    }
    if (request->struct_size != sizeof(*request)) {
        return VC_HG_RESULT_INVALID_SIZE;
    }
    if (request->capabilities !=
        VC_HG_CAPABILITY_SELF_SEGMENT_READ) {
        return VC_HG_RESULT_CAPABILITY_MISMATCH;
    }
    if (request->reserved0 != 0u) {
        return VC_HG_RESULT_RESERVED_NOT_ZERO;
    }
    if (request->handle == 0u) {
        return VC_HG_RESULT_INVALID_HANDLE;
    }
    if (request->length == 0u ||
        request->length > VC_HG_MAX_READ) {
        return VC_HG_RESULT_RANGE_DENIED;
    }
    return VC_HG_RESULT_OK;
}

static vc_hg_result vc_hg_copy_request(
    vc_hg_service *service,
    uint32_t process_id,
    void *destination,
    const void *request_user,
    size_t size)
{
    if (request_user == NULL ||
        !service->dependencies.copy_from_user(
            service->dependencies.context,
            process_id, destination, request_user, size)) {
        vc_hg_wipe(destination, size);
        return VC_HG_RESULT_COPY_FROM_USER_FAILED;
    }
    return VC_HG_RESULT_OK;
}

static vc_hg_result vc_hg_copy_response(
    vc_hg_service *service,
    uint32_t process_id,
    void *response_user,
    const void *response,
    size_t size)
{
    if (response_user == NULL ||
        !service->dependencies.copy_to_user(
            service->dependencies.context,
            process_id, response_user, response, size)) {
        return VC_HG_RESULT_COPY_TO_USER_FAILED;
    }
    return VC_HG_RESULT_OK;
}

static bool vc_hg_get_caller(
    vc_hg_service *service,
    uint32_t *process_id)
{
    *process_id = 0u;
    return service->dependencies.get_caller_pid(
               service->dependencies.context,
               process_id) &&
           *process_id != 0u &&
           *process_id <= INT32_MAX;
}

static bool vc_hg_get_time(
    vc_hg_service *service,
    uint64_t *now_us)
{
    *now_us = 0u;
    return service->dependencies.get_time_us(
        service->dependencies.context, now_us);
}

static vc_hg_result vc_hg_validate_session(
    vc_hg_service *service,
    uint32_t process_id,
    uint64_t handle,
    uint64_t now_us,
    const vc_hg_module_snapshot *module)
{
    if (!service->session.active ||
        service->session.handle != handle) {
        return VC_HG_RESULT_INVALID_HANDLE;
    }
    if (now_us >= service->session.deadline_us) {
        vc_hg_scrub_session(service);
        return VC_HG_RESULT_EXPIRED;
    }
    if (service->session.process_id != process_id) {
        vc_hg_scrub_session(service);
        return VC_HG_RESULT_CALLER_MISMATCH;
    }
    if (!vc_hg_module_equal(&service->session.module, module)) {
        vc_hg_scrub_session(service);
        return VC_HG_RESULT_MODULE_MISMATCH;
    }
    return VC_HG_RESULT_OK;
}

vc_hg_result vc_hg_service_init(
    vc_hg_service *service,
    const vc_hg_config *config,
    const vc_hg_dependencies *dependencies,
    uint64_t first_handle)
{
    if (service == NULL || !vc_hg_config_valid(config) ||
        !vc_hg_dependencies_valid(dependencies) ||
        first_handle == 0u) {
        return VC_HG_RESULT_INVALID_ARGUMENT;
    }
    vc_hg_wipe(service, sizeof(*service));
    vc_hg_copy_bytes(
        &service->config, config, sizeof(service->config));
    vc_hg_copy_bytes(
        &service->dependencies, dependencies,
        sizeof(service->dependencies));
    atomic_init(&service->transaction_busy, 0u);
    atomic_init(&service->adapter_active, 0u);
    service->next_handle = first_handle;
    service->last_result = config->enabled
                               ? VC_HG_RESULT_NOT_RUNNING
                               : VC_HG_RESULT_DISABLED;
    service->marker = (uint32_t)VC_HG_SERVICE_MARKER;
    return VC_HG_RESULT_OK;
}

vc_hg_result vc_hg_service_start(vc_hg_service *service)
{
    vc_hg_result result = vc_hg_enter(service, false, false);

    if (result != VC_HG_RESULT_OK) {
        return result;
    }
    vc_hg_scrub_session(service);
    service->running = true;
    service->last_result = service->config.enabled
                               ? VC_HG_RESULT_OK
                               : VC_HG_RESULT_DISABLED;
    vc_hg_leave(service);
    return VC_HG_RESULT_OK;
}

vc_hg_result vc_hg_service_stop(vc_hg_service *service)
{
    vc_hg_result result = vc_hg_enter(service, false, false);

    if (result != VC_HG_RESULT_OK) {
        return result;
    }
    vc_hg_scrub_session(service);
    service->running = false;
    service->last_result = VC_HG_RESULT_STOPPED;
    vc_hg_leave(service);
    return VC_HG_RESULT_OK;
}

vc_hg_result vc_hg_service_get_status(
    vc_hg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_hg_status_request request;
    vc_hg_status_response response;
    vc_hg_result result;
    uint32_t process_id;

    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    result = vc_hg_enter(service, false, false);
    if (result != VC_HG_RESULT_OK) {
        return result;
    }
    vc_hg_adapter_begin(service);
    if (!vc_hg_get_caller(service, &process_id)) {
        result = VC_HG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_hg_copy_request(
            service, process_id, &request,
            request_user, sizeof(request));
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_status_request(&request);
    }
    vc_hg_adapter_lock(service);
    if (result == VC_HG_RESULT_OK) {
        response.version = VC_HG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.capabilities =
            service->config.enabled &&
                    service->config.api_available
                                    ? VC_HG_KNOWN_CAPABILITIES
                                    : 0u;
        response.max_read = VC_HG_MAX_READ;
        response.timeout_ms = VC_HG_DEFAULT_TIMEOUT_MS;
        response.max_segments = VC_HG_MAX_SEGMENTS;
        response.abi_flags = VC_HG_ABI_FLAGS;
        response.last_result = service->last_result;
        if (!service->config.enabled) {
            response.status = VC_HG_RUNTIME_DISABLED;
        } else if (!service->config.api_available) {
            response.status = VC_HG_RUNTIME_API_MISMATCH;
        } else if (!service->running) {
            response.status = VC_HG_RUNTIME_STOPPED;
        } else if (
            service->last_result ==
                VC_HG_RESULT_CALLER_UNAVAILABLE ||
            service->last_result ==
                VC_HG_RESULT_MODULE_UNAVAILABLE ||
            service->last_result ==
                VC_HG_RESULT_PLATFORM_FAILURE) {
            response.status = VC_HG_RUNTIME_UNAVAILABLE;
        } else if (service->session.active) {
            response.status = VC_HG_RUNTIME_SESSION_OPEN;
        } else {
            response.status = VC_HG_RUNTIME_READY;
        }
        vc_hg_leave(service);
        result = vc_hg_copy_response(
            service, process_id, response_user,
            &response, sizeof(response));
        vc_hg_adapter_lock(service);
    }
    if (result != VC_HG_RESULT_OK) {
        vc_hg_record_result(service, result);
    }
    vc_hg_adapter_finish(service);
    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    return result;
}

vc_hg_result vc_hg_service_open_self(
    vc_hg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_hg_open_request request;
    vc_hg_open_response response;
    vc_hg_module_snapshot module;
    uint8_t title_id[VC_HG_TITLE_ID_CAPACITY];
    uint32_t process_id = 0u;
    uint64_t now_us = 0u;
    vc_hg_result result;

    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module, sizeof(module));
    vc_hg_wipe(title_id, sizeof(title_id));
    result = vc_hg_enter(service, true, true);
    if (result != VC_HG_RESULT_OK) {
        return result;
    }
    vc_hg_adapter_begin(service);
    if (!vc_hg_get_caller(service, &process_id)) {
        result = VC_HG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_hg_copy_request(
            service, process_id, &request,
            request_user, sizeof(request));
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_status_request(&request);
    }
    if (result == VC_HG_RESULT_OK &&
        (!vc_hg_get_time(service, &now_us) ||
         !service->dependencies.get_title_id(
             service->dependencies.context,
             process_id, title_id))) {
        result = VC_HG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_title(service, title_id);
    }
    if (result == VC_HG_RESULT_OK &&
        !service->dependencies.get_main_module(
            service->dependencies.context,
            process_id, &module)) {
        result = VC_HG_RESULT_MODULE_UNAVAILABLE;
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_module(
            service, process_id, &module);
    }
    vc_hg_adapter_lock(service);
    if (result == VC_HG_RESULT_OK &&
        service->session.active &&
        now_us >= service->session.deadline_us) {
        vc_hg_scrub_session(service);
    }
    if (result == VC_HG_RESULT_OK &&
        service->session.active) {
        result = VC_HG_RESULT_SESSION_ACTIVE;
    }
    if (result == VC_HG_RESULT_OK &&
        service->handle_exhausted) {
        result = VC_HG_RESULT_HANDLE_EXHAUSTED;
    }
    if (result == VC_HG_RESULT_OK &&
        now_us > UINT64_MAX - service->config.timeout_us) {
        result = VC_HG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_HG_RESULT_OK) {
        response.version = VC_HG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.status = VC_HG_RESULT_OK;
        response.capabilities =
            VC_HG_CAPABILITY_SELF_SEGMENT_READ;
        response.handle = service->next_handle;
        response.deadline_us =
            now_us + service->config.timeout_us;
        response.segment_count = module.segment_count;
        response.module_fingerprint =
            module.module_fingerprint;
        vc_hg_leave(service);
        result = vc_hg_copy_response(
            service, process_id, response_user,
            &response, sizeof(response));
        vc_hg_adapter_lock(service);
    }
    if (result == VC_HG_RESULT_OK) {
        vc_hg_copy_bytes(
            &service->session.module, &module,
            sizeof(service->session.module));
        service->session.handle = response.handle;
        service->session.deadline_us = response.deadline_us;
        service->session.process_id = process_id;
        service->session.active = true;
        if (service->next_handle == UINT64_MAX) {
            service->handle_exhausted = true;
        } else {
            ++service->next_handle;
        }
    }
    vc_hg_record_result(service, result);
    vc_hg_adapter_finish(service);
    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module, sizeof(module));
    vc_hg_wipe(title_id, sizeof(title_id));
    return result;
}

vc_hg_result vc_hg_service_get_self_main_module(
    vc_hg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_hg_session_request request;
    vc_hg_module_response response;
    vc_hg_module_snapshot module;
    uint32_t process_id = 0u;
    uint64_t now_us = 0u;
    uint32_t index;
    vc_hg_result result;

    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module, sizeof(module));
    result = vc_hg_enter(service, true, true);
    if (result != VC_HG_RESULT_OK) {
        return result;
    }
    vc_hg_adapter_begin(service);
    if (!vc_hg_get_caller(service, &process_id)) {
        result = VC_HG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_hg_copy_request(
            service, process_id, &request,
            request_user, sizeof(request));
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_session_request(&request);
    }
    if (result == VC_HG_RESULT_OK &&
        (!vc_hg_get_time(service, &now_us) ||
         !service->dependencies.get_main_module(
             service->dependencies.context,
             process_id, &module))) {
        result = VC_HG_RESULT_MODULE_UNAVAILABLE;
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_module(
            service, process_id, &module);
    }
    vc_hg_adapter_lock(service);
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_session(
            service, process_id, request.handle,
            now_us, &module);
    }
    if (result == VC_HG_RESULT_OK) {
        response.version = VC_HG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.status = VC_HG_RESULT_OK;
        response.capabilities =
            VC_HG_CAPABILITY_SELF_SEGMENT_READ;
        response.segment_count = module.segment_count;
        response.handle = request.handle;
        response.module_fingerprint =
            module.module_fingerprint;
        response.module_name_size = (uint32_t)vc_hg_text_size(
            module.module_name,
            sizeof(module.module_name));
        vc_hg_copy_bytes(
            response.module_name, module.module_name,
            sizeof(response.module_name));
        for (index = 0; index < module.segment_count; ++index) {
            response.segments[index].segment_index = index;
            response.segments[index].permissions =
                module.segments[index].permissions;
            response.segments[index].size =
                module.segments[index].size;
        }
        vc_hg_leave(service);
        result = vc_hg_copy_response(
            service, process_id, response_user,
            &response, sizeof(response));
        vc_hg_adapter_lock(service);
    }
    vc_hg_record_result(service, result);
    vc_hg_adapter_finish(service);
    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module, sizeof(module));
    return result;
}

vc_hg_result vc_hg_service_read_self_segment(
    vc_hg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_hg_read_request request;
    vc_hg_read_response response;
    vc_hg_module_snapshot module_before;
    vc_hg_module_snapshot module_after;
    uint8_t bounce[VC_HG_MAX_READ];
    const vc_hg_segment_snapshot *segment = NULL;
    uintptr_t source_address = (uintptr_t)0;
    uint32_t process_id = 0u;
    uint64_t now_before = 0u;
    uint64_t now_after = 0u;
    vc_hg_result result;

    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module_before, sizeof(module_before));
    vc_hg_wipe(&module_after, sizeof(module_after));
    vc_hg_wipe(bounce, sizeof(bounce));
    result = vc_hg_enter(service, true, true);
    if (result != VC_HG_RESULT_OK) {
        return result;
    }
    vc_hg_adapter_begin(service);
    if (!vc_hg_get_caller(service, &process_id)) {
        result = VC_HG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_hg_copy_request(
            service, process_id, &request,
            request_user, sizeof(request));
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_read_request(&request);
    }
    if (result == VC_HG_RESULT_OK &&
        (!vc_hg_get_time(service, &now_before) ||
         !service->dependencies.get_main_module(
             service->dependencies.context,
             process_id, &module_before))) {
        result = VC_HG_RESULT_MODULE_UNAVAILABLE;
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_module(
            service, process_id, &module_before);
    }
    vc_hg_adapter_lock(service);
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_session(
            service, process_id, request.handle,
            now_before, &module_before);
    }
    if (result == VC_HG_RESULT_OK &&
        request.segment_index >= module_before.segment_count) {
        result = VC_HG_RESULT_INVALID_SEGMENT;
    }
    if (result == VC_HG_RESULT_OK) {
        segment =
            &module_before.segments[request.segment_index];
        if ((segment->permissions &
             VC_HG_PERMISSION_USER_READ) == 0u) {
            result = VC_HG_RESULT_PERMISSION_DENIED;
        } else if (request.offset > segment->size ||
                   request.length >
                       segment->size - request.offset ||
                   segment->base >
                       UINTPTR_MAX - request.offset) {
            result = VC_HG_RESULT_RANGE_DENIED;
        } else {
            source_address =
                segment->base + request.offset;
        }
    }
    vc_hg_leave(service);
    if (result == VC_HG_RESULT_OK &&
        !service->dependencies.read_process(
            service->dependencies.context,
            process_id, bounce, source_address,
            request.length)) {
        result = VC_HG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_HG_RESULT_OK &&
        (!service->dependencies.get_main_module(
             service->dependencies.context,
             process_id, &module_after) ||
         !vc_hg_get_time(service, &now_after))) {
        result = VC_HG_RESULT_PLATFORM_FAILURE;
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_module(
            service, process_id, &module_after);
    }
    vc_hg_adapter_lock(service);
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_session(
            service, process_id, request.handle,
            now_after, &module_after);
    }
    if (result == VC_HG_RESULT_OK &&
        !vc_hg_module_equal(
            &module_before, &module_after)) {
        vc_hg_scrub_session(service);
        result = VC_HG_RESULT_MODULE_MISMATCH;
    }
    if (result == VC_HG_RESULT_OK) {
        response.version = VC_HG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.status = VC_HG_RESULT_OK;
        response.length = request.length;
        vc_hg_copy_bytes(
            response.bytes, bounce, request.length);
        vc_hg_leave(service);
        result = vc_hg_copy_response(
            service, process_id, response_user,
            &response, sizeof(response));
        vc_hg_adapter_lock(service);
    }
    vc_hg_record_result(service, result);
    vc_hg_adapter_finish(service);
    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module_before, sizeof(module_before));
    vc_hg_wipe(&module_after, sizeof(module_after));
    vc_hg_wipe(bounce, sizeof(bounce));
    return result;
}

vc_hg_result vc_hg_service_close(
    vc_hg_service *service,
    const void *request_user,
    void *response_user)
{
    vc_hg_close_request request;
    vc_hg_close_response response;
    vc_hg_module_snapshot module;
    uint32_t process_id = 0u;
    uint64_t now_us = 0u;
    vc_hg_result result;

    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module, sizeof(module));
    result = vc_hg_enter(service, true, true);
    if (result != VC_HG_RESULT_OK) {
        return result;
    }
    vc_hg_adapter_begin(service);
    if (!vc_hg_get_caller(service, &process_id)) {
        result = VC_HG_RESULT_CALLER_UNAVAILABLE;
    } else {
        result = vc_hg_copy_request(
            service, process_id, &request,
            request_user, sizeof(request));
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_session_request(&request);
    }
    if (result == VC_HG_RESULT_OK &&
        (!vc_hg_get_time(service, &now_us) ||
         !service->dependencies.get_main_module(
             service->dependencies.context,
             process_id, &module))) {
        result = VC_HG_RESULT_MODULE_UNAVAILABLE;
    }
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_module(
            service, process_id, &module);
    }
    vc_hg_adapter_lock(service);
    if (result == VC_HG_RESULT_OK) {
        result = vc_hg_validate_session(
            service, process_id, request.handle,
            now_us, &module);
    }
    if (result == VC_HG_RESULT_OK) {
        vc_hg_scrub_session(service);
        response.version = VC_HG_ABI_VERSION;
        response.struct_size = sizeof(response);
        response.status = VC_HG_RESULT_OK;
        vc_hg_leave(service);
        result = vc_hg_copy_response(
            service, process_id, response_user,
            &response, sizeof(response));
        vc_hg_adapter_lock(service);
    }
    vc_hg_record_result(service, result);
    vc_hg_adapter_finish(service);
    vc_hg_wipe(&request, sizeof(request));
    vc_hg_wipe(&response, sizeof(response));
    vc_hg_wipe(&module, sizeof(module));
    return result;
}
