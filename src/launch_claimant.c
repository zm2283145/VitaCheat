#include "vitacheat/launch_claimant.h"

#include <string.h>

enum {
    VC_LAUNCH_CLAIMANT_MARKER = 0x5643434cu
};

_Static_assert(VC_LAUNCH_ABI_VERSION == UINT16_C(1),
               "launch claimant supports only launch ABI v1");
_Static_assert(VC_LAUNCH_REQUEST_WIRE_SIZE == UINT16_C(64),
               "launch claimant request size changed");
_Static_assert(VC_LAUNCH_RESPONSE_WIRE_SIZE == UINT16_C(72),
               "launch claimant response size changed");

static bool vc_claimant_dependencies_valid(
    const vc_launch_claimant_dependencies *dependencies)
{
    return dependencies != NULL &&
           dependencies->get_observation != NULL &&
           dependencies->get_time != NULL &&
           dependencies->transport != NULL;
}

static bool vc_claimant_valid(const vc_launch_claimant *claimant)
{
    return claimant != NULL &&
           claimant->marker == (uint32_t)VC_LAUNCH_CLAIMANT_MARKER;
}

static vc_launch_claimant_result vc_claimant_enter(
    vc_launch_claimant *claimant)
{
    if (!vc_claimant_valid(claimant)) {
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_INITIALIZED;
    }
    if (atomic_load_explicit(&claimant->adapter_active,
                             memory_order_acquire) != 0u) {
        return VC_LAUNCH_CLAIMANT_RESULT_BUSY;
    }
    if (atomic_exchange_explicit(&claimant->transaction_busy, 1u,
                                 memory_order_acquire) != 0u) {
        return VC_LAUNCH_CLAIMANT_RESULT_BUSY;
    }
    if (atomic_load_explicit(&claimant->adapter_active,
                             memory_order_acquire) != 0u) {
        atomic_store_explicit(&claimant->transaction_busy, 0u,
                              memory_order_release);
        return VC_LAUNCH_CLAIMANT_RESULT_BUSY;
    }
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static void vc_claimant_leave(vc_launch_claimant *claimant)
{
    atomic_store_explicit(&claimant->transaction_busy, 0u,
                          memory_order_release);
}

static void vc_claimant_adapter_begin(vc_launch_claimant *claimant)
{
    atomic_store_explicit(&claimant->adapter_active, 1u,
                          memory_order_release);
    atomic_store_explicit(&claimant->transaction_busy, 0u,
                          memory_order_release);
}

static void vc_claimant_adapter_end(vc_launch_claimant *claimant)
{
    (void)atomic_exchange_explicit(&claimant->transaction_busy, 1u,
                                   memory_order_acquire);
    atomic_store_explicit(&claimant->adapter_active, 0u,
                          memory_order_release);
}

static void vc_claimant_set_status(vc_launch_claimant *claimant,
                                   vc_launch_claimant_status status)
{
    atomic_store_explicit(&claimant->status, (unsigned int)status,
                          memory_order_release);
}

static bool vc_claimant_bytes_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool vc_claimant_foreground_normalize(
    const vc_launch_foreground_snapshot *snapshot,
    vc_launch_foreground_snapshot *normalized)
{
    if (snapshot == NULL || normalized == NULL ||
        snapshot->sequence == 0 ||
        snapshot->present > 1u ||
        snapshot->title_id_size > VC_LAUNCH_SERVICE_TITLE_ID_MAX) {
        return false;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->sequence = snapshot->sequence;
    normalized->present = snapshot->present;
    if (snapshot->present == 0u) {
        return snapshot->target_process_id == 0 &&
               snapshot->target_generation == 0 &&
               snapshot->title_id_size == 0 &&
               vc_claimant_bytes_zero(snapshot->title_id,
                                       sizeof(snapshot->title_id));
    }
    if (snapshot->target_process_id == 0 ||
        snapshot->target_generation == 0 ||
        snapshot->title_id_size == 0) {
        return false;
    }

    normalized->target_process_id = snapshot->target_process_id;
    normalized->target_generation = snapshot->target_generation;
    normalized->title_id_size = snapshot->title_id_size;
    memcpy(normalized->title_id, snapshot->title_id,
           snapshot->title_id_size);
    return true;
}

static bool vc_claimant_identity_normalize(
    const vc_launch_claimant_identity_snapshot *identity,
    vc_launch_claimant_identity_snapshot *normalized)
{
    vc_launch_foreground_snapshot foreground;

    if (identity == NULL || normalized == NULL ||
        identity->sequence == 0 ||
        identity->caller.role != VC_LAUNCH_CALLER_GAME_PLUGIN ||
        identity->caller.reserved0 != 0 ||
        identity->caller.process_id == 0 ||
        identity->caller.process_generation == 0 ||
        identity->caller.module_generation == 0 ||
        identity->title_id_size == 0 ||
        identity->title_id_size > VC_LAUNCH_SERVICE_TITLE_ID_MAX ||
        !vc_claimant_foreground_normalize(
            &identity->foreground, &foreground)) {
        return false;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->sequence = identity->sequence;
    normalized->caller = identity->caller;
    normalized->foreground = foreground;
    normalized->title_id_size = identity->title_id_size;
    memcpy(normalized->title_id, identity->title_id,
           identity->title_id_size);

    return foreground.present == 1u &&
           foreground.target_process_id ==
               identity->caller.process_id &&
           foreground.target_generation ==
               identity->caller.process_generation &&
           foreground.title_id_size == identity->title_id_size &&
           memcmp(foreground.title_id, identity->title_id,
                  identity->title_id_size) == 0;
}

static bool vc_claimant_overlay_normalize(
    const vc_launch_overlay_snapshot *snapshot,
    vc_launch_overlay_snapshot *normalized)
{
    if (snapshot == NULL || normalized == NULL ||
        snapshot->sequence == 0 || snapshot->generation == 0 ||
        snapshot->state > VC_LAUNCH_OVERLAY_CLOSED ||
        snapshot->reserved0 != 0) {
        return false;
    }
    *normalized = *snapshot;
    return true;
}

static bool vc_claimant_presentation_normalize(
    const vc_launch_presentation_snapshot *snapshot,
    const vc_launch_claimant_identity_snapshot *identity,
    vc_launch_presentation_snapshot *normalized)
{
    if (snapshot == NULL || identity == NULL || normalized == NULL ||
        snapshot->sequence == 0 ||
        snapshot->process_id != identity->caller.process_id ||
        snapshot->process_generation !=
            identity->caller.process_generation ||
        snapshot->module_generation !=
            identity->caller.module_generation ||
        snapshot->state > VC_LAUNCH_PRESENTATION_LOST) {
        return false;
    }
    *normalized = *snapshot;
    return true;
}

static vc_launch_claimant_result vc_claimant_observation_normalize(
    const vc_launch_claimant_observation *observation,
    vc_launch_claimant_observation *normalized)
{
    if (observation == NULL || normalized == NULL) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION;
    }
    memset(normalized, 0, sizeof(*normalized));
    if (!vc_claimant_identity_normalize(
            &observation->identity, &normalized->identity) ||
        !vc_claimant_overlay_normalize(
            &observation->overlay, &normalized->overlay) ||
        !vc_claimant_presentation_normalize(
            &observation->presentation, &normalized->identity,
            &normalized->presentation)) {
        memset(normalized, 0, sizeof(*normalized));
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION;
    }
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static bool vc_claimant_identity_instance_equal(
    const vc_launch_claimant_identity_snapshot *left,
    const vc_launch_claimant_identity_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->caller.role == right->caller.role &&
           left->caller.process_id == right->caller.process_id &&
           left->caller.process_generation ==
               right->caller.process_generation &&
           left->caller.module_generation ==
               right->caller.module_generation &&
           left->title_id_size == right->title_id_size &&
           memcmp(left->title_id, right->title_id,
                  sizeof(left->title_id)) == 0;
}

static bool vc_claimant_foreground_equal(
    const vc_launch_foreground_snapshot *left,
    const vc_launch_foreground_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->present == right->present &&
           left->target_process_id == right->target_process_id &&
           left->target_generation == right->target_generation &&
           left->title_id_size == right->title_id_size &&
           memcmp(left->title_id, right->title_id,
                  sizeof(left->title_id)) == 0;
}

static bool vc_claimant_identity_equal(
    const vc_launch_claimant_identity_snapshot *left,
    const vc_launch_claimant_identity_snapshot *right)
{
    return vc_claimant_identity_instance_equal(left, right) &&
           vc_claimant_foreground_equal(&left->foreground,
                                        &right->foreground);
}

static bool vc_claimant_overlay_equal(
    const vc_launch_overlay_snapshot *left,
    const vc_launch_overlay_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->generation == right->generation &&
           left->state == right->state;
}

static bool vc_claimant_presentation_equal(
    const vc_launch_presentation_snapshot *left,
    const vc_launch_presentation_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->process_id == right->process_id &&
           left->process_generation == right->process_generation &&
           left->module_generation == right->module_generation &&
           left->state == right->state;
}

static bool vc_claimant_observation_equal(
    const vc_launch_claimant_observation *left,
    const vc_launch_claimant_observation *right)
{
    return vc_claimant_identity_equal(&left->identity, &right->identity) &&
           vc_claimant_overlay_equal(&left->overlay, &right->overlay) &&
           vc_claimant_presentation_equal(
               &left->presentation, &right->presentation);
}

static bool vc_claimant_has_authority(
    const vc_launch_claimant *claimant)
{
    return claimant->authorization_available ||
           claimant->authorization_consumed ||
           claimant->menu_open;
}

static bool vc_claimant_status_terminal(
    const vc_launch_claimant *claimant)
{
    vc_launch_claimant_status status =
        vc_launch_claimant_get_status(claimant);

    return status == VC_LAUNCH_CLAIMANT_STATUS_EXPIRED ||
           status == VC_LAUNCH_CLAIMANT_STATUS_STALE ||
           status == VC_LAUNCH_CLAIMANT_STATUS_CANCELLED ||
           status == VC_LAUNCH_CLAIMANT_STATUS_ERROR;
}

static void vc_claimant_clear_action(vc_launch_claimant *claimant)
{
    memset(&claimant->action_observation, 0,
           sizeof(claimant->action_observation));
    memset(claimant->request_wire, 0,
           sizeof(claimant->request_wire));
    claimant->request_now_ms = 0;
    claimant->action_lifecycle_generation = 0;
    claimant->action = VC_LAUNCH_CLAIMANT_ACTION_NONE;
    claimant->request_wire_valid = false;
    claimant->in_flight = false;
}

static void vc_claimant_revoke_authorization(
    vc_launch_claimant *claimant)
{
    memset(&claimant->authorization_observation, 0,
           sizeof(claimant->authorization_observation));
    claimant->authorization_deadline_ms = 0;
    claimant->authorization_id = 0;
    claimant->authorization_available = false;
    claimant->authorization_consumed = false;
    claimant->menu_open = false;
}

static void vc_claimant_clear_request(vc_launch_claimant *claimant)
{
    vc_claimant_clear_action(claimant);
    claimant->request_id = 0;
    claimant->request_deadline_ms = 0;
    claimant->server_claimed = false;
    claimant->cancel_requested = false;
}

static void vc_claimant_clear_runtime(vc_launch_claimant *claimant)
{
    vc_claimant_clear_request(claimant);
    vc_claimant_revoke_authorization(claimant);
    memset(&claimant->observation, 0,
           sizeof(claimant->observation));
    claimant->first_closed_sequence = 0;
    claimant->identity_bound = false;
    claimant->overlay_closed_stable = false;
    claimant->claim_requested = false;
    claimant->has_time = false;
    claimant->last_now_ms = 0;
}

static void vc_claimant_set_waiting_status(
    vc_launch_claimant *claimant)
{
    if (!claimant->identity_bound) {
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
    } else if (!claimant->overlay_closed_stable ||
               claimant->observation.overlay.state !=
                   VC_LAUNCH_OVERLAY_CLOSED) {
        vc_claimant_set_status(
            claimant,
            VC_LAUNCH_CLAIMANT_STATUS_WAITING_OVERLAY_CLOSE);
    } else if (claimant->observation.presentation.state !=
               VC_LAUNCH_PRESENTATION_READY) {
        vc_claimant_set_status(
            claimant,
            VC_LAUNCH_CLAIMANT_STATUS_WAITING_PRESENTATION);
    } else {
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_WAITING_REQUEST);
    }
}

static void vc_claimant_mark_stale(vc_launch_claimant *claimant)
{
    vc_claimant_clear_request(claimant);
    vc_claimant_revoke_authorization(claimant);
    claimant->identity_bound = false;
    claimant->claim_requested = false;
    vc_claimant_set_status(claimant,
                           VC_LAUNCH_CLAIMANT_STATUS_STALE);
}

static void vc_claimant_mark_gate_lost(
    vc_launch_claimant *claimant)
{
    if (vc_claimant_has_authority(claimant)) {
        vc_claimant_revoke_authorization(claimant);
        claimant->request_id = 0;
        claimant->request_deadline_ms = 0;
        claimant->server_claimed = false;
        claimant->claim_requested = false;
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
    }
}

static vc_launch_claimant_result vc_claimant_apply_overlay(
    vc_launch_claimant *claimant,
    const vc_launch_overlay_snapshot *snapshot)
{
    const vc_launch_overlay_snapshot previous =
        claimant->observation.overlay;

    if (previous.sequence != 0) {
        if (snapshot->sequence < previous.sequence ||
            (snapshot->sequence == previous.sequence &&
             !vc_claimant_overlay_equal(&previous, snapshot))) {
            return VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION;
        }
        if (vc_claimant_has_authority(claimant) &&
            !vc_claimant_overlay_equal(&previous, snapshot)) {
            vc_claimant_mark_gate_lost(claimant);
        }
    }

    if (previous.generation != snapshot->generation ||
        snapshot->state != VC_LAUNCH_OVERLAY_CLOSED) {
        claimant->first_closed_sequence =
            snapshot->state == VC_LAUNCH_OVERLAY_CLOSED
                ? snapshot->sequence
                : 0;
        claimant->overlay_closed_stable = false;
    } else if (previous.state != VC_LAUNCH_OVERLAY_CLOSED) {
        claimant->first_closed_sequence = snapshot->sequence;
        claimant->overlay_closed_stable = false;
    } else if (snapshot->sequence >
               claimant->first_closed_sequence) {
        claimant->overlay_closed_stable = true;
    }
    claimant->observation.overlay = *snapshot;
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static vc_launch_claimant_result vc_claimant_apply_presentation(
    vc_launch_claimant *claimant,
    const vc_launch_presentation_snapshot *snapshot)
{
    const vc_launch_presentation_snapshot previous =
        claimant->observation.presentation;

    if (previous.sequence != 0) {
        if (snapshot->sequence < previous.sequence ||
            (snapshot->sequence == previous.sequence &&
             !vc_claimant_presentation_equal(&previous, snapshot))) {
            return VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION;
        }
        if (vc_claimant_has_authority(claimant) &&
            !vc_claimant_presentation_equal(&previous, snapshot)) {
            vc_claimant_mark_gate_lost(claimant);
        }
    }
    claimant->observation.presentation = *snapshot;
    if (snapshot->state != VC_LAUNCH_PRESENTATION_READY) {
        vc_claimant_mark_gate_lost(claimant);
    }
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static vc_launch_claimant_result vc_claimant_apply_observation(
    vc_launch_claimant *claimant,
    const vc_launch_claimant_observation *observation,
    bool binding)
{
    vc_launch_claimant_result result;

    if (!binding && claimant->identity_bound) {
        if (!vc_claimant_identity_instance_equal(
                &claimant->observation.identity,
                &observation->identity) ||
            observation->identity.foreground.sequence <
                claimant->observation.identity.foreground.sequence ||
            (observation->identity.foreground.sequence ==
                 claimant->observation.identity.foreground.sequence &&
             !vc_claimant_foreground_equal(
                 &claimant->observation.identity.foreground,
                 &observation->identity.foreground))) {
            vc_claimant_mark_stale(claimant);
            return VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
        }
        if (vc_claimant_has_authority(claimant) &&
            !vc_claimant_identity_equal(
                &claimant->observation.identity,
                &observation->identity)) {
            vc_claimant_mark_stale(claimant);
            return VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
        }
    }

    if (binding) {
        claimant->observation = *observation;
        claimant->identity_bound = true;
        claimant->first_closed_sequence =
            observation->overlay.state == VC_LAUNCH_OVERLAY_CLOSED
                ? observation->overlay.sequence
                : 0;
        claimant->overlay_closed_stable = false;
        vc_claimant_set_waiting_status(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_OK;
    }

    claimant->observation.identity = observation->identity;
    result = vc_claimant_apply_overlay(
        claimant, &observation->overlay);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        vc_claimant_mark_stale(claimant);
        return result;
    }
    result = vc_claimant_apply_presentation(
        claimant, &observation->presentation);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        vc_claimant_mark_stale(claimant);
        return result;
    }
    if (!vc_claimant_has_authority(claimant) &&
        !vc_claimant_status_terminal(claimant) &&
        claimant->action == VC_LAUNCH_CLAIMANT_ACTION_NONE) {
        vc_claimant_set_waiting_status(claimant);
    }
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static vc_launch_claimant_result vc_claimant_capture_observation(
    vc_launch_claimant *claimant,
    vc_launch_claimant_observation *observation)
{
    vc_launch_claimant_observation raw;
    bool success;

    memset(&raw, 0, sizeof(raw));
    vc_claimant_adapter_begin(claimant);
    success = claimant->dependencies.get_observation(
        claimant->dependencies.context, &raw);
    vc_claimant_adapter_end(claimant);
    if (!success) {
        return VC_LAUNCH_CLAIMANT_RESULT_UNAVAILABLE;
    }
    return vc_claimant_observation_normalize(&raw, observation);
}

static vc_launch_claimant_result vc_claimant_capture_time(
    vc_launch_claimant *claimant,
    uint64_t *now_ms)
{
    bool success;

    vc_claimant_adapter_begin(claimant);
    success = claimant->dependencies.get_time(
        claimant->dependencies.context, now_ms);
    vc_claimant_adapter_end(claimant);
    if (!success) {
        return VC_LAUNCH_CLAIMANT_RESULT_UNAVAILABLE;
    }
    if (claimant->has_time && *now_ms < claimant->last_now_ms) {
        vc_claimant_clear_request(claimant);
        vc_claimant_revoke_authorization(claimant);
        claimant->claim_requested = false;
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_ERROR);
        return VC_LAUNCH_CLAIMANT_RESULT_CLOCK_ROLLBACK;
    }
    claimant->has_time = true;
    claimant->last_now_ms = *now_ms;
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static vc_launch_claimant_result vc_claimant_refresh(
    vc_launch_claimant *claimant,
    vc_launch_claimant_observation *captured,
    uint64_t *now_ms)
{
    vc_launch_claimant_result result =
        vc_claimant_capture_observation(claimant, captured);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        if (result ==
            VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION) {
            vc_claimant_mark_stale(claimant);
            return VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
        }
        vc_claimant_clear_runtime(claimant);
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
        return result;
    }
    if (!claimant->identity_bound) {
        return VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
    }
    result = vc_claimant_apply_observation(
        claimant, captured, false);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    result = vc_claimant_capture_time(claimant, now_ms);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        if (result !=
            VC_LAUNCH_CLAIMANT_RESULT_CLOCK_ROLLBACK) {
            vc_claimant_clear_request(claimant);
            vc_claimant_revoke_authorization(claimant);
            claimant->claim_requested = false;
            vc_claimant_set_status(
                claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
        }
        return result;
    }
    if (!claimant->request_wire_valid &&
        claimant->request_id != 0 &&
        !claimant->server_claimed &&
        *now_ms >= claimant->request_deadline_ms) {
        vc_claimant_clear_request(claimant);
        claimant->claim_requested = false;
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_EXPIRED);
        return VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED;
    }
    if ((claimant->authorization_available ||
         claimant->authorization_consumed) &&
        *now_ms >= claimant->authorization_deadline_ms) {
        vc_claimant_revoke_authorization(claimant);
        claimant->request_id = 0;
        claimant->request_deadline_ms = 0;
        claimant->server_claimed = false;
        claimant->claim_requested = false;
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_EXPIRED);
        return VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED;
    }
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static bool vc_claimant_next_lifecycle(
    vc_launch_claimant *claimant)
{
    if (claimant->lifecycle_generation == UINT64_MAX) {
        return false;
    }
    ++claimant->lifecycle_generation;
    return claimant->lifecycle_generation != 0;
}

static vc_launch_claimant_result vc_claimant_build_request(
    vc_launch_claimant *claimant,
    vc_launch_claimant_action action,
    const vc_launch_claimant_observation *observation,
    uint64_t now_ms)
{
    vc_launch_request request;
    vc_launch_status status;
    size_t encoded_size = 0;

    vc_launch_request_init(
        &request,
        action == VC_LAUNCH_CLAIMANT_ACTION_DISCOVER
            ? VC_LAUNCH_OPERATION_STATUS
            : action == VC_LAUNCH_CLAIMANT_ACTION_CLAIM
                  ? VC_LAUNCH_OPERATION_CLAIM
                  : VC_LAUNCH_OPERATION_CANCEL,
        VC_LAUNCH_CALLER_GAME_PLUGIN);
    request.target_process_id =
        observation->identity.caller.process_id;
    request.target_generation =
        observation->identity.caller.process_generation;
    request.now_ms = now_ms;
    if (action == VC_LAUNCH_CLAIMANT_ACTION_DISCOVER) {
        request.capabilities = VC_LAUNCH_CAPABILITY_STATUS;
    } else if (action == VC_LAUNCH_CLAIMANT_ACTION_CLAIM) {
        request.capabilities = VC_LAUNCH_CAPABILITY_CLAIM;
        request.request_id = claimant->request_id;
        request.presentation_ready = 1;
    } else {
        request.capabilities = VC_LAUNCH_CAPABILITY_CANCEL;
        request.request_id = claimant->request_id;
    }

    status = vc_launch_request_encode(
        &request, claimant->request_wire,
        sizeof(claimant->request_wire), &encoded_size);
    if (status != VC_LAUNCH_STATUS_OK ||
        encoded_size != sizeof(claimant->request_wire)) {
        memset(claimant->request_wire, 0,
               sizeof(claimant->request_wire));
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }

    claimant->action = (uint32_t)action;
    claimant->action_observation = *observation;
    claimant->request_now_ms = now_ms;
    claimant->action_lifecycle_generation =
        claimant->lifecycle_generation;
    claimant->request_wire_valid = true;
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static bool vc_claimant_response_base_valid(
    const vc_launch_claimant *claimant,
    const vc_launch_response *response,
    uint16_t operation)
{
    if (response->operation != operation ||
        response->observed_ms != claimant->request_now_ms) {
        return false;
    }
    if (response->launch_state == VC_LAUNCH_STATE_ABSENT) {
        return response->capabilities == 0 &&
               response->target_process_id == 0 &&
               response->target_generation == 0 &&
               response->request_id == 0;
    }
    return response->target_process_id ==
               claimant->action_observation.identity.caller.process_id &&
           response->target_generation ==
               claimant->action_observation.identity.caller
                   .process_generation;
}

static bool vc_claimant_response_state_matches(
    const vc_launch_response *response)
{
    switch ((vc_launch_state)response->launch_state) {
    case VC_LAUNCH_STATE_ABSENT:
        return response->status == VC_LAUNCH_STATUS_ABSENT ||
               response->status ==
                   VC_LAUNCH_STATUS_WRONG_REQUEST_ID ||
               response->status == VC_LAUNCH_STATUS_WRONG_TARGET;
    case VC_LAUNCH_STATE_PENDING:
        return response->status == VC_LAUNCH_STATUS_PENDING ||
               response->status == VC_LAUNCH_STATUS_NOT_READY;
    case VC_LAUNCH_STATE_CLAIMED:
        return response->status == VC_LAUNCH_STATUS_CLAIMED;
    case VC_LAUNCH_STATE_EXPIRED:
        return response->status == VC_LAUNCH_STATUS_EXPIRED;
    case VC_LAUNCH_STATE_CANCELLED:
        return response->status == VC_LAUNCH_STATUS_CANCELLED;
    case VC_LAUNCH_STATE_STALE_TARGET:
        return response->status ==
               VC_LAUNCH_STATUS_STALE_TARGET;
    case VC_LAUNCH_STATE_CLOCK_ROLLBACK:
        return response->status ==
               VC_LAUNCH_STATUS_CLOCK_ROLLBACK;
    default:
        return false;
    }
}

static bool vc_claimant_cleanup_response_valid(
    const vc_launch_claimant *claimant,
    const vc_launch_response *response,
    vc_launch_claimant_action action,
    uint64_t now_ms)
{
    uint16_t operation =
        action == VC_LAUNCH_CLAIMANT_ACTION_DISCOVER
            ? VC_LAUNCH_OPERATION_STATUS
            : action == VC_LAUNCH_CLAIMANT_ACTION_CLAIM
                  ? VC_LAUNCH_OPERATION_CLAIM
                  : VC_LAUNCH_OPERATION_CANCEL;

    if (!vc_claimant_response_base_valid(
            claimant, response, operation) ||
        !vc_claimant_response_state_matches(response)) {
        return false;
    }
    if (action == VC_LAUNCH_CLAIMANT_ACTION_DISCOVER) {
        return (response->status == VC_LAUNCH_STATUS_ABSENT &&
                response->launch_state == VC_LAUNCH_STATE_ABSENT) ||
               (response->status == VC_LAUNCH_STATUS_PENDING &&
                response->launch_state == VC_LAUNCH_STATE_PENDING &&
                response->capabilities ==
                    VC_LAUNCH_CAPABILITY_STATUS &&
                response->request_id != 0 &&
                response->deadline_ms > now_ms);
    }
    if (response->launch_state != VC_LAUNCH_STATE_ABSENT &&
        response->request_id != claimant->request_id) {
        return false;
    }
    if (action == VC_LAUNCH_CLAIMANT_ACTION_CLAIM) {
        return (response->status == VC_LAUNCH_STATUS_CLAIMED &&
                response->launch_state == VC_LAUNCH_STATE_CLAIMED &&
                response->capabilities ==
                    VC_LAUNCH_CAPABILITY_CLAIM) ||
               ((response->status == VC_LAUNCH_STATUS_EXPIRED ||
                 response->status ==
                     VC_LAUNCH_STATUS_STALE_TARGET ||
                 response->status ==
                     VC_LAUNCH_STATUS_CLOCK_ROLLBACK ||
                 response->status ==
                     VC_LAUNCH_STATUS_CANCELLED) &&
                response->capabilities == 0) ||
               ((response->status ==
                     VC_LAUNCH_STATUS_WRONG_REQUEST_ID ||
                 response->status ==
                     VC_LAUNCH_STATUS_WRONG_TARGET ||
                 response->status == VC_LAUNCH_STATUS_ABSENT) &&
                response->launch_state ==
                    VC_LAUNCH_STATE_ABSENT);
    }
    return (response->status == VC_LAUNCH_STATUS_CANCELLED &&
            response->launch_state == VC_LAUNCH_STATE_CANCELLED &&
            response->capabilities ==
                VC_LAUNCH_CAPABILITY_CANCEL) ||
           ((response->status == VC_LAUNCH_STATUS_CLAIMED ||
             response->status == VC_LAUNCH_STATUS_EXPIRED ||
             response->status ==
                 VC_LAUNCH_STATUS_STALE_TARGET ||
             response->status ==
                 VC_LAUNCH_STATUS_CLOCK_ROLLBACK) &&
            response->capabilities == 0) ||
           ((response->status ==
                 VC_LAUNCH_STATUS_WRONG_REQUEST_ID ||
             response->status ==
                 VC_LAUNCH_STATUS_WRONG_TARGET ||
             response->status == VC_LAUNCH_STATUS_ABSENT) &&
            response->launch_state == VC_LAUNCH_STATE_ABSENT);
}

static vc_launch_claimant_result vc_claimant_authorize(
    vc_launch_claimant *claimant,
    const vc_launch_response *response,
    const vc_launch_claimant_observation *after,
    uint64_t now_ms)
{
    uint64_t authorization_id = claimant->next_authorization_id;

    if (response->status != VC_LAUNCH_STATUS_CLAIMED ||
        response->launch_state != VC_LAUNCH_STATE_CLAIMED ||
        response->capabilities != VC_LAUNCH_CAPABILITY_CLAIM ||
        response->request_id != claimant->request_id ||
        response->deadline_ms <= now_ms ||
        authorization_id == 0 ||
        now_ms > UINT64_MAX - claimant->authorization_ttl_ms) {
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }
    if (authorization_id == UINT64_MAX) {
        claimant->next_authorization_id = 0;
    } else {
        claimant->next_authorization_id = authorization_id + 1u;
    }

    claimant->authorization_id = authorization_id;
    claimant->authorization_observation = *after;
    claimant->authorization_deadline_ms =
        now_ms + claimant->authorization_ttl_ms;
    claimant->server_claimed = true;
    claimant->authorization_available = true;
    claimant->authorization_consumed = false;
    claimant->menu_open = false;
    claimant->claim_requested = false;
    claimant->cancel_requested = false;
    vc_claimant_set_status(
        claimant, VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED);
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static vc_launch_claimant_result vc_claimant_accept_response(
    vc_launch_claimant *claimant,
    const uint8_t *wire,
    size_t response_size,
    const vc_launch_claimant_observation *after,
    uint64_t now_ms)
{
    vc_launch_response response;
    const vc_launch_claimant_action action =
        (vc_launch_claimant_action)claimant->action;
    uint16_t operation;

    if (response_size != VC_LAUNCH_RESPONSE_WIRE_SIZE ||
        vc_launch_response_decode(wire, response_size, &response) !=
            VC_LAUNCH_STATUS_OK) {
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }
    operation = action == VC_LAUNCH_CLAIMANT_ACTION_DISCOVER
                    ? VC_LAUNCH_OPERATION_STATUS
                    : action == VC_LAUNCH_CLAIMANT_ACTION_CLAIM
                          ? VC_LAUNCH_OPERATION_CLAIM
                          : VC_LAUNCH_OPERATION_CANCEL;
    if (!vc_claimant_response_base_valid(
            claimant, &response, operation) ||
        !vc_claimant_response_state_matches(&response)) {
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }

    if (action == VC_LAUNCH_CLAIMANT_ACTION_DISCOVER) {
        if (response.status == VC_LAUNCH_STATUS_ABSENT &&
            response.launch_state == VC_LAUNCH_STATE_ABSENT) {
            vc_claimant_clear_action(claimant);
            vc_claimant_set_waiting_status(claimant);
            return VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION;
        }
        if (response.status != VC_LAUNCH_STATUS_PENDING ||
            response.launch_state != VC_LAUNCH_STATE_PENDING ||
            response.capabilities != VC_LAUNCH_CAPABILITY_STATUS ||
            response.request_id == 0 ||
            response.deadline_ms <= now_ms) {
            return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
        }
        claimant->request_id = response.request_id;
        claimant->request_deadline_ms = response.deadline_ms;
        vc_claimant_clear_action(claimant);
        vc_claimant_set_waiting_status(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_OK;
    }

    if (response.launch_state != VC_LAUNCH_STATE_ABSENT &&
        response.request_id != claimant->request_id) {
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }
    if (action == VC_LAUNCH_CLAIMANT_ACTION_CLAIM) {
        if (response.status == VC_LAUNCH_STATUS_CLAIMED &&
            response.launch_state == VC_LAUNCH_STATE_CLAIMED &&
            response.capabilities == VC_LAUNCH_CAPABILITY_CLAIM) {
            vc_launch_claimant_result result =
                vc_claimant_authorize(
                    claimant, &response, after, now_ms);

            vc_claimant_clear_action(claimant);
            return result;
        }
        if ((response.status == VC_LAUNCH_STATUS_EXPIRED &&
             response.launch_state == VC_LAUNCH_STATE_EXPIRED &&
             response.capabilities == 0) ||
            (response.status ==
                 VC_LAUNCH_STATUS_CLOCK_ROLLBACK &&
             response.launch_state ==
                 VC_LAUNCH_STATE_CLOCK_ROLLBACK &&
             response.capabilities == 0) ||
            response.status == VC_LAUNCH_STATUS_WRONG_REQUEST_ID ||
            response.status == VC_LAUNCH_STATUS_ABSENT) {
            vc_claimant_clear_request(claimant);
            claimant->claim_requested = false;
            vc_claimant_set_status(
                claimant, VC_LAUNCH_CLAIMANT_STATUS_EXPIRED);
            return VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED;
        }
        if ((response.status == VC_LAUNCH_STATUS_STALE_TARGET &&
             response.launch_state ==
                 VC_LAUNCH_STATE_STALE_TARGET &&
             response.capabilities == 0) ||
            response.status == VC_LAUNCH_STATUS_WRONG_TARGET) {
            vc_claimant_mark_stale(claimant);
            return VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
        }
        if (response.status == VC_LAUNCH_STATUS_CANCELLED &&
            response.launch_state ==
                VC_LAUNCH_STATE_CANCELLED &&
            response.capabilities == 0) {
            vc_claimant_clear_request(claimant);
            claimant->claim_requested = false;
            vc_claimant_set_status(
                claimant, VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
            return VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION;
        }
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }

    if ((response.status == VC_LAUNCH_STATUS_CANCELLED &&
         response.launch_state == VC_LAUNCH_STATE_CANCELLED &&
         response.capabilities == VC_LAUNCH_CAPABILITY_CANCEL) ||
        ((response.status == VC_LAUNCH_STATUS_CLAIMED ||
          response.status == VC_LAUNCH_STATUS_EXPIRED ||
          response.status == VC_LAUNCH_STATUS_STALE_TARGET ||
          response.status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK) &&
         response.capabilities == 0) ||
        ((response.status == VC_LAUNCH_STATUS_WRONG_REQUEST_ID ||
          response.status == VC_LAUNCH_STATUS_WRONG_TARGET ||
          response.status == VC_LAUNCH_STATUS_ABSENT) &&
         response.launch_state == VC_LAUNCH_STATE_ABSENT &&
         response.capabilities == 0)) {
        vc_claimant_clear_request(claimant);
        claimant->claim_requested = false;
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
        return VC_LAUNCH_CLAIMANT_RESULT_OK;
    }
    return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
}

static vc_launch_claimant_result vc_claimant_verify_authorization(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_observation observation;
    uint64_t now_ms = 0;
    vc_launch_claimant_result result =
        vc_claimant_refresh(claimant, &observation, &now_ms);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (!vc_claimant_observation_equal(
            &claimant->authorization_observation,
            &observation) ||
        observation.overlay.state != VC_LAUNCH_OVERLAY_CLOSED ||
        !claimant->overlay_closed_stable ||
        observation.presentation.state !=
            VC_LAUNCH_PRESENTATION_READY) {
        vc_claimant_mark_gate_lost(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
    }
    if (now_ms >= claimant->authorization_deadline_ms) {
        vc_claimant_revoke_authorization(claimant);
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_EXPIRED);
        return VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED;
    }
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static bool vc_claimant_cleanup_transport(
    vc_launch_claimant *claimant)
{
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    vc_launch_claimant_observation before;
    vc_launch_claimant_observation after;
    vc_launch_claimant_transport_result transport_result;
    vc_launch_response response;
    vc_launch_claimant_result result;
    vc_launch_claimant_action action;
    uint64_t before_ms = 0;
    uint64_t after_ms = 0;
    size_t response_size = 0;

    if ((!claimant->request_wire_valid &&
         claimant->request_id == 0) ||
        claimant->server_claimed ||
        !claimant->identity_bound) {
        return true;
    }

    result = vc_claimant_refresh(
        claimant, &before, &before_ms);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result ==
               VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED;
    }
    if ((!claimant->request_wire_valid &&
         claimant->request_id == 0) ||
        claimant->server_claimed) {
        return true;
    }

    if (claimant->request_wire_valid) {
        if (claimant->action_lifecycle_generation !=
                claimant->lifecycle_generation ||
            !vc_claimant_observation_equal(
                &claimant->action_observation, &before)) {
            return false;
        }
        action = (vc_launch_claimant_action)claimant->action;
    } else {
        action = VC_LAUNCH_CLAIMANT_ACTION_CANCEL;
        result = vc_claimant_build_request(
            claimant, action, &before, before_ms);
        if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
            return false;
        }
    }

    memset(response_wire, 0, sizeof(response_wire));
    claimant->in_flight = true;
    vc_claimant_adapter_begin(claimant);
    transport_result = claimant->dependencies.transport(
        claimant->dependencies.context,
        claimant->request_wire,
        sizeof(claimant->request_wire),
        response_wire,
        sizeof(response_wire),
        &response_size);
    vc_claimant_adapter_end(claimant);

    result = vc_claimant_capture_observation(claimant, &after);
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK) {
        result = vc_claimant_capture_time(claimant, &after_ms);
    }
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK ||
        !vc_claimant_observation_equal(
            &claimant->action_observation, &after) ||
        transport_result != VC_LAUNCH_CLAIMANT_TRANSPORT_OK ||
        response_size != VC_LAUNCH_RESPONSE_WIRE_SIZE ||
        vc_launch_response_decode(
            response_wire, response_size, &response) !=
            VC_LAUNCH_STATUS_OK) {
        memset(response_wire, 0, sizeof(response_wire));
        return false;
    }
    memset(response_wire, 0, sizeof(response_wire));
    if (response.observed_ms != claimant->request_now_ms ||
        response.operation !=
            (action == VC_LAUNCH_CLAIMANT_ACTION_DISCOVER
                 ? VC_LAUNCH_OPERATION_STATUS
                 : action == VC_LAUNCH_CLAIMANT_ACTION_CLAIM
                       ? VC_LAUNCH_OPERATION_CLAIM
                       : VC_LAUNCH_OPERATION_CANCEL)) {
        return false;
    }
    if (action == VC_LAUNCH_CLAIMANT_ACTION_CANCEL) {
        return response.request_id == claimant->request_id &&
               response.target_process_id ==
                   claimant->action_observation.identity.caller
                       .process_id &&
               response.target_generation ==
                   claimant->action_observation.identity.caller
                       .process_generation &&
               response.status == VC_LAUNCH_STATUS_CANCELLED &&
               response.launch_state ==
                   VC_LAUNCH_STATE_CANCELLED &&
               response.capabilities ==
                   VC_LAUNCH_CAPABILITY_CANCEL;
    }

    /*
     * An exact journal retry may complete an earlier discovery/claim while
     * stop is already revoking local authority. Retrieving and validating the
     * exact response clears the service journal; no authorization is minted.
     */
    return vc_claimant_cleanup_response_valid(
        claimant, &response, action, after_ms);
}

vc_launch_claimant_result vc_launch_claimant_init(
    vc_launch_claimant *claimant,
    const vc_launch_claimant_dependencies *dependencies,
    uint64_t authorization_ttl_ms)
{
    if (claimant == NULL ||
        !vc_claimant_dependencies_valid(dependencies) ||
        authorization_ttl_ms == 0 ||
        authorization_ttl_ms >
            VC_LAUNCH_CLAIMANT_MAX_AUTHORIZATION_TTL_MS) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT;
    }
    if (claimant->marker == (uint32_t)VC_LAUNCH_CLAIMANT_MARKER) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT;
    }

    memset(claimant, 0, sizeof(*claimant));
    atomic_init(&claimant->transaction_busy, 0u);
    atomic_init(&claimant->adapter_active, 0u);
    atomic_init(&claimant->status,
                (unsigned int)VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
    claimant->dependencies = *dependencies;
    claimant->authorization_ttl_ms = authorization_ttl_ms;
    claimant->next_authorization_id = 1;
    claimant->phase = VC_LAUNCH_CLAIMANT_PHASE_INITIALIZED;
    claimant->marker = (uint32_t)VC_LAUNCH_CLAIMANT_MARKER;
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

vc_launch_claimant_result vc_launch_claimant_start(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase == VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_OK;
    }
    if (claimant->unload_pending) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_CLEANUP_FAILED;
    }
    if (!vc_claimant_next_lifecycle(claimant)) {
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_ERROR);
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }
    vc_claimant_clear_runtime(claimant);
    claimant->phase = VC_LAUNCH_CLAIMANT_PHASE_RUNNING;
    claimant->unload_pending = false;
    memset(&claimant->unload_identity, 0,
           sizeof(claimant->unload_identity));
    claimant->unload_now_ms = 0;
    vc_claimant_set_status(
        claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
    vc_claimant_leave(claimant);
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

vc_launch_claimant_result vc_launch_claimant_bind_identity(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_observation observation;
    uint64_t now_ms = 0;
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    result = vc_claimant_capture_observation(
        claimant, &observation);
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK) {
        if (claimant->identity_bound) {
            result = vc_claimant_apply_observation(
                claimant, &observation, false);
        } else {
            result = vc_claimant_apply_observation(
                claimant, &observation, true);
        }
    }
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK) {
        result = vc_claimant_capture_time(claimant, &now_ms);
    }
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK &&
        claimant->identity_bound) {
        vc_claimant_clear_runtime(claimant);
    }
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK &&
        result != VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY) {
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
    }
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_observe(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_observation observation;
    uint64_t now_ms = 0;
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    result = vc_claimant_refresh(claimant, &observation, &now_ms);
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_tick(
    vc_launch_claimant *claimant)
{
    return vc_launch_claimant_observe(claimant);
}

vc_launch_claimant_result vc_launch_claimant_notify_overlay(
    vc_launch_claimant *claimant,
    const vc_launch_overlay_snapshot *snapshot)
{
    vc_launch_overlay_snapshot normalized;
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING ||
        !claimant->identity_bound) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (!vc_claimant_overlay_normalize(snapshot, &normalized)) {
        vc_claimant_mark_stale(claimant);
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION;
    }
    result = vc_claimant_apply_overlay(claimant, &normalized);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        vc_claimant_mark_stale(claimant);
    }
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK &&
        !vc_claimant_has_authority(claimant) &&
        !vc_claimant_status_terminal(claimant)) {
        vc_claimant_set_waiting_status(claimant);
    }
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_notify_presentation(
    vc_launch_claimant *claimant,
    const vc_launch_presentation_snapshot *snapshot)
{
    vc_launch_presentation_snapshot normalized;
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING ||
        !claimant->identity_bound) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (!vc_claimant_presentation_normalize(
            snapshot, &claimant->observation.identity,
            &normalized)) {
        vc_claimant_mark_stale(claimant);
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION;
    }
    result = vc_claimant_apply_presentation(
        claimant, &normalized);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        vc_claimant_mark_stale(claimant);
    }
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK &&
        !vc_claimant_has_authority(claimant) &&
        !vc_claimant_status_terminal(claimant)) {
        vc_claimant_set_waiting_status(claimant);
    }
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_request_claim(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING ||
        !claimant->identity_bound) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (claimant->server_claimed ||
        vc_claimant_has_authority(claimant)) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION;
    }
    claimant->claim_requested = true;
    vc_claimant_set_waiting_status(claimant);
    vc_claimant_leave(claimant);
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

