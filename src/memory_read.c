#include "vitacheat/memory_read.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

enum {
    VC_MEMORY_REQUEST_OFFSET_VERSION = 0,
    VC_MEMORY_REQUEST_OFFSET_SIZE = 2,
    VC_MEMORY_REQUEST_OFFSET_OPERATION = 4,
    VC_MEMORY_REQUEST_OFFSET_ROLE = 6,
    VC_MEMORY_REQUEST_OFFSET_CAPABILITIES = 8,
    VC_MEMORY_REQUEST_OFFSET_RESERVED0 = 12,
    VC_MEMORY_REQUEST_OFFSET_REQUEST_ID = 16,
    VC_MEMORY_REQUEST_OFFSET_SESSION_ID = 24,
    VC_MEMORY_REQUEST_OFFSET_TARGET_PID = 32,
    VC_MEMORY_REQUEST_OFFSET_MODULE_ID = 36,
    VC_MEMORY_REQUEST_OFFSET_TARGET_GENERATION = 40,
    VC_MEMORY_REQUEST_OFFSET_ATTESTATION_REVISION = 48,
    VC_MEMORY_REQUEST_OFFSET_MODULE_GENERATION = 56,
    VC_MEMORY_REQUEST_OFFSET_SEGMENT_INDEX = 64,
    VC_MEMORY_REQUEST_OFFSET_SEGMENT_OFFSET = 68,
    VC_MEMORY_REQUEST_OFFSET_LENGTH = 72,
    VC_MEMORY_REQUEST_OFFSET_RESERVED1 = 76
};

enum {
    VC_MEMORY_RESPONSE_OFFSET_VERSION = 0,
    VC_MEMORY_RESPONSE_OFFSET_HEADER_SIZE = 2,
    VC_MEMORY_RESPONSE_OFFSET_TOTAL_SIZE = 4,
    VC_MEMORY_RESPONSE_OFFSET_OPERATION = 6,
    VC_MEMORY_RESPONSE_OFFSET_STATUS = 8,
    VC_MEMORY_RESPONSE_OFFSET_STATE = 12,
    VC_MEMORY_RESPONSE_OFFSET_CAPABILITIES = 16,
    VC_MEMORY_RESPONSE_OFFSET_PAYLOAD_SIZE = 20,
    VC_MEMORY_RESPONSE_OFFSET_REQUEST_ID = 24,
    VC_MEMORY_RESPONSE_OFFSET_SESSION_ID = 32,
    VC_MEMORY_RESPONSE_OFFSET_REMAINING_BYTES = 40,
    VC_MEMORY_RESPONSE_OFFSET_REMAINING_OPERATIONS = 48,
    VC_MEMORY_RESPONSE_OFFSET_RESERVED0 = 52,
    VC_MEMORY_RESPONSE_OFFSET_RESERVED1 = 56,
    VC_MEMORY_RESPONSE_OFFSET_PAYLOAD = 64
};

_Static_assert(VC_MEMORY_READ_REQUEST_WIRE_SIZE == UINT16_C(80),
               "memory request ABI size changed");
_Static_assert(VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE == UINT16_C(64),
               "memory response header ABI size changed");
_Static_assert(VC_MEMORY_READ_RESPONSE_WIRE_MAX == UINT32_C(320),
               "memory response maximum changed");

static uint16_t vc_memory_read_u16(const uint8_t *buffer)
{
    return (uint16_t)((uint16_t)buffer[0] |
                      ((uint16_t)buffer[1] << 8));
}

static uint32_t vc_memory_read_u32(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8) |
           ((uint32_t)buffer[2] << 16) |
           ((uint32_t)buffer[3] << 24);
}

static uint64_t vc_memory_read_u64(const uint8_t *buffer)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)buffer[index] << (index * 8);
    }
    return value;
}

static void vc_memory_write_u16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static void vc_memory_write_u32(uint8_t *buffer, uint32_t value)
{
    unsigned int index;

    for (index = 0; index < 4; ++index) {
        buffer[index] = (uint8_t)(value >> (index * 8));
    }
}

