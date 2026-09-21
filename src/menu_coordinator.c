#include "vitacheat/menu_coordinator.h"

#include <string.h>

enum {
    VC_MENU_COORDINATOR_MARKER = 0x56434d43u,
    VC_MENU_CLAIMANT_ACTION_NONE = 0,
    VC_MENU_CLAIMANT_ACTION_CLOSE = 1,
    VC_MENU_CLAIMANT_ACTION_RESET = 2,
    VC_MENU_CLAIMANT_ACTION_UNLOAD = 3,
    VC_MENU_CLAIMANT_ACTION_STOP = 4
};

static void vc_menu_clear_configuration(
    vc_menu_coordinator *coordinator);

static bool vc_menu_dependencies_valid(
    const vc_menu_coordinator_dependencies *dependencies)
{
    return dependencies != NULL &&
           dependencies->get_runtime_snapshot != NULL &&
           dependencies->verify_thread_ownership != NULL &&
           dependencies->suspend_thread != NULL &&
           dependencies->resume_thread != NULL;
}

static bool vc_menu_valid(const vc_menu_coordinator *coordinator)
{
    return coordinator != NULL &&
           coordinator->marker ==
               (uint32_t)VC_MENU_COORDINATOR_MARKER;
}

static vc_menu_coordinator_result vc_menu_enter(
    vc_menu_coordinator *coordinator)
{
    if (!vc_menu_valid(coordinator)) {
        return VC_MENU_COORDINATOR_RESULT_NOT_INITIALIZED;
    }
    if (atomic_load_explicit(
            &coordinator->adapter_active,
            memory_order_acquire) != 0u) {
        return VC_MENU_COORDINATOR_RESULT_BUSY;
    }
    if (atomic_exchange_explicit(
            &coordinator->transaction_busy, 1u,
            memory_order_acquire) != 0u) {
        return VC_MENU_COORDINATOR_RESULT_BUSY;
    }
    if (atomic_load_explicit(
            &coordinator->adapter_active,
            memory_order_acquire) != 0u) {
        atomic_store_explicit(
            &coordinator->transaction_busy, 0u,
            memory_order_release);
        return VC_MENU_COORDINATOR_RESULT_BUSY;
    }
    return VC_MENU_COORDINATOR_RESULT_OK;
}

static void vc_menu_leave(vc_menu_coordinator *coordinator)
{
    atomic_store_explicit(
        &coordinator->transaction_busy, 0u,
        memory_order_release);
}

static void vc_menu_adapter_begin(
    vc_menu_coordinator *coordinator)
{
    atomic_store_explicit(
        &coordinator->adapter_active, 1u,
        memory_order_release);
    atomic_store_explicit(
        &coordinator->transaction_busy, 0u,
        memory_order_release);
}

static void vc_menu_adapter_end(
    vc_menu_coordinator *coordinator)
{
    (void)atomic_exchange_explicit(
        &coordinator->transaction_busy, 1u,
        memory_order_acquire);
    atomic_store_explicit(
        &coordinator->adapter_active, 0u,
        memory_order_release);
}

static void vc_menu_set_status(
    vc_menu_coordinator *coordinator,
    vc_menu_coordinator_status status)
{
    atomic_store_explicit(
        &coordinator->status, (unsigned int)status,
        memory_order_release);
}

static vc_menu_coordinator_result vc_menu_result(
    vc_menu_coordinator *coordinator,
    vc_menu_coordinator_result result)
{
    atomic_store_explicit(
        &coordinator->last_result, (int)result,
        memory_order_release);
    return result;
}

