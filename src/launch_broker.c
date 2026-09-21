#include "vitacheat/launch_broker.h"

#include <limits.h>
#include <string.h>

enum {
    VC_REQUEST_OFFSET_VERSION = 0,
    VC_REQUEST_OFFSET_SIZE = 2,
    VC_REQUEST_OFFSET_OPERATION = 4,
    VC_REQUEST_OFFSET_ROLE = 6,
    VC_REQUEST_OFFSET_CAPABILITIES = 8,
    VC_REQUEST_OFFSET_PROCESS_ID = 12,
    VC_REQUEST_OFFSET_GENERATION = 16,
    VC_REQUEST_OFFSET_REQUEST_ID = 24,
    VC_REQUEST_OFFSET_NOW_MS = 32,
    VC_REQUEST_OFFSET_TTL_MS = 40,
    VC_REQUEST_OFFSET_PRESENTATION_READY = 48,
    VC_REQUEST_OFFSET_RESERVED0 = 52,
    VC_REQUEST_OFFSET_RESERVED1 = 56
};

enum {
    VC_RESPONSE_OFFSET_VERSION = 0,
    VC_RESPONSE_OFFSET_SIZE = 2,
    VC_RESPONSE_OFFSET_OPERATION = 4,
    VC_RESPONSE_OFFSET_RESERVED0 = 6,
    VC_RESPONSE_OFFSET_STATUS = 8,
    VC_RESPONSE_OFFSET_STATE = 12,
    VC_RESPONSE_OFFSET_CAPABILITIES = 16,
    VC_RESPONSE_OFFSET_PROCESS_ID = 20,
    VC_RESPONSE_OFFSET_GENERATION = 24,
    VC_RESPONSE_OFFSET_REQUEST_ID = 32,
    VC_RESPONSE_OFFSET_CREATED_MS = 40,
    VC_RESPONSE_OFFSET_DEADLINE_MS = 48,
    VC_RESPONSE_OFFSET_OBSERVED_MS = 56,
    VC_RESPONSE_OFFSET_RESERVED1 = 64
};

static uint16_t vc_read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)((uint16_t)buffer[0] |
                      ((uint16_t)buffer[1] << 8));
}

static uint32_t vc_read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8) |
           ((uint32_t)buffer[2] << 16) |
           ((uint32_t)buffer[3] << 24);
}

static uint64_t vc_read_u64_le(const uint8_t *buffer)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)buffer[index] << (index * 8);
    }
    return value;
}

static int32_t vc_read_i32_le(const uint8_t *buffer)
{
    uint32_t value = vc_read_u32_le(buffer);

    if (value <= (uint32_t)INT32_MAX) {
        return (int32_t)value;
    }
    if (value >= UINT32_MAX - UINT32_C(11)) {
        return -(int32_t)(UINT32_MAX - value) - 1;
    }
    return INT32_MIN;
}

static void vc_write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static void vc_write_u32_le(uint8_t *buffer, uint32_t value)
{
    unsigned int index;

    for (index = 0; index < 4; ++index) {
        buffer[index] = (uint8_t)(value >> (index * 8));
    }
}

