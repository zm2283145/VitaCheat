#include "vitacheat/launch_service.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VC_LAUNCH_SERVICE_FUZZ_MAX_STEPS = 256
};

typedef struct fuzz_platform {
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground;
    bool copy_from_ok;
    bool copy_to_ok;
    bool attest_ok;
    bool foreground_ok;
} fuzz_platform;

static void fuzz_check(int condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool fuzz_copy_from_user(void *context,
                                void *destination,
                                const void *source,
                                size_t size)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->copy_from_ok) {
        memmove(destination, source, size);
    } else if (size != 0) {
        memmove(destination, source, size / 2);
    }
    return platform->copy_from_ok;
}

static bool fuzz_copy_to_user(void *context,
                              void *destination,
                              const void *source,
                              size_t size)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->copy_to_ok) {
        memmove(destination, source, size);
    } else if (size != 0) {
        memmove(destination, source, size / 2);
    }
    return platform->copy_to_ok;
}

static vc_launch_attestation_result fuzz_attest(
    void *context,
    const void *caller_context,
    vc_launch_trusted_caller *caller)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    (void)caller_context;
    if (!platform->attest_ok) {
        return VC_LAUNCH_ATTESTATION_UNKNOWN;
    }
    *caller = platform->caller;
    return VC_LAUNCH_ATTESTATION_ACCEPTED;
}

static bool fuzz_foreground(
    void *context,
    vc_launch_foreground_snapshot *foreground)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->foreground_ok) {
        *foreground = platform->foreground;
    }
    return platform->foreground_ok;
}

static bool fuzz_cleanup(void *context)
{
    (void)context;
    return true;
}

static void set_present_foreground(fuzz_platform *platform,
                                   uint64_t sequence,
                                   uint32_t process_id,
                                   uint64_t generation,
                                   uint8_t title_variant)
{
    static const uint8_t title_prefix[] = {
        'P', 'C', 'S', 'A', '0', '0', '0', '0'
    };

    memset(&platform->foreground, 0, sizeof(platform->foreground));
    platform->foreground.sequence = sequence;
    platform->foreground.target_process_id = process_id;
    platform->foreground.target_generation = generation;
    platform->foreground.title_id_size = sizeof(title_prefix) + 1;
    platform->foreground.present = 1;
    memcpy(platform->foreground.title_id, title_prefix,
           sizeof(title_prefix));
    platform->foreground.title_id[sizeof(title_prefix)] = title_variant;
}

static void set_caller(fuzz_platform *platform,
                       uint16_t role,
                       uint32_t process_id,
                       uint64_t generation,
                       uint64_t module_generation)
{
    memset(&platform->caller, 0, sizeof(platform->caller));
    platform->caller.role = role;
    platform->caller.process_id = process_id;
    platform->caller.process_generation = generation;
    platform->caller.module_generation = module_generation;
}

