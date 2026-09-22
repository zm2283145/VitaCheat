#include "vitacheat/memory_read.h"

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

static vc_memory_read_request read_request(void)
{
    vc_memory_read_request request;

    vc_memory_read_request_init(
        &request, VC_MEMORY_READ_OPERATION_READ);
    request.request_id = UINT64_C(0x0102030405060708);
    request.session_id = UINT64_C(0x1112131415161718);
    request.target_process_id = UINT32_C(0x21222324);
    request.module_id = 0;
    request.target_generation =
        UINT64_C(0x3132333435363738);
    request.attestation_revision =
        UINT64_C(0x4142434445464748);
    request.module_load_generation =
        UINT64_C(0x5152535455565758);
    request.segment_index = 1;
    request.segment_offset = UINT32_C(0x61626364);
    request.length = 4;
    return request;
}

static vc_memory_read_response read_response(void)
{
    vc_memory_read_response response;

    memset(&response, 0, sizeof(response));
    response.version = VC_MEMORY_READ_ABI_VERSION;
    response.header_size =
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE;
    response.total_size =
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE + 4u;
    response.operation = VC_MEMORY_READ_OPERATION_READ;
    response.status = VC_MEMORY_READ_STATUS_OK;
    response.state = VC_MEMORY_READ_STATE_ACTIVE;
    response.capabilities =
        VC_MEMORY_READ_CAPABILITY_READ_TARGET;
    response.payload_size = 4;
    response.request_id = UINT64_C(0x0102030405060708);
    response.session_id = UINT64_C(0x1112131415161718);
    response.remaining_bytes =
        UINT64_C(0x2122232425262728);
    response.remaining_operations = UINT32_C(0x31323334);
    response.payload[0] = 0xaa;
    response.payload[1] = 0xbb;
    response.payload[2] = 0xcc;
    response.payload[3] = 0xdd;
    return response;
}

static vc_memory_read_request status_request(void)
{
    vc_memory_read_request request;

    vc_memory_read_request_init(
        &request, VC_MEMORY_READ_OPERATION_STATUS);
    request.request_id = UINT64_C(0x0102030405060708);
    request.session_id = UINT64_C(0x1112131415161718);
    request.target_process_id = UINT32_C(0x21222324);
    request.target_generation =
        UINT64_C(0x3132333435363738);
    request.attestation_revision =
        UINT64_C(0x4142434445464748);
    return request;
}

static vc_memory_read_response status_response(void)
{
    vc_memory_read_response response = read_response();

    response.total_size =
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE;
    response.operation = VC_MEMORY_READ_OPERATION_STATUS;
    response.payload_size = 0;
    memset(response.payload, 0, sizeof(response.payload));
    return response;
}

