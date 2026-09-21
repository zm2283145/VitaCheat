#include "vitacheat/launch_broker.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

#define PID UINT32_C(0x12345678)
#define GENERATION UINT64_C(0x0102030405060708)

static vc_launch_request request_for(vc_launch_operation operation,
                                     vc_launch_caller_role role,
                                     uint64_t now_ms)
{
    vc_launch_request request;

    vc_launch_request_init(&request, operation, role);
    request.target_process_id = PID;
    request.target_generation = GENERATION;
    request.now_ms = now_ms;
    switch (operation) {
    case VC_LAUNCH_OPERATION_SUBMIT:
        request.capabilities = VC_LAUNCH_CAPABILITY_LAUNCH;
        request.ttl_ms = VC_LAUNCH_DEFAULT_TTL_MS;
        break;
    case VC_LAUNCH_OPERATION_CLAIM:
        request.capabilities = VC_LAUNCH_CAPABILITY_CLAIM;
        request.request_id = 1;
        break;
    case VC_LAUNCH_OPERATION_CANCEL:
        request.capabilities = VC_LAUNCH_CAPABILITY_CANCEL;
        request.request_id = 1;
        break;
    case VC_LAUNCH_OPERATION_STATUS:
        request.capabilities = VC_LAUNCH_CAPABILITY_STATUS;
        request.request_id = 1;
        break;
    default:
        break;
    }
    return request;
}

static void check_request_equal(const vc_launch_request *left,
                                const vc_launch_request *right)
{
    CHECK(left->version == right->version);
    CHECK(left->struct_size == right->struct_size);
    CHECK(left->operation == right->operation);
    CHECK(left->caller_role == right->caller_role);
    CHECK(left->capabilities == right->capabilities);
    CHECK(left->target_process_id == right->target_process_id);
    CHECK(left->target_generation == right->target_generation);
    CHECK(left->request_id == right->request_id);
    CHECK(left->now_ms == right->now_ms);
    CHECK(left->ttl_ms == right->ttl_ms);
    CHECK(left->presentation_ready == right->presentation_ready);
    CHECK(left->reserved0 == right->reserved0);
    CHECK(left->reserved1 == right->reserved1);
}

static void check_response_equal(const vc_launch_response *left,
                                 const vc_launch_response *right)
{
    CHECK(left->version == right->version);
    CHECK(left->struct_size == right->struct_size);
    CHECK(left->operation == right->operation);
    CHECK(left->reserved0 == right->reserved0);
    CHECK(left->status == right->status);
    CHECK(left->launch_state == right->launch_state);
    CHECK(left->capabilities == right->capabilities);
    CHECK(left->target_process_id == right->target_process_id);
    CHECK(left->target_generation == right->target_generation);
    CHECK(left->request_id == right->request_id);
    CHECK(left->created_ms == right->created_ms);
    CHECK(left->deadline_ms == right->deadline_ms);
    CHECK(left->observed_ms == right->observed_ms);
    CHECK(left->reserved1 == right->reserved1);
}

