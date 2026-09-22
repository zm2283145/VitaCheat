#include "vitacheat/hardware_gate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct fuzz_platform {
    vc_hg_module_snapshot module;
    uint8_t title_id[VC_HG_TITLE_ID_CAPACITY];
    uint8_t memory[128];
    uint64_t now_us;
    uint32_t process_id;
    bool copies_ok;
    bool read_ok;
} fuzz_platform;

typedef union fuzz_buffer {
    vc_hg_module_response alignment;
    uint8_t bytes[sizeof(vc_hg_module_response)];
} fuzz_buffer;

static void fuzz_check(bool condition)
{
    if (!condition) {
        abort();
    }
}

static bool fuzz_get_caller(
    void *context,
    uint32_t *process_id)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    *process_id = platform->process_id;
    return true;
}

static bool fuzz_get_time(void *context, uint64_t *now_us)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    *now_us = platform->now_us;
    return true;
}

static bool fuzz_get_title(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_HG_TITLE_ID_CAPACITY])
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (process_id != platform->process_id) {
        return false;
    }
    memcpy(title_id, platform->title_id,
           VC_HG_TITLE_ID_CAPACITY);
    return true;
}

static bool fuzz_get_module(
    void *context,
    uint32_t process_id,
    vc_hg_module_snapshot *module)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (process_id != platform->process_id) {
        return false;
    }
    *module = platform->module;
    return true;
}

static bool fuzz_copy_from(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *source,
    size_t size)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (!platform->copies_ok ||
        process_id != platform->process_id) {
        return false;
    }
    memcpy(destination, source, size);
    return true;
}

static bool fuzz_copy_to(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *source,
    size_t size)
{
    return fuzz_copy_from(
        context, process_id, destination, source, size);
}

static bool fuzz_read(
    void *context,
    uint32_t process_id,
    void *destination,
    uintptr_t address,
    size_t size)
{
    fuzz_platform *platform = (fuzz_platform *)context;
    uintptr_t begin = (uintptr_t)platform->memory;
    uintptr_t end = begin + sizeof(platform->memory);

    if (!platform->read_ok ||
        process_id != platform->process_id ||
        address < begin || address > end ||
        size > (size_t)(end - address)) {
        return false;
    }
    memcpy(destination, (const void *)address, size);
    return true;
}

static uint64_t fuzz_u64(
    const uint8_t *data,
    size_t size)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0; index < 8u && index < size; ++index) {
        value |= (uint64_t)data[index] << (index * 8u);
    }
    return value;
}

