#include "vitacheat/foreign_target_gate.h"
#include "vitacheat/foreign_target_startup.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_TARGET_PID UINT32_C(41)
#define FUZZ_CONTROLLER_PID UINT32_C(42)
#define FUZZ_OTHER_PID UINT32_C(43)

typedef struct fuzz_platform {
    vc_ftg_service *service;
    vc_ftg_module_snapshot module;
    uint8_t memory[128];
    uint64_t now_us;
    uint32_t caller_process_id;
    bool target_alive;
    bool controller_alive;
    bool module_ok;
    bool copies_ok;
    bool read_ok;
    bool exit_during_read;
} fuzz_platform;

typedef union fuzz_buffer {
    vc_ftg_read_response alignment;
    uint8_t bytes[sizeof(vc_ftg_read_response)];
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

    *process_id = platform->caller_process_id;
    return *process_id != 0u;
}

static bool fuzz_get_time(
    void *context,
    uint64_t *now_us)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    *now_us = platform->now_us;
    return true;
}

static bool fuzz_get_title(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_FTG_TITLE_ID_CAPACITY])
{
    fuzz_platform *platform = (fuzz_platform *)context;

    memset(title_id, 0, VC_FTG_TITLE_ID_CAPACITY);
    if (process_id == FUZZ_TARGET_PID &&
        platform->target_alive) {
        memcpy(title_id, "VCFT00001", sizeof("VCFT00001"));
        return true;
    }
    if (process_id == FUZZ_CONTROLLER_PID &&
        platform->controller_alive) {
        memcpy(title_id, "VCFC00001", sizeof("VCFC00001"));
        return true;
    }
    if (process_id == FUZZ_OTHER_PID) {
        memcpy(title_id, "VCOX00001", sizeof("VCOX00001"));
        return true;
    }
    return false;
}

static bool fuzz_get_module(
    void *context,
    uint32_t process_id,
    vc_ftg_module_snapshot *module)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (!platform->module_ok ||
        !platform->target_alive ||
        process_id != FUZZ_TARGET_PID) {
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
        process_id != FUZZ_CONTROLLER_PID) {
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
    const uintptr_t begin = (uintptr_t)platform->memory;
    const uintptr_t end = begin + sizeof(platform->memory);

    if (platform->exit_during_read) {
        platform->target_alive = false;
        (void)vc_ftg_service_process_event(
            platform->service,
            VC_FTG_PROCESS_EXITED,
            FUZZ_TARGET_PID);
    }
    if (!platform->read_ok ||
        process_id != FUZZ_TARGET_PID ||
        address < begin ||
        address > end ||
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

    for (index = 0u; index < 8u && index < size; ++index) {
        value |= (uint64_t)data[index] << (index * 8u);
    }
    return value;
}

static void fuzz_startup_protocol(
    const uint8_t *data,
    size_t size)
{
    vc_ftg_startup_record record;
    vc_ftg_startup_record decoded;
    vc_ftg_readiness_observation readiness;
    vc_ftg_result_freshness freshness;
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];
    const int32_t result = (int32_t)fuzz_u64(data, size);
    const uint8_t final_stage =
        size == 0u ? 0u : (uint8_t)(data[0] % 8u);

    if (vc_ftg_startup_record_decode(data, size, &decoded)) {
        fuzz_check(vc_ftg_startup_record_encode(
            &decoded, encoded));
        fuzz_check(memcmp(encoded, data, sizeof(encoded)) == 0);
    }

    vc_ftg_startup_record_init(&record);
    memset(&readiness, 0, sizeof(readiness));
    readiness.attempt_count =
        (uint16_t)(1u +
            (fuzz_u64(data, size) %
             VC_FTG_READINESS_MAX_ATTEMPTS));
    readiness.elapsed_ms =
        (uint16_t)(fuzz_u64(data, size) %
            (VC_FTG_READINESS_DEADLINE_US /
             UINT64_C(1000) + UINT64_C(1)));
    readiness.last_result =
        VC_FTG_RESULT_CALLER_TITLE_MISMATCH;
    readiness.probe_result = VC_FTG_RESULT_OK;
    if (final_stage >= 1u) {
        fuzz_check(vc_ftg_startup_record_complete(
            &record,
            VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
            result));
    }
    if (final_stage >= 2u) {
        fuzz_check(vc_ftg_startup_record_complete_readiness(
            &record, &readiness));
    }
    if (final_stage >= 3u) {
        fuzz_check(vc_ftg_startup_record_complete(
            &record,
            VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE,
            result));
    }
    if (final_stage >= 4u) {
        fuzz_check(vc_ftg_startup_record_complete(
            &record,
            VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE,
            result));
    }
    if (final_stage >= 5u) {
        fuzz_check(vc_ftg_startup_record_mark(
            &record, VC_FTG_STARTUP_STAGE_PROMPT_READY));
    }
    if (final_stage >= 6u) {
        fuzz_check(vc_ftg_startup_record_mark(
            &record, VC_FTG_STARTUP_STAGE_CROSS_OBSERVED));
    }
    if (final_stage >= 7u) {
        fuzz_check(vc_ftg_startup_record_complete(
            &record,
            VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE,
            result));
    }
    fuzz_check(vc_ftg_startup_record_encode(&record, encoded));
    fuzz_check(vc_ftg_startup_record_decode(
        encoded, sizeof(encoded), &decoded));
    fuzz_check(memcmp(&record, &decoded, sizeof(record)) == 0);
    if (size > 1u) {
        const size_t corrupt_index =
            (size_t)data[1] % sizeof(encoded);

        encoded[corrupt_index] ^= UINT8_C(1);
        fuzz_check(!vc_ftg_startup_record_decode(
            encoded, sizeof(encoded), &decoded));
    }

    vc_ftg_result_freshness_init(
        &freshness, fuzz_u64(data, size));
    (void)vc_ftg_result_freshness_observe(
        &freshness,
        (vc_ftg_result_observation_kind)(
            size == 0u ? 0u : data[0] % 5u),
        size > 1u && (data[1] & 1u) != 0u,
        size > 1u && (data[1] & 2u) != 0u,
        fuzz_u64(data + (size > 1u ? 1u : 0u),
                 size > 1u ? size - 1u : 0u));
}

