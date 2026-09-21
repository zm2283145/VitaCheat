#include "vitacheat/launch_broker.h"

#include <stdint.h>
#include <stdlib.h>

enum {
    VC_LAUNCH_FUZZ_MAX_STEPS = 256
};

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

static vc_launch_request make_request(vc_launch_operation operation,
                                      uint32_t process_id,
                                      uint64_t generation,
                                      uint64_t request_id,
                                      uint64_t now_ms)
{
    vc_launch_request request;
    vc_launch_caller_role role = operation == VC_LAUNCH_OPERATION_SUBMIT
                                     ? VC_LAUNCH_CALLER_SCE_SHELL
                                     : VC_LAUNCH_CALLER_GAME_PLUGIN;

    vc_launch_request_init(&request, operation, role);
    request.target_process_id = process_id;
    request.target_generation = generation;
    request.request_id = request_id;
    request.now_ms = now_ms;
    switch (operation) {
    case VC_LAUNCH_OPERATION_SUBMIT:
        request.capabilities = VC_LAUNCH_CAPABILITY_LAUNCH;
        request.request_id = 0;
        request.ttl_ms = VC_LAUNCH_DEFAULT_TTL_MS;
        break;
    case VC_LAUNCH_OPERATION_CLAIM:
        request.capabilities = VC_LAUNCH_CAPABILITY_CLAIM;
        break;
    case VC_LAUNCH_OPERATION_CANCEL:
        request.capabilities = VC_LAUNCH_CAPABILITY_CANCEL;
        break;
    case VC_LAUNCH_OPERATION_STATUS:
        request.capabilities = VC_LAUNCH_CAPABILITY_STATUS;
        break;
    default:
        break;
    }
    return request;
}

static void check_invariants(const vc_launch_broker *broker)
{
    fuzz_check(broker->initialized);
    fuzz_check(broker->record.state <= VC_LAUNCH_STATE_CLOCK_ROLLBACK);
    if (broker->record.state != VC_LAUNCH_STATE_ABSENT) {
        fuzz_check(broker->record.request_id != 0);
        fuzz_check(broker->record.target_process_id != 0);
        fuzz_check(broker->record.target_generation != 0);
        fuzz_check(broker->record.deadline_ms > broker->record.created_ms);
        fuzz_check(broker->record.deadline_ms - broker->record.created_ms <=
                   VC_LAUNCH_MAX_TTL_MS);
    }
    if (!broker->id_exhausted) {
        fuzz_check(broker->next_request_id != 0);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
    vc_launch_broker broker;
    vc_launch_response response;
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint64_t first_id;
    uint64_t generation;
    uint64_t now_ms;
    uint32_t process_id;
    size_t index;
    size_t step_count;

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
    now_ms = 0;
    fuzz_check(vc_launch_broker_init(&broker, first_id) ==
               VC_LAUNCH_STATUS_OK);
    (void)vc_launch_broker_foreground_changed(
        &broker, process_id, generation, now_ms);

    step_count = data_size - 20;
    if (step_count > VC_LAUNCH_FUZZ_MAX_STEPS) {
        step_count = VC_LAUNCH_FUZZ_MAX_STEPS;
    }
    for (index = 0; index < step_count; ++index) {
        const uint8_t command = data[20 + index];
        const uint64_t request_id = broker.record.request_id != 0
                                        ? broker.record.request_id
                                        : 1;
        vc_launch_request request;

        if ((command & 0x20u) != 0 && now_ms != 0) {
            --now_ms;
        } else if (now_ms != UINT64_MAX) {
            now_ms += command & 3u;
        }

        switch (command & 7u) {
        case 0:
            request = make_request(VC_LAUNCH_OPERATION_SUBMIT,
                                   process_id, generation,
                                   request_id, now_ms);
            request.ttl_ms = (uint64_t)(command % 15u) + 1;
            (void)vc_launch_broker_submit(&broker, &request, &response);
            break;
        case 1:
            request = make_request(VC_LAUNCH_OPERATION_CLAIM,
                                   process_id, generation,
                                   request_id, now_ms);
            request.presentation_ready = (command >> 3) & 1u;
            (void)vc_launch_broker_claim(&broker, &request, &response);
            break;
        case 2:
            request = make_request(VC_LAUNCH_OPERATION_CANCEL,
                                   process_id, generation,
                                   request_id, now_ms);
            (void)vc_launch_broker_cancel(&broker, &request, &response);
            break;
        case 3:
            request = make_request(VC_LAUNCH_OPERATION_STATUS,
                                   process_id, generation,
                                   request_id, now_ms);
            (void)vc_launch_broker_status(&broker, &request, &response);
            break;
        case 4:
            (void)vc_launch_broker_tick(&broker, now_ms);
            break;
        case 5:
            (void)vc_launch_broker_plugin_unload(
                &broker, process_id, generation, now_ms);
            break;
        case 6:
            (void)vc_launch_broker_service_reset(&broker, now_ms);
            (void)vc_launch_broker_foreground_changed(
                &broker, process_id, generation, now_ms);
            break;
        default:
            if (data_size - (20 + index) >= VC_LAUNCH_REQUEST_WIRE_SIZE) {
                size_t response_size = 0;

                (void)vc_launch_broker_dispatch_wire(
                    &broker, data + 20 + index, VC_LAUNCH_REQUEST_WIRE_SIZE,
                    response_wire, sizeof(response_wire), &response_size);
                fuzz_check(response_size == 0 ||
                           response_size == VC_LAUNCH_RESPONSE_WIRE_SIZE);
            } else {
                (void)vc_launch_broker_process_exit(
                    &broker, process_id, generation, now_ms);
                (void)vc_launch_broker_foreground_changed(
                    &broker, process_id, generation, now_ms);
            }
            break;
        }
        check_invariants(&broker);
    }
    return 0;
}

#ifdef VC_LAUNCH_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[512];
    uint32_t random_state = UINT32_C(0x51a7e123);
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