static void test_request_golden_vectors(void)
{
    static const uint8_t expected[][VC_LAUNCH_REQUEST_WIRE_SIZE] = {
        {
            0x01, 0x00, 0x40, 0x00, 0x01, 0x00, 0x01, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        {
            0x01, 0x00, 0x40, 0x00, 0x02, 0x00, 0x02, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
            0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        {
            0x01, 0x00, 0x40, 0x00, 0x03, 0x00, 0x02, 0x00,
            0x04, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
            0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        {
            0x01, 0x00, 0x40, 0x00, 0x04, 0x00, 0x01, 0x00,
            0x08, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
            0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        }
    };
    const vc_launch_operation operations[] = {
        VC_LAUNCH_OPERATION_SUBMIT,
        VC_LAUNCH_OPERATION_CLAIM,
        VC_LAUNCH_OPERATION_CANCEL,
        VC_LAUNCH_OPERATION_STATUS
    };
    const vc_launch_caller_role roles[] = {
        VC_LAUNCH_CALLER_SCE_SHELL,
        VC_LAUNCH_CALLER_GAME_PLUGIN,
        VC_LAUNCH_CALLER_GAME_PLUGIN,
        VC_LAUNCH_CALLER_SCE_SHELL
    };
    size_t index;

    for (index = 0; index < sizeof(operations) / sizeof(operations[0]); ++index) {
        uint8_t encoded[VC_LAUNCH_REQUEST_WIRE_SIZE];
        vc_launch_request request =
            request_for(operations[index], roles[index],
                        UINT64_C(0x1112131415161718));
        vc_launch_request decoded;
        size_t encoded_size = 0;

        if (operations[index] == VC_LAUNCH_OPERATION_SUBMIT) {
            request.ttl_ms = UINT64_C(0x1000);
        } else {
            request.request_id = UINT64_C(0x2122232425262728);
        }
        if (operations[index] == VC_LAUNCH_OPERATION_CLAIM) {
            request.presentation_ready = 1;
        }

        CHECK(vc_launch_request_encode(&request, encoded, sizeof(encoded),
                                       &encoded_size) == VC_LAUNCH_STATUS_OK);
        CHECK(encoded_size == sizeof(encoded));
        CHECK(memcmp(encoded, expected[index], sizeof(encoded)) == 0);
        CHECK(vc_launch_request_decode(expected[index], sizeof(expected[index]),
                                       &decoded) == VC_LAUNCH_STATUS_OK);
        check_request_equal(&request, &decoded);
    }
}

static void test_response_golden_vectors(void)
{
    static const uint8_t expected[][VC_LAUNCH_RESPONSE_WIRE_SIZE] = {
        {
            0x01, 0x00, 0x48, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        {
            0x01, 0x00, 0x48, 0x00, 0x02, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        {
            0x01, 0x00, 0x48, 0x00, 0x03, 0x00, 0x00, 0x00,
            0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        {
            0x01, 0x00, 0x48, 0x00, 0x04, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
            0x08, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        }
    };
    const int32_t statuses[] = {
        VC_LAUNCH_STATUS_PENDING,
        VC_LAUNCH_STATUS_CLAIMED,
        VC_LAUNCH_STATUS_CANCELLED,
        VC_LAUNCH_STATUS_EXPIRED
    };
    const uint32_t states[] = {
        VC_LAUNCH_STATE_PENDING,
        VC_LAUNCH_STATE_CLAIMED,
        VC_LAUNCH_STATE_CANCELLED,
        VC_LAUNCH_STATE_EXPIRED
    };
    const uint32_t capabilities[] = {
        VC_LAUNCH_CAPABILITY_LAUNCH,
        VC_LAUNCH_CAPABILITY_CLAIM,
        VC_LAUNCH_CAPABILITY_CANCEL,
        VC_LAUNCH_CAPABILITY_STATUS
    };
    size_t index;

    for (index = 0; index < 4; ++index) {
        uint8_t encoded[VC_LAUNCH_RESPONSE_WIRE_SIZE];
        vc_launch_response response;
        vc_launch_response decoded;
        size_t encoded_size = 0;

        memset(&response, 0, sizeof(response));
        response.version = VC_LAUNCH_ABI_VERSION;
        response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
        response.operation = (uint16_t)(index + 1);
        response.status = statuses[index];
        response.launch_state = states[index];
        response.capabilities = capabilities[index];
        response.target_process_id = PID;
        response.target_generation = GENERATION;
        response.request_id = UINT64_C(0x2122232425262728);
        response.created_ms = UINT64_C(0x1000);
        response.deadline_ms = UINT64_C(0x2000);
        response.observed_ms = UINT64_C(0x5152535455565758);

        CHECK(vc_launch_response_encode(&response, encoded, sizeof(encoded),
                                        &encoded_size) == VC_LAUNCH_STATUS_OK);
        CHECK(encoded_size == sizeof(encoded));
        CHECK(memcmp(encoded, expected[index], sizeof(encoded)) == 0);
        CHECK(vc_launch_response_decode(expected[index], sizeof(expected[index]),
                                        &decoded) == VC_LAUNCH_STATUS_OK);
        check_response_equal(&response, &decoded);
    }
}

static void test_abi_rejections(void)
{
    uint8_t wire[VC_LAUNCH_REQUEST_WIRE_SIZE + 1];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE + 1];
    vc_launch_request request =
        request_for(VC_LAUNCH_OPERATION_SUBMIT,
                    VC_LAUNCH_CALLER_SCE_SHELL, 100);
    vc_launch_request decoded;
    vc_launch_response response;
    size_t encoded_size;

    CHECK(vc_launch_request_encode(&request, wire,
                                   VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &encoded_size) == VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE - 1,
                                   &decoded) == VC_LAUNCH_STATUS_INVALID_SIZE);
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE + 1,
                                   &decoded) == VC_LAUNCH_STATUS_INVALID_SIZE);

    wire[2] = 0x3f;
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) == VC_LAUNCH_STATUS_INVALID_SIZE);
    wire[2] = 0x40;
    wire[0] = 2;
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) ==
          VC_LAUNCH_STATUS_UNSUPPORTED_VERSION);
    wire[0] = 1;
    wire[4] = 0xff;
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) ==
          VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION);
    wire[4] = VC_LAUNCH_OPERATION_SUBMIT;
    wire[6] = 3;
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) == VC_LAUNCH_STATUS_INVALID_ROLE);
    wire[6] = VC_LAUNCH_CALLER_SCE_SHELL;
    wire[8] = 0x80;
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) ==
          VC_LAUNCH_STATUS_INVALID_CAPABILITY);
    wire[8] = VC_LAUNCH_CAPABILITY_LAUNCH;
    wire[52] = 1;
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) ==
          VC_LAUNCH_STATUS_INVALID_RESERVED);
    wire[52] = 0;
    wire[63] = 1;
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) ==
          VC_LAUNCH_STATUS_INVALID_RESERVED);

    memset(&response, 0, sizeof(response));
    response.version = VC_LAUNCH_ABI_VERSION;
    response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response.status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    response.launch_state = VC_LAUNCH_STATE_ABSENT;
    CHECK(vc_launch_response_encode(&response, response_wire,
                                    VC_LAUNCH_RESPONSE_WIRE_SIZE,
                                    &encoded_size) == VC_LAUNCH_STATUS_OK);
    CHECK(response_wire[8] == 0xff && response_wire[9] == 0xff &&
          response_wire[10] == 0xff && response_wire[11] == 0xff);
    CHECK(vc_launch_response_decode(response_wire,
                                    VC_LAUNCH_RESPONSE_WIRE_SIZE,
                                    &response) == VC_LAUNCH_STATUS_OK);
    CHECK(response.status == VC_LAUNCH_STATUS_INVALID_ARGUMENT);
    CHECK(vc_launch_response_decode(response_wire,
                                    VC_LAUNCH_RESPONSE_WIRE_SIZE - 1,
                                    &response) ==
          VC_LAUNCH_STATUS_INVALID_SIZE);
    CHECK(vc_launch_response_decode(response_wire,
                                    VC_LAUNCH_RESPONSE_WIRE_SIZE + 1,
                                    &response) ==
          VC_LAUNCH_STATUS_INVALID_SIZE);
    response_wire[64] = 1;
    CHECK(vc_launch_response_decode(response_wire,
                                    VC_LAUNCH_RESPONSE_WIRE_SIZE,
                                    &response) ==
          VC_LAUNCH_STATUS_INVALID_RESERVED);

    memset(&response, 0, sizeof(response));
    response.version = VC_LAUNCH_ABI_VERSION;
    response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response.operation = VC_LAUNCH_OPERATION_SUBMIT;
    response.status = VC_LAUNCH_STATUS_PENDING;
    response.launch_state = VC_LAUNCH_STATE_PENDING;
    response.capabilities = VC_LAUNCH_CAPABILITY_CLAIM;
    response.target_process_id = PID;
    response.target_generation = GENERATION;
    response.request_id = 1;
    response.deadline_ms = 1;
    CHECK(vc_launch_response_encode(&response, response_wire,
                                    VC_LAUNCH_RESPONSE_WIRE_SIZE,
                                    &encoded_size) ==
          VC_LAUNCH_STATUS_INVALID_CAPABILITY);

    request = request_for(VC_LAUNCH_OPERATION_STATUS,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, UINT64_MAX);
    request.target_process_id = UINT32_MAX;
    request.target_generation = UINT64_MAX;
    request.request_id = UINT64_MAX;
    CHECK(vc_launch_request_encode(&request, wire,
                                   VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &encoded_size) == VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_request_decode(wire, VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &decoded) == VC_LAUNCH_STATUS_OK);
    check_request_equal(&request, &decoded);

    request = request_for(VC_LAUNCH_OPERATION_CLAIM,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 1);
    request.presentation_ready = UINT32_MAX;
    CHECK(vc_launch_request_encode(&request, wire,
                                   VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &encoded_size) ==
          VC_LAUNCH_STATUS_MALFORMED_FIELD);
}