static bool vc_menu_bytes_zero(
    const uint8_t *bytes,
    size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool vc_menu_foreground_normalize(
    const vc_launch_foreground_snapshot *foreground,
    vc_launch_foreground_snapshot *normalized)
{
    if (foreground == NULL || normalized == NULL ||
        foreground->sequence == 0 ||
        foreground->present > 1u ||
        foreground->title_id_size >
            VC_LAUNCH_SERVICE_TITLE_ID_MAX) {
        return false;
    }
    memset(normalized, 0, sizeof(*normalized));
    normalized->sequence = foreground->sequence;
    normalized->present = foreground->present;
    if (foreground->present == 0u) {
        return foreground->target_process_id == 0 &&
               foreground->target_generation == 0 &&
               foreground->title_id_size == 0 &&
               vc_menu_bytes_zero(
                   foreground->title_id,
                   sizeof(foreground->title_id));
    }
    if (foreground->target_process_id == 0 ||
        foreground->target_generation == 0 ||
        foreground->title_id_size == 0) {
        return false;
    }
    normalized->target_process_id =
        foreground->target_process_id;
    normalized->target_generation =
        foreground->target_generation;
    normalized->title_id_size = foreground->title_id_size;
    memcpy(normalized->title_id, foreground->title_id,
           foreground->title_id_size);
    return true;
}

static bool vc_menu_identity_normalize(
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
        identity->title_id_size >
            VC_LAUNCH_SERVICE_TITLE_ID_MAX ||
        !vc_menu_foreground_normalize(
            &identity->foreground, &foreground)) {
        return false;
    }
    if (foreground.present != 1u ||
        foreground.target_process_id !=
            identity->caller.process_id ||
        foreground.target_generation !=
            identity->caller.process_generation ||
        foreground.title_id_size != identity->title_id_size ||
        memcmp(foreground.title_id, identity->title_id,
               identity->title_id_size) != 0) {
        return false;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->sequence = identity->sequence;
    normalized->caller = identity->caller;
    normalized->foreground = foreground;
    normalized->title_id_size = identity->title_id_size;
    memcpy(normalized->title_id, identity->title_id,
           identity->title_id_size);
    return true;
}

static bool vc_menu_foreground_equal(
    const vc_launch_foreground_snapshot *left,
    const vc_launch_foreground_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->target_generation == right->target_generation &&
           left->target_process_id == right->target_process_id &&
           left->title_id_size == right->title_id_size &&
           left->present == right->present &&
           memcmp(left->title_id, right->title_id,
                  sizeof(left->title_id)) == 0;
}

static bool vc_menu_identity_equal(
    const vc_launch_claimant_identity_snapshot *left,
    const vc_launch_claimant_identity_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->caller.role == right->caller.role &&
           left->caller.reserved0 == right->caller.reserved0 &&
           left->caller.process_id == right->caller.process_id &&
           left->caller.process_generation ==
               right->caller.process_generation &&
           left->caller.module_generation ==
               right->caller.module_generation &&
           left->title_id_size == right->title_id_size &&
           memcmp(left->title_id, right->title_id,
                  sizeof(left->title_id)) == 0 &&
           vc_menu_foreground_equal(
               &left->foreground, &right->foreground);
}

static bool vc_menu_overlay_valid(
    const vc_launch_overlay_snapshot *overlay)
{
    return overlay != NULL &&
           overlay->sequence != 0 &&
           overlay->generation != 0 &&
           overlay->state <= VC_LAUNCH_OVERLAY_CLOSED &&
           overlay->reserved0 == 0;
}

static bool vc_menu_overlay_equal(
    const vc_launch_overlay_snapshot *left,
    const vc_launch_overlay_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->generation == right->generation &&
           left->state == right->state &&
           left->reserved0 == right->reserved0;
}

static bool vc_menu_presentation_valid(
    const vc_launch_presentation_snapshot *presentation,
    const vc_launch_claimant_identity_snapshot *identity)
{
    return presentation != NULL &&
           presentation->sequence != 0 &&
           presentation->process_id ==
               identity->caller.process_id &&
           presentation->process_generation ==
               identity->caller.process_generation &&
           presentation->module_generation ==
               identity->caller.module_generation &&
           presentation->state <=
               VC_LAUNCH_PRESENTATION_LOST;
}

static bool vc_menu_presentation_equal(
    const vc_launch_presentation_snapshot *left,
    const vc_launch_presentation_snapshot *right)
{
    return left->sequence == right->sequence &&
           left->process_generation ==
               right->process_generation &&
           left->module_generation ==
               right->module_generation &&
           left->process_id == right->process_id &&
           left->state == right->state;
}

static bool vc_menu_observation_normalize(
    const vc_launch_claimant_observation *observation,
    vc_launch_claimant_observation *normalized)
{
    if (observation == NULL || normalized == NULL) {
        return false;
    }
    memset(normalized, 0, sizeof(*normalized));
    if (!vc_menu_identity_normalize(
            &observation->identity,
            &normalized->identity) ||
        !vc_menu_overlay_valid(&observation->overlay) ||
        !vc_menu_presentation_valid(
            &observation->presentation,
            &normalized->identity)) {
        memset(normalized, 0, sizeof(*normalized));
        return false;
    }
    normalized->overlay = observation->overlay;
    normalized->presentation = observation->presentation;
    return true;
}

static bool vc_menu_observation_equal(
    const vc_launch_claimant_observation *left,
    const vc_launch_claimant_observation *right)
{
    return vc_menu_identity_equal(
               &left->identity, &right->identity) &&
           vc_menu_overlay_equal(
               &left->overlay, &right->overlay) &&
           vc_menu_presentation_equal(
               &left->presentation, &right->presentation);
}

static bool vc_menu_authorization_snapshot_equal(
    const vc_launch_open_authorization_snapshot *left,
    const vc_launch_open_authorization_snapshot *right)
{
    return vc_menu_observation_equal(
               &left->observation, &right->observation) &&
           left->authorization.value ==
               right->authorization.value &&
           left->authorization.lifecycle_generation ==
               right->authorization.lifecycle_generation &&
           left->deadline_ms == right->deadline_ms;
}

static bool vc_menu_runtime_normalize(
    const vc_menu_runtime_snapshot *snapshot,
    vc_menu_runtime_snapshot *normalized)
{
    if (snapshot == NULL || normalized == NULL ||
        snapshot->allowlist_revision == 0 ||
        !vc_menu_observation_normalize(
            &snapshot->observation,
            &normalized->observation)) {
        return false;
    }
    normalized->allowlist_revision =
        snapshot->allowlist_revision;
    return true;
}

static bool vc_menu_capture_runtime(
    vc_menu_coordinator *coordinator,
    vc_menu_runtime_snapshot *snapshot)
{
    vc_menu_runtime_snapshot raw;
    bool success;

    memset(&raw, 0, sizeof(raw));
    vc_menu_adapter_begin(coordinator);
    success =
        coordinator->dependencies.get_runtime_snapshot(
            coordinator->dependencies.context, &raw);
    vc_menu_adapter_end(coordinator);
    return success &&
           vc_menu_runtime_normalize(&raw, snapshot);
}

static bool vc_menu_runtime_matches_authorization(
    const vc_menu_coordinator *coordinator,
    const vc_menu_runtime_snapshot *snapshot)
{
    return coordinator->target_bound &&
           coordinator->allowlist_configured &&
           snapshot->allowlist_revision ==
               coordinator->allowlist_revision &&
           vc_menu_identity_equal(
               &coordinator->target,
               &snapshot->observation.identity) &&
           vc_menu_observation_equal(
               &coordinator->authorization_snapshot.observation,
               &snapshot->observation) &&
           snapshot->observation.overlay.state ==
               VC_LAUNCH_OVERLAY_CLOSED &&
           snapshot->observation.presentation.state ==
               VC_LAUNCH_PRESENTATION_READY;
}

static bool vc_menu_claimant_authority_matches(
    vc_menu_coordinator *coordinator,
    vc_launch_claimant_status expected_status)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    result = vc_launch_claimant_validate_open_authorization(
        coordinator->claimant,
        &coordinator->consumed_authorization,
        expected_status);
    vc_menu_adapter_end(coordinator);
    return result == VC_LAUNCH_CLAIMANT_RESULT_OK;
}

static bool vc_menu_validate_suspend_boundary(void *context)
{
    vc_menu_coordinator *coordinator =
        (vc_menu_coordinator *)context;
    vc_menu_runtime_snapshot snapshot;

    return vc_menu_capture_runtime(coordinator, &snapshot) &&
           vc_menu_runtime_matches_authorization(
               coordinator, &snapshot) &&
           vc_menu_claimant_authority_matches(
               coordinator,
               VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED);
}

static bool vc_menu_verify_configured_ownership(
    vc_menu_coordinator *coordinator)
{
    bool success;

    vc_menu_adapter_begin(coordinator);
    success =
        coordinator->dependencies.verify_thread_ownership(
            coordinator->dependencies.context,
            &coordinator->target,
            coordinator->thread_ids,
            coordinator->thread_count);
    vc_menu_adapter_end(coordinator);
    return success;
}

static bool vc_menu_verify_owned_threads(
    vc_menu_coordinator *coordinator)
{
    vc_thread_id owned_thread_ids[VC_PAUSE_MAX_THREADS];
    size_t owned_thread_count = 0;
    size_t index;
    bool success;

    for (index = 0;
         index < coordinator->pause.thread_count;
         ++index) {
        if ((coordinator->pause.owned_mask &
             (UINT64_C(1) << index)) != 0) {
            owned_thread_ids[owned_thread_count] =
                coordinator->pause.thread_ids[index];
            ++owned_thread_count;
        }
    }
    if (owned_thread_count == 0) {
        return true;
    }

    vc_menu_adapter_begin(coordinator);
    success =
        coordinator->dependencies.verify_thread_ownership(
            coordinator->dependencies.context,
            &coordinator->target,
            owned_thread_ids,
            owned_thread_count);
    vc_menu_adapter_end(coordinator);
    return success;
}

static bool vc_menu_validate_resume_boundary(void *context)
{
    return vc_menu_verify_owned_threads(
        (vc_menu_coordinator *)context);
}