static void fuzz_initialize(
    vc_hg_service *service,
    vc_hg_config *config,
    vc_hg_dependencies *dependencies,
    fuzz_platform *platform)
{
    size_t index;

    memset(service, 0, sizeof(*service));
    memset(config, 0, sizeof(*config));
    memset(dependencies, 0, sizeof(*dependencies));
    memset(platform, 0, sizeof(*platform));
    memcpy(config->expected_title_id,
           "VCHG00001", sizeof("VCHG00001"));
    memcpy(config->expected_module_name,
           "VitaCheatHgClient",
           sizeof("VitaCheatHgClient"));
    config->timeout_us = VC_HG_DEFAULT_TIMEOUT_US;
    config->enabled = true;
    config->api_available = true;
    platform->now_us = 1u;
    platform->process_id = 17u;
    platform->copies_ok = true;
    platform->read_ok = true;
    memcpy(platform->title_id,
           "VCHG00001", sizeof("VCHG00001"));
    platform->module.process_id = platform->process_id;
    platform->module.module_id = 29;
    platform->module.module_fingerprint = 31u;
    platform->module.segment_count = 1u;
    memcpy(platform->module.module_name,
           "VitaCheatHgClient",
           sizeof("VitaCheatHgClient"));
    platform->module.segments[0].base =
        (uintptr_t)platform->memory;
    platform->module.segments[0].size =
        sizeof(platform->memory);
    platform->module.segments[0].permissions =
        VC_HG_PERMISSION_USER_READ;
    for (index = 0; index < sizeof(platform->memory);
         ++index) {
        platform->memory[index] = (uint8_t)index;
    }
    dependencies->get_caller_pid = fuzz_get_caller;
    dependencies->get_time_us = fuzz_get_time;
    dependencies->get_title_id = fuzz_get_title;
    dependencies->get_main_module = fuzz_get_module;
    dependencies->copy_from_user = fuzz_copy_from;
    dependencies->copy_to_user = fuzz_copy_to;
    dependencies->read_process = fuzz_read;
    dependencies->context = platform;
    fuzz_check(vc_hg_service_init(
                   service, config, dependencies, 1u) ==
               VC_HG_RESULT_OK);
    fuzz_check(vc_hg_service_start(service) ==
               VC_HG_RESULT_OK);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    vc_hg_service service;
    vc_hg_config config;
    vc_hg_dependencies dependencies;
    fuzz_platform platform;
    vc_hg_open_request open_request;
    vc_hg_open_response open_response;
    fuzz_buffer request;
    fuzz_buffer response;
    uint64_t handle = 0u;
    vc_hg_result result;
    size_t request_size;
    unsigned int operation;

    if (size == 0u) {
        return 0;
    }
    fuzz_initialize(
        &service, &config, &dependencies, &platform);
    memset(&open_request, 0, sizeof(open_request));
    open_request.version = VC_HG_ABI_VERSION;
    open_request.struct_size = sizeof(open_request);
    if (vc_hg_service_open_self(
            &service, &open_request, &open_response) ==
        VC_HG_RESULT_OK) {
        handle = open_response.handle;
    }
    platform.now_us =
        fuzz_u64(data + 1u, size - 1u);
    platform.copies_ok = (data[0] & 0x20u) == 0u;
    platform.read_ok = (data[0] & 0x40u) == 0u;
    if ((data[0] & 0x80u) != 0u) {
        ++platform.module.module_fingerprint;
    }
    memset(&request, 0, sizeof(request));
    memset(&response, 0xa5, sizeof(response));
    request_size = size - 1u;
    if (request_size > sizeof(request.bytes)) {
        request_size = sizeof(request.bytes);
    }
    memcpy(request.bytes, data + 1u, request_size);
    operation = data[0] % 5u;
    if ((data[0] & 0x10u) != 0u) {
        if (operation <= 1u) {
            vc_hg_status_request *status =
                (vc_hg_status_request *)request.bytes;

            status->version = VC_HG_ABI_VERSION;
            status->struct_size = sizeof(*status);
            status->capabilities = 0u;
            status->reserved0 = 0u;
            status->reserved1 = 0u;
        } else if (operation == 2u ||
                   operation == 4u) {
            vc_hg_session_request *session =
                (vc_hg_session_request *)request.bytes;

            session->version = VC_HG_ABI_VERSION;
            session->struct_size = sizeof(*session);
            session->capabilities =
                VC_HG_CAPABILITY_SELF_SEGMENT_READ;
            session->handle = handle;
            session->reserved0 = 0u;
        } else {
            vc_hg_read_request *read =
                (vc_hg_read_request *)request.bytes;

            read->version = VC_HG_ABI_VERSION;
            read->struct_size = sizeof(*read);
            read->capabilities =
                VC_HG_CAPABILITY_SELF_SEGMENT_READ;
            read->handle = handle;
            read->reserved0 = 0u;
        }
    }

    switch (operation) {
    case 0u:
        result = vc_hg_service_get_status(
            &service, request.bytes, response.bytes);
        break;
    case 1u:
        result = vc_hg_service_open_self(
            &service, request.bytes, response.bytes);
        break;
    case 2u:
        result = vc_hg_service_get_self_main_module(
            &service, request.bytes, response.bytes);
        break;
    case 3u:
        result = vc_hg_service_read_self_segment(
            &service, request.bytes, response.bytes);
        if (result == VC_HG_RESULT_OK) {
            const vc_hg_read_response *read_response =
                (const vc_hg_read_response *)response.bytes;
            uint32_t index;

            fuzz_check(read_response->version ==
                       VC_HG_ABI_VERSION);
            fuzz_check(read_response->struct_size ==
                       sizeof(*read_response));
            fuzz_check(read_response->length > 0u);
            fuzz_check(read_response->length <=
                       VC_HG_MAX_READ);
            for (index = read_response->length;
                 index < VC_HG_MAX_READ; ++index) {
                fuzz_check(
                    read_response->bytes[index] == 0u);
            }
        }
        break;
    default:
        result = vc_hg_service_close(
            &service, request.bytes, response.bytes);
        break;
    }
    fuzz_check(result <= VC_HG_RESULT_OK);
    fuzz_check(service.session.active == false ||
               service.session.handle != 0u);
    fuzz_check(service.session.active == false ||
               service.session.process_id != 0u);
    return 0;
}

#ifdef VC_HG_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[160];
    uint32_t state = UINT32_C(0x56434847);
    size_t run;
    size_t index;

    for (run = 0; run < 10000u; ++run) {
        for (index = 0; index < sizeof(data); ++index) {
            state = state * UINT32_C(1664525) +
                    UINT32_C(1013904223);
            data[index] = (uint8_t)(state >> 24);
        }
        (void)LLVMFuzzerTestOneInput(
            data, run % (sizeof(data) + 1u));
    }
    return 0;
}
#endif