static void vc_memory_write_u64(uint8_t *buffer, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        buffer[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool vc_memory_operation_valid(uint16_t operation, bool allow_none)
{
    return (allow_none && operation == VC_MEMORY_READ_OPERATION_NONE) ||
           operation == VC_MEMORY_READ_OPERATION_READ ||
           operation == VC_MEMORY_READ_OPERATION_STATUS;
}

static bool vc_memory_state_valid(uint32_t state)
{
    return state <= VC_MEMORY_READ_STATE_ERROR;
}

static bool vc_memory_status_valid(int32_t status)
{
    return status == VC_MEMORY_READ_STATUS_OK ||
           (status <= VC_MEMORY_READ_STATUS_INVALID_ARGUMENT &&
            status >= VC_MEMORY_READ_STATUS_BOUNDED_ERROR);
}

static bool vc_memory_decode_status(uint32_t wire, int32_t *status)
{
    uint32_t magnitude;

    if (wire == 0) {
        *status = VC_MEMORY_READ_STATUS_OK;
        return true;
    }
    magnitude = UINT32_MAX - wire + 1u;
    if (magnitude == 0 || magnitude > 28u) {
        return false;
    }
    *status = -(int32_t)magnitude;
    return vc_memory_status_valid(*status);
}

static vc_memory_read_status vc_memory_validate_request(
    const vc_memory_read_request *request)
{
    if (request == NULL) {
        return VC_MEMORY_READ_STATUS_INVALID_ARGUMENT;
    }
    if (request->version != VC_MEMORY_READ_ABI_VERSION) {
        return VC_MEMORY_READ_STATUS_UNSUPPORTED_VERSION;
    }
    if (request->struct_size != VC_MEMORY_READ_REQUEST_WIRE_SIZE) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    if (!vc_memory_operation_valid(request->operation, false)) {
        return VC_MEMORY_READ_STATUS_UNSUPPORTED_OPERATION;
    }
    if (request->caller_role != VC_LAUNCH_CALLER_GAME_PLUGIN) {
        return VC_MEMORY_READ_STATUS_INVALID_ROLE;
    }
    if (request->capabilities !=
        VC_MEMORY_READ_CAPABILITY_READ_TARGET) {
        return VC_MEMORY_READ_STATUS_INVALID_CAPABILITY;
    }
    if (request->reserved0 != 0 || request->reserved1 != 0) {
        return VC_MEMORY_READ_STATUS_INVALID_RESERVED;
    }
    if (request->request_id == 0 || request->session_id == 0 ||
        request->target_process_id == 0 ||
        request->target_generation == 0 ||
        request->attestation_revision == 0) {
        return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
    }

    if (request->operation == VC_MEMORY_READ_OPERATION_READ) {
        if (request->module_load_generation == 0 ||
            request->length == 0 ||
            request->length > VC_MEMORY_READ_MAX_PAYLOAD ||
            request->segment_offset >
                UINT32_MAX - request->length) {
            return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
        }
    } else if (request->module_id != 0 ||
               request->module_load_generation != 0 ||
               request->segment_index != 0 ||
               request->segment_offset != 0 ||
               request->length != 0) {
        return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
    }
    return VC_MEMORY_READ_STATUS_OK;
}

static vc_memory_read_status vc_memory_validate_response(
    const vc_memory_read_response *response)
{
    uint32_t expected_total;

    if (response == NULL) {
        return VC_MEMORY_READ_STATUS_INVALID_ARGUMENT;
    }
    if (response->version != VC_MEMORY_READ_ABI_VERSION) {
        return VC_MEMORY_READ_STATUS_UNSUPPORTED_VERSION;
    }
    if (response->header_size !=
        VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    if (!vc_memory_operation_valid(response->operation, true)) {
        return VC_MEMORY_READ_STATUS_UNSUPPORTED_OPERATION;
    }
    if (!vc_memory_status_valid(response->status) ||
        !vc_memory_state_valid(response->state)) {
        return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
    }
    if (response->reserved0 != 0 || response->reserved1 != 0) {
        return VC_MEMORY_READ_STATUS_INVALID_RESERVED;
    }
    if (response->payload_size > VC_MEMORY_READ_MAX_PAYLOAD) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    expected_total =
        (uint32_t)VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE +
        response->payload_size;
    if (expected_total > UINT16_MAX ||
        response->total_size != (uint16_t)expected_total) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    if (response->operation == VC_MEMORY_READ_OPERATION_NONE) {
        if (response->capabilities != 0 ||
            response->payload_size != 0 ||
            response->request_id != 0 ||
            response->session_id != 0 ||
            response->remaining_bytes != 0 ||
            response->remaining_operations != 0) {
            return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
        }
        return VC_MEMORY_READ_STATUS_OK;
    }
    if (response->capabilities !=
            VC_MEMORY_READ_CAPABILITY_READ_TARGET ||
        response->request_id == 0) {
        return VC_MEMORY_READ_STATUS_INVALID_CAPABILITY;
    }
    if (response->status == VC_MEMORY_READ_STATUS_OK) {
        if (response->session_id == 0 ||
            response->state != VC_MEMORY_READ_STATE_ACTIVE) {
            return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
        }
        if ((response->operation == VC_MEMORY_READ_OPERATION_READ &&
             response->payload_size == 0) ||
            (response->operation == VC_MEMORY_READ_OPERATION_STATUS &&
             response->payload_size != 0)) {
            return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
        }
    } else if (response->payload_size != 0) {
        return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
    }
    return VC_MEMORY_READ_STATUS_OK;
}

void vc_memory_read_request_init(
    vc_memory_read_request *request,
    vc_memory_read_operation operation)
{
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
        request->version = VC_MEMORY_READ_ABI_VERSION;
        request->struct_size = VC_MEMORY_READ_REQUEST_WIRE_SIZE;
        request->operation = (uint16_t)operation;
        request->caller_role = VC_LAUNCH_CALLER_GAME_PLUGIN;
        request->capabilities =
            VC_MEMORY_READ_CAPABILITY_READ_TARGET;
    }
}

vc_memory_read_status vc_memory_read_request_encode(
    const vc_memory_read_request *request,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    vc_memory_read_status status;

    if (buffer == NULL || encoded_size == NULL) {
        return VC_MEMORY_READ_STATUS_INVALID_ARGUMENT;
    }
    *encoded_size = 0;
    if (buffer_size != VC_MEMORY_READ_REQUEST_WIRE_SIZE) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    status = vc_memory_validate_request(request);
    if (status != VC_MEMORY_READ_STATUS_OK) {
        return status;
    }

    memset(buffer, 0, buffer_size);
    vc_memory_write_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_VERSION,
        request->version);
    vc_memory_write_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_SIZE,
        request->struct_size);
    vc_memory_write_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_OPERATION,
        request->operation);
    vc_memory_write_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_ROLE,
        request->caller_role);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_CAPABILITIES,
        request->capabilities);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_RESERVED0,
        request->reserved0);
    vc_memory_write_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_REQUEST_ID,
        request->request_id);
    vc_memory_write_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_SESSION_ID,
        request->session_id);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_TARGET_PID,
        request->target_process_id);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_MODULE_ID,
        request->module_id);
    vc_memory_write_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_TARGET_GENERATION,
        request->target_generation);
    vc_memory_write_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_ATTESTATION_REVISION,
        request->attestation_revision);
    vc_memory_write_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_MODULE_GENERATION,
        request->module_load_generation);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_SEGMENT_INDEX,
        request->segment_index);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_SEGMENT_OFFSET,
        request->segment_offset);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_LENGTH,
        request->length);
    vc_memory_write_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_RESERVED1,
        request->reserved1);
    *encoded_size = VC_MEMORY_READ_REQUEST_WIRE_SIZE;
    return VC_MEMORY_READ_STATUS_OK;
}