static bool vc_menu_thread_is_protected(
    const vc_menu_coordinator *coordinator,
    vc_thread_id thread_id)
{
    const vc_menu_protected_threads *protected_threads =
        &coordinator->protected_threads;

    return thread_id ==
               protected_threads->plugin_control_worker ||
           thread_id == protected_threads->input_hook ||
           thread_id ==
               protected_threads->renderer_present_hook ||
           thread_id == protected_threads->watchdog_worker ||
           thread_id == protected_threads->cleanup_worker ||
           thread_id == protected_threads->current_thread;
}

static bool vc_menu_thread_is_allowed(
    const vc_menu_coordinator *coordinator,
    vc_thread_id thread_id)
{
    size_t index;

    if (thread_id <= 0 ||
        vc_menu_thread_is_protected(coordinator, thread_id)) {
        return false;
    }
    for (index = 0; index < coordinator->thread_count; ++index) {
        if (coordinator->thread_ids[index] == thread_id) {
            return true;
        }
    }
    return false;
}

static int vc_menu_suspend_thread(
    void *context,
    vc_thread_id thread_id)
{
    vc_menu_coordinator *coordinator =
        (vc_menu_coordinator *)context;
    int result;

    if (!vc_menu_thread_is_allowed(coordinator, thread_id)) {
        return -1;
    }
    vc_menu_adapter_begin(coordinator);
    result = coordinator->dependencies.suspend_thread(
        coordinator->dependencies.context, thread_id);
    vc_menu_adapter_end(coordinator);
    return result;
}

static int vc_menu_resume_thread(
    void *context,
    vc_thread_id thread_id)
{
    vc_menu_coordinator *coordinator =
        (vc_menu_coordinator *)context;
    int result;

    if (!vc_menu_thread_is_allowed(coordinator, thread_id)) {
        return -1;
    }
    vc_menu_adapter_begin(coordinator);
    result = coordinator->dependencies.resume_thread(
        coordinator->dependencies.context, thread_id);
    vc_menu_adapter_end(coordinator);
    return result;
}

static vc_pause_operations vc_menu_pause_operations(
    vc_menu_coordinator *coordinator)
{
    vc_pause_operations operations;

    operations.suspend_thread = vc_menu_suspend_thread;
    operations.resume_thread = vc_menu_resume_thread;
    operations.context = coordinator;
    return operations;
}

static vc_pause_validations vc_menu_pause_validations(
    vc_menu_coordinator *coordinator)
{
    vc_pause_validations validations;

    validations.validate_suspend =
        vc_menu_validate_suspend_boundary;
    validations.validate_resume =
        vc_menu_validate_resume_boundary;
    validations.context = coordinator;
    return validations;
}

static vc_launch_claimant_result
vc_menu_claimant_inspect(
    vc_menu_coordinator *coordinator,
    vc_launch_open_authorization_snapshot *snapshot)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    result =
        vc_launch_claimant_inspect_open_authorization(
            coordinator->claimant, snapshot);
    vc_menu_adapter_end(coordinator);
    return result;
}

static vc_launch_claimant_result
vc_menu_claimant_consume(
    vc_menu_coordinator *coordinator,
    vc_launch_open_authorization *authorization)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    result =
        vc_launch_claimant_consume_open_authorization(
            coordinator->claimant, authorization);
    vc_menu_adapter_end(coordinator);
    return result;
}

static vc_launch_claimant_result
vc_menu_claimant_acknowledge(
    vc_menu_coordinator *coordinator)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    result = vc_launch_claimant_acknowledge_open(
        coordinator->claimant,
        &coordinator->consumed_authorization);
    vc_menu_adapter_end(coordinator);
    return result;
}

static vc_launch_claimant_result
vc_menu_claimant_observe(
    vc_menu_coordinator *coordinator)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    result = vc_launch_claimant_observe(
        coordinator->claimant);
    vc_menu_adapter_end(coordinator);
    return result;
}

static vc_launch_claimant_result
vc_menu_claimant_notify_overlay(
    vc_menu_coordinator *coordinator,
    const vc_launch_overlay_snapshot *snapshot)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    result = vc_launch_claimant_notify_overlay(
        coordinator->claimant, snapshot);
    vc_menu_adapter_end(coordinator);
    return result;
}

static vc_launch_claimant_result
vc_menu_claimant_notify_presentation(
    vc_menu_coordinator *coordinator,
    const vc_launch_presentation_snapshot *snapshot)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    result = vc_launch_claimant_notify_presentation(
        coordinator->claimant, snapshot);
    vc_menu_adapter_end(coordinator);
    return result;
}

static vc_launch_claimant_result
vc_menu_claimant_action(
    vc_menu_coordinator *coordinator,
    uint32_t action)
{
    vc_launch_claimant_result result;

    vc_menu_adapter_begin(coordinator);
    switch (action) {
    case VC_MENU_CLAIMANT_ACTION_RESET:
        result = vc_launch_claimant_reset(
            coordinator->claimant);
        break;
    case VC_MENU_CLAIMANT_ACTION_UNLOAD:
        result = vc_launch_claimant_plugin_unload(
            coordinator->claimant);
        break;
    case VC_MENU_CLAIMANT_ACTION_STOP:
        result = vc_launch_claimant_stop(
            coordinator->claimant);
        break;
    case VC_MENU_CLAIMANT_ACTION_CLOSE:
    default:
        result = vc_launch_claimant_close(
            coordinator->claimant);
        break;
    }
    vc_menu_adapter_end(coordinator);
    return result;
}

static bool vc_menu_claimant_action_complete(
    uint32_t action,
    vc_launch_claimant_result result)
{
    if (result == VC_LAUNCH_CLAIMANT_RESULT_OK ||
        result == VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION) {
        return true;
    }
    return action == VC_MENU_CLAIMANT_ACTION_CLOSE &&
           result == VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING;
}

static bool vc_menu_next_generation(uint64_t *generation)
{
    if (*generation == UINT64_MAX) {
        return false;
    }
    ++*generation;
    return *generation != 0;
}

static void vc_menu_clear_lease(
    vc_menu_coordinator *coordinator)
{
    memset(&coordinator->lease, 0,
           sizeof(coordinator->lease));
    coordinator->lease_pause_generation = 0;
    coordinator->lease_lifecycle_generation = 0;
    coordinator->menu_authority_active = false;
    coordinator->acknowledged = false;
}

static bool vc_menu_mint_lease(
    vc_menu_coordinator *coordinator,
    vc_menu_open_lease *lease)
{
    uint64_t value = coordinator->next_lease_id;

    if (value == 0) {
        return false;
    }
    coordinator->next_lease_id =
        value == UINT64_MAX ? 0 : value + 1u;
    coordinator->lease.opaque[0] = value;
    coordinator->lease.opaque[1] =
        (value * UINT64_C(0x9e3779b97f4a7c15)) ^
        coordinator->lifecycle_generation ^
        coordinator->pause_generation;
    coordinator->lease_pause_generation =
        coordinator->pause_generation;
    coordinator->lease_lifecycle_generation =
        coordinator->lifecycle_generation;
    coordinator->menu_authority_active = true;
    *lease = coordinator->lease;
    return true;
}