static void test_request_golden_and_round_trip(void)
{
    static const uint8_t golden[VC_MEMORY_READ_REQUEST_WIRE_SIZE] = {
        0x01, 0x00, 0x50, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
        0x24, 0x23, 0x22, 0x21, 0x00, 0x00, 0x00, 0x00,
        0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31,
        0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41,
        0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51,
        0x01, 0x00, 0x00, 0x00, 0x64, 0x63, 0x62, 0x61,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    vc_memory_read_request request = read_request();
    vc_memory_read_request decoded;
    uint8_t wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    size_t encoded_size = 0;

    CHECK(sizeof(request) == VC_MEMORY_READ_REQUEST_WIRE_SIZE);
    CHECK(vc_memory_read_request_encode(
              &request, wire, sizeof(wire), &encoded_size) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(encoded_size == sizeof(wire));
    CHECK(memcmp(wire, golden, sizeof(golden)) == 0);
    memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(vc_memory_read_request_decode(
              wire, sizeof(wire), &decoded) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(memcmp(&decoded, &request, sizeof(request)) == 0);
}

static void test_response_golden_and_round_trip(void)
{
    static const uint8_t golden[] = {
        0x01, 0x00, 0x40, 0x00, 0x44, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
        0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
        0x34, 0x33, 0x32, 0x31, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xaa, 0xbb, 0xcc, 0xdd
    };
    vc_memory_read_response response = read_response();
    vc_memory_read_response decoded;
    uint8_t wire[sizeof(golden)];
    size_t encoded_size = 0;

    CHECK(sizeof(response) == VC_MEMORY_READ_RESPONSE_WIRE_MAX);
    CHECK(vc_memory_read_response_encode(
              &response, wire, sizeof(wire), &encoded_size) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(encoded_size == sizeof(golden));
    CHECK(memcmp(wire, golden, sizeof(golden)) == 0);
    memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(vc_memory_read_response_decode(
              wire, sizeof(wire), &decoded) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(decoded.payload_size == 4);
    CHECK(memcmp(decoded.payload, response.payload, 4) == 0);
    CHECK(decoded.payload[4] == 0);

    response = status_response();
    CHECK(vc_memory_read_response_encode(
              &response, wire,
              VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE,
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);
    CHECK(encoded_size ==
          VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE);
    CHECK(vc_memory_read_response_decode(
              wire, encoded_size, &decoded) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(decoded.operation ==
          VC_MEMORY_READ_OPERATION_STATUS);
    CHECK(decoded.payload_size == 0);
}

static void test_status_golden_and_round_trip(void)
{
    static const uint8_t request_golden[
        VC_MEMORY_READ_REQUEST_WIRE_SIZE] = {
        0x01, 0x00, 0x50, 0x00, 0x02, 0x00, 0x02, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
        0x24, 0x23, 0x22, 0x21, 0x00, 0x00, 0x00, 0x00,
        0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31,
        0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t response_golden[
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE] = {
        0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
        0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
        0x34, 0x33, 0x32, 0x31, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    vc_memory_read_request request = status_request();
    vc_memory_read_request decoded_request;
    vc_memory_read_response response = status_response();
    vc_memory_read_response decoded_response;
    uint8_t request_wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    uint8_t response_wire[
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE];
    size_t encoded_size = 0;

    CHECK(vc_memory_read_request_encode(
              &request, request_wire, sizeof(request_wire),
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);
    CHECK(encoded_size == sizeof(request_golden));
    CHECK(memcmp(request_wire, request_golden,
                 sizeof(request_golden)) == 0);
    CHECK(vc_memory_read_request_decode(
              request_wire, sizeof(request_wire),
              &decoded_request) == VC_MEMORY_READ_STATUS_OK);
    CHECK(memcmp(&decoded_request, &request,
                 sizeof(request)) == 0);

    CHECK(vc_memory_read_response_encode(
              &response, response_wire,
              sizeof(response_wire), &encoded_size) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(encoded_size == sizeof(response_golden));
    CHECK(memcmp(response_wire, response_golden,
                 sizeof(response_golden)) == 0);
    CHECK(vc_memory_read_response_decode(
              response_wire, sizeof(response_wire),
              &decoded_response) == VC_MEMORY_READ_STATUS_OK);
    CHECK(memcmp(&decoded_response, &response,
                 sizeof(response)) == 0);
}

static void test_request_rejections(void)
{
    vc_memory_read_request request = read_request();
    vc_memory_read_request decoded;
    uint8_t wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE + 1u];
    size_t encoded_size = 9;

    CHECK(vc_memory_read_request_encode(
              NULL, wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &encoded_size) ==
          VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_request_encode(
              &request, NULL, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &encoded_size) ==
          VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_request_encode(
              &request, wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              NULL) == VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_request_encode(
              &request, wire, 0, &encoded_size) ==
          VC_MEMORY_READ_STATUS_INVALID_SIZE);
    CHECK(encoded_size == 0);
    CHECK(vc_memory_read_request_decode(
              NULL, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &decoded) ==
          VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_request_decode(
              wire, 0, &decoded) ==
          VC_MEMORY_READ_STATUS_INVALID_SIZE);
    CHECK(vc_memory_read_request_decode(
              wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE - 1u,
              &decoded) ==
          VC_MEMORY_READ_STATUS_INVALID_SIZE);
    CHECK(vc_memory_read_request_decode(
              wire, sizeof(wire), &decoded) ==
          VC_MEMORY_READ_STATUS_INVALID_SIZE);

#define REJECT_REQUEST(member, value, expected)                                  \
    do {                                                                         \
        vc_memory_read_request changed = request;                                \
        changed.member = (value);                                                \
        CHECK(vc_memory_read_request_encode(                                     \
                  &changed, wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,               \
                  &encoded_size) == (expected));                                 \
    } while (0)

    REJECT_REQUEST(version, 2,
                   VC_MEMORY_READ_STATUS_UNSUPPORTED_VERSION);
    REJECT_REQUEST(struct_size, 79,
                   VC_MEMORY_READ_STATUS_INVALID_SIZE);
    REJECT_REQUEST(operation, 3,
                   VC_MEMORY_READ_STATUS_UNSUPPORTED_OPERATION);
    REJECT_REQUEST(caller_role, VC_LAUNCH_CALLER_SCE_SHELL,
                   VC_MEMORY_READ_STATUS_INVALID_ROLE);
    REJECT_REQUEST(capabilities, 0,
                   VC_MEMORY_READ_STATUS_INVALID_CAPABILITY);
    REJECT_REQUEST(capabilities, 3,
                   VC_MEMORY_READ_STATUS_INVALID_CAPABILITY);
    REJECT_REQUEST(reserved0, 1,
                   VC_MEMORY_READ_STATUS_INVALID_RESERVED);
    REJECT_REQUEST(reserved1, 1,
                   VC_MEMORY_READ_STATUS_INVALID_RESERVED);
    REJECT_REQUEST(request_id, 0,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_REQUEST(session_id, 0,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_REQUEST(target_process_id, 0,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_REQUEST(target_generation, 0,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_REQUEST(attestation_revision, 0,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_REQUEST(module_load_generation, 0,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_REQUEST(length, 0,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_REQUEST(length, VC_MEMORY_READ_MAX_PAYLOAD + 1u,
                   VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    request.segment_offset = UINT32_MAX;
    request.length = 1;
    CHECK(vc_memory_read_request_encode(
              &request, wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &encoded_size) ==
          VC_MEMORY_READ_STATUS_MALFORMED_FIELD);

    vc_memory_read_request_init(
        &request, VC_MEMORY_READ_OPERATION_STATUS);
    request.request_id = 1;
    request.session_id = 2;
    request.target_process_id = 3;
    request.target_generation = 4;
    request.attestation_revision = 5;
    CHECK(vc_memory_read_request_encode(
              &request, wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);
    request.length = 1;
    CHECK(vc_memory_read_request_encode(
              &request, wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &encoded_size) ==
          VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
#undef REJECT_REQUEST
}

static void test_response_rejections(void)
{
    vc_memory_read_response response = read_response();
    vc_memory_read_response decoded;
    uint8_t wire[VC_MEMORY_READ_RESPONSE_WIRE_MAX + 1u];
    size_t encoded_size = 0;

    CHECK(vc_memory_read_response_encode(
              NULL, wire, response.total_size,
              &encoded_size) ==
          VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_response_encode(
              &response, NULL, response.total_size,
              &encoded_size) ==
          VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_response_encode(
              &response, wire, response.total_size,
              NULL) == VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_response_decode(
              NULL, response.total_size, &decoded) ==
          VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_response_decode(
              wire, response.total_size, NULL) ==
          VC_MEMORY_READ_STATUS_INVALID_ARGUMENT);
    CHECK(vc_memory_read_response_decode(
              wire, 0, &decoded) ==
          VC_MEMORY_READ_STATUS_INVALID_SIZE);
    CHECK(vc_memory_read_response_encode(
              &response, wire, response.total_size,
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);
    CHECK(vc_memory_read_response_encode(
              &response, wire, sizeof(wire),
              &encoded_size) == VC_MEMORY_READ_STATUS_INVALID_SIZE);
    CHECK(vc_memory_read_response_decode(
              wire, VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE - 1u,
              &decoded) == VC_MEMORY_READ_STATUS_INVALID_SIZE);
    CHECK(vc_memory_read_response_decode(
              wire, VC_MEMORY_READ_RESPONSE_WIRE_MAX + 1u,
              &decoded) == VC_MEMORY_READ_STATUS_INVALID_SIZE);
    CHECK(vc_memory_read_response_decode(
              wire, response.total_size - 1u,
              &decoded) == VC_MEMORY_READ_STATUS_INVALID_SIZE);

#define REJECT_RESPONSE(member, value, expected)                                 \
    do {                                                                         \
        vc_memory_read_response changed = response;                              \
        changed.member = (value);                                                \
        CHECK(vc_memory_read_response_encode(                                    \
                  &changed, wire, changed.total_size,                            \
                  &encoded_size) == (expected));                                 \
    } while (0)

    REJECT_RESPONSE(version, 2,
                    VC_MEMORY_READ_STATUS_UNSUPPORTED_VERSION);
    REJECT_RESPONSE(header_size, 63,
                    VC_MEMORY_READ_STATUS_INVALID_SIZE);
    REJECT_RESPONSE(operation, 3,
                    VC_MEMORY_READ_STATUS_UNSUPPORTED_OPERATION);
    REJECT_RESPONSE(status, -29,
                    VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_RESPONSE(state, VC_MEMORY_READ_STATE_ERROR + 1u,
                    VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
    REJECT_RESPONSE(capabilities, 0,
                    VC_MEMORY_READ_STATUS_INVALID_CAPABILITY);
    REJECT_RESPONSE(capabilities, 3,
                    VC_MEMORY_READ_STATUS_INVALID_CAPABILITY);
    REJECT_RESPONSE(reserved0, 1,
                    VC_MEMORY_READ_STATUS_INVALID_RESERVED);
    REJECT_RESPONSE(reserved1, 1,
                    VC_MEMORY_READ_STATUS_INVALID_RESERVED);

    response.payload_size = VC_MEMORY_READ_MAX_PAYLOAD + 1u;
    CHECK(vc_memory_read_response_encode(
              &response, wire, sizeof(wire),
              &encoded_size) == VC_MEMORY_READ_STATUS_INVALID_SIZE);
    response = read_response();
    response.total_size = UINT16_MAX;
    CHECK(vc_memory_read_response_encode(
              &response, wire, sizeof(wire),
              &encoded_size) == VC_MEMORY_READ_STATUS_INVALID_SIZE);
#undef REJECT_RESPONSE

    response = read_response();
    response.payload_size = VC_MEMORY_READ_MAX_PAYLOAD;
    response.total_size = VC_MEMORY_READ_RESPONSE_WIRE_MAX;
    memset(response.payload, 0x5a, sizeof(response.payload));
    CHECK(vc_memory_read_response_encode(
              &response, wire, VC_MEMORY_READ_RESPONSE_WIRE_MAX,
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);
    CHECK(encoded_size == VC_MEMORY_READ_RESPONSE_WIRE_MAX);
    memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(vc_memory_read_response_decode(
              wire, encoded_size, &decoded) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(decoded.payload_size == VC_MEMORY_READ_MAX_PAYLOAD);
    CHECK(memcmp(decoded.payload, response.payload,
                 sizeof(response.payload)) == 0);

    response = read_response();
    CHECK(vc_memory_read_response_encode(
              &response, wire, response.total_size,
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);
    wire[0] = 2;
    CHECK(vc_memory_read_response_decode(
              wire, encoded_size, &decoded) ==
          VC_MEMORY_READ_STATUS_UNSUPPORTED_VERSION);
    wire[0] = 1;
    wire[6] = 3;
    CHECK(vc_memory_read_response_decode(
              wire, encoded_size, &decoded) ==
          VC_MEMORY_READ_STATUS_UNSUPPORTED_OPERATION);
    wire[6] = VC_MEMORY_READ_OPERATION_READ;
    wire[8] = 0xe3;
    wire[9] = 0xff;
    wire[10] = 0xff;
    wire[11] = 0xff;
    CHECK(vc_memory_read_response_decode(
              wire, encoded_size, &decoded) ==
          VC_MEMORY_READ_STATUS_MALFORMED_FIELD);
}

int main(void)
{
    test_request_golden_and_round_trip();
    test_response_golden_and_round_trip();
    test_status_golden_and_round_trip();
    test_request_rejections();
    test_response_rejections();

    if (failures != 0) {
        fprintf(stderr, "%d memory read test(s) failed\n", failures);
        return 1;
    }
    puts("memory read ABI tests passed");
    return 0;
}