vc_memory_read_status vc_memory_read_request_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    vc_memory_read_request *request)
{
    vc_memory_read_request decoded;
    vc_memory_read_status status;

    if (buffer == NULL || request == NULL) {
        return VC_MEMORY_READ_STATUS_INVALID_ARGUMENT;
    }
    if (buffer_size != VC_MEMORY_READ_REQUEST_WIRE_SIZE) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.version = vc_memory_read_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_VERSION);
    decoded.struct_size = vc_memory_read_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_SIZE);
    decoded.operation = vc_memory_read_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_OPERATION);
    decoded.caller_role = vc_memory_read_u16(
        buffer + VC_MEMORY_REQUEST_OFFSET_ROLE);
    decoded.capabilities = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_CAPABILITIES);
    decoded.reserved0 = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_RESERVED0);
    decoded.request_id = vc_memory_read_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_REQUEST_ID);
    decoded.session_id = vc_memory_read_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_SESSION_ID);
    decoded.target_process_id = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_TARGET_PID);
    decoded.module_id = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_MODULE_ID);
    decoded.target_generation = vc_memory_read_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_TARGET_GENERATION);
    decoded.attestation_revision = vc_memory_read_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_ATTESTATION_REVISION);
    decoded.module_load_generation = vc_memory_read_u64(
        buffer + VC_MEMORY_REQUEST_OFFSET_MODULE_GENERATION);
    decoded.segment_index = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_SEGMENT_INDEX);
    decoded.segment_offset = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_SEGMENT_OFFSET);
    decoded.length = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_LENGTH);
    decoded.reserved1 = vc_memory_read_u32(
        buffer + VC_MEMORY_REQUEST_OFFSET_RESERVED1);

    status = vc_memory_validate_request(&decoded);
    if (status != VC_MEMORY_READ_STATUS_OK) {
        return status;
    }
    *request = decoded;
    return VC_MEMORY_READ_STATUS_OK;
}