static bool vc_menu_lease_matches(
    const vc_menu_coordinator *coordinator,
    const vc_menu_open_lease *lease)
{
    return lease != NULL &&
           coordinator->menu_authority_active &&
           coordinator->lease.opaque[0] != 0 &&
           lease->opaque[0] ==
               coordinator->lease.opaque[0] &&
           lease->opaque[1] ==
               coordinator->lease.opaque[1] &&
           coordinator->lease_pause_generation ==
               coordinator->pause_generation &&
           coordinator->lease_lifecycle_generation ==
               coordinator->lifecycle_generation &&
           coordinator->consumed_authorization.value ==
               coordinator->authorization_snapshot
                   .authorization.value &&
           coordinator->consumed_authorization
                   .lifecycle_generation ==
               coordinator->authorization_snapshot
                   .authorization.lifecycle_generation;
}

static bool vc_menu_record_time(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    if (coordinator->has_time &&
        now_ms < coordinator->last_now_ms) {
        return false;
    }
    coordinator->has_time = true;
    coordinator->last_now_ms = now_ms;
    return true;
}

static bool vc_menu_allowlist_locally_valid(
    const vc_menu_coordinator *coordinator,
    const vc_menu_thread_allowlist *allowlist,
    const vc_thread_id *thread_ids)
{
    const vc_menu_protected_threads *protected_threads;
    size_t index;

    if (!coordinator->target_bound ||
        allowlist == NULL ||
        allowlist->thread_ids == NULL ||
        allowlist->thread_count == 0 ||
        allowlist->thread_count > VC_PAUSE_MAX_THREADS ||
        allowlist->revision == 0 ||
        allowlist->process_id !=
            coordinator->target.caller.process_id ||
        allowlist->process_generation !=
            coordinator->target.caller.process_generation ||
        allowlist->module_generation !=
            coordinator->target.caller.module_generation) {
        return false;
    }
    protected_threads = &allowlist->protected_threads;
    if (protected_threads->plugin_control_worker <= 0 ||
        protected_threads->input_hook <= 0 ||
        protected_threads->renderer_present_hook <= 0 ||
        protected_threads->watchdog_worker <= 0 ||
        protected_threads->cleanup_worker <= 0 ||
        protected_threads->current_thread <= 0) {
        return false;
    }

    for (index = 0; index < allowlist->thread_count; ++index) {
        const vc_thread_id thread_id = thread_ids[index];

        if (thread_id <= 0 ||
            (index != 0 &&
             thread_ids[index - 1u] >= thread_id) ||
            thread_id ==
                protected_threads->plugin_control_worker ||
            thread_id == protected_threads->input_hook ||
            thread_id ==
                protected_threads->renderer_present_hook ||
            thread_id ==
                protected_threads->watchdog_worker ||
            thread_id ==
                protected_threads->cleanup_worker ||
            thread_id ==
                protected_threads->current_thread) {
            return false;
        }
    }
    return true;
}

static bool vc_menu_has_cleanup(
    const vc_menu_coordinator *coordinator)
{
    return coordinator->pause.active ||
           coordinator->claimant_cleanup_action !=
               VC_MENU_CLAIMANT_ACTION_NONE;
}

static void vc_menu_revoke_authority(
    vc_menu_coordinator *coordinator)
{
    vc_menu_clear_lease(coordinator);
}

static vc_menu_coordinator_result vc_menu_cleanup(
    vc_menu_coordinator *coordinator,
    vc_menu_coordinator_status final_status,
    uint32_t claimant_action,
    bool abandon_target)
{
    vc_pause_operations operations =
        vc_menu_pause_operations(coordinator);
    vc_pause_validations validations =
        vc_menu_pause_validations(coordinator);
    vc_launch_claimant_result claimant_result;
    vc_pause_status pause_status = VC_PAUSE_STATUS_OK;
    uint32_t action = claimant_action;

    vc_menu_revoke_authority(coordinator);
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_STOPPED) {
        vc_menu_set_status(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_CLOSING);
    }

    if (coordinator->claimant_cleanup_action !=
            VC_MENU_CLAIMANT_ACTION_NONE &&
        (claimant_action == VC_MENU_CLAIMANT_ACTION_NONE ||
         claimant_action == VC_MENU_CLAIMANT_ACTION_CLOSE)) {
        action = coordinator->claimant_cleanup_action;
    }
    if (action != VC_MENU_CLAIMANT_ACTION_NONE) {
        claimant_result =
            vc_menu_claimant_action(coordinator, action);
        coordinator->claimant_cleanup_action =
            vc_menu_claimant_action_complete(
                action, claimant_result)
                ? VC_MENU_CLAIMANT_ACTION_NONE
                : action;
    }

    if (coordinator->pause.active) {
        if (abandon_target) {
            pause_status = vc_pause_abandon(
                &coordinator->pause,
                coordinator->target.caller
                    .process_generation);
        } else {
            pause_status = vc_pause_end_checked(
                &coordinator->pause,
                coordinator->target.caller
                    .process_generation,
                &operations, &validations);
        }
    }

    if (vc_menu_has_cleanup(coordinator)) {
        vc_menu_set_status(
            coordinator,
            coordinator->phase ==
                    VC_MENU_COORDINATOR_PHASE_STOPPED
                ? VC_MENU_COORDINATOR_STATUS_STOPPED
                : VC_MENU_COORDINATOR_STATUS_RESUME_PENDING);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    }

    memset(&coordinator->authorization_snapshot, 0,
           sizeof(coordinator->authorization_snapshot));
    memset(&coordinator->consumed_authorization, 0,
           sizeof(coordinator->consumed_authorization));
    coordinator->started_ms = 0;
    coordinator->acknowledgement_deadline_ms = 0;
    coordinator->deadline_ms = 0;
    if (coordinator->clear_configuration_pending) {
        vc_menu_clear_configuration(coordinator);
        coordinator->clear_configuration_pending = false;
        coordinator->has_time = false;
        coordinator->last_now_ms = 0;
    }
    vc_menu_set_status(
        coordinator,
        coordinator->phase ==
                VC_MENU_COORDINATOR_PHASE_STOPPED
            ? VC_MENU_COORDINATOR_STATUS_STOPPED
            : final_status);
    if (pause_status == VC_PAUSE_STATUS_VALIDATION_FAILED) {
        return vc_menu_result(
            coordinator,
            final_status ==
                    VC_MENU_COORDINATOR_STATUS_EXPIRED
                ? VC_MENU_COORDINATOR_RESULT_EXPIRED
                : VC_MENU_COORDINATOR_RESULT_STALE);
    }
    return vc_menu_result(
        coordinator,
        final_status ==
                VC_MENU_COORDINATOR_STATUS_EXPIRED
            ? VC_MENU_COORDINATOR_RESULT_EXPIRED
            : final_status ==
                      VC_MENU_COORDINATOR_STATUS_STALE
                  ? VC_MENU_COORDINATOR_RESULT_STALE
                  : VC_MENU_COORDINATOR_RESULT_OK);
}