static uint64_t submit_request(vc_launch_broker *broker,
                               uint64_t now_ms,
                               uint64_t ttl_ms,
                               vc_launch_response *response)
{
    vc_launch_request request =
        request_for(VC_LAUNCH_OPERATION_SUBMIT,
                    VC_LAUNCH_CALLER_SCE_SHELL, now_ms);

    request.ttl_ms = ttl_ms;
    CHECK(vc_launch_broker_submit(broker, &request, response) ==
          VC_LAUNCH_STATUS_PENDING);
    return response->request_id;
}

static vc_launch_status claim_request(vc_launch_broker *broker,
                                      uint64_t request_id,
                                      uint64_t now_ms,
                                      bool ready,
                                      vc_launch_response *response)
{
    vc_launch_request request =
        request_for(VC_LAUNCH_OPERATION_CLAIM,
                    VC_LAUNCH_CALLER_GAME_PLUGIN, now_ms);

    request.request_id = request_id;
    request.presentation_ready = ready ? 1u : 0u;
    return vc_launch_broker_claim(broker, &request, response);
}

static void prepare_broker(vc_launch_broker *broker, uint64_t first_id)
{
    CHECK(vc_launch_broker_init(broker, first_id) == VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_broker_foreground_changed(
              broker, PID, GENERATION, 100) == VC_LAUNCH_STATUS_OK);
}