static void check_invariants(const vc_launch_service *service)
{
    fuzz_check(service->broker.initialized);
    fuzz_check(service->broker.record.state <=
               VC_LAUNCH_STATE_CLOCK_ROLLBACK);
    if (service->broker.record.state != VC_LAUNCH_STATE_ABSENT) {
        fuzz_check(service->broker.record.request_id != 0);
        fuzz_check(service->broker.record.target_process_id != 0);
        fuzz_check(service->broker.record.target_generation != 0);
        fuzz_check(service->broker.record.deadline_ms >
                   service->broker.record.created_ms);
    }
    if (service->journal.valid) {
        fuzz_check(service->journal.operation_status >=
                       VC_LAUNCH_STATUS_MALFORMED_FIELD);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
    vc_launch_service service = VC_LAUNCH_SERVICE_INITIALIZER;
    vc_launch_service_dependencies dependencies;
    fuzz_platform platform;
    vc_launch_status operation_status;
    vc_launch_status lifecycle_status;
    uint8_t request[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint64_t first_id;
    uint64_t generation;
    uint64_t now_ms;
    uint64_t sequence = 1;
    uint32_t process_id;
    size_t cursor;
    size_t step;

    if (data_size < 20) {
        return 0;
    }
    first_id = read_u64(data);
    if (first_id == 0) {
        first_id = 1;
    }
    process_id = read_u32(data + 8);
    if (process_id == 0) {
        process_id = 1;
    }
    generation = read_u64(data + 12);
    if (generation == 0) {
        generation = 1;
    }

    memset(&platform, 0, sizeof(platform));
    platform.copy_from_ok = true;
    platform.copy_to_ok = true;
    platform.attest_ok = true;
    platform.foreground_ok = true;
    set_present_foreground(&platform, sequence, process_id,
                           generation, 0);
    set_caller(&platform, VC_LAUNCH_CALLER_SCE_SHELL,
               process_id + 1u, generation, 1);

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.copy_from_user = fuzz_copy_from_user;
    dependencies.copy_to_user = fuzz_copy_to_user;
    dependencies.attest_caller = fuzz_attest;
    dependencies.get_foreground = fuzz_foreground;
    dependencies.cleanup = fuzz_cleanup;
    dependencies.context = &platform;

    fuzz_check(vc_launch_service_init(&service, &dependencies,
                                      first_id) ==
               VC_LAUNCH_SERVICE_STATUS_OK);
    fuzz_check(vc_launch_service_start(&service, 0,
                                       &lifecycle_status) ==
               VC_LAUNCH_SERVICE_STATUS_OK);

    now_ms = 0;
    cursor = 20;
    for (step = 0;
         cursor < data_size && step < VC_LAUNCH_SERVICE_FUZZ_MAX_STEPS;
         ++step) {
        const uint8_t command = data[cursor++];
        const uint8_t action = command & 7u;

        if ((command & 0x20u) != 0 && now_ms != 0) {
            --now_ms;
        } else if (now_ms != UINT64_MAX) {
            now_ms += command & 3u;
        }
        platform.copy_from_ok = (command & 0x40u) == 0;
        platform.copy_to_ok = (command & 0x80u) == 0;
        platform.attest_ok = (command & 0x10u) == 0;

        if (action <= 2u) {
            size_t request_size = VC_LAUNCH_REQUEST_WIRE_SIZE;
            size_t response_capacity = VC_LAUNCH_RESPONSE_WIRE_SIZE;
            size_t response_size = 0;
            size_t index;

            for (index = 0; index < sizeof(request); ++index) {
                request[index] = data[(cursor + index) % data_size];
            }
            if ((command & 8u) != 0) {
                request_size = command % (VC_LAUNCH_REQUEST_WIRE_SIZE + 2u);
            }
            if ((command & 4u) != 0) {
                response_capacity =
                    command % (VC_LAUNCH_RESPONSE_WIRE_SIZE + 2u);
            }
            if ((command & 2u) != 0) {
                set_caller(&platform, VC_LAUNCH_CALLER_GAME_PLUGIN,
                           process_id, generation, sequence + 1u);
            } else {
                set_caller(&platform, VC_LAUNCH_CALLER_SCE_SHELL,
                           process_id + 1u, generation, sequence + 1u);
            }
            (void)vc_launch_service_dispatch(
                &service, NULL, request, request_size,
                response, response_capacity, now_ms,
                &response_size, &operation_status);
            fuzz_check(response_size == 0 ||
                       response_size == VC_LAUNCH_RESPONSE_WIRE_SIZE);
        } else if (action == 3u) {
            (void)vc_launch_service_tick(
                &service, now_ms, &lifecycle_status);
        } else if (action == 4u) {
            ++sequence;
            if ((command & 8u) != 0) {
                memset(&platform.foreground, 0,
                       sizeof(platform.foreground));
                platform.foreground.sequence = sequence;
            } else {
                generation += generation != UINT64_MAX ? 1u : 0u;
                set_present_foreground(
                    &platform, sequence, process_id, generation,
                    (uint8_t)sequence);
            }
            (void)vc_launch_service_foreground_changed(
                &service, &platform.foreground, now_ms,
                &lifecycle_status);
        } else if (action == 5u) {
            if (service.foreground.present != 0u) {
                (void)vc_launch_service_plugin_unload(
                    &service,
                    service.foreground.target_process_id,
                    service.foreground.target_generation,
                    now_ms, &lifecycle_status);
            }
        } else if (action == 6u) {
            (void)vc_launch_service_reset(
                &service, now_ms, &lifecycle_status);
        } else {
            (void)vc_launch_service_stop(
                &service, now_ms, &lifecycle_status);
            (void)vc_launch_service_start(
                &service, now_ms, &lifecycle_status);
        }
        check_invariants(&service);
    }
    return 0;
}

#ifdef VC_LAUNCH_SERVICE_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[512];
    uint32_t random_state = UINT32_C(0x7e571ce5);
    unsigned int run;
    size_t index;

    for (run = 0; run < 10000; ++run) {
        for (index = 0; index < sizeof(data); ++index) {
            random_state =
                random_state * UINT32_C(1664525) + UINT32_C(1013904223);
            data[index] = (uint8_t)(random_state >> 24);
        }
        (void)LLVMFuzzerTestOneInput(data, sizeof(data));
    }
    return 0;
}
#endif