vc_launch_claimant_result vc_launch_claimant_worker_step(
    vc_launch_claimant *claimant)
{
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    vc_launch_claimant_observation before;
    vc_launch_claimant_observation after;
    vc_launch_claimant_transport_result transport_result;
    vc_launch_claimant_action action;
    vc_launch_claimant_result result = vc_claimant_enter(claimant);
    uint64_t before_ms = 0;
    uint64_t after_ms = 0;
    size_t response_size = 0;

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING ||
        !claimant->identity_bound) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (!claimant->claim_requested &&
        !claimant->cancel_requested &&
        !claimant->request_wire_valid) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION;
    }

    result = vc_claimant_refresh(
        claimant, &before, &before_ms);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        vc_claimant_leave(claimant);
        return result;
    }
    if (!claimant->request_wire_valid) {
        if (claimant->cancel_requested &&
            claimant->request_id != 0 &&
            !claimant->server_claimed) {
            action = VC_LAUNCH_CLAIMANT_ACTION_CANCEL;
        } else {
            if (!claimant->overlay_closed_stable ||
                before.overlay.state !=
                    VC_LAUNCH_OVERLAY_CLOSED) {
                vc_claimant_set_status(
                    claimant,
                    VC_LAUNCH_CLAIMANT_STATUS_WAITING_OVERLAY_CLOSE);
                vc_claimant_leave(claimant);
                return VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION;
            }
            if (before.presentation.state !=
                VC_LAUNCH_PRESENTATION_READY) {
                vc_claimant_set_status(
                    claimant,
                    VC_LAUNCH_CLAIMANT_STATUS_WAITING_PRESENTATION);
                vc_claimant_leave(claimant);
                return VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION;
            }
            action = claimant->request_id == 0
                         ? VC_LAUNCH_CLAIMANT_ACTION_DISCOVER
                         : VC_LAUNCH_CLAIMANT_ACTION_CLAIM;
        }
        result = vc_claimant_build_request(
            claimant, action, &before, before_ms);
        if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
            vc_claimant_set_status(
                claimant, VC_LAUNCH_CLAIMANT_STATUS_ERROR);
            vc_claimant_leave(claimant);
            return result;
        }
    } else {
        if (claimant->action_lifecycle_generation !=
                claimant->lifecycle_generation ||
            !vc_claimant_observation_equal(
                &claimant->action_observation, &before)) {
            vc_claimant_clear_action(claimant);
            vc_claimant_mark_stale(claimant);
            vc_claimant_leave(claimant);
            return VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
        }
    }

    claimant->in_flight = true;
    vc_claimant_set_status(
        claimant, VC_LAUNCH_CLAIMANT_STATUS_CLAIMING);
    memset(response_wire, 0, sizeof(response_wire));
    vc_claimant_adapter_begin(claimant);
    transport_result = claimant->dependencies.transport(
        claimant->dependencies.context,
        claimant->request_wire,
        sizeof(claimant->request_wire),
        response_wire,
        sizeof(response_wire),
        &response_size);
    vc_claimant_adapter_end(claimant);

    result = vc_claimant_capture_observation(claimant, &after);
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK) {
        result = vc_claimant_capture_time(claimant, &after_ms);
    }
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK ||
        claimant->action_lifecycle_generation !=
            claimant->lifecycle_generation ||
        !vc_claimant_observation_equal(
            &claimant->action_observation, &after)) {
        memset(response_wire, 0, sizeof(response_wire));
        vc_claimant_clear_action(claimant);
        if (result == VC_LAUNCH_CLAIMANT_RESULT_OK ||
            result ==
                VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION) {
            vc_claimant_mark_stale(claimant);
            result = VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY;
        }
        vc_claimant_leave(claimant);
        return result;
    }
    result = vc_claimant_apply_observation(
        claimant, &after, false);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        memset(response_wire, 0, sizeof(response_wire));
        vc_claimant_clear_action(claimant);
        vc_claimant_leave(claimant);
        return result;
    }

    if (transport_result == VC_LAUNCH_CLAIMANT_TRANSPORT_BUSY ||
        transport_result == VC_LAUNCH_CLAIMANT_TRANSPORT_RETRY) {
        claimant->in_flight = false;
        if (transport_result ==
            VC_LAUNCH_CLAIMANT_TRANSPORT_BUSY) {
            vc_claimant_clear_action(claimant);
        }
        vc_claimant_set_waiting_status(claimant);
        memset(response_wire, 0, sizeof(response_wire));
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_RETRY;
    }
    if (transport_result ==
        VC_LAUNCH_CLAIMANT_TRANSPORT_UNAVAILABLE) {
        vc_claimant_clear_action(claimant);
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
        memset(response_wire, 0, sizeof(response_wire));
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_UNAVAILABLE;
    }
    if (transport_result != VC_LAUNCH_CLAIMANT_TRANSPORT_OK) {
        vc_claimant_clear_action(claimant);
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_ERROR);
        memset(response_wire, 0, sizeof(response_wire));
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_TRANSPORT_FAILURE;
    }

    result = vc_claimant_accept_response(
        claimant, response_wire, response_size, &after, after_ms);
    memset(response_wire, 0, sizeof(response_wire));
    if (result == VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE) {
        vc_claimant_clear_request(claimant);
        vc_claimant_revoke_authorization(claimant);
        claimant->claim_requested = false;
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_ERROR);
    }
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_consume_open_authorization(
    vc_launch_claimant *claimant,
    vc_launch_open_authorization *authorization)
{
    vc_launch_claimant_result result;

    if (authorization == NULL) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT;
    }
    result = vc_claimant_enter(claimant);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (!claimant->authorization_available ||
        claimant->authorization_consumed ||
        claimant->authorization_id == 0) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION;
    }
    result = vc_claimant_verify_authorization(claimant);
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK) {
        authorization->value = claimant->authorization_id;
        authorization->lifecycle_generation =
            claimant->lifecycle_generation;
        claimant->authorization_available = false;
        claimant->authorization_consumed = true;
        vc_claimant_set_status(
            claimant,
            VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED);
    }
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_inspect_open_authorization(
    vc_launch_claimant *claimant,
    vc_launch_open_authorization_snapshot *snapshot)
{
    vc_launch_claimant_result result;

    if (snapshot == NULL) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    result = vc_claimant_enter(claimant);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (!claimant->authorization_available ||
        claimant->authorization_consumed ||
        claimant->authorization_id == 0) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION;
    }
    result = vc_claimant_verify_authorization(claimant);
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK) {
        snapshot->observation =
            claimant->authorization_observation;
        snapshot->authorization.value =
            claimant->authorization_id;
        snapshot->authorization.lifecycle_generation =
            claimant->lifecycle_generation;
        snapshot->deadline_ms =
            claimant->authorization_deadline_ms;
    }
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_validate_open_authorization(
    vc_launch_claimant *claimant,
    const vc_launch_open_authorization *authorization,
    vc_launch_claimant_status expected_status)
{
    vc_launch_claimant_result result;
    bool valid;

    if (authorization == NULL ||
        (expected_status !=
             VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED &&
         expected_status != VC_LAUNCH_CLAIMANT_STATUS_OPEN)) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT;
    }
    result = vc_claimant_enter(claimant);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }

    valid =
        authorization->value != 0 &&
        authorization->value == claimant->authorization_id &&
        authorization->lifecycle_generation != 0 &&
        authorization->lifecycle_generation ==
            claimant->lifecycle_generation &&
        vc_launch_claimant_get_status(claimant) == expected_status;
    if (expected_status ==
        VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED) {
        valid = valid &&
                claimant->authorization_consumed &&
                !claimant->authorization_available &&
                !claimant->menu_open;
    } else {
        valid = valid &&
                claimant->menu_open &&
                !claimant->authorization_available &&
                !claimant->authorization_consumed;
    }
    vc_claimant_leave(claimant);
    return valid
               ? VC_LAUNCH_CLAIMANT_RESULT_OK
               : VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION;
}