static void test_happy_path_and_duplicate(void)
{
    vc_launch_broker broker;
    vc_launch_response response;
    vc_launch_request request;
    uint64_t request_id;

    prepare_broker(&broker, 41);
    request_id = submit_request(&broker, 100, 100, &response);
    CHECK(request_id == 41);
    CHECK(response.deadline_ms == 200);

    request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                          VC_LAUNCH_CALLER_SCE_SHELL, 120);
    request.ttl_ms = 200;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_PENDING);
    CHECK(response.request_id == request_id);
    CHECK(response.deadline_ms == 200);
    CHECK(broker.next_request_id == 42);

    CHECK(claim_request(&broker, request_id, 150, false, &response) ==
          VC_LAUNCH_STATUS_NOT_READY);
    CHECK(broker.record.state == VC_LAUNCH_STATE_PENDING);
    CHECK(claim_request(&broker, request_id, 151, true, &response) ==
          VC_LAUNCH_STATUS_CLAIMED);
    CHECK(broker.record.state == VC_LAUNCH_STATE_CLAIMED);
    CHECK(claim_request(&broker, request_id, 152, true, &response) ==
          VC_LAUNCH_STATUS_CLAIMED);

    request = request_for(VC_LAUNCH_OPERATION_STATUS,
                          VC_LAUNCH_CALLER_SCE_SHELL, 153);
    request.request_id = request_id;
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_CLAIMED);
    CHECK(response.launch_state == VC_LAUNCH_STATE_CLAIMED);
}

static void test_absent_status(void)
{
    vc_launch_broker broker;
    vc_launch_request request;
    vc_launch_response response;

    prepare_broker(&broker, 1);
    request = request_for(VC_LAUNCH_OPERATION_STATUS,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 101);
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_ABSENT);
    CHECK(response.status == VC_LAUNCH_STATUS_ABSENT);
    CHECK(response.launch_state == VC_LAUNCH_STATE_ABSENT);
    CHECK(response.request_id == 0);
}