static void vc_write_u64_le(uint8_t *buffer, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        buffer[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool vc_launch_operation_valid(uint16_t operation, bool allow_none)
{
    return (allow_none && operation == VC_LAUNCH_OPERATION_NONE) ||
           operation == VC_LAUNCH_OPERATION_SUBMIT ||
           operation == VC_LAUNCH_OPERATION_CLAIM ||
           operation == VC_LAUNCH_OPERATION_CANCEL ||
           operation == VC_LAUNCH_OPERATION_STATUS;
}

static bool vc_launch_role_valid(uint16_t role)
{
    return role == VC_LAUNCH_CALLER_SCE_SHELL ||
           role == VC_LAUNCH_CALLER_GAME_PLUGIN;
}

static bool vc_launch_state_valid(uint32_t state)
{
    return state <= VC_LAUNCH_STATE_CLOCK_ROLLBACK;
}

static bool vc_launch_status_valid(int32_t status)
{
    return (status >= VC_LAUNCH_STATUS_OK &&
            status <= VC_LAUNCH_STATUS_CLOCK_ROLLBACK) ||
           (status <= VC_LAUNCH_STATUS_INVALID_ARGUMENT &&
            status >= VC_LAUNCH_STATUS_MALFORMED_FIELD);
}

static vc_launch_status vc_launch_validate_operation_fields(
    const vc_launch_request *request)
{
    uint16_t required_role;
    uint32_t required_capability;

    if (!vc_launch_operation_valid(request->operation, false)) {
        return VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
    }
    if (!vc_launch_role_valid(request->caller_role)) {
        return VC_LAUNCH_STATUS_INVALID_ROLE;
    }
    if ((request->capabilities & ~VC_LAUNCH_CAPABILITY_ALL) != 0) {
        return VC_LAUNCH_STATUS_INVALID_CAPABILITY;
    }

    switch ((vc_launch_operation)request->operation) {
    case VC_LAUNCH_OPERATION_SUBMIT:
        required_role = VC_LAUNCH_CALLER_SCE_SHELL;
        required_capability = VC_LAUNCH_CAPABILITY_LAUNCH;
        if (request->request_id != 0 ||
            request->ttl_ms == 0 ||
            request->ttl_ms > VC_LAUNCH_MAX_TTL_MS ||
            request->presentation_ready != 0) {
            return VC_LAUNCH_STATUS_MALFORMED_FIELD;
        }
        break;
    case VC_LAUNCH_OPERATION_CLAIM:
        required_role = VC_LAUNCH_CALLER_GAME_PLUGIN;
        required_capability = VC_LAUNCH_CAPABILITY_CLAIM;
        if (request->request_id == 0 ||
            request->ttl_ms != 0 ||
            request->presentation_ready > 1) {
            return VC_LAUNCH_STATUS_MALFORMED_FIELD;
        }
        break;
    case VC_LAUNCH_OPERATION_CANCEL:
        required_role = VC_LAUNCH_CALLER_GAME_PLUGIN;
        required_capability = VC_LAUNCH_CAPABILITY_CANCEL;
        if (request->request_id == 0 ||
            request->ttl_ms != 0 ||
            request->presentation_ready != 0) {
            return VC_LAUNCH_STATUS_MALFORMED_FIELD;
        }
        break;
    case VC_LAUNCH_OPERATION_STATUS:
        required_role = request->caller_role;
        required_capability = VC_LAUNCH_CAPABILITY_STATUS;
        if (request->request_id == 0 ||
            request->ttl_ms != 0 ||
            request->presentation_ready != 0) {
            return VC_LAUNCH_STATUS_MALFORMED_FIELD;
        }
        break;
    default:
        return VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
    }

    if (request->caller_role != required_role) {
        return VC_LAUNCH_STATUS_INVALID_ROLE;
    }
    if (request->capabilities != required_capability) {
        return VC_LAUNCH_STATUS_INVALID_CAPABILITY;
    }
    return VC_LAUNCH_STATUS_OK;
}

static vc_launch_status vc_launch_validate_request(
    const vc_launch_request *request)
{
    vc_launch_status status;

    if (request == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    if (request->version != VC_LAUNCH_ABI_VERSION) {
        return VC_LAUNCH_STATUS_UNSUPPORTED_VERSION;
    }
    if (request->struct_size != VC_LAUNCH_REQUEST_WIRE_SIZE) {
        return VC_LAUNCH_STATUS_INVALID_SIZE;
    }
    if (request->reserved0 != 0 || request->reserved1 != 0) {
        return VC_LAUNCH_STATUS_INVALID_RESERVED;
    }
    if (request->target_process_id == 0 ||
        request->target_generation == 0) {
        return VC_LAUNCH_STATUS_INVALID_IDENTITY;
    }

    status = vc_launch_validate_operation_fields(request);
    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }
    if (request->operation == VC_LAUNCH_OPERATION_SUBMIT &&
        request->now_ms > UINT64_MAX - request->ttl_ms) {
        return VC_LAUNCH_STATUS_MALFORMED_FIELD;
    }
    return VC_LAUNCH_STATUS_OK;
}

static vc_launch_status vc_launch_validate_response(
    const vc_launch_response *response)
{
    uint32_t allowed_capability = 0;

    if (response == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    if (response->version != VC_LAUNCH_ABI_VERSION) {
        return VC_LAUNCH_STATUS_UNSUPPORTED_VERSION;
    }
    if (response->struct_size != VC_LAUNCH_RESPONSE_WIRE_SIZE) {
        return VC_LAUNCH_STATUS_INVALID_SIZE;
    }
    if (!vc_launch_operation_valid(response->operation, true)) {
        return VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
    }
    if (!vc_launch_status_valid(response->status) ||
        !vc_launch_state_valid(response->launch_state)) {
        return VC_LAUNCH_STATUS_MALFORMED_FIELD;
    }
    if ((response->capabilities & ~VC_LAUNCH_CAPABILITY_ALL) != 0) {
        return VC_LAUNCH_STATUS_INVALID_CAPABILITY;
    }
    switch ((vc_launch_operation)response->operation) {
    case VC_LAUNCH_OPERATION_NONE:
        break;
    case VC_LAUNCH_OPERATION_SUBMIT:
        allowed_capability = VC_LAUNCH_CAPABILITY_LAUNCH;
        break;
    case VC_LAUNCH_OPERATION_CLAIM:
        allowed_capability = VC_LAUNCH_CAPABILITY_CLAIM;
        break;
    case VC_LAUNCH_OPERATION_CANCEL:
        allowed_capability = VC_LAUNCH_CAPABILITY_CANCEL;
        break;
    case VC_LAUNCH_OPERATION_STATUS:
        allowed_capability = VC_LAUNCH_CAPABILITY_STATUS;
        break;
    default:
        return VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
    }
    if (response->capabilities != 0 &&
        response->capabilities != allowed_capability) {
        return VC_LAUNCH_STATUS_INVALID_CAPABILITY;
    }
    if (response->reserved0 != 0 || response->reserved1 != 0) {
        return VC_LAUNCH_STATUS_INVALID_RESERVED;
    }
    if (response->launch_state == VC_LAUNCH_STATE_ABSENT) {
        if (response->target_process_id != 0 ||
            response->target_generation != 0 ||
            response->request_id != 0 ||
            response->created_ms != 0 ||
            response->deadline_ms != 0) {
            return VC_LAUNCH_STATUS_MALFORMED_FIELD;
        }
    } else if (response->target_process_id == 0 ||
               response->target_generation == 0 ||
               response->request_id == 0 ||
               response->deadline_ms <= response->created_ms ||
               response->deadline_ms - response->created_ms >
                   VC_LAUNCH_MAX_TTL_MS) {
        return VC_LAUNCH_STATUS_MALFORMED_FIELD;
    }
    return VC_LAUNCH_STATUS_OK;
}

void vc_launch_request_init(vc_launch_request *request,
                            vc_launch_operation operation,
                            vc_launch_caller_role caller_role)
{
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
        request->version = VC_LAUNCH_ABI_VERSION;
        request->struct_size = VC_LAUNCH_REQUEST_WIRE_SIZE;
        request->operation = (uint16_t)operation;
        request->caller_role = (uint16_t)caller_role;
    }
}

vc_launch_status vc_launch_request_encode(const vc_launch_request *request,
                                          uint8_t *buffer,
                                          size_t buffer_size,
                                          size_t *encoded_size)
{
    vc_launch_status status;

    if (buffer == NULL || encoded_size == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    *encoded_size = 0;
    if (buffer_size != VC_LAUNCH_REQUEST_WIRE_SIZE) {
        return VC_LAUNCH_STATUS_INVALID_SIZE;
    }

    status = vc_launch_validate_request(request);
    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }

    memset(buffer, 0, buffer_size);
    vc_write_u16_le(buffer + VC_REQUEST_OFFSET_VERSION, request->version);
    vc_write_u16_le(buffer + VC_REQUEST_OFFSET_SIZE, request->struct_size);
    vc_write_u16_le(buffer + VC_REQUEST_OFFSET_OPERATION, request->operation);
    vc_write_u16_le(buffer + VC_REQUEST_OFFSET_ROLE, request->caller_role);
    vc_write_u32_le(buffer + VC_REQUEST_OFFSET_CAPABILITIES,
                    request->capabilities);
    vc_write_u32_le(buffer + VC_REQUEST_OFFSET_PROCESS_ID,
                    request->target_process_id);
    vc_write_u64_le(buffer + VC_REQUEST_OFFSET_GENERATION,
                    request->target_generation);
    vc_write_u64_le(buffer + VC_REQUEST_OFFSET_REQUEST_ID,
                    request->request_id);
    vc_write_u64_le(buffer + VC_REQUEST_OFFSET_NOW_MS, request->now_ms);
    vc_write_u64_le(buffer + VC_REQUEST_OFFSET_TTL_MS, request->ttl_ms);
    vc_write_u32_le(buffer + VC_REQUEST_OFFSET_PRESENTATION_READY,
                    request->presentation_ready);
    vc_write_u32_le(buffer + VC_REQUEST_OFFSET_RESERVED0, request->reserved0);
    vc_write_u64_le(buffer + VC_REQUEST_OFFSET_RESERVED1, request->reserved1);
    *encoded_size = VC_LAUNCH_REQUEST_WIRE_SIZE;
    return VC_LAUNCH_STATUS_OK;
}

vc_launch_status vc_launch_request_decode(const uint8_t *buffer,
                                          size_t buffer_size,
                                          vc_launch_request *request)
{
    vc_launch_request decoded;

    if (buffer == NULL || request == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    if (buffer_size != VC_LAUNCH_REQUEST_WIRE_SIZE) {
        return VC_LAUNCH_STATUS_INVALID_SIZE;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.version = vc_read_u16_le(buffer + VC_REQUEST_OFFSET_VERSION);
    decoded.struct_size = vc_read_u16_le(buffer + VC_REQUEST_OFFSET_SIZE);
    decoded.operation = vc_read_u16_le(buffer + VC_REQUEST_OFFSET_OPERATION);
    decoded.caller_role = vc_read_u16_le(buffer + VC_REQUEST_OFFSET_ROLE);
    decoded.capabilities =
        vc_read_u32_le(buffer + VC_REQUEST_OFFSET_CAPABILITIES);
    decoded.target_process_id =
        vc_read_u32_le(buffer + VC_REQUEST_OFFSET_PROCESS_ID);
    decoded.target_generation =
        vc_read_u64_le(buffer + VC_REQUEST_OFFSET_GENERATION);
    decoded.request_id =
        vc_read_u64_le(buffer + VC_REQUEST_OFFSET_REQUEST_ID);
    decoded.now_ms = vc_read_u64_le(buffer + VC_REQUEST_OFFSET_NOW_MS);
    decoded.ttl_ms = vc_read_u64_le(buffer + VC_REQUEST_OFFSET_TTL_MS);
    decoded.presentation_ready =
        vc_read_u32_le(buffer + VC_REQUEST_OFFSET_PRESENTATION_READY);
    decoded.reserved0 =
        vc_read_u32_le(buffer + VC_REQUEST_OFFSET_RESERVED0);
    decoded.reserved1 =
        vc_read_u64_le(buffer + VC_REQUEST_OFFSET_RESERVED1);

    {
        vc_launch_status status = vc_launch_validate_request(&decoded);

        if (status != VC_LAUNCH_STATUS_OK) {
            return status;
        }
    }
    *request = decoded;
    return VC_LAUNCH_STATUS_OK;
}

vc_launch_status vc_launch_response_encode(const vc_launch_response *response,
                                           uint8_t *buffer,
                                           size_t buffer_size,
                                           size_t *encoded_size)
{
    vc_launch_status status;

    if (buffer == NULL || encoded_size == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    *encoded_size = 0;
    if (buffer_size != VC_LAUNCH_RESPONSE_WIRE_SIZE) {
        return VC_LAUNCH_STATUS_INVALID_SIZE;
    }

    status = vc_launch_validate_response(response);
    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }

    memset(buffer, 0, buffer_size);
    vc_write_u16_le(buffer + VC_RESPONSE_OFFSET_VERSION, response->version);
    vc_write_u16_le(buffer + VC_RESPONSE_OFFSET_SIZE, response->struct_size);
    vc_write_u16_le(buffer + VC_RESPONSE_OFFSET_OPERATION,
                    response->operation);
    vc_write_u16_le(buffer + VC_RESPONSE_OFFSET_RESERVED0,
                    response->reserved0);
    vc_write_u32_le(buffer + VC_RESPONSE_OFFSET_STATUS,
                    (uint32_t)response->status);
    vc_write_u32_le(buffer + VC_RESPONSE_OFFSET_STATE,
                    response->launch_state);
    vc_write_u32_le(buffer + VC_RESPONSE_OFFSET_CAPABILITIES,
                    response->capabilities);
    vc_write_u32_le(buffer + VC_RESPONSE_OFFSET_PROCESS_ID,
                    response->target_process_id);
    vc_write_u64_le(buffer + VC_RESPONSE_OFFSET_GENERATION,
                    response->target_generation);
    vc_write_u64_le(buffer + VC_RESPONSE_OFFSET_REQUEST_ID,
                    response->request_id);
    vc_write_u64_le(buffer + VC_RESPONSE_OFFSET_CREATED_MS,
                    response->created_ms);
    vc_write_u64_le(buffer + VC_RESPONSE_OFFSET_DEADLINE_MS,
                    response->deadline_ms);
    vc_write_u64_le(buffer + VC_RESPONSE_OFFSET_OBSERVED_MS,
                    response->observed_ms);
    vc_write_u64_le(buffer + VC_RESPONSE_OFFSET_RESERVED1,
                    response->reserved1);
    *encoded_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    return VC_LAUNCH_STATUS_OK;
}

vc_launch_status vc_launch_response_decode(const uint8_t *buffer,
                                           size_t buffer_size,
                                           vc_launch_response *response)
{
    vc_launch_response decoded;

    if (buffer == NULL || response == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    if (buffer_size != VC_LAUNCH_RESPONSE_WIRE_SIZE) {
        return VC_LAUNCH_STATUS_INVALID_SIZE;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.version = vc_read_u16_le(buffer + VC_RESPONSE_OFFSET_VERSION);
    decoded.struct_size = vc_read_u16_le(buffer + VC_RESPONSE_OFFSET_SIZE);
    decoded.operation =
        vc_read_u16_le(buffer + VC_RESPONSE_OFFSET_OPERATION);
    decoded.reserved0 =
        vc_read_u16_le(buffer + VC_RESPONSE_OFFSET_RESERVED0);
    decoded.status =
        vc_read_i32_le(buffer + VC_RESPONSE_OFFSET_STATUS);
    decoded.launch_state =
        vc_read_u32_le(buffer + VC_RESPONSE_OFFSET_STATE);
    decoded.capabilities =
        vc_read_u32_le(buffer + VC_RESPONSE_OFFSET_CAPABILITIES);
    decoded.target_process_id =
        vc_read_u32_le(buffer + VC_RESPONSE_OFFSET_PROCESS_ID);
    decoded.target_generation =
        vc_read_u64_le(buffer + VC_RESPONSE_OFFSET_GENERATION);
    decoded.request_id =
        vc_read_u64_le(buffer + VC_RESPONSE_OFFSET_REQUEST_ID);
    decoded.created_ms =
        vc_read_u64_le(buffer + VC_RESPONSE_OFFSET_CREATED_MS);
    decoded.deadline_ms =
        vc_read_u64_le(buffer + VC_RESPONSE_OFFSET_DEADLINE_MS);
    decoded.observed_ms =
        vc_read_u64_le(buffer + VC_RESPONSE_OFFSET_OBSERVED_MS);
    decoded.reserved1 =
        vc_read_u64_le(buffer + VC_RESPONSE_OFFSET_RESERVED1);

    {
        vc_launch_status status = vc_launch_validate_response(&decoded);

        if (status != VC_LAUNCH_STATUS_OK) {
            return status;
        }
    }
    *response = decoded;
    return VC_LAUNCH_STATUS_OK;
}

static bool vc_launch_broker_valid(const vc_launch_broker *broker)
{
    return broker != NULL && broker->initialized;
}

static vc_launch_status vc_launch_record_status(const vc_launch_record *record)
{
    switch ((vc_launch_state)record->state) {
    case VC_LAUNCH_STATE_ABSENT:
        return VC_LAUNCH_STATUS_ABSENT;
    case VC_LAUNCH_STATE_PENDING:
        return VC_LAUNCH_STATUS_PENDING;
    case VC_LAUNCH_STATE_CLAIMED:
        return VC_LAUNCH_STATUS_CLAIMED;
    case VC_LAUNCH_STATE_EXPIRED:
        return VC_LAUNCH_STATUS_EXPIRED;
    case VC_LAUNCH_STATE_CANCELLED:
        return VC_LAUNCH_STATUS_CANCELLED;
    case VC_LAUNCH_STATE_STALE_TARGET:
        return VC_LAUNCH_STATUS_STALE_TARGET;
    case VC_LAUNCH_STATE_CLOCK_ROLLBACK:
        return VC_LAUNCH_STATUS_CLOCK_ROLLBACK;
    default:
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
}

static vc_launch_status vc_launch_advance_time(vc_launch_broker *broker,
                                               uint64_t now_ms,
                                               bool commit)
{
    if (!broker->has_time) {
        if (commit) {
            broker->has_time = true;
            broker->last_now_ms = now_ms;
        }
    } else if (now_ms < broker->last_now_ms) {
        if (broker->record.state == VC_LAUNCH_STATE_PENDING) {
            broker->record.state = VC_LAUNCH_STATE_CLOCK_ROLLBACK;
        }
        return VC_LAUNCH_STATUS_CLOCK_ROLLBACK;
    } else if (commit) {
        broker->last_now_ms = now_ms;
    }

    if (broker->record.state == VC_LAUNCH_STATE_PENDING &&
        now_ms >= broker->record.deadline_ms) {
        broker->record.state = VC_LAUNCH_STATE_EXPIRED;
        broker->has_time = true;
        broker->last_now_ms = now_ms;
        return VC_LAUNCH_STATUS_EXPIRED;
    }
    return VC_LAUNCH_STATUS_OK;
}

static void vc_launch_response_init(vc_launch_response *response,
                                    uint16_t operation,
                                    uint64_t observed_ms,
                                    vc_launch_status status)
{
    memset(response, 0, sizeof(*response));
    response->version = VC_LAUNCH_ABI_VERSION;
    response->struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response->operation = operation;
    response->status = status;
    response->observed_ms = observed_ms;
}

static void vc_launch_response_record(vc_launch_response *response,
                                      const vc_launch_broker *broker,
                                      uint32_t capability,
                                      vc_launch_status status)
{
    response->status = status;
    response->launch_state = broker->record.state;
    response->capabilities = capability;
    response->target_process_id = broker->record.target_process_id;
    response->target_generation = broker->record.target_generation;
    response->request_id = broker->record.request_id;
    response->created_ms = broker->record.created_ms;
    response->deadline_ms = broker->record.deadline_ms;
}

static vc_launch_status vc_launch_prepare_operation(
    vc_launch_broker *broker,
    const vc_launch_request *request,
    vc_launch_response *response,
    vc_launch_operation expected_operation)
{
    vc_launch_status status;

    if (response == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    vc_launch_response_init(response,
                            request != NULL ? request->operation
                                            : VC_LAUNCH_OPERATION_NONE,
                            request != NULL ? request->now_ms : 0,
                            VC_LAUNCH_STATUS_INVALID_ARGUMENT);
    if (!vc_launch_broker_valid(broker)) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }

    status = vc_launch_validate_request(request);
    if (status != VC_LAUNCH_STATUS_OK) {
        response->status = status;
        return status;
    }
    if (request->operation != (uint16_t)expected_operation) {
        response->status = VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
        return VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
    }

    status = vc_launch_advance_time(broker, request->now_ms, false);
    if (status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        response->status = status;
        if (broker->record.state != VC_LAUNCH_STATE_ABSENT) {
            vc_launch_response_record(response, broker, 0, status);
        }
        return status;
    }
    return VC_LAUNCH_STATUS_OK;
}

static vc_launch_status vc_launch_match_record(
    const vc_launch_broker *broker,
    const vc_launch_request *request)
{
    if (broker->record.state == VC_LAUNCH_STATE_ABSENT) {
        return VC_LAUNCH_STATUS_ABSENT;
    }
    if (broker->record.request_id != request->request_id) {
        return VC_LAUNCH_STATUS_WRONG_REQUEST_ID;
    }
    if (broker->record.target_process_id != request->target_process_id ||
        broker->record.target_generation != request->target_generation) {
        return VC_LAUNCH_STATUS_WRONG_TARGET;
    }
    return vc_launch_record_status(&broker->record);
}

vc_launch_status vc_launch_broker_init(vc_launch_broker *broker,
                                       uint64_t first_request_id)
{
    if (broker == NULL || first_request_id == 0) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }

    memset(broker, 0, sizeof(*broker));
    broker->next_request_id = first_request_id;
    broker->initialized = true;
    return VC_LAUNCH_STATUS_OK;
}

vc_launch_status vc_launch_broker_foreground_changed(
    vc_launch_broker *broker,
    uint32_t target_process_id,
    uint64_t target_generation,
    uint64_t now_ms)
{
    vc_launch_status status;

    if (!vc_launch_broker_valid(broker) ||
        target_process_id == 0 ||
        target_generation == 0) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    status = vc_launch_advance_time(broker, now_ms, true);
    if (status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        return status;
    }
    if (broker->foreground_process_id == target_process_id &&
        broker->foreground_generation == target_generation) {
        return status == VC_LAUNCH_STATUS_EXPIRED
                   ? VC_LAUNCH_STATUS_EXPIRED
                   : VC_LAUNCH_STATUS_OK;
    }

    if (broker->record.state == VC_LAUNCH_STATE_PENDING) {
        broker->record.state = VC_LAUNCH_STATE_STALE_TARGET;
        status = VC_LAUNCH_STATUS_STALE_TARGET;
    } else if (status != VC_LAUNCH_STATUS_EXPIRED) {
        status = broker->foreground_process_id == 0 &&
                         broker->foreground_generation == 0
                     ? VC_LAUNCH_STATUS_OK
                     : VC_LAUNCH_STATUS_STALE_TARGET;
    }
    broker->foreground_process_id = target_process_id;
    broker->foreground_generation = target_generation;
    return status;
}

vc_launch_status vc_launch_broker_process_exit(vc_launch_broker *broker,
                                               uint32_t target_process_id,
                                               uint64_t target_generation,
                                               uint64_t now_ms)
{
    vc_launch_status status;

    if (!vc_launch_broker_valid(broker) ||
        target_process_id == 0 ||
        target_generation == 0) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    status = vc_launch_advance_time(broker, now_ms, false);
    if (status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        return status;
    }
    if (broker->foreground_process_id != target_process_id ||
        broker->foreground_generation != target_generation) {
        if (broker->record.target_process_id == target_process_id &&
            broker->record.target_generation == target_generation) {
            return vc_launch_record_status(&broker->record);
        }
        return VC_LAUNCH_STATUS_WRONG_TARGET;
    }

    (void)vc_launch_advance_time(broker, now_ms, true);
    status = VC_LAUNCH_STATUS_OK;
    if (broker->record.state == VC_LAUNCH_STATE_PENDING &&
        broker->record.target_process_id == target_process_id &&
        broker->record.target_generation == target_generation) {
        broker->record.state = VC_LAUNCH_STATE_STALE_TARGET;
        status = VC_LAUNCH_STATUS_STALE_TARGET;
    } else if (broker->record.target_process_id == target_process_id &&
               broker->record.target_generation == target_generation) {
        status = vc_launch_record_status(&broker->record);
    }
    broker->foreground_process_id = 0;
    broker->foreground_generation = 0;
    return status;
}

vc_launch_status vc_launch_broker_plugin_unload(vc_launch_broker *broker,
                                                uint32_t target_process_id,
                                                uint64_t target_generation,
                                                uint64_t now_ms)
{
    vc_launch_status status;

    if (!vc_launch_broker_valid(broker) ||
        target_process_id == 0 ||
        target_generation == 0) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    status = vc_launch_advance_time(broker, now_ms, false);
    if (status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        return status;
    }
    if (broker->record.state == VC_LAUNCH_STATE_ABSENT) {
        return VC_LAUNCH_STATUS_ABSENT;
    }
    if (broker->record.target_process_id != target_process_id ||
        broker->record.target_generation != target_generation) {
        return VC_LAUNCH_STATUS_WRONG_TARGET;
    }
    (void)vc_launch_advance_time(broker, now_ms, true);
    if (broker->record.state == VC_LAUNCH_STATE_PENDING) {
        broker->record.state = VC_LAUNCH_STATE_CANCELLED;
    }
    return vc_launch_record_status(&broker->record);
}

vc_launch_status vc_launch_broker_service_reset(vc_launch_broker *broker,
                                                uint64_t now_ms)
{
    vc_launch_status status;

    if (!vc_launch_broker_valid(broker)) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    status = vc_launch_advance_time(broker, now_ms, true);
    if (status != VC_LAUNCH_STATUS_CLOCK_ROLLBACK &&
        broker->record.state == VC_LAUNCH_STATE_PENDING) {
        broker->record.state = VC_LAUNCH_STATE_CANCELLED;
        status = VC_LAUNCH_STATUS_CANCELLED;
    }
    broker->foreground_process_id = 0;
    broker->foreground_generation = 0;
    return status;
}

vc_launch_status vc_launch_broker_tick(vc_launch_broker *broker,
                                       uint64_t now_ms)
{
    vc_launch_status status;

    if (!vc_launch_broker_valid(broker)) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    status = vc_launch_advance_time(broker, now_ms, true);
    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }
    return vc_launch_record_status(&broker->record);
}

vc_launch_status vc_launch_broker_submit(vc_launch_broker *broker,
                                         const vc_launch_request *request,
                                         vc_launch_response *response)
{
    vc_launch_status status = vc_launch_prepare_operation(
        broker, request, response, VC_LAUNCH_OPERATION_SUBMIT);
    uint64_t request_id;

    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }
    if (broker->foreground_process_id != request->target_process_id ||
        broker->foreground_generation != request->target_generation) {
        response->status = VC_LAUNCH_STATUS_STALE_TARGET;
        return VC_LAUNCH_STATUS_STALE_TARGET;
    }
    if (broker->record.state == VC_LAUNCH_STATE_PENDING) {
        if (broker->record.target_process_id != request->target_process_id ||
            broker->record.target_generation != request->target_generation) {
            response->status = VC_LAUNCH_STATUS_STALE_TARGET;
            return VC_LAUNCH_STATUS_STALE_TARGET;
        }
        vc_launch_response_record(response, broker,
                                  VC_LAUNCH_CAPABILITY_LAUNCH,
                                  VC_LAUNCH_STATUS_PENDING);
        (void)vc_launch_advance_time(broker, request->now_ms, true);
        return VC_LAUNCH_STATUS_PENDING;
    }
    if (broker->id_exhausted || broker->next_request_id == 0) {
        response->status = VC_LAUNCH_STATUS_ID_EXHAUSTED;
        return VC_LAUNCH_STATUS_ID_EXHAUSTED;
    }

    request_id = broker->next_request_id;
    if (request_id == UINT64_MAX) {
        broker->id_exhausted = true;
    } else {
        broker->next_request_id = request_id + 1;
    }

    memset(&broker->record, 0, sizeof(broker->record));
    broker->record.request_id = request_id;
    broker->record.target_process_id = request->target_process_id;
    broker->record.target_generation = request->target_generation;
    broker->record.created_ms = request->now_ms;
    broker->record.deadline_ms = request->now_ms + request->ttl_ms;
    broker->record.state = VC_LAUNCH_STATE_PENDING;
    (void)vc_launch_advance_time(broker, request->now_ms, true);
    vc_launch_response_record(response, broker,
                              VC_LAUNCH_CAPABILITY_LAUNCH,
                              VC_LAUNCH_STATUS_PENDING);
    return VC_LAUNCH_STATUS_PENDING;
}

vc_launch_status vc_launch_broker_claim(vc_launch_broker *broker,
                                        const vc_launch_request *request,
                                        vc_launch_response *response)
{
    vc_launch_status status = vc_launch_prepare_operation(
        broker, request, response, VC_LAUNCH_OPERATION_CLAIM);

    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }
    status = vc_launch_match_record(broker, request);
    if (status != VC_LAUNCH_STATUS_PENDING) {
        response->status = status;
        if (status >= VC_LAUNCH_STATUS_ABSENT &&
            status <= VC_LAUNCH_STATUS_CLOCK_ROLLBACK &&
            status != VC_LAUNCH_STATUS_ABSENT) {
            vc_launch_response_record(response, broker, 0, status);
        }
        return status;
    }
    if (broker->foreground_process_id != request->target_process_id ||
        broker->foreground_generation != request->target_generation) {
        broker->record.state = VC_LAUNCH_STATE_STALE_TARGET;
        vc_launch_response_record(response, broker, 0,
                                  VC_LAUNCH_STATUS_STALE_TARGET);
        return VC_LAUNCH_STATUS_STALE_TARGET;
    }
    if (request->presentation_ready == 0) {
        vc_launch_response_record(response, broker,
                                  VC_LAUNCH_CAPABILITY_CLAIM,
                                  VC_LAUNCH_STATUS_NOT_READY);
        return VC_LAUNCH_STATUS_NOT_READY;
    }

    broker->record.state = VC_LAUNCH_STATE_CLAIMED;
    (void)vc_launch_advance_time(broker, request->now_ms, true);
    vc_launch_response_record(response, broker,
                              VC_LAUNCH_CAPABILITY_CLAIM,
                              VC_LAUNCH_STATUS_CLAIMED);
    return VC_LAUNCH_STATUS_CLAIMED;
}

vc_launch_status vc_launch_broker_cancel(vc_launch_broker *broker,
                                         const vc_launch_request *request,
                                         vc_launch_response *response)
{
    vc_launch_status status = vc_launch_prepare_operation(
        broker, request, response, VC_LAUNCH_OPERATION_CANCEL);

    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }
    status = vc_launch_match_record(broker, request);
    if (status == VC_LAUNCH_STATUS_PENDING) {
        broker->record.state = VC_LAUNCH_STATE_CANCELLED;
        status = VC_LAUNCH_STATUS_CANCELLED;
    }
    if (status >= VC_LAUNCH_STATUS_PENDING &&
        status <= VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        (void)vc_launch_advance_time(broker, request->now_ms, true);
        vc_launch_response_record(response, broker,
                                  status == VC_LAUNCH_STATUS_CANCELLED
                                      ? VC_LAUNCH_CAPABILITY_CANCEL
                                      : 0,
                                  status);
    } else {
        response->status = status;
    }
    return status;
}

vc_launch_status vc_launch_broker_status(vc_launch_broker *broker,
                                         const vc_launch_request *request,
                                         vc_launch_response *response)
{
    vc_launch_status status = vc_launch_prepare_operation(
        broker, request, response, VC_LAUNCH_OPERATION_STATUS);

    if (status != VC_LAUNCH_STATUS_OK) {
        return status;
    }
    status = vc_launch_match_record(broker, request);
    if (status >= VC_LAUNCH_STATUS_PENDING &&
        status <= VC_LAUNCH_STATUS_CLOCK_ROLLBACK) {
        (void)vc_launch_advance_time(broker, request->now_ms, true);
        vc_launch_response_record(response, broker,
                                  VC_LAUNCH_CAPABILITY_STATUS,
                                  status);
    } else {
        response->status = status;
    }
    return status;
}

static vc_launch_status vc_launch_dispatch_native(
    vc_launch_broker *broker,
    const vc_launch_request *request,
    vc_launch_response *response)
{
    switch ((vc_launch_operation)request->operation) {
    case VC_LAUNCH_OPERATION_SUBMIT:
        return vc_launch_broker_submit(broker, request, response);
    case VC_LAUNCH_OPERATION_CLAIM:
        return vc_launch_broker_claim(broker, request, response);
    case VC_LAUNCH_OPERATION_CANCEL:
        return vc_launch_broker_cancel(broker, request, response);
    case VC_LAUNCH_OPERATION_STATUS:
        return vc_launch_broker_status(broker, request, response);
    default:
        vc_launch_response_init(response, request->operation, request->now_ms,
                                VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION);
        return VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
    }
}

vc_launch_status vc_launch_broker_dispatch_wire(
    vc_launch_broker *broker,
    const uint8_t *request_buffer,
    size_t request_size,
    uint8_t *response_buffer,
    size_t response_capacity,
    size_t *response_size)
{
    vc_launch_request request;
    vc_launch_response response;
    vc_launch_status status;
    vc_launch_status encode_status;

    if (response_size == NULL) {
        return VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    }
    *response_size = 0;
    if (response_buffer == NULL ||
        response_capacity != VC_LAUNCH_RESPONSE_WIRE_SIZE) {
        return VC_LAUNCH_STATUS_INVALID_SIZE;
    }

    status = vc_launch_request_decode(request_buffer, request_size, &request);
    if (status != VC_LAUNCH_STATUS_OK) {
        uint16_t operation = VC_LAUNCH_OPERATION_NONE;

        if (request_buffer != NULL &&
            request_size >= VC_REQUEST_OFFSET_OPERATION + sizeof(uint16_t)) {
            operation =
                vc_read_u16_le(request_buffer + VC_REQUEST_OFFSET_OPERATION);
            if (!vc_launch_operation_valid(operation, true)) {
                operation = VC_LAUNCH_OPERATION_NONE;
            }
        }
        vc_launch_response_init(&response, operation, 0, status);
    } else if (!vc_launch_broker_valid(broker)) {
        vc_launch_response_init(&response, request.operation, request.now_ms,
                                VC_LAUNCH_STATUS_INVALID_ARGUMENT);
        status = VC_LAUNCH_STATUS_INVALID_ARGUMENT;
    } else {
        status = vc_launch_dispatch_native(broker, &request, &response);
    }

    encode_status = vc_launch_response_encode(
        &response, response_buffer, response_capacity, response_size);
    return encode_status == VC_LAUNCH_STATUS_OK ? status : encode_status;
}