vc_launch_claimant_result vc_launch_claimant_acknowledge_open(
    vc_launch_claimant *claimant,
    const vc_launch_open_authorization *authorization)
{
    vc_launch_claimant_result result;

    if (authorization == NULL) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT;
    }
    result = vc_claimant_enter(claimant);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (!claimant->authorization_consumed ||
        claimant->authorization_id == 0 ||
        authorization->value != claimant->authorization_id ||
        authorization->lifecycle_generation !=
            claimant->lifecycle_generation) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION;
    }
    result = vc_claimant_verify_authorization(claimant);
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK) {
        claimant->authorization_consumed = false;
        claimant->authorization_deadline_ms = 0;
        memset(&claimant->authorization_observation, 0,
               sizeof(claimant->authorization_observation));
        claimant->menu_open = true;
        claimant->request_id = 0;
        claimant->request_deadline_ms = 0;
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_OPEN);
    }
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_cancel(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    claimant->claim_requested = false;
    vc_claimant_revoke_authorization(claimant);
    if (claimant->request_id != 0 &&
        !claimant->server_claimed) {
        claimant->cancel_requested = true;
    } else {
        vc_claimant_clear_request(claimant);
    }
    vc_claimant_set_status(
        claimant, VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
    vc_claimant_leave(claimant);
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

vc_launch_claimant_result vc_launch_claimant_close(
    vc_launch_claimant *claimant)
{
    return vc_launch_claimant_cancel(claimant);
}

static vc_launch_claimant_result vc_claimant_stop_internal(
    vc_launch_claimant *claimant,
    bool notify_unload)
{
    vc_launch_claimant_identity_snapshot identity;
    vc_launch_claimant_plugin_unload_fn unload;
    uint64_t now_ms;
    bool cancel_ok;
    bool unload_ok = true;

    identity = claimant->identity_bound
                   ? claimant->observation.identity
                   : claimant->unload_identity;
    now_ms = claimant->identity_bound
                 ? claimant->last_now_ms
                 : claimant->unload_now_ms;
    unload = claimant->dependencies.plugin_unload;
    claimant->phase = VC_LAUNCH_CLAIMANT_PHASE_STOPPING;
    cancel_ok = vc_claimant_cleanup_transport(claimant);
    if (claimant->identity_bound) {
        identity = claimant->observation.identity;
        now_ms = claimant->last_now_ms;
    }
    (void)vc_claimant_next_lifecycle(claimant);
    if (notify_unload && identity.sequence != 0) {
        claimant->unload_identity = identity;
        claimant->unload_now_ms = now_ms;
    }
    vc_claimant_clear_runtime(claimant);
    claimant->phase = VC_LAUNCH_CLAIMANT_PHASE_STOPPED;
    claimant->unload_pending =
        notify_unload && unload != NULL &&
        claimant->unload_identity.sequence != 0;
    if (!notify_unload) {
        memset(&claimant->unload_identity, 0,
               sizeof(claimant->unload_identity));
        claimant->unload_now_ms = 0;
    }
    vc_claimant_set_status(
        claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);

    if (claimant->unload_pending && unload != NULL) {
        vc_claimant_adapter_begin(claimant);
        unload_ok = unload(
            claimant->dependencies.context,
            &claimant->unload_identity, now_ms);
        vc_claimant_adapter_end(claimant);
        if (unload_ok) {
            claimant->unload_pending = false;
            memset(&claimant->unload_identity, 0,
                   sizeof(claimant->unload_identity));
            claimant->unload_now_ms = 0;
        }
    }
    return unload_ok && cancel_ok
               ? VC_LAUNCH_CLAIMANT_RESULT_OK
               : VC_LAUNCH_CLAIMANT_RESULT_CLEANUP_FAILED;
}

vc_launch_claimant_result vc_launch_claimant_plugin_unload(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase == VC_LAUNCH_CLAIMANT_PHASE_STOPPED &&
        !claimant->unload_pending) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_OK;
    }
    result = vc_claimant_stop_internal(claimant, true);
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_stop(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase == VC_LAUNCH_CLAIMANT_PHASE_STOPPED) {
        if (claimant->unload_pending) {
            result = vc_claimant_stop_internal(claimant, true);
            vc_claimant_leave(claimant);
            return result;
        }
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_OK;
    }
    result = vc_claimant_stop_internal(claimant, false);
    vc_claimant_leave(claimant);
    return result;
}