static void test_game_status_discovery(void)
{
    vc_launch_broker broker;
    vc_launch_request request;
    vc_launch_response response;
    uint64_t request_id;

    prepare_broker(&broker, 17);
    request_id = submit_request(&broker, 100, 100, &response);
    CHECK(request_id == 17);

    request = request_for(VC_LAUNCH_OPERATION_STATUS,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 101);
    request.request_id = 0;
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_PENDING);
    CHECK(response.request_id == request_id);
    CHECK(response.target_process_id == PID);
    CHECK(response.target_generation == GENERATION);
    CHECK(response.capabilities == VC_LAUNCH_CAPABILITY_STATUS);

    request.caller_role = VC_LAUNCH_CALLER_SCE_SHELL;
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_MALFORMED_FIELD);

    request.caller_role = VC_LAUNCH_CALLER_GAME_PLUGIN;
    request.target_generation++;
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_ABSENT);
    CHECK(response.request_id == 0);
    CHECK(response.target_process_id == 0);

    request.target_generation = GENERATION;
    CHECK(claim_request(&broker, request_id, 102, true, &response) ==
          VC_LAUNCH_STATUS_CLAIMED);
    request.now_ms = 103;
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_ABSENT);
    CHECK(response.request_id == 0);

    prepare_broker(&broker, 23);
    request_id = submit_request(&broker, 200, 1, &response);
    request = request_for(VC_LAUNCH_OPERATION_STATUS,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 201);
    request.request_id = 0;
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_ABSENT);
    CHECK(response.status == VC_LAUNCH_STATUS_ABSENT);
    CHECK(response.launch_state == VC_LAUNCH_STATE_ABSENT);
    CHECK(response.request_id == 0);
    CHECK(response.target_process_id == 0);
    CHECK(response.target_generation == 0);
    CHECK(request_id == 23);

    prepare_broker(&broker, 29);
    (void)submit_request(&broker, 300, 100, &response);
    request = request_for(VC_LAUNCH_OPERATION_STATUS,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 299);
    request.request_id = 0;
    CHECK(vc_launch_broker_status(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_CLOCK_ROLLBACK);
    CHECK(response.status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK);
    CHECK(response.launch_state == VC_LAUNCH_STATE_ABSENT);
    CHECK(response.request_id == 0);
    CHECK(response.target_process_id == 0);
    CHECK(response.target_generation == 0);
}

static void check_unchanged(const vc_launch_broker *before,
                            const vc_launch_broker *after)
{
    CHECK(memcmp(before, after, sizeof(*before)) == 0);
}

static void test_wrong_authority_is_transactional(void)
{
    vc_launch_broker broker;
    vc_launch_broker before;
    vc_launch_request request;
    vc_launch_response response;
    uint64_t request_id;

    prepare_broker(&broker, 1);
    request_id = submit_request(&broker, 100, 100, &response);

    request = request_for(VC_LAUNCH_OPERATION_CLAIM,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 101);
    request.request_id = request_id;
    request.presentation_ready = 1;

    before = broker;
    request.target_process_id++;
    CHECK(vc_launch_broker_claim(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_WRONG_TARGET);
    check_unchanged(&before, &broker);

    request.target_process_id = PID;
    request.target_generation++;
    CHECK(vc_launch_broker_claim(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_WRONG_TARGET);
    check_unchanged(&before, &broker);

    request.target_generation = GENERATION;
    request.request_id++;
    CHECK(vc_launch_broker_claim(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_WRONG_REQUEST_ID);
    check_unchanged(&before, &broker);

    request.request_id = request_id;
    request.caller_role = VC_LAUNCH_CALLER_SCE_SHELL;
    CHECK(vc_launch_broker_claim(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_INVALID_ROLE);
    check_unchanged(&before, &broker);

    request.caller_role = VC_LAUNCH_CALLER_GAME_PLUGIN;
    request.capabilities = VC_LAUNCH_CAPABILITY_CLAIM |
                           VC_LAUNCH_CAPABILITY_LAUNCH;
    CHECK(vc_launch_broker_claim(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_INVALID_CAPABILITY);
    check_unchanged(&before, &broker);

    request = request_for(VC_LAUNCH_OPERATION_CANCEL,
                          VC_LAUNCH_CALLER_SCE_SHELL, 101);
    request.request_id = request_id;
    CHECK(vc_launch_broker_cancel(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_INVALID_ROLE);
    check_unchanged(&before, &broker);

    request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 101);
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_INVALID_ROLE);
    check_unchanged(&before, &broker);
}

static void test_explicit_retarget_only(void)
{
    vc_launch_broker broker;
    vc_launch_broker before;
    vc_launch_request request;
    vc_launch_response response;
    uint64_t first_id;

    prepare_broker(&broker, 20);
    first_id = submit_request(&broker, 100, 1000, &response);
    before = broker;

    request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                          VC_LAUNCH_CALLER_SCE_SHELL, 101);
    request.target_process_id = PID + 1;
    request.target_generation = GENERATION + 1;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_STALE_TARGET);
    check_unchanged(&before, &broker);

    CHECK(vc_launch_broker_foreground_changed(
              &broker, PID + 1, GENERATION + 1, 102) ==
          VC_LAUNCH_STATUS_STALE_TARGET);
    CHECK(broker.record.request_id == first_id);
    CHECK(broker.record.state == VC_LAUNCH_STATE_STALE_TARGET);

    request.now_ms = 103;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_PENDING);
    CHECK(response.request_id == first_id + 1);
    CHECK(response.target_process_id == PID + 1);
}

static void test_expiry_ttl_and_clock(void)
{
    vc_launch_broker broker;
    vc_launch_broker before;
    vc_launch_request request;
    vc_launch_response response;
    uint64_t request_id;

    prepare_broker(&broker, 1);
    request_id = submit_request(&broker, 100, 50, &response);
    CHECK(vc_launch_broker_tick(&broker, 149) == VC_LAUNCH_STATUS_PENDING);
    CHECK(claim_request(&broker, request_id, 150, true, &response) ==
          VC_LAUNCH_STATUS_EXPIRED);
    CHECK(broker.record.state == VC_LAUNCH_STATE_EXPIRED);
    CHECK(claim_request(&broker, request_id, 151, true, &response) ==
          VC_LAUNCH_STATUS_EXPIRED);

    CHECK(vc_launch_broker_foreground_changed(
              &broker, PID, GENERATION + 1, 200) ==
          VC_LAUNCH_STATUS_STALE_TARGET);
    request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                          VC_LAUNCH_CALLER_SCE_SHELL, 200);
    request.target_generation = GENERATION + 1;
    request.ttl_ms = 1;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_PENDING);
    CHECK(vc_launch_broker_tick(&broker, 202) == VC_LAUNCH_STATUS_EXPIRED);

    before = broker;
    request.now_ms = 203;
    request.ttl_ms = 0;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_MALFORMED_FIELD);
    check_unchanged(&before, &broker);
    request.ttl_ms = VC_LAUNCH_MAX_TTL_MS + 1;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_MALFORMED_FIELD);
    check_unchanged(&before, &broker);
    request.now_ms = UINT64_MAX;
    request.ttl_ms = 1;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_MALFORMED_FIELD);
    check_unchanged(&before, &broker);

    prepare_broker(&broker, 10);
    request_id = submit_request(&broker, 100, 100, &response);
    CHECK(vc_launch_broker_tick(&broker, 99) ==
          VC_LAUNCH_STATUS_CLOCK_ROLLBACK);
    CHECK(broker.record.state == VC_LAUNCH_STATE_CLOCK_ROLLBACK);
    CHECK(claim_request(&broker, request_id, 100, true, &response) ==
          VC_LAUNCH_STATUS_CLOCK_ROLLBACK);
    CHECK(broker.record.state == VC_LAUNCH_STATE_CLOCK_ROLLBACK);
}