static bool vc_menu_claimant_status_matches(
    vc_menu_coordinator *coordinator)
{
    return vc_menu_claimant_authority_matches(
        coordinator,
        coordinator->acknowledged
            ? VC_LAUNCH_CLAIMANT_STATUS_OPEN
            : VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED);
}

static vc_menu_coordinator_result vc_menu_tick_locked(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_runtime_snapshot snapshot;

    if (!coordinator->menu_authority_active) {
        if (vc_menu_has_cleanup(coordinator)) {
            return vc_menu_cleanup(
                coordinator,
                VC_MENU_COORDINATOR_STATUS_ERROR,
                VC_MENU_CLAIMANT_ACTION_NONE, false);
        }
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NO_ACTION);
    }
    if (!vc_menu_capture_runtime(coordinator, &snapshot) ||
        !vc_menu_runtime_matches_authorization(
            coordinator, &snapshot) ||
        !vc_menu_claimant_status_matches(coordinator)) {
        return vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
    }
    if (now_ms >= coordinator->deadline_ms ||
        (!coordinator->acknowledged &&
         now_ms >=
             coordinator->acknowledgement_deadline_ms)) {
        return vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
    }
    return vc_menu_result(
        coordinator, VC_MENU_COORDINATOR_RESULT_OK);
}

static void vc_menu_clear_configuration(
    vc_menu_coordinator *coordinator)
{
    memset(&coordinator->target, 0,
           sizeof(coordinator->target));
    memset(coordinator->thread_ids, 0,
           sizeof(coordinator->thread_ids));
    memset(&coordinator->protected_threads, 0,
           sizeof(coordinator->protected_threads));
    coordinator->allowlist_revision = 0;
    coordinator->thread_count = 0;
    coordinator->target_bound = false;
    coordinator->allowlist_configured = false;
}

vc_menu_coordinator_result vc_menu_coordinator_init(
    vc_menu_coordinator *coordinator,
    vc_launch_claimant *claimant,
    const vc_menu_coordinator_dependencies *dependencies)
{
    if (coordinator == NULL || claimant == NULL ||
        !vc_menu_dependencies_valid(dependencies)) {
        return VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT;
    }
    if (coordinator->marker ==
        (uint32_t)VC_MENU_COORDINATOR_MARKER) {
        return VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT;
    }

    memset(coordinator, 0, sizeof(*coordinator));
    atomic_init(&coordinator->transaction_busy, 0u);
    atomic_init(&coordinator->adapter_active, 0u);
    atomic_init(
        &coordinator->status,
        (unsigned int)VC_MENU_COORDINATOR_STATUS_STOPPED);
    atomic_init(
        &coordinator->last_result,
        (int)VC_MENU_COORDINATOR_RESULT_OK);
    coordinator->dependencies = *dependencies;
    coordinator->claimant = claimant;
    coordinator->next_lease_id = 1;
    coordinator->phase =
        VC_MENU_COORDINATOR_PHASE_INITIALIZED;
    coordinator->marker =
        (uint32_t)VC_MENU_COORDINATOR_MARKER;
    vc_pause_state_init(&coordinator->pause);
    return vc_menu_result(
        coordinator, VC_MENU_COORDINATOR_RESULT_OK);
}

vc_menu_coordinator_result vc_menu_coordinator_start(
    vc_menu_coordinator *coordinator)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase ==
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator, VC_MENU_COORDINATOR_RESULT_OK);
    }
    if (vc_menu_has_cleanup(coordinator)) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    }
    if (!vc_menu_next_generation(
            &coordinator->lifecycle_generation)) {
        vc_menu_set_status(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR);
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_ID_EXHAUSTED);
    }
    coordinator->phase =
        VC_MENU_COORDINATOR_PHASE_RUNNING;
    coordinator->has_time = false;
    coordinator->last_now_ms = 0;
    vc_menu_set_status(
        coordinator, VC_MENU_COORDINATOR_STATUS_READY);
    vc_menu_leave(coordinator);
    return vc_menu_result(
        coordinator, VC_MENU_COORDINATOR_RESULT_OK);
}

vc_menu_coordinator_result vc_menu_coordinator_bind_target(
    vc_menu_coordinator *coordinator,
    const vc_launch_claimant_identity_snapshot *target)
{
    vc_launch_claimant_identity_snapshot normalized;
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (coordinator->menu_authority_active ||
        vc_menu_has_cleanup(coordinator)) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_BUSY);
    }
    if (!vc_menu_identity_normalize(
            target, &normalized)) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_INVALID_TARGET);
    }
    coordinator->target = normalized;
    coordinator->target_bound = true;
    coordinator->allowlist_configured = false;
    coordinator->allowlist_revision = 0;
    coordinator->thread_count = 0;
    memset(coordinator->thread_ids, 0,
           sizeof(coordinator->thread_ids));
    memset(&coordinator->protected_threads, 0,
           sizeof(coordinator->protected_threads));
    vc_menu_set_status(
        coordinator, VC_MENU_COORDINATOR_STATUS_READY);
    vc_menu_leave(coordinator);
    return vc_menu_result(
        coordinator, VC_MENU_COORDINATOR_RESULT_OK);
}

vc_menu_coordinator_result vc_menu_coordinator_configure_allowlist(
    vc_menu_coordinator *coordinator,
    const vc_menu_thread_allowlist *allowlist)
{
    vc_thread_id copied[VC_PAUSE_MAX_THREADS];
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (coordinator->menu_authority_active ||
        vc_menu_has_cleanup(coordinator)) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_BUSY);
    }
    if (allowlist == NULL ||
        allowlist->thread_ids == NULL ||
        allowlist->thread_count == 0 ||
        allowlist->thread_count > VC_PAUSE_MAX_THREADS) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
    }
    memcpy(copied, allowlist->thread_ids,
           allowlist->thread_count * sizeof(copied[0]));
    if (!vc_menu_allowlist_locally_valid(
            coordinator, allowlist, copied)) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
    }

    memset(coordinator->thread_ids, 0,
           sizeof(coordinator->thread_ids));
    memcpy(coordinator->thread_ids, copied,
           allowlist->thread_count *
               sizeof(coordinator->thread_ids[0]));
    coordinator->thread_count = allowlist->thread_count;
    coordinator->protected_threads =
        allowlist->protected_threads;
    coordinator->allowlist_revision = allowlist->revision;
    coordinator->allowlist_configured = true;
    vc_menu_set_status(
        coordinator, VC_MENU_COORDINATOR_STATUS_READY);
    vc_menu_leave(coordinator);
    return vc_menu_result(
        coordinator, VC_MENU_COORDINATOR_RESULT_OK);
}