vc_launch_claimant_result vc_launch_claimant_reset(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_result result = vc_claimant_enter(claimant);

    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    if (claimant->phase != VC_LAUNCH_CLAIMANT_PHASE_RUNNING) {
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
    }
    if (!vc_claimant_next_lifecycle(claimant)) {
        vc_claimant_set_status(
            claimant, VC_LAUNCH_CLAIMANT_STATUS_ERROR);
        vc_claimant_leave(claimant);
        return VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE;
    }
    vc_claimant_clear_runtime(claimant);
    vc_claimant_set_status(
        claimant, VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE);
    vc_claimant_leave(claimant);
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

vc_launch_claimant_result vc_launch_claimant_destroy(
    vc_launch_claimant *claimant)
{
    vc_launch_claimant_result result;

    if (claimant == NULL) {
        return VC_LAUNCH_CLAIMANT_RESULT_INVALID_ARGUMENT;
    }
    if (!vc_claimant_valid(claimant)) {
        memset(claimant, 0, sizeof(*claimant));
        return VC_LAUNCH_CLAIMANT_RESULT_OK;
    }
    result = vc_launch_claimant_stop(claimant);
    if (result != VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return result;
    }
    memset(claimant, 0, sizeof(*claimant));
    return VC_LAUNCH_CLAIMANT_RESULT_OK;
}

vc_launch_claimant_status vc_launch_claimant_get_status(
    const vc_launch_claimant *claimant)
{
    unsigned int status;

    if (!vc_claimant_valid(claimant)) {
        return VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE;
    }
    status = atomic_load_explicit(&claimant->status,
                                  memory_order_acquire);
    if (status >
        (unsigned int)VC_LAUNCH_CLAIMANT_STATUS_ERROR) {
        return VC_LAUNCH_CLAIMANT_STATUS_ERROR;
    }
    return (vc_launch_claimant_status)status;
}

size_t vc_launch_claimant_format_status(
    const vc_launch_claimant *claimant,
    char *buffer,
    size_t capacity)
{
    const char *text;
    size_t length;
    size_t copy_size;

    switch (vc_launch_claimant_get_status(claimant)) {
    case VC_LAUNCH_CLAIMANT_STATUS_UNAVAILABLE:
        text = "VitaCheat unavailable";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_WAITING_REQUEST:
        text = "Waiting for launch request";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_WAITING_OVERLAY_CLOSE:
        text = "Waiting for system overlay to close";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_WAITING_PRESENTATION:
        text = "Waiting for compatible presentation";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_CLAIMING:
        text = "Claiming launch request";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED:
        text = "Menu open authorized";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED:
        text = "Menu authorization consumed";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_OPEN:
        text = "VitaCheat menu open";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_EXPIRED:
        text = "Launch authorization expired";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_STALE:
        text = "Launch identity changed";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_CANCELLED:
        text = "Launch cancelled";
        break;
    case VC_LAUNCH_CLAIMANT_STATUS_ERROR:
    default:
        text = "Launch failed";
        break;
    }

    length = strlen(text);
    if (buffer == NULL || capacity == 0) {
        return length;
    }
    copy_size = length;
    if (copy_size >= capacity) {
        copy_size = capacity - 1u;
    }
    if (copy_size != 0) {
        memcpy(buffer, text, copy_size);
    }
    buffer[copy_size] = '\0';
    return length;
}