static void test_lifecycle_cleanup(void)
{
    vc_launch_broker broker;
    vc_launch_request request;
    vc_launch_response response;
    uint64_t request_id;

    prepare_broker(&broker, 100);
    request_id = submit_request(&broker, 100, 100, &response);
    CHECK(vc_launch_broker_plugin_unload(
              &broker, PID, GENERATION, 101) ==
          VC_LAUNCH_STATUS_CANCELLED);
    CHECK(vc_launch_broker_plugin_unload(
              &broker, PID, GENERATION, 102) ==
          VC_LAUNCH_STATUS_CANCELLED);
    CHECK(claim_request(&broker, request_id, 103, true, &response) ==
          VC_LAUNCH_STATUS_CANCELLED);

    request_id = submit_request(&broker, 104, 100, &response);
    request = request_for(VC_LAUNCH_OPERATION_CANCEL,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 105);
    request.request_id = request_id;
    CHECK(vc_launch_broker_cancel(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_CANCELLED);
    CHECK(vc_launch_broker_cancel(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_CANCELLED);

    request_id = submit_request(&broker, 106, 100, &response);
    CHECK(vc_launch_broker_process_exit(
              &broker, PID, GENERATION, 107) ==
          VC_LAUNCH_STATUS_STALE_TARGET);
    CHECK(vc_launch_broker_process_exit(
              &broker, PID, GENERATION, 108) ==
          VC_LAUNCH_STATUS_STALE_TARGET);
    CHECK(claim_request(&broker, request_id, 109, true, &response) ==
          VC_LAUNCH_STATUS_STALE_TARGET);

    CHECK(vc_launch_broker_foreground_changed(
              &broker, PID, GENERATION + 1, 110) ==
          VC_LAUNCH_STATUS_OK);
    request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                          VC_LAUNCH_CALLER_SCE_SHELL, 110);
    request.target_generation = GENERATION + 1;
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_PENDING);
    CHECK(vc_launch_broker_service_reset(&broker, 111) ==
          VC_LAUNCH_STATUS_CANCELLED);
    CHECK(vc_launch_broker_service_reset(&broker, 112) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(broker.foreground_process_id == 0);
    CHECK(broker.foreground_generation == 0);
    CHECK(broker.record.state == VC_LAUNCH_STATE_CANCELLED);
}

static void test_request_id_exhaustion(void)
{
    vc_launch_broker broker;
    vc_launch_request request;
    vc_launch_response response;
    uint64_t request_id;

    prepare_broker(&broker, UINT64_MAX - 1);
    request_id = submit_request(&broker, 100, 10, &response);
    CHECK(request_id == UINT64_MAX - 1);
    CHECK(claim_request(&broker, request_id, 101, true, &response) ==
          VC_LAUNCH_STATUS_CLAIMED);
    request_id = submit_request(&broker, 102, 10, &response);
    CHECK(request_id == UINT64_MAX);
    CHECK(broker.id_exhausted);
    CHECK(claim_request(&broker, request_id, 103, true, &response) ==
          VC_LAUNCH_STATUS_CLAIMED);

    request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                          VC_LAUNCH_CALLER_SCE_SHELL, 104);
    CHECK(vc_launch_broker_submit(&broker, &request, &response) ==
          VC_LAUNCH_STATUS_ID_EXHAUSTED);
    CHECK(broker.record.request_id == UINT64_MAX);
    CHECK(broker.record.state == VC_LAUNCH_STATE_CLAIMED);
    CHECK(vc_launch_broker_init(&broker, 0) ==
          VC_LAUNCH_STATUS_INVALID_ARGUMENT);
}