vc_menu_coordinator_result vc_menu_coordinator_begin_open(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    uint64_t menu_duration_ms,
    uint64_t acknowledgement_timeout_ms,
    vc_menu_open_lease *lease)
{
    vc_launch_open_authorization_snapshot inspected;
    vc_launch_open_authorization_snapshot reinspected;
    vc_launch_open_authorization consumed;
    vc_menu_runtime_snapshot runtime;
    vc_pause_operations operations;
    vc_pause_validations validations;
    vc_menu_open_lease minted_lease;
    vc_pause_status pause_status;
    vc_launch_claimant_result claimant_result;
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (lease == NULL || menu_duration_ms == 0 ||
        menu_duration_ms > VC_PAUSE_MAX_DURATION_MS ||
        acknowledgement_timeout_ms == 0 ||
        acknowledgement_timeout_ms >
            VC_MENU_COORDINATOR_MAX_ACK_MS ||
        acknowledgement_timeout_ms > menu_duration_ms ||
        now_ms > UINT64_MAX - menu_duration_ms ||
        now_ms >
            UINT64_MAX - acknowledgement_timeout_ms) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK);
    }
    if (!coordinator->target_bound ||
        !coordinator->allowlist_configured) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_INVALID_TARGET);
    }
    if (coordinator->menu_authority_active ||
        vc_menu_has_cleanup(coordinator) ||
        coordinator->pause.active) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_BUSY);
    }
    if (coordinator->pause_generation == UINT64_MAX ||
        coordinator->next_lease_id == 0) {
        vc_menu_set_status(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR);
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_ID_EXHAUSTED);
    }

    vc_menu_set_status(
        coordinator,
        VC_MENU_COORDINATOR_STATUS_VALIDATING);
    memset(&inspected, 0, sizeof(inspected));
    claimant_result =
        vc_menu_claimant_inspect(
            coordinator, &inspected);
    if (claimant_result !=
        VC_LAUNCH_CLAIMANT_RESULT_OK) {
        vc_menu_set_status(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_READY);
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            claimant_result ==
                    VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION
                ? VC_MENU_COORDINATOR_RESULT_NO_AUTHORIZATION
                : VC_MENU_COORDINATOR_RESULT_AUTHORIZATION_FAILED);
    }
    if (inspected.authorization.value == 0 ||
        inspected.authorization.lifecycle_generation == 0 ||
        inspected.deadline_ms <= now_ms ||
        !vc_menu_identity_equal(
            &coordinator->target,
            &inspected.observation.identity) ||
        inspected.observation.overlay.state !=
            VC_LAUNCH_OVERLAY_CLOSED ||
        inspected.observation.presentation.state !=
            VC_LAUNCH_PRESENTATION_READY) {
        coordinator->authorization_snapshot = inspected;
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_STALE);
    }

    coordinator->authorization_snapshot = inspected;
    if (!vc_menu_capture_runtime(
            coordinator, &runtime) ||
        !vc_menu_runtime_matches_authorization(
            coordinator, &runtime)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_STALE);
    }
    if (!vc_menu_verify_configured_ownership(coordinator)) {
        memset(&coordinator->authorization_snapshot, 0,
               sizeof(coordinator->authorization_snapshot));
        vc_menu_set_status(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR);
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_OWNERSHIP_FAILED);
    }
    if (!vc_menu_capture_runtime(
            coordinator, &runtime) ||
        !vc_menu_runtime_matches_authorization(
            coordinator, &runtime)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_STALE);
    }
    memset(&reinspected, 0, sizeof(reinspected));
    claimant_result =
        vc_menu_claimant_inspect(
            coordinator, &reinspected);
    if (claimant_result !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        !vc_menu_authorization_snapshot_equal(
            &inspected, &reinspected) ||
        !vc_menu_capture_runtime(
            coordinator, &runtime) ||
        !vc_menu_runtime_matches_authorization(
            coordinator, &runtime)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         claimant_result ==
                                 VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION
                             ? VC_MENU_COORDINATOR_RESULT_NO_AUTHORIZATION
                             : VC_MENU_COORDINATOR_RESULT_STALE);
    }

    coordinator->started_ms = now_ms;
    coordinator->acknowledgement_deadline_ms =
        now_ms + acknowledgement_timeout_ms;
    coordinator->deadline_ms =
        now_ms + menu_duration_ms;
    ++coordinator->pause_generation;
    vc_menu_set_status(
        coordinator,
        VC_MENU_COORDINATOR_STATUS_SUSPENDING);

    memset(&consumed, 0, sizeof(consumed));
    claimant_result =
        vc_menu_claimant_consume(
            coordinator, &consumed);
    if (claimant_result !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        consumed.value !=
            inspected.authorization.value ||
        consumed.lifecycle_generation !=
            inspected.authorization.lifecycle_generation) {
        coordinator->consumed_authorization = consumed;
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR,
            consumed.value != 0
                ? VC_MENU_CLAIMANT_ACTION_CLOSE
                : VC_MENU_CLAIMANT_ACTION_NONE,
            false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_AUTHORIZATION_FAILED);
    }
    coordinator->consumed_authorization = consumed;

    operations = vc_menu_pause_operations(coordinator);
    validations = vc_menu_pause_validations(coordinator);
    pause_status = vc_pause_begin_checked(
        &coordinator->pause,
        coordinator->target.caller.process_generation,
        now_ms, menu_duration_ms,
        coordinator->thread_ids,
        coordinator->thread_count,
        &operations, &validations);
    if (pause_status != VC_PAUSE_STATUS_OK) {
        result = vc_menu_cleanup(
            coordinator,
            pause_status ==
                    VC_PAUSE_STATUS_VALIDATION_FAILED
                ? VC_MENU_COORDINATOR_STATUS_STALE
                : VC_MENU_COORDINATOR_STATUS_ERROR,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         pause_status ==
                                 VC_PAUSE_STATUS_VALIDATION_FAILED
                             ? VC_MENU_COORDINATOR_RESULT_STALE
                             : VC_MENU_COORDINATOR_RESULT_PAUSE_FAILED);
    }

    if (!vc_menu_claimant_authority_matches(
            coordinator,
            VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_STALE);
    }
    memset(&minted_lease, 0, sizeof(minted_lease));
    if (!vc_menu_mint_lease(
            coordinator, &minted_lease)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_ID_EXHAUSTED);
    }
    *lease = minted_lease;
    vc_menu_set_status(
        coordinator,
        VC_MENU_COORDINATOR_STATUS_AWAITING_OPEN_ACK);
    vc_menu_leave(coordinator);
    return vc_menu_result(
        coordinator, VC_MENU_COORDINATOR_RESULT_OK);
}