vc_memory_read_status vc_memory_read_response_encode(
    const vc_memory_read_response *response,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    vc_memory_read_status status;
    size_t total_size;

    if (buffer == NULL || encoded_size == NULL) {
        return VC_MEMORY_READ_STATUS_INVALID_ARGUMENT;
    }
    *encoded_size = 0;
    status = vc_memory_validate_response(response);
    if (status != VC_MEMORY_READ_STATUS_OK) {
        return status;
    }
    total_size = response->total_size;
    if (buffer_size != total_size) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }

    memset(buffer, 0, total_size);
    vc_memory_write_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_VERSION,
        response->version);
    vc_memory_write_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_HEADER_SIZE,
        response->header_size);
    vc_memory_write_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_TOTAL_SIZE,
        response->total_size);
    vc_memory_write_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_OPERATION,
        response->operation);
    vc_memory_write_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_STATUS,
        (uint32_t)response->status);
    vc_memory_write_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_STATE,
        response->state);
    vc_memory_write_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_CAPABILITIES,
        response->capabilities);
    vc_memory_write_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_PAYLOAD_SIZE,
        response->payload_size);
    vc_memory_write_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_REQUEST_ID,
        response->request_id);
    vc_memory_write_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_SESSION_ID,
        response->session_id);
    vc_memory_write_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_REMAINING_BYTES,
        response->remaining_bytes);
    vc_memory_write_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_REMAINING_OPERATIONS,
        response->remaining_operations);
    vc_memory_write_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_RESERVED0,
        response->reserved0);
    vc_memory_write_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_RESERVED1,
        response->reserved1);
    if (response->payload_size != 0) {
        memcpy(buffer + VC_MEMORY_RESPONSE_OFFSET_PAYLOAD,
               response->payload, response->payload_size);
    }
    *encoded_size = total_size;
    return VC_MEMORY_READ_STATUS_OK;
}

vc_memory_read_status vc_memory_read_response_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    vc_memory_read_response *response)
{
    vc_memory_read_response decoded;
    vc_memory_read_status status;
    uint32_t wire_status;

    if (buffer == NULL || response == NULL) {
        return VC_MEMORY_READ_STATUS_INVALID_ARGUMENT;
    }
    if (buffer_size < VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE ||
        buffer_size > VC_MEMORY_READ_RESPONSE_WIRE_MAX) {
        return VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.version = vc_memory_read_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_VERSION);
    decoded.header_size = vc_memory_read_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_HEADER_SIZE);
    decoded.total_size = vc_memory_read_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_TOTAL_SIZE);
    decoded.operation = vc_memory_read_u16(
        buffer + VC_MEMORY_RESPONSE_OFFSET_OPERATION);
    wire_status = vc_memory_read_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_STATUS);
    if (!vc_memory_decode_status(wire_status, &decoded.status)) {
        return VC_MEMORY_READ_STATUS_MALFORMED_FIELD;
    }
    decoded.state = vc_memory_read_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_STATE);
    decoded.capabilities = vc_memory_read_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_CAPABILITIES);
    decoded.payload_size = vc_memory_read_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_PAYLOAD_SIZE);
    decoded.request_id = vc_memory_read_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_REQUEST_ID);
    decoded.session_id = vc_memory_read_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_SESSION_ID);
    decoded.remaining_bytes = vc_memory_read_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_REMAINING_BYTES);
    decoded.remaining_operations = vc_memory_read_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_REMAINING_OPERATIONS);
    decoded.reserved0 = vc_memory_read_u32(
        buffer + VC_MEMORY_RESPONSE_OFFSET_RESERVED0);
    decoded.reserved1 = vc_memory_read_u64(
        buffer + VC_MEMORY_RESPONSE_OFFSET_RESERVED1);

    status = vc_memory_validate_response(&decoded);
    if (status != VC_MEMORY_READ_STATUS_OK ||
        decoded.total_size != buffer_size) {
        return status != VC_MEMORY_READ_STATUS_OK
                   ? status
                   : VC_MEMORY_READ_STATUS_INVALID_SIZE;
    }
    if (decoded.payload_size != 0) {
        memcpy(decoded.payload,
               buffer + VC_MEMORY_RESPONSE_OFFSET_PAYLOAD,
               decoded.payload_size);
    }
    *response = decoded;
    return VC_MEMORY_READ_STATUS_OK;
}