static void test_wire_dispatch_and_failed_decode_no_mutation(void)
{
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    vc_launch_broker broker;
    vc_launch_broker before;
    vc_launch_request request =
        request_for(VC_LAUNCH_OPERATION_SUBMIT,
                    VC_LAUNCH_CALLER_SCE_SHELL, 100);
    vc_launch_response response;
    size_t request_size;
    size_t response_size;

    prepare_broker(&broker, 77);
    CHECK(vc_launch_request_encode(&request, request_wire,
                                   sizeof(request_wire), &request_size) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size,
              response_wire, sizeof(response_wire), &response_size) ==
          VC_LAUNCH_STATUS_PENDING);
    CHECK(response_size == sizeof(response_wire));
    CHECK(vc_launch_response_decode(response_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    CHECK(response.request_id == 77);

    request = request_for(VC_LAUNCH_OPERATION_CLAIM,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 101);
    request.request_id = 77;
    request.presentation_ready = 1;
    CHECK(vc_launch_request_encode(&request, request_wire,
                                   sizeof(request_wire), &request_size) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size,
              response_wire, sizeof(response_wire), &response_size) ==
          VC_LAUNCH_STATUS_CLAIMED);

    request = request_for(VC_LAUNCH_OPERATION_STATUS,
                          VC_LAUNCH_CALLER_SCE_SHELL, 102);
    request.request_id = 77;
    CHECK(vc_launch_request_encode(&request, request_wire,
                                   sizeof(request_wire), &request_size) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size,
              response_wire, sizeof(response_wire), &response_size) ==
          VC_LAUNCH_STATUS_CLAIMED);

    request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                          VC_LAUNCH_CALLER_SCE_SHELL, 103);
    CHECK(vc_launch_request_encode(&request, request_wire,
                                   sizeof(request_wire), &request_size) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size,
              response_wire, sizeof(response_wire), &response_size) ==
          VC_LAUNCH_STATUS_PENDING);
    CHECK(vc_launch_response_decode(response_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    CHECK(response.request_id == 78);

    request = request_for(VC_LAUNCH_OPERATION_CANCEL,
                          VC_LAUNCH_CALLER_GAME_PLUGIN, 104);
    request.request_id = 78;
    CHECK(vc_launch_request_encode(&request, request_wire,
                                   sizeof(request_wire), &request_size) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size,
              response_wire, sizeof(response_wire), &response_size) ==
          VC_LAUNCH_STATUS_CANCELLED);

    before = broker;
    request_wire[56] = 1;
    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size,
              response_wire, sizeof(response_wire), &response_size) ==
          VC_LAUNCH_STATUS_INVALID_RESERVED);
    check_unchanged(&before, &broker);
    CHECK(vc_launch_response_decode(response_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    CHECK(response.status == VC_LAUNCH_STATUS_INVALID_RESERVED);

    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size - 1,
              response_wire, sizeof(response_wire), &response_size) ==
          VC_LAUNCH_STATUS_INVALID_SIZE);
    check_unchanged(&before, &broker);
    CHECK(vc_launch_broker_dispatch_wire(
              &broker, request_wire, request_size,
              response_wire, sizeof(response_wire) - 1, &response_size) ==
          VC_LAUNCH_STATUS_INVALID_SIZE);
    check_unchanged(&before, &broker);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void check_broker_invariants(const vc_launch_broker *broker)
{
    CHECK(broker->initialized);
    CHECK(broker->record.state <= VC_LAUNCH_STATE_CLOCK_ROLLBACK);
    if (broker->record.state != VC_LAUNCH_STATE_ABSENT) {
        CHECK(broker->record.request_id != 0);
        CHECK(broker->record.target_process_id != 0);
        CHECK(broker->record.target_generation != 0);
        CHECK(broker->record.deadline_ms > broker->record.created_ms);
        CHECK(broker->record.deadline_ms - broker->record.created_ms <=
              VC_LAUNCH_MAX_TTL_MS);
    }
    if (!broker->id_exhausted) {
        CHECK(broker->next_request_id != 0);
    }
}