vc_menu_coordinator_result vc_menu_coordinator_acknowledge_open(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    vc_menu_open_lease *lease)
{
    vc_menu_runtime_snapshot runtime;
    vc_launch_claimant_result claimant_result;
    vc_menu_open_lease replacement;
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (!vc_menu_lease_matches(coordinator, lease) ||
        coordinator->acknowledged ||
        vc_menu_coordinator_get_status(coordinator) !=
            VC_MENU_COORDINATOR_STATUS_AWAITING_OPEN_ACK) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_WRONG_LEASE);
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        memset(lease, 0, sizeof(*lease));
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK);
    }
    if (now_ms >= coordinator->deadline_ms ||
        now_ms >=
            coordinator->acknowledgement_deadline_ms) {
        memset(lease, 0, sizeof(*lease));
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result;
    }
    if (coordinator->next_lease_id == 0) {
        memset(lease, 0, sizeof(*lease));
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_ID_EXHAUSTED);
    }
    if (!vc_menu_capture_runtime(
            coordinator, &runtime) ||
        !vc_menu_runtime_matches_authorization(
            coordinator, &runtime) ||
        !vc_menu_claimant_authority_matches(
            coordinator,
            VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED)) {
        memset(lease, 0, sizeof(*lease));
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result;
    }

    claimant_result =
        vc_menu_claimant_acknowledge(coordinator);
    if (claimant_result !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        !vc_menu_capture_runtime(
            coordinator, &runtime) ||
        !vc_menu_runtime_matches_authorization(
            coordinator, &runtime) ||
        !vc_menu_claimant_authority_matches(
            coordinator,
            VC_LAUNCH_CLAIMANT_STATUS_OPEN)) {
        memset(lease, 0, sizeof(*lease));
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_ACK_FAILED);
    }

    memset(lease, 0, sizeof(*lease));
    vc_menu_clear_lease(coordinator);
    if (!vc_menu_mint_lease(
            coordinator, &replacement)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_ERROR,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_ID_EXHAUSTED);
    }
    *lease = replacement;
    coordinator->acknowledged = true;
    vc_menu_set_status(
        coordinator, VC_MENU_COORDINATOR_STATUS_OPEN);
    vc_menu_leave(coordinator);
    return vc_menu_result(
        coordinator, VC_MENU_COORDINATOR_RESULT_OK);
}

vc_menu_coordinator_result vc_menu_coordinator_tick(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK);
    }
    result = vc_menu_tick_locked(coordinator, now_ms);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_close(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    vc_menu_open_lease *lease)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (!vc_menu_lease_matches(coordinator, lease)) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_WRONG_LEASE);
    }
    memset(lease, 0, sizeof(*lease));
    if (!vc_menu_record_time(coordinator, now_ms)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK);
    }
    result = vc_menu_cleanup(
        coordinator, VC_MENU_COORDINATOR_STATUS_READY,
        VC_MENU_CLAIMANT_ACTION_CLOSE, false);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_target_changed(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_launch_claimant_result claimant_result;
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result ==
                       VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING
                   ? result
                   : vc_menu_result(
                         coordinator,
                         VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK);
    }
    claimant_result =
        vc_menu_claimant_observe(coordinator);
    if (claimant_result !=
            VC_LAUNCH_CLAIMANT_RESULT_OK &&
        coordinator->menu_authority_active) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
    } else {
        result = vc_menu_tick_locked(coordinator, now_ms);
    }
    vc_menu_leave(coordinator);
    return result;
}

static vc_menu_coordinator_result
vc_menu_finish_observation_event(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    bool changed,
    vc_launch_claimant_result claimant_result)
{
    if ((coordinator->menu_authority_active ||
         coordinator->pause.active) &&
        (changed ||
         claimant_result !=
             VC_LAUNCH_CLAIMANT_RESULT_OK)) {
        return vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STALE,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
    }
    if (claimant_result !=
            VC_LAUNCH_CLAIMANT_RESULT_OK &&
        claimant_result !=
            VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION) {
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_STALE);
    }
    return vc_menu_tick_locked(coordinator, now_ms);
}

vc_menu_coordinator_result vc_menu_coordinator_notify_overlay(
    vc_menu_coordinator *coordinator,
    const vc_launch_overlay_snapshot *snapshot,
    uint64_t now_ms)
{
    vc_launch_claimant_result claimant_result;
    bool changed;
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (snapshot == NULL) {
        result = coordinator->menu_authority_active ||
                         coordinator->pause.active
                     ? vc_menu_cleanup(
                           coordinator,
                           VC_MENU_COORDINATOR_STATUS_STALE,
                           VC_MENU_CLAIMANT_ACTION_CLOSE, false)
                     : vc_menu_result(
                           coordinator,
                           VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
        vc_menu_leave(coordinator);
        return result;
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result;
    }
    changed =
        coordinator->menu_authority_active &&
        !vc_menu_overlay_equal(
            &coordinator->authorization_snapshot
                 .observation.overlay,
            snapshot);
    if (changed) {
        vc_menu_revoke_authority(coordinator);
    }
    claimant_result =
        vc_menu_claimant_notify_overlay(
            coordinator, snapshot);
    result = vc_menu_finish_observation_event(
        coordinator, now_ms, changed, claimant_result);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_notify_presentation(
    vc_menu_coordinator *coordinator,
    const vc_launch_presentation_snapshot *snapshot,
    uint64_t now_ms)
{
    vc_launch_claimant_result claimant_result;
    bool changed;
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    if (snapshot == NULL) {
        result = coordinator->menu_authority_active ||
                         coordinator->pause.active
                     ? vc_menu_cleanup(
                           coordinator,
                           VC_MENU_COORDINATOR_STATUS_STALE,
                           VC_MENU_CLAIMANT_ACTION_CLOSE, false)
                     : vc_menu_result(
                           coordinator,
                           VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
        vc_menu_leave(coordinator);
        return result;
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        result = vc_menu_cleanup(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_EXPIRED,
            VC_MENU_CLAIMANT_ACTION_CLOSE, false);
        vc_menu_leave(coordinator);
        return result;
    }
    changed =
        coordinator->menu_authority_active &&
        !vc_menu_presentation_equal(
            &coordinator->authorization_snapshot
                 .observation.presentation,
            snapshot);
    if (changed) {
        vc_menu_revoke_authority(coordinator);
    }
    claimant_result =
        vc_menu_claimant_notify_presentation(
            coordinator, snapshot);
    result = vc_menu_finish_observation_event(
        coordinator, now_ms, changed, claimant_result);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_target_exit(
    vc_menu_coordinator *coordinator,
    uint32_t process_id,
    uint64_t process_generation,
    uint64_t now_ms)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (process_id == 0 || process_generation == 0) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
    }
    if (!coordinator->target_bound ||
        process_id != coordinator->target.caller.process_id ||
        process_generation !=
            coordinator->target.caller.process_generation) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NO_ACTION);
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        coordinator->last_now_ms = now_ms;
    }
    result = vc_menu_cleanup(
        coordinator,
        VC_MENU_COORDINATOR_STATUS_STALE,
        VC_MENU_CLAIMANT_ACTION_CLOSE, true);
    vc_menu_leave(coordinator);
    return result;
}