static void fuzz_initialize(
    vc_ftg_service *service,
    vc_ftg_config *config,
    vc_ftg_dependencies *dependencies,
    fuzz_platform *platform)
{
    size_t index;

    memset(service, 0, sizeof(*service));
    memset(config, 0, sizeof(*config));
    memset(dependencies, 0, sizeof(*dependencies));
    memset(platform, 0, sizeof(*platform));
    memcpy(
        config->target_title_id,
        "VCFT00001",
        sizeof("VCFT00001"));
    memcpy(
        config->controller_title_id,
        "VCFC00001",
        sizeof("VCFC00001"));
    memcpy(
        config->target_module_name,
        "VitaCheatFtTarget",
        sizeof("VitaCheatFtTarget"));
    config->timeout_us = VC_FTG_DEFAULT_TIMEOUT_US;
    config->enabled = true;
    config->api_available = true;
    config->foreign_lifecycle_enabled = true;

    platform->service = service;
    platform->now_us = 1u;
    platform->caller_process_id = FUZZ_CONTROLLER_PID;
    platform->target_alive = true;
    platform->controller_alive = true;
    platform->module_ok = true;
    platform->copies_ok = true;
    platform->read_ok = true;
    platform->module.process_id = FUZZ_TARGET_PID;
    platform->module.kernel_module_id = 51;
    platform->module.process_module_id = 52;
    platform->module.module_fingerprint = 53u;
    platform->module.segment_count = 1u;
    memcpy(
        platform->module.module_name,
        "VitaCheatFtTarget",
        sizeof("VitaCheatFtTarget"));
    platform->module.segments[0].base =
        (uintptr_t)platform->memory;
    platform->module.segments[0].size =
        sizeof(platform->memory);
    platform->module.segments[0].permissions =
        VC_FTG_PERMISSION_USER_READ;
    for (index = 0u; index < sizeof(platform->memory); ++index) {
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

    fuzz_check(vc_ftg_service_init(
                   service,
                   config,
                   dependencies,
                   UINT64_C(1),
                   UINT64_C(1),
                   UINT64_C(1)) ==
               VC_FTG_RESULT_OK);
    fuzz_check(vc_ftg_service_start(service) ==
               VC_FTG_RESULT_OK);
    fuzz_check(vc_ftg_service_set_registered(
                   service,
                   true,
                   VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED) ==
               VC_FTG_RESULT_OK);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    vc_ftg_service service;
    vc_ftg_config config;
    vc_ftg_dependencies dependencies;
    fuzz_platform platform;
    vc_ftg_status_request open_request;
    vc_ftg_open_response open_response;
    fuzz_buffer request;
    fuzz_buffer response;
    uint64_t handle = 0u;
    size_t index;

    if (size == 0u) {
        return 0;
    }
    fuzz_startup_protocol(data, size);
    fuzz_initialize(
        &service, &config, &dependencies, &platform);
    for (index = 0u; index < size && index < 32u; ++index) {
        const uint8_t opcode = data[index];
        uint32_t pid;
        vc_ftg_process_event event;
        vc_ftg_result event_result;
        uint64_t generation_before =
            service.registry.generation;
        uint64_t revision_before =
            service.registry.lifecycle_revision;
        const bool target_start_revalidation =
            ((opcode >> 2u) & 3u) == 1u &&
            (opcode & 3u) == 0u &&
            service.registry.state ==
                VC_FTG_TARGET_STARTED &&
            service.registry.process_id ==
                FUZZ_TARGET_PID;

        switch (opcode & 3u) {
        case 0u:
            pid = FUZZ_TARGET_PID;
            break;
        case 1u:
            pid = FUZZ_CONTROLLER_PID;
            break;
        default:
            pid = FUZZ_OTHER_PID;
            break;
        }
        event = (vc_ftg_process_event)(
            VC_FTG_PROCESS_CREATED +
            ((opcode >> 2u) & 3u));
        if (event == VC_FTG_PROCESS_CREATED ||
            event == VC_FTG_PROCESS_STARTED) {
            platform.target_alive = true;
        }
        event_result =
            vc_ftg_service_process_event_with_type(
                &service,
                event,
                pid,
                (uint32_t)fuzz_u64(
                    data + index, size - index));
        if (target_start_revalidation &&
            event_result == VC_FTG_RESULT_OK) {
            fuzz_check(service.registry.generation ==
                       generation_before);
            fuzz_check(
                service.registry.lifecycle_revision ==
                revision_before);
        }
        if ((event == VC_FTG_PROCESS_EXITED ||
             event == VC_FTG_PROCESS_KILLED) &&
            pid == FUZZ_TARGET_PID) {
            platform.target_alive = false;
        }
    }

    memset(&open_request, 0, sizeof(open_request));
    open_request.version = VC_FTG_ABI_VERSION;
    open_request.struct_size = sizeof(open_request);
    memset(&open_response, 0, sizeof(open_response));
    if (vc_ftg_service_open_exact_fixture(
            &service, &open_request, &open_response) ==
        VC_FTG_RESULT_OK) {
        handle = open_response.handle;
    }

    platform.now_us = fuzz_u64(data, size);
    platform.module_ok = (data[0] & 0x10u) == 0u;
    platform.copies_ok = (data[0] & 0x20u) == 0u;
    platform.read_ok = (data[0] & 0x40u) == 0u;
    platform.exit_during_read = (data[0] & 0x80u) != 0u;
    if (size > 1u && (data[1] & 1u) != 0u) {
        ++platform.module.process_module_id;
    }
    memset(&request, 0, sizeof(request));
    memset(&response, 0xa5, sizeof(response));
    if (size > 1u) {
        size_t request_size = size - 1u;

        if (request_size > sizeof(request.bytes)) {
            request_size = sizeof(request.bytes);
        }
        memcpy(request.bytes, data + 1u, request_size);
    }
    switch (data[0] % 4u) {
    case 0u:
        (void)vc_ftg_service_get_status(
            &service, request.bytes, response.bytes);
        break;
    case 1u:
        (void)vc_ftg_service_open_exact_fixture(
            &service, request.bytes, response.bytes);
        break;
    case 2u: {
        vc_ftg_read_request *read =
            (vc_ftg_read_request *)request.bytes;
        vc_ftg_result result;

        if ((data[0] & 0x08u) != 0u) {
            read->version = VC_FTG_ABI_VERSION;
            read->struct_size = sizeof(*read);
            read->capabilities =
                VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
            read->handle = handle;
            read->reserved0 = 0u;
        }
        result = vc_ftg_service_read_fixture_segment(
            &service, request.bytes, response.bytes);
        if (result == VC_FTG_RESULT_OK) {
            const vc_ftg_read_response *read_response =
                (const vc_ftg_read_response *)response.bytes;
            uint32_t byte;

            fuzz_check(read_response->length > 0u);
            fuzz_check(
                read_response->length <= VC_FTG_MAX_READ);
            for (byte = read_response->length;
                 byte < VC_FTG_MAX_READ;
                 ++byte) {
                fuzz_check(read_response->bytes[byte] == 0u);
            }
        }
        break;
    }
    default:
        (void)vc_ftg_service_close(
            &service, request.bytes, response.bytes);
        break;
    }
    fuzz_check(service.registry.state <=
               VC_FTG_TARGET_STARTED);
    fuzz_check(!service.session.active ||
               service.session.handle != 0u);
    fuzz_check(!service.session.active ||
               service.session.target_generation != 0u);
    fuzz_check(
        (atomic_load_explicit(
             &service.counter_saturation_flags,
             memory_order_relaxed) &
         ~VC_FTG_COUNTER_SAT_KNOWN_MASK) == 0u);
    return 0;
}

#ifdef VC_FTG_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[192];
    uint32_t state = UINT32_C(0x56434647);
    size_t run;
    size_t index;

    for (run = 0u; run < 10000u; ++run) {
        for (index = 0u; index < sizeof(data); ++index) {
            state = state * UINT32_C(1664525) +
                    UINT32_C(1013904223);
            data[index] = (uint8_t)(state >> 24u);
        }
        (void)LLVMFuzzerTestOneInput(
            data, run % (sizeof(data) + 1u));
    }
    return 0;
}
#endif