static void test_bounded_sequence_model(void)
{
    vc_launch_broker broker;
    vc_launch_response response;
    uint32_t random_state = UINT32_C(0xc0ffee);
    uint64_t now_ms = 100;
    size_t index;

    prepare_broker(&broker, 1);
    for (index = 0; index < 5000; ++index) {
        uint32_t choice = next_random(&random_state) % 8;
        vc_launch_request request;

        now_ms += next_random(&random_state) % 3;
        switch (choice) {
        case 0:
            request = request_for(VC_LAUNCH_OPERATION_SUBMIT,
                                  VC_LAUNCH_CALLER_SCE_SHELL, now_ms);
            request.ttl_ms = 1 + next_random(&random_state) %
                                     (uint32_t)VC_LAUNCH_MAX_TTL_MS;
            (void)vc_launch_broker_submit(&broker, &request, &response);
            break;
        case 1:
            request = request_for(VC_LAUNCH_OPERATION_CLAIM,
                                  VC_LAUNCH_CALLER_GAME_PLUGIN, now_ms);
            request.request_id = broker.record.request_id != 0
                                     ? broker.record.request_id
                                     : 1;
            request.presentation_ready = next_random(&random_state) & 1u;
            (void)vc_launch_broker_claim(&broker, &request, &response);
            break;
        case 2:
            request = request_for(VC_LAUNCH_OPERATION_CANCEL,
                                  VC_LAUNCH_CALLER_GAME_PLUGIN, now_ms);
            request.request_id = broker.record.request_id != 0
                                     ? broker.record.request_id
                                     : 1;
            (void)vc_launch_broker_cancel(&broker, &request, &response);
            break;
        case 3:
            request = request_for(VC_LAUNCH_OPERATION_STATUS,
                                  VC_LAUNCH_CALLER_SCE_SHELL, now_ms);
            request.request_id = broker.record.request_id != 0
                                     ? broker.record.request_id
                                     : 1;
            (void)vc_launch_broker_status(&broker, &request, &response);
            break;
        case 4:
            (void)vc_launch_broker_tick(&broker, now_ms);
            break;
        case 5:
            (void)vc_launch_broker_plugin_unload(
                &broker, PID, GENERATION, now_ms);
            break;
        case 6:
            (void)vc_launch_broker_service_reset(&broker, now_ms);
            (void)vc_launch_broker_foreground_changed(
                &broker, PID, GENERATION, now_ms);
            break;
        default:
            (void)vc_launch_broker_foreground_changed(
                &broker, PID, GENERATION, now_ms);
            break;
        }
        check_broker_invariants(&broker);
    }
}

int main(void)
{
    test_request_golden_vectors();
    test_response_golden_vectors();
    test_abi_rejections();
    test_happy_path_and_duplicate();
    test_absent_status();
    test_game_status_discovery();
    test_wrong_authority_is_transactional();
    test_explicit_retarget_only();
    test_expiry_ttl_and_clock();
    test_lifecycle_cleanup();
    test_request_id_exhaustion();
    test_wire_dispatch_and_failed_decode_no_mutation();
    test_bounded_sequence_model();

    if (failures != 0) {
        fprintf(stderr, "%d launch broker test(s) failed\n", failures);
        return 1;
    }

    puts("all launch broker tests passed");
    return 0;
}