static vc_menu_coordinator_result
vc_menu_lifecycle_cleanup(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms,
    uint32_t claimant_action,
    bool stop)
{
    vc_menu_coordinator_result result;

    if (!vc_menu_record_time(coordinator, now_ms)) {
        coordinator->last_now_ms = now_ms;
    }
    if (stop) {
        coordinator->phase =
            VC_MENU_COORDINATOR_PHASE_STOPPED;
        vc_menu_set_status(
            coordinator,
            VC_MENU_COORDINATOR_STATUS_STOPPED);
    }
    coordinator->clear_configuration_pending = true;
    result = vc_menu_cleanup(
        coordinator,
        stop ? VC_MENU_COORDINATOR_STATUS_STOPPED
             : VC_MENU_COORDINATOR_STATUS_READY,
        claimant_action, false);
    if (!vc_menu_has_cleanup(coordinator) && !stop) {
        coordinator->has_time = false;
        coordinator->last_now_ms = 0;
    }
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_claimant_reset(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (coordinator->phase !=
        VC_MENU_COORDINATOR_PHASE_RUNNING) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NOT_RUNNING);
    }
    result = vc_menu_lifecycle_cleanup(
        coordinator, now_ms,
        VC_MENU_CLAIMANT_ACTION_RESET, false);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_plugin_unload(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    result = vc_menu_lifecycle_cleanup(
        coordinator, now_ms,
        VC_MENU_CLAIMANT_ACTION_UNLOAD, true);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_service_stop(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    result = vc_menu_lifecycle_cleanup(
        coordinator, now_ms,
        VC_MENU_CLAIMANT_ACTION_STOP, true);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_retry_cleanup(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_coordinator_status final_status;
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (!vc_menu_has_cleanup(coordinator)) {
        vc_menu_leave(coordinator);
        return vc_menu_result(
            coordinator,
            VC_MENU_COORDINATOR_RESULT_NO_ACTION);
    }
    if (!vc_menu_record_time(coordinator, now_ms)) {
        coordinator->last_now_ms = now_ms;
    }
    final_status =
        coordinator->phase ==
                VC_MENU_COORDINATOR_PHASE_STOPPED
            ? VC_MENU_COORDINATOR_STATUS_STOPPED
            : VC_MENU_COORDINATOR_STATUS_READY;
    result = vc_menu_cleanup(
        coordinator, final_status,
        VC_MENU_CLAIMANT_ACTION_NONE, false);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_stop(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    coordinator->phase =
        VC_MENU_COORDINATOR_PHASE_STOPPED;
    vc_menu_set_status(
        coordinator,
        VC_MENU_COORDINATOR_STATUS_STOPPED);
    coordinator->clear_configuration_pending = true;
    if (!vc_menu_record_time(coordinator, now_ms)) {
        coordinator->last_now_ms = now_ms;
    }
    result = vc_menu_cleanup(
        coordinator,
        VC_MENU_COORDINATOR_STATUS_STOPPED,
        VC_MENU_CLAIMANT_ACTION_CLOSE, false);
    vc_menu_leave(coordinator);
    return result;
}

vc_menu_coordinator_result vc_menu_coordinator_reset(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    return vc_menu_coordinator_claimant_reset(
        coordinator, now_ms);
}

vc_menu_coordinator_result vc_menu_coordinator_destroy(
    vc_menu_coordinator *coordinator,
    uint64_t now_ms)
{
    vc_menu_coordinator_result result;

    if (coordinator == NULL) {
        return VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT;
    }
    if (!vc_menu_valid(coordinator)) {
        memset(coordinator, 0, sizeof(*coordinator));
        return VC_MENU_COORDINATOR_RESULT_OK;
    }
    result = vc_menu_coordinator_stop(
        coordinator, now_ms);
    if (result ==
        VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING) {
        return result;
    }
    memset(coordinator, 0, sizeof(*coordinator));
    return VC_MENU_COORDINATOR_RESULT_OK;
}

vc_menu_coordinator_status vc_menu_coordinator_get_status(
    const vc_menu_coordinator *coordinator)
{
    unsigned int status;

    if (!vc_menu_valid(coordinator)) {
        return VC_MENU_COORDINATOR_STATUS_STOPPED;
    }
    status = atomic_load_explicit(
        &coordinator->status, memory_order_acquire);
    if (status >
        (unsigned int)VC_MENU_COORDINATOR_STATUS_ERROR) {
        return VC_MENU_COORDINATOR_STATUS_ERROR;
    }
    return (vc_menu_coordinator_status)status;
}

vc_menu_coordinator_result vc_menu_coordinator_get_state(
    vc_menu_coordinator *coordinator,
    vc_menu_coordinator_state *state)
{
    vc_menu_coordinator_result result =
        vc_menu_enter(coordinator);

    if (result != VC_MENU_COORDINATOR_RESULT_OK) {
        return result;
    }
    if (state == NULL) {
        vc_menu_leave(coordinator);
        return VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT;
    }
    state->phase = coordinator->phase;
    state->status =
        vc_menu_coordinator_get_status(coordinator);
    state->last_result =
        (vc_menu_coordinator_result)atomic_load_explicit(
            &coordinator->last_result, memory_order_acquire);
    state->cleanup_pending =
        vc_menu_has_cleanup(coordinator);
    state->pause_owned = coordinator->pause.active;
    state->menu_authority_active =
        coordinator->menu_authority_active;
    vc_menu_leave(coordinator);
    return VC_MENU_COORDINATOR_RESULT_OK;
}

size_t vc_menu_coordinator_format_status(
    const vc_menu_coordinator *coordinator,
    char *buffer,
    size_t capacity)
{
    const char *text;
    size_t length;
    size_t copy_size;

    switch (vc_menu_coordinator_get_status(coordinator)) {
    case VC_MENU_COORDINATOR_STATUS_STOPPED:
        text = "Menu coordinator stopped";
        break;
    case VC_MENU_COORDINATOR_STATUS_READY:
        text = "Menu coordinator ready";
        break;
    case VC_MENU_COORDINATOR_STATUS_VALIDATING:
        text = "Validating menu open";
        break;
    case VC_MENU_COORDINATOR_STATUS_SUSPENDING:
        text = "Pausing gameplay";
        break;
    case VC_MENU_COORDINATOR_STATUS_AWAITING_OPEN_ACK:
        text = "Waiting for menu readiness";
        break;
    case VC_MENU_COORDINATOR_STATUS_OPEN:
        text = "VitaCheat menu open";
        break;
    case VC_MENU_COORDINATOR_STATUS_CLOSING:
        text = "Closing VitaCheat menu";
        break;
    case VC_MENU_COORDINATOR_STATUS_RESUME_PENDING:
        text = "Gameplay resume required";
        break;
    case VC_MENU_COORDINATOR_STATUS_EXPIRED:
        text = "Menu lease expired";
        break;
    case VC_MENU_COORDINATOR_STATUS_STALE:
        text = "Menu target changed";
        break;
    case VC_MENU_COORDINATOR_STATUS_ERROR:
    default:
        text = "Menu lifecycle failed";
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
