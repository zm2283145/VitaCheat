#include "vitacheat/target_attestation.h"

#include <limits.h>
#include <string.h>

enum {
    VC_TARGET_MARKER = 0x56435441u
};

static bool vc_target_dependencies_valid(
    const vc_target_dependencies *dependencies)
{
    return dependencies != NULL &&
           dependencies->begin_snapshot != NULL &&
           dependencies->read_module != NULL &&
           dependencies->read_thread != NULL &&
           dependencies->end_snapshot != NULL;
}

static bool vc_target_dependencies_equal(
    const vc_target_dependencies *left,
    const vc_target_dependencies *right)
{
    return left->begin_snapshot == right->begin_snapshot &&
           left->read_module == right->read_module &&
           left->read_thread == right->read_thread &&
           left->end_snapshot == right->end_snapshot &&
           left->cleanup == right->cleanup &&
           left->context == right->context;
}

static bool vc_target_valid(const vc_target_attestation *attestation)
{
    return attestation != NULL &&
           attestation->marker == (uint32_t)VC_TARGET_MARKER;
}

static void vc_target_set_status(
    vc_target_attestation *attestation,
    vc_target_runtime_status status)
{
    atomic_store_explicit(
        &attestation->status, (unsigned int)status,
        memory_order_release);
}

static vc_target_status vc_target_result(
    vc_target_attestation *attestation,
    vc_target_status result)
{
    atomic_store_explicit(
        &attestation->last_result, (int)result,
        memory_order_release);
    return result;
}

static vc_target_status vc_target_enter(
    vc_target_attestation *attestation,
    bool require_running)
{
    if (!vc_target_valid(attestation)) {
        return VC_TARGET_STATUS_NOT_INITIALIZED;
    }
    if (atomic_load_explicit(
            &attestation->adapter_active,
            memory_order_acquire) != 0u) {
        return VC_TARGET_STATUS_BUSY;
    }
    if (atomic_exchange_explicit(
            &attestation->transaction_busy, 1u,
            memory_order_acquire) != 0u) {
        return VC_TARGET_STATUS_BUSY;
    }
    if (atomic_load_explicit(
            &attestation->adapter_active,
            memory_order_acquire) != 0u) {
        atomic_store_explicit(
            &attestation->transaction_busy, 0u,
            memory_order_release);
        return VC_TARGET_STATUS_BUSY;
    }
    if (require_running &&
        attestation->phase != VC_TARGET_PHASE_RUNNING) {
        vc_target_status status =
            attestation->phase == VC_TARGET_PHASE_STOPPED
                ? VC_TARGET_STATUS_STOPPED
                : VC_TARGET_STATUS_NOT_INITIALIZED;

        atomic_store_explicit(
            &attestation->transaction_busy, 0u,
            memory_order_release);
        return status;
    }
    return VC_TARGET_STATUS_OK;
}

static void vc_target_leave(vc_target_attestation *attestation)
{
    atomic_store_explicit(
        &attestation->transaction_busy, 0u,
        memory_order_release);
}

static void vc_target_adapter_begin(
    vc_target_attestation *attestation)
{
    atomic_store_explicit(
        &attestation->adapter_active, 1u,
        memory_order_release);
    atomic_store_explicit(
        &attestation->transaction_busy, 0u,
        memory_order_release);
}

static void vc_target_adapter_end(
    vc_target_attestation *attestation)
{
    (void)atomic_exchange_explicit(
        &attestation->transaction_busy, 1u,
        memory_order_acquire);
    atomic_store_explicit(
        &attestation->adapter_active, 0u,
        memory_order_release);
}

static bool vc_target_next_nonzero(uint64_t *value)
{
    if (*value == UINT64_MAX) {
        return false;
    }
    ++*value;
    return true;
}

static void vc_target_clear_snapshot(
    vc_target_attestation *attestation)
{
    memset(&attestation->snapshot, 0,
           sizeof(attestation->snapshot));
    attestation->snapshot_available = false;
}

static void vc_target_invalidate(
    vc_target_attestation *attestation,
    vc_target_runtime_status status)
{
    vc_target_clear_snapshot(attestation);
    vc_target_set_status(attestation, status);
}

static bool vc_target_title_character(uint8_t character)
{
    return (character >= (uint8_t)'A' &&
            character <= (uint8_t)'Z') ||
           (character >= (uint8_t)'0' &&
            character <= (uint8_t)'9');
}

static bool vc_target_version_character(uint8_t character)
{
    return (character >= (uint8_t)'A' &&
            character <= (uint8_t)'Z') ||
           (character >= (uint8_t)'a' &&
            character <= (uint8_t)'z') ||
           (character >= (uint8_t)'0' &&
            character <= (uint8_t)'9') ||
           character == (uint8_t)'.' ||
           character == (uint8_t)'_' ||
           character == (uint8_t)'+' ||
           character == (uint8_t)'-';
}

static bool vc_target_bytes_valid(
    const uint8_t *bytes,
    uint32_t size,
    bool (*validator)(uint8_t))
{
    uint32_t index;

    for (index = 0; index < size; ++index) {
        if (!validator(bytes[index])) {
            return false;
        }
    }
    return true;
}

static vc_target_status vc_target_normalize_identity(
    const vc_target_identity *identity,
    vc_target_identity *normalized)
{
    if (identity->process_id == 0 ||
        identity->process_generation == 0 ||
        identity->foreground_sequence == 0 ||
        identity->reserved0 != 0) {
        return VC_TARGET_STATUS_INVALID_IDENTITY;
    }
    if (identity->title_id_size == 0 ||
        identity->title_id_size > VC_TARGET_TITLE_ID_MAX ||
        !vc_target_bytes_valid(
            identity->title_id, identity->title_id_size,
            vc_target_title_character)) {
        return VC_TARGET_STATUS_INVALID_TITLE;
    }
    if (identity->version_size > VC_TARGET_VERSION_MAX ||
        !vc_target_bytes_valid(
            identity->version, identity->version_size,
            vc_target_version_character)) {
        return VC_TARGET_STATUS_INVALID_VERSION;
    }
    if ((identity->fingerprint_algorithm == 0) !=
            (identity->fingerprint_size == 0) ||
        identity->fingerprint_size >
            VC_TARGET_FINGERPRINT_MAX) {
        return VC_TARGET_STATUS_INVALID_FINGERPRINT;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->process_id = identity->process_id;
    normalized->process_generation =
        identity->process_generation;
    normalized->foreground_sequence =
        identity->foreground_sequence;
    normalized->title_id_size = identity->title_id_size;
    memcpy(normalized->title_id, identity->title_id,
           identity->title_id_size);
    normalized->version_size = identity->version_size;
    memcpy(normalized->version, identity->version,
           identity->version_size);
    normalized->fingerprint_algorithm =
        identity->fingerprint_algorithm;
    normalized->fingerprint_size =
        identity->fingerprint_size;
    memcpy(normalized->fingerprint, identity->fingerprint,
           identity->fingerprint_size);
    return VC_TARGET_STATUS_OK;
}

static vc_target_status vc_target_normalize_segment(
    const vc_target_segment *segment,
    vc_target_segment *normalized)
{
    if (segment->flags != 0 || segment->reserved0 != 0) {
        return VC_TARGET_STATUS_INVALID_SEGMENT;
    }
    if (segment->size == 0) {
        return VC_TARGET_STATUS_INVALID_SEGMENT;
    }
    if (segment->permissions == 0 ||
        (segment->permissions &
         ~VC_TARGET_PERMISSION_ALL) != 0) {
        return VC_TARGET_STATUS_UNKNOWN_PERMISSION;
    }
    if (segment->size > UINT32_MAX - segment->base) {
        return VC_TARGET_STATUS_OVERFLOW;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->segment_index = segment->segment_index;
    normalized->base = segment->base;
    normalized->size = segment->size;
    normalized->permissions = segment->permissions;
    return VC_TARGET_STATUS_OK;
}

static bool vc_target_segments_overlap(
    const vc_target_segment *left,
    const vc_target_segment *right)
{
    const uint32_t left_end = left->base + left->size;
    const uint32_t right_end = right->base + right->size;

    return left->base < right_end &&
           right->base < left_end;
}

static vc_target_status vc_target_normalize_module(
    const vc_target_module *module,
    vc_target_module *normalized)
{
    uint32_t index;
    uint32_t other;

    if (module->module_id == 0 ||
        module->load_generation == 0) {
        return VC_TARGET_STATUS_INVALID_MODULE;
    }
    if (module->segment_count == 0) {
        return VC_TARGET_STATUS_INVALID_SEGMENT;
    }
    if (module->segment_count > VC_TARGET_MAX_SEGMENTS) {
        return VC_TARGET_STATUS_LIMIT;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->module_id = module->module_id;
    normalized->load_generation = module->load_generation;
    normalized->segment_count = module->segment_count;
    for (index = 0; index < module->segment_count; ++index) {
        vc_target_status status =
            vc_target_normalize_segment(
                &module->segments[index],
                &normalized->segments[index]);

        if (status != VC_TARGET_STATUS_OK) {
            memset(normalized, 0, sizeof(*normalized));
            return status;
        }
    }
    for (index = 0; index < module->segment_count; ++index) {
        for (other = index + 1u;
             other < module->segment_count; ++other) {
            if (normalized->segments[index].segment_index ==
                normalized->segments[other].segment_index) {
                memset(normalized, 0, sizeof(*normalized));
                return VC_TARGET_STATUS_DUPLICATE;
            }
            if (vc_target_segments_overlap(
                    &normalized->segments[index],
                    &normalized->segments[other])) {
                memset(normalized, 0, sizeof(*normalized));
                return VC_TARGET_STATUS_OVERLAP;
            }
        }
    }
    return VC_TARGET_STATUS_OK;
}

static vc_target_status vc_target_normalize_thread(
    const vc_target_thread *thread,
    const vc_target_identity *identity,
    vc_target_thread *normalized)
{
    if (thread->thread_id <= 0 ||
        thread->process_id != identity->process_id ||
        thread->process_generation !=
            identity->process_generation ||
        thread->role_flags >
            VC_TARGET_THREAD_ROLE_FLAGS_MAX ||
        thread->reserved0 != 0) {
        return VC_TARGET_STATUS_INVALID_THREAD;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->thread_id = thread->thread_id;
    normalized->process_id = thread->process_id;
    normalized->process_generation =
        thread->process_generation;
    normalized->role_flags = thread->role_flags;
    return VC_TARGET_STATUS_OK;
}

static vc_target_status vc_target_normalize_snapshot(
    const vc_target_snapshot *snapshot,
    vc_target_snapshot *normalized)
{
    uint32_t index;
    uint32_t other;
    vc_target_status status;

    if (snapshot->module_count == 0) {
        return VC_TARGET_STATUS_INVALID_MODULE;
    }
    if (snapshot->thread_count == 0) {
        return VC_TARGET_STATUS_INVALID_THREAD;
    }
    if (snapshot->module_count > VC_TARGET_MAX_MODULES ||
        snapshot->thread_count > VC_TARGET_MAX_THREADS) {
        return VC_TARGET_STATUS_LIMIT;
    }

    memset(normalized, 0, sizeof(*normalized));
    status = vc_target_normalize_identity(
        &snapshot->identity, &normalized->identity);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }

    normalized->module_count = snapshot->module_count;
    normalized->thread_count = snapshot->thread_count;
    for (index = 0; index < snapshot->module_count; ++index) {
        status = vc_target_normalize_module(
            &snapshot->modules[index],
            &normalized->modules[index]);
        if (status != VC_TARGET_STATUS_OK) {
            memset(normalized, 0, sizeof(*normalized));
            return status;
        }
    }
    for (index = 0; index < snapshot->module_count; ++index) {
        for (other = index + 1u;
             other < snapshot->module_count; ++other) {
            if (normalized->modules[index].module_id ==
                normalized->modules[other].module_id) {
                memset(normalized, 0, sizeof(*normalized));
                return VC_TARGET_STATUS_DUPLICATE;
            }
        }
    }

    for (index = 0; index < snapshot->thread_count; ++index) {
        status = vc_target_normalize_thread(
            &snapshot->threads[index],
            &normalized->identity,
            &normalized->threads[index]);
        if (status != VC_TARGET_STATUS_OK) {
            memset(normalized, 0, sizeof(*normalized));
            return status;
        }
    }
    for (index = 0; index < snapshot->thread_count; ++index) {
        for (other = index + 1u;
             other < snapshot->thread_count; ++other) {
            if (normalized->threads[index].thread_id ==
                normalized->threads[other].thread_id) {
                memset(normalized, 0, sizeof(*normalized));
                return VC_TARGET_STATUS_DUPLICATE;
            }
        }
    }
    return VC_TARGET_STATUS_OK;
}

static vc_target_status vc_target_collect_once(
    vc_target_attestation *attestation,
    vc_target_snapshot *snapshot)
{
    vc_target_snapshot collected;
    vc_target_snapshot normalized;
    uint64_t mutation_token = 0;
    uint64_t completion_token = 0;
    uint32_t module_count = 0;
    uint32_t thread_count = 0;
    uint32_t index;
    vc_target_status status = VC_TARGET_STATUS_OK;
    bool ended;

    memset(&collected, 0, sizeof(collected));
    memset(&normalized, 0, sizeof(normalized));
    if (!attestation->dependencies.begin_snapshot(
            attestation->dependencies.context,
            &collected.identity, &module_count,
            &thread_count, &mutation_token)) {
        return VC_TARGET_STATUS_ADAPTER_FAILURE;
    }

    if (mutation_token == 0) {
        status = VC_TARGET_STATUS_MUTATED_SNAPSHOT;
    } else if (module_count > VC_TARGET_MAX_MODULES ||
               thread_count > VC_TARGET_MAX_THREADS) {
        status = VC_TARGET_STATUS_LIMIT;
    } else {
        collected.module_count = module_count;
        collected.thread_count = thread_count;
        for (index = 0; index < module_count; ++index) {
            if (!attestation->dependencies.read_module(
                    attestation->dependencies.context,
                    mutation_token, index,
                    &collected.modules[index])) {
                status = VC_TARGET_STATUS_ADAPTER_FAILURE;
                break;
            }
        }
        if (status == VC_TARGET_STATUS_OK) {
            for (index = 0; index < thread_count; ++index) {
                if (!attestation->dependencies.read_thread(
                        attestation->dependencies.context,
                        mutation_token, index,
                        &collected.threads[index])) {
                    status =
                        VC_TARGET_STATUS_ADAPTER_FAILURE;
                    break;
                }
            }
        }
    }

    ended = attestation->dependencies.end_snapshot(
        attestation->dependencies.context,
        mutation_token, &completion_token);
    if (!ended) {
        return VC_TARGET_STATUS_ADAPTER_FAILURE;
    }
    if (completion_token == 0 ||
        completion_token != mutation_token) {
        return VC_TARGET_STATUS_MUTATED_SNAPSHOT;
    }
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }

    status = vc_target_normalize_snapshot(
        &collected, &normalized);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    *snapshot = normalized;
    return VC_TARGET_STATUS_OK;
}

static bool vc_target_snapshot_facts_equal(
    const vc_target_snapshot *left,
    const vc_target_snapshot *right)
{
    return memcmp(&left->identity, &right->identity,
                  sizeof(left->identity)) == 0 &&
           left->module_count == right->module_count &&
           left->thread_count == right->thread_count &&
           memcmp(left->modules, right->modules,
                  sizeof(left->modules)) == 0 &&
           memcmp(left->threads, right->threads,
                  sizeof(left->threads)) == 0;
}

static vc_target_status vc_target_require_snapshot(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision)
{
    if (snapshot_revision == 0) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    if (!attestation->snapshot_available) {
        return VC_TARGET_STATUS_STALE_SNAPSHOT;
    }
    if (attestation->snapshot.revision !=
        snapshot_revision) {
        return VC_TARGET_STATUS_STALE_SNAPSHOT;
    }
    return VC_TARGET_STATUS_OK;
}

static vc_target_module *vc_target_find_module(
    vc_target_snapshot *snapshot,
    uint32_t module_id,
    uint64_t load_generation)
{
    uint32_t index;

    for (index = 0; index < snapshot->module_count; ++index) {
        vc_target_module *module = &snapshot->modules[index];

        if (module->module_id == module_id &&
            module->load_generation == load_generation) {
            return module;
        }
    }
    return NULL;
}

static vc_target_status vc_target_validate_id_set(
    const int32_t *thread_ids,
    size_t thread_count,
    bool allow_empty)
{
    size_t index;

    if (thread_count > VC_TARGET_MAX_THREADS) {
        return VC_TARGET_STATUS_LIMIT;
    }
    if (thread_count == 0) {
        return allow_empty ? VC_TARGET_STATUS_OK
                           : VC_TARGET_STATUS_INVALID_THREAD;
    }
    if (thread_ids == NULL) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < thread_count; ++index) {
        if (thread_ids[index] <= 0 ||
            (index != 0 &&
             thread_ids[index - 1u] >= thread_ids[index])) {
            return VC_TARGET_STATUS_INVALID_THREAD;
        }
    }
    return VC_TARGET_STATUS_OK;
}

static bool vc_target_thread_present(
    const vc_target_snapshot *snapshot,
    int32_t thread_id)
{
    uint32_t index;

    for (index = 0; index < snapshot->thread_count; ++index) {
        if (snapshot->threads[index].thread_id == thread_id &&
            snapshot->threads[index].process_id ==
                snapshot->identity.process_id &&
            snapshot->threads[index].process_generation ==
                snapshot->identity.process_generation) {
            return true;
        }
    }
    return false;
}

static bool vc_target_set_contains(
    const int32_t *thread_ids,
    size_t thread_count,
    int32_t thread_id)
{
    size_t index;

    for (index = 0; index < thread_count; ++index) {
        if (thread_ids[index] == thread_id) {
            return true;
        }
        if (thread_ids[index] > thread_id) {
            return false;
        }
    }
    return false;
}

static vc_target_status vc_target_validate_threads_locked(
    const vc_target_snapshot *snapshot,
    const int32_t *gameplay_thread_ids,
    size_t gameplay_thread_count,
    const int32_t *protected_thread_ids,
    size_t protected_thread_count)
{
    size_t index;

    for (index = 0; index < gameplay_thread_count; ++index) {
        if (vc_target_set_contains(
                protected_thread_ids,
                protected_thread_count,
                gameplay_thread_ids[index])) {
            return VC_TARGET_STATUS_PROTECTED_THREAD;
        }
        if (!vc_target_thread_present(
                snapshot, gameplay_thread_ids[index])) {
            return VC_TARGET_STATUS_INVALID_THREAD;
        }
    }
    return VC_TARGET_STATUS_OK;
}

static vc_target_status vc_target_normalize_policy(
    const vc_target_policy *policy,
    vc_target_policy *normalized)
{
    uint32_t index;
    uint32_t other;

    if (policy == NULL || normalized == NULL) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    if (policy->reserved0 != 0) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    if (policy->title_id_size == 0 ||
        policy->title_id_size > VC_TARGET_TITLE_ID_MAX ||
        !vc_target_bytes_valid(
            policy->title_id, policy->title_id_size,
            vc_target_title_character)) {
        return VC_TARGET_STATUS_INVALID_TITLE;
    }
    if (policy->version_size > VC_TARGET_VERSION_MAX ||
        !vc_target_bytes_valid(
            policy->version, policy->version_size,
            vc_target_version_character)) {
        return VC_TARGET_STATUS_INVALID_VERSION;
    }
    if ((policy->fingerprint_algorithm == 0) !=
            (policy->fingerprint_size == 0) ||
        policy->fingerprint_size >
            VC_TARGET_FINGERPRINT_MAX) {
        return VC_TARGET_STATUS_INVALID_FINGERPRINT;
    }
    if (policy->module_count > VC_TARGET_MAX_MODULES) {
        return VC_TARGET_STATUS_LIMIT;
    }

    memset(normalized, 0, sizeof(*normalized));
    normalized->title_id_size = policy->title_id_size;
    memcpy(normalized->title_id, policy->title_id,
           policy->title_id_size);
    normalized->version_size = policy->version_size;
    memcpy(normalized->version, policy->version,
           policy->version_size);
    normalized->fingerprint_algorithm =
        policy->fingerprint_algorithm;
    normalized->fingerprint_size =
        policy->fingerprint_size;
    memcpy(normalized->fingerprint, policy->fingerprint,
           policy->fingerprint_size);
    normalized->module_count = policy->module_count;
    for (index = 0; index < policy->module_count; ++index) {
        if (policy->modules[index].module_id == 0 ||
            policy->modules[index].load_generation == 0 ||
            policy->modules[index].reserved0 != 0) {
            memset(normalized, 0, sizeof(*normalized));
            return VC_TARGET_STATUS_INVALID_MODULE;
        }
        normalized->modules[index] = policy->modules[index];
    }
    for (index = 0; index < policy->module_count; ++index) {
        for (other = index + 1u;
             other < policy->module_count; ++other) {
            if (normalized->modules[index].module_id ==
                normalized->modules[other].module_id) {
                memset(normalized, 0, sizeof(*normalized));
                return VC_TARGET_STATUS_DUPLICATE;
            }
        }
    }
    return VC_TARGET_STATUS_OK;
}

vc_target_status vc_target_attestation_init(
    vc_target_attestation *attestation,
    const vc_target_dependencies *dependencies)
{
    if (attestation == NULL ||
        !vc_target_dependencies_valid(dependencies)) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    if (attestation->marker == (uint32_t)VC_TARGET_MARKER) {
        vc_target_status status =
            vc_target_enter(attestation, false);

        if (status != VC_TARGET_STATUS_OK) {
            return status;
        }
        if (vc_target_dependencies_equal(
                &attestation->dependencies, dependencies)) {
            vc_target_leave(attestation);
            return vc_target_result(
                attestation, VC_TARGET_STATUS_OK);
        }
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_ARGUMENT);
    }

    memset(attestation, 0, sizeof(*attestation));
    atomic_init(&attestation->transaction_busy, 0u);
    atomic_init(&attestation->adapter_active, 0u);
    atomic_init(
        &attestation->status,
        (unsigned int)VC_TARGET_RUNTIME_STOPPED);
    atomic_init(
        &attestation->last_result,
        (int)VC_TARGET_STATUS_OK);
    attestation->dependencies = *dependencies;
    attestation->phase = VC_TARGET_PHASE_INITIALIZED;
    attestation->marker = (uint32_t)VC_TARGET_MARKER;
    return VC_TARGET_STATUS_OK;
}

vc_target_status vc_target_attestation_start(
    vc_target_attestation *attestation)
{
    vc_target_status status =
        vc_target_enter(attestation, false);

    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    if (attestation->phase == VC_TARGET_PHASE_RUNNING) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_OK);
    }
    if (!vc_target_next_nonzero(
            &attestation->lifecycle_generation)) {
        vc_target_set_status(
            attestation, VC_TARGET_RUNTIME_ERROR);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_BOUNDED_ERROR);
    }

    vc_target_clear_snapshot(attestation);
    attestation->last_foreground_sequence = 0;
    attestation->phase = VC_TARGET_PHASE_RUNNING;
    vc_target_set_status(
        attestation, VC_TARGET_RUNTIME_UNAVAILABLE);
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_refresh(
    vc_target_attestation *attestation)
{
    vc_target_snapshot candidate;
    vc_target_status status;
    uint64_t lifecycle_generation;
    uint32_t attempt;

    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    lifecycle_generation =
        attestation->lifecycle_generation;
    vc_target_set_status(
        attestation, VC_TARGET_RUNTIME_REFRESHING);
    memset(&candidate, 0, sizeof(candidate));

    vc_target_adapter_begin(attestation);
    status = VC_TARGET_STATUS_MUTATED_SNAPSHOT;
    for (attempt = 0;
         attempt < VC_TARGET_REFRESH_ATTEMPTS;
         ++attempt) {
        status = vc_target_collect_once(
            attestation, &candidate);
        if (status !=
            VC_TARGET_STATUS_MUTATED_SNAPSHOT) {
            break;
        }
    }
    vc_target_adapter_end(attestation);

    if (attestation->phase != VC_TARGET_PHASE_RUNNING ||
        attestation->lifecycle_generation !=
            lifecycle_generation) {
        vc_target_set_status(
            attestation, VC_TARGET_RUNTIME_STALE);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_STALE_SNAPSHOT);
    }
    if (status != VC_TARGET_STATUS_OK) {
        vc_target_set_status(
            attestation,
            attestation->snapshot_available
                ? VC_TARGET_RUNTIME_AVAILABLE
                : VC_TARGET_RUNTIME_ERROR);
        vc_target_leave(attestation);
        return vc_target_result(attestation, status);
    }
    if (candidate.identity.foreground_sequence <
        attestation->last_foreground_sequence) {
        vc_target_invalidate(
            attestation, VC_TARGET_RUNTIME_STALE);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation,
            VC_TARGET_STATUS_SEQUENCE_ROLLBACK);
    }
    if (attestation->snapshot_available &&
        candidate.identity.foreground_sequence ==
            attestation->last_foreground_sequence &&
        !vc_target_snapshot_facts_equal(
            &candidate, &attestation->snapshot)) {
        vc_target_invalidate(
            attestation, VC_TARGET_RUNTIME_STALE);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation,
            VC_TARGET_STATUS_MUTATED_SNAPSHOT);
    }
    if (!vc_target_next_nonzero(
            &attestation->next_revision)) {
        vc_target_set_status(
            attestation, VC_TARGET_RUNTIME_ERROR);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_BOUNDED_ERROR);
    }

    candidate.revision = attestation->next_revision;
    candidate.lifecycle_generation =
        attestation->lifecycle_generation;
    attestation->snapshot = candidate;
    attestation->snapshot_available = true;
    attestation->last_foreground_sequence =
        candidate.identity.foreground_sequence;
    vc_target_set_status(
        attestation, VC_TARGET_RUNTIME_AVAILABLE);
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_get_snapshot(
    vc_target_attestation *attestation,
    vc_target_snapshot *snapshot)
{
    vc_target_status status;

    if (snapshot == NULL) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    if (!attestation->snapshot_available) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_UNAVAILABLE);
    }
    *snapshot = attestation->snapshot;
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_match_policy(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    const vc_target_policy *policy)
{
    vc_target_policy normalized;
    vc_target_status status;
    uint32_t index;

    memset(&normalized, 0, sizeof(normalized));
    status = vc_target_normalize_policy(policy, &normalized);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_require_snapshot(
        attestation, snapshot_revision);
    if (status != VC_TARGET_STATUS_OK) {
        vc_target_leave(attestation);
        return vc_target_result(attestation, status);
    }
    if (normalized.title_id_size !=
            attestation->snapshot.identity.title_id_size ||
        memcmp(normalized.title_id,
               attestation->snapshot.identity.title_id,
               sizeof(normalized.title_id)) != 0) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_TITLE);
    }
    if (normalized.version_size != 0 &&
        (normalized.version_size !=
             attestation->snapshot.identity.version_size ||
         memcmp(normalized.version,
                attestation->snapshot.identity.version,
                sizeof(normalized.version)) != 0)) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_VERSION);
    }
    if (normalized.fingerprint_size != 0 &&
        (normalized.fingerprint_algorithm !=
             attestation->snapshot.identity
                 .fingerprint_algorithm ||
         normalized.fingerprint_size !=
             attestation->snapshot.identity
                 .fingerprint_size ||
         memcmp(normalized.fingerprint,
                attestation->snapshot.identity.fingerprint,
                sizeof(normalized.fingerprint)) != 0)) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation,
            VC_TARGET_STATUS_INVALID_FINGERPRINT);
    }
    for (index = 0; index < normalized.module_count; ++index) {
        if (vc_target_find_module(
                &attestation->snapshot,
                normalized.modules[index].module_id,
                normalized.modules[index]
                    .load_generation) == NULL) {
            vc_target_leave(attestation);
            return vc_target_result(
                attestation, VC_TARGET_STATUS_INVALID_MODULE);
        }
    }
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_find_segment(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    uint32_t module_id,
    uint64_t module_load_generation,
    uint32_t segment_index,
    vc_target_segment *segment)
{
    vc_target_status status;
    vc_target_module *module;
    uint32_t index;

    if (module_id == 0 ||
        module_load_generation == 0 ||
        segment == NULL) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    memset(segment, 0, sizeof(*segment));
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_require_snapshot(
        attestation, snapshot_revision);
    if (status != VC_TARGET_STATUS_OK) {
        vc_target_leave(attestation);
        return vc_target_result(attestation, status);
    }
    module = vc_target_find_module(
        &attestation->snapshot, module_id,
        module_load_generation);
    if (module == NULL) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_MODULE);
    }
    for (index = 0; index < module->segment_count; ++index) {
        if (module->segments[index].segment_index ==
            segment_index) {
            *segment = module->segments[index];
            vc_target_leave(attestation);
            return vc_target_result(
                attestation, VC_TARGET_STATUS_OK);
        }
    }
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_INVALID_SEGMENT);
}

vc_target_status vc_target_attestation_resolve_range(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    uint32_t module_id,
    uint64_t module_load_generation,
    uint32_t address,
    uint32_t length,
    uint32_t required_permissions,
    vc_target_range *range)
{
    vc_target_status status;
    vc_target_module *module;
    vc_target_segment *matched = NULL;
    uint32_t range_end;
    uint32_t index;

    if (module_id == 0 ||
        module_load_generation == 0 ||
        length == 0 || range == NULL) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    memset(range, 0, sizeof(*range));
    if (required_permissions == 0 ||
        (required_permissions &
         ~VC_TARGET_PERMISSION_ALL) != 0) {
        return VC_TARGET_STATUS_UNKNOWN_PERMISSION;
    }
    if (length > UINT32_MAX - address) {
        return VC_TARGET_STATUS_OVERFLOW;
    }
    range_end = address + length;

    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_require_snapshot(
        attestation, snapshot_revision);
    if (status != VC_TARGET_STATUS_OK) {
        vc_target_leave(attestation);
        return vc_target_result(attestation, status);
    }
    module = vc_target_find_module(
        &attestation->snapshot, module_id,
        module_load_generation);
    if (module == NULL) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_MODULE);
    }
    for (index = 0; index < module->segment_count; ++index) {
        vc_target_segment *candidate =
            &module->segments[index];
        const uint32_t segment_end =
            candidate->base + candidate->size;

        if (address >= candidate->base &&
            address < segment_end) {
            matched = candidate;
            if (range_end > segment_end) {
                vc_target_leave(attestation);
                return vc_target_result(
                    attestation,
                    VC_TARGET_STATUS_CROSS_SEGMENT_RANGE);
            }
            break;
        }
    }
    if (matched == NULL) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation,
            VC_TARGET_STATUS_UNMAPPED_RANGE);
    }
    if ((matched->permissions & required_permissions) !=
        required_permissions) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation,
            VC_TARGET_STATUS_PERMISSION_DENIED);
    }

    range->snapshot_revision = snapshot_revision;
    range->module_id = module->module_id;
    range->module_load_generation =
        module->load_generation;
    range->segment_index = matched->segment_index;
    range->segment_offset = address - matched->base;
    range->length = length;
    range->permissions = required_permissions;
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_validate_threads(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    const int32_t *gameplay_thread_ids,
    size_t gameplay_thread_count,
    const int32_t *protected_thread_ids,
    size_t protected_thread_count)
{
    vc_target_status status;

    status = vc_target_validate_id_set(
        gameplay_thread_ids, gameplay_thread_count, false);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_validate_id_set(
        protected_thread_ids, protected_thread_count, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_require_snapshot(
        attestation, snapshot_revision);
    if (status != VC_TARGET_STATUS_OK) {
        vc_target_leave(attestation);
        return vc_target_result(attestation, status);
    }
    status = vc_target_validate_threads_locked(
        &attestation->snapshot,
        gameplay_thread_ids, gameplay_thread_count,
        protected_thread_ids, protected_thread_count);
    vc_target_leave(attestation);
    return vc_target_result(attestation, status);
}

vc_target_status vc_target_attestation_validate_allowlist(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    const vc_target_runtime_binding *binding,
    const int32_t *gameplay_thread_ids,
    size_t gameplay_thread_count,
    const int32_t *protected_thread_ids,
    size_t protected_thread_count)
{
    vc_target_status status;
    uint32_t index;
    bool module_matches = false;
    bool title_matches;

    if (binding == NULL || binding->process_id == 0 ||
        binding->process_generation == 0 ||
        binding->foreground_sequence == 0 ||
        binding->module_id == 0 ||
        binding->module_load_generation == 0 ||
        binding->reserved0 != 0) {
        return VC_TARGET_STATUS_INVALID_IDENTITY;
    }
    if (binding->title_id_size == 0 ||
        binding->title_id_size > VC_TARGET_TITLE_ID_MAX ||
        !vc_target_bytes_valid(
            binding->title_id, binding->title_id_size,
            vc_target_title_character)) {
        return VC_TARGET_STATUS_INVALID_TITLE;
    }
    status = vc_target_validate_id_set(
        gameplay_thread_ids, gameplay_thread_count, false);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_validate_id_set(
        protected_thread_ids, protected_thread_count, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_require_snapshot(
        attestation, snapshot_revision);
    if (status != VC_TARGET_STATUS_OK) {
        vc_target_leave(attestation);
        return vc_target_result(attestation, status);
    }
    title_matches =
        binding->title_id_size ==
        attestation->snapshot.identity.title_id_size;
    for (index = 0;
         title_matches &&
         index < binding->title_id_size;
         ++index) {
        title_matches =
            binding->title_id[index] ==
            attestation->snapshot.identity.title_id[index];
    }
    if (binding->process_id !=
            attestation->snapshot.identity.process_id ||
        binding->process_generation !=
            attestation->snapshot.identity
                .process_generation ||
        binding->foreground_sequence !=
            attestation->snapshot.identity
                .foreground_sequence) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_IDENTITY);
    }
    if (!title_matches) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_TITLE);
    }
    for (index = 0;
         index < attestation->snapshot.module_count;
         ++index) {
        if (attestation->snapshot.modules[index].module_id ==
                binding->module_id &&
            attestation->snapshot.modules[index]
                    .load_generation ==
                binding->module_load_generation) {
            module_matches = true;
            break;
        }
    }
    if (!module_matches) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_INVALID_MODULE);
    }
    status = vc_target_validate_threads_locked(
        &attestation->snapshot,
        gameplay_thread_ids, gameplay_thread_count,
        protected_thread_ids, protected_thread_count);
    vc_target_leave(attestation);
    return vc_target_result(attestation, status);
}

static vc_target_status vc_target_foreground_event(
    vc_target_attestation *attestation,
    uint64_t foreground_sequence)
{
    vc_target_status status;

    if (foreground_sequence == 0) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    if (foreground_sequence <
        attestation->last_foreground_sequence) {
        vc_target_invalidate(
            attestation, VC_TARGET_RUNTIME_STALE);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation,
            VC_TARGET_STATUS_SEQUENCE_ROLLBACK);
    }
    if (foreground_sequence ==
            attestation->last_foreground_sequence &&
        attestation->last_foreground_sequence != 0) {
        if (!attestation->snapshot_available) {
            vc_target_leave(attestation);
            return vc_target_result(
                attestation, VC_TARGET_STATUS_OK);
        }
        vc_target_invalidate(
            attestation, VC_TARGET_RUNTIME_STALE);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation,
            VC_TARGET_STATUS_MUTATED_SNAPSHOT);
    }
    attestation->last_foreground_sequence =
        foreground_sequence;
    vc_target_invalidate(
        attestation, VC_TARGET_RUNTIME_STALE);
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_foreground_changed(
    vc_target_attestation *attestation,
    uint64_t foreground_sequence)
{
    return vc_target_foreground_event(
        attestation, foreground_sequence);
}

vc_target_status vc_target_attestation_foreground_lost(
    vc_target_attestation *attestation,
    uint64_t foreground_sequence)
{
    return vc_target_foreground_event(
        attestation, foreground_sequence);
}

vc_target_status vc_target_attestation_process_exit(
    vc_target_attestation *attestation,
    uint32_t process_id,
    uint64_t process_generation)
{
    vc_target_status status;
    bool process_matches;
    bool generation_matches;

    if (process_id == 0 || process_generation == 0) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    if (!attestation->snapshot_available) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_OK);
    }
    process_matches =
        attestation->snapshot.identity.process_id ==
            process_id;
    generation_matches =
        attestation->snapshot.identity.process_generation ==
            process_generation;
    if (process_matches) {
        vc_target_invalidate(
            attestation, VC_TARGET_RUNTIME_STALE);
    }
    vc_target_leave(attestation);
    return vc_target_result(
        attestation,
        process_matches && generation_matches
            ? VC_TARGET_STATUS_OK
            : VC_TARGET_STATUS_STALE_SNAPSHOT);
}

vc_target_status vc_target_attestation_module_changed(
    vc_target_attestation *attestation,
    uint32_t module_id,
    uint64_t previous_load_generation)
{
    vc_target_status status;
    uint32_t index;
    bool found = false;
    bool generation_matches = false;

    if (module_id == 0 ||
        previous_load_generation == 0) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    status = vc_target_enter(attestation, true);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    if (!attestation->snapshot_available) {
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_OK);
    }
    for (index = 0;
         index < attestation->snapshot.module_count;
         ++index) {
        if (attestation->snapshot.modules[index].module_id ==
            module_id) {
            found = true;
            generation_matches =
                attestation->snapshot.modules[index]
                    .load_generation ==
                previous_load_generation;
            break;
        }
    }
    if (found) {
        vc_target_invalidate(
            attestation, VC_TARGET_RUNTIME_STALE);
    }
    vc_target_leave(attestation);
    return vc_target_result(
        attestation,
        found && generation_matches
            ? VC_TARGET_STATUS_OK
            : VC_TARGET_STATUS_STALE_SNAPSHOT);
}

vc_target_status vc_target_attestation_plugin_unload(
    vc_target_attestation *attestation)
{
    vc_target_status status =
        vc_target_enter(attestation, true);

    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    vc_target_invalidate(
        attestation, VC_TARGET_RUNTIME_STALE);
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_reset(
    vc_target_attestation *attestation)
{
    vc_target_status status =
        vc_target_enter(attestation, true);

    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    if (!vc_target_next_nonzero(
            &attestation->lifecycle_generation)) {
        vc_target_invalidate(
            attestation, VC_TARGET_RUNTIME_ERROR);
        vc_target_leave(attestation);
        return vc_target_result(
            attestation, VC_TARGET_STATUS_BOUNDED_ERROR);
    }
    vc_target_clear_snapshot(attestation);
    attestation->last_foreground_sequence = 0;
    vc_target_set_status(
        attestation, VC_TARGET_RUNTIME_UNAVAILABLE);
    vc_target_leave(attestation);
    return vc_target_result(
        attestation, VC_TARGET_STATUS_OK);
}

vc_target_status vc_target_attestation_stop(
    vc_target_attestation *attestation)
{
    vc_target_status status =
        vc_target_enter(attestation, false);
    vc_target_cleanup_fn cleanup;
    void *context;
    bool cleanup_ok = true;
    bool generation_ok = true;

    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }

    vc_target_clear_snapshot(attestation);
    attestation->last_foreground_sequence = 0;
    if (attestation->phase != VC_TARGET_PHASE_STOPPED) {
        generation_ok = vc_target_next_nonzero(
            &attestation->lifecycle_generation);
    }
    attestation->phase = VC_TARGET_PHASE_STOPPED;
    vc_target_set_status(
        attestation, VC_TARGET_RUNTIME_STOPPED);
    cleanup = attestation->dependencies.cleanup;
    context = attestation->dependencies.context;

    if (cleanup != NULL) {
        vc_target_adapter_begin(attestation);
        cleanup_ok = cleanup(context);
        vc_target_adapter_end(attestation);
    }
    vc_target_leave(attestation);
    if (!cleanup_ok) {
        return vc_target_result(
            attestation, VC_TARGET_STATUS_CLEANUP_FAILED);
    }
    return vc_target_result(
        attestation,
        generation_ok
            ? VC_TARGET_STATUS_OK
            : VC_TARGET_STATUS_BOUNDED_ERROR);
}

vc_target_status vc_target_attestation_destroy(
    vc_target_attestation *attestation)
{
    vc_target_status status;

    if (!vc_target_valid(attestation)) {
        return VC_TARGET_STATUS_NOT_INITIALIZED;
    }
    status = vc_target_attestation_stop(attestation);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    status = vc_target_enter(attestation, false);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    memset(attestation, 0, sizeof(*attestation));
    return VC_TARGET_STATUS_OK;
}

vc_target_runtime_status vc_target_attestation_get_status(
    const vc_target_attestation *attestation)
{
    unsigned int status;

    if (!vc_target_valid(attestation)) {
        return VC_TARGET_RUNTIME_STOPPED;
    }
    status = atomic_load_explicit(
        &attestation->status, memory_order_acquire);
    if (status > (unsigned int)VC_TARGET_RUNTIME_ERROR) {
        return VC_TARGET_RUNTIME_ERROR;
    }
    return (vc_target_runtime_status)status;
}

vc_target_status vc_target_attestation_get_state(
    vc_target_attestation *attestation,
    vc_target_state *state)
{
    vc_target_status status;

    if (state == NULL) {
        return VC_TARGET_STATUS_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    status = vc_target_enter(attestation, false);
    if (status != VC_TARGET_STATUS_OK) {
        return status;
    }
    state->phase = attestation->phase;
    state->status =
        vc_target_attestation_get_status(attestation);
    state->last_result =
        (vc_target_status)atomic_load_explicit(
            &attestation->last_result,
            memory_order_acquire);
    state->snapshot_available =
        attestation->snapshot_available;
    state->snapshot_revision =
        attestation->snapshot_available
            ? attestation->snapshot.revision
            : 0;
    state->lifecycle_generation =
        attestation->lifecycle_generation;
    vc_target_leave(attestation);
    return VC_TARGET_STATUS_OK;
}

size_t vc_target_attestation_format_status(
    const vc_target_attestation *attestation,
    char *buffer,
    size_t capacity)
{
    const char *text;
    vc_target_status result;
    size_t length;
    size_t copy_size;

    if (!vc_target_valid(attestation)) {
        text = "Target attestation not initialized";
    } else {
        result = (vc_target_status)atomic_load_explicit(
            &attestation->last_result,
            memory_order_acquire);
        switch (result) {
        case VC_TARGET_STATUS_OK:
            switch (vc_target_attestation_get_status(
                        attestation)) {
            case VC_TARGET_RUNTIME_STOPPED:
                text = "Target attestation stopped";
                break;
            case VC_TARGET_RUNTIME_UNAVAILABLE:
                text = "Target snapshot unavailable";
                break;
            case VC_TARGET_RUNTIME_REFRESHING:
                text = "Refreshing target snapshot";
                break;
            case VC_TARGET_RUNTIME_AVAILABLE:
                text = "Target snapshot available";
                break;
            case VC_TARGET_RUNTIME_STALE:
                text = "Target snapshot stale";
                break;
            case VC_TARGET_RUNTIME_ERROR:
            default:
                text = "Target attestation failed";
                break;
            }
            break;
        case VC_TARGET_STATUS_UNAVAILABLE:
            text = "Target snapshot unavailable";
            break;
        case VC_TARGET_STATUS_INVALID_ARGUMENT:
            text = "Target request invalid";
            break;
        case VC_TARGET_STATUS_NOT_INITIALIZED:
            text = "Target attestation not initialized";
            break;
        case VC_TARGET_STATUS_STOPPED:
            text = "Target attestation stopped";
            break;
        case VC_TARGET_STATUS_BUSY:
            text = "Target attestation busy";
            break;
        case VC_TARGET_STATUS_STALE_SNAPSHOT:
            text = "Target snapshot stale";
            break;
        case VC_TARGET_STATUS_MUTATED_SNAPSHOT:
            text = "Target changed during refresh";
            break;
        case VC_TARGET_STATUS_INVALID_IDENTITY:
            text = "Target identity invalid";
            break;
        case VC_TARGET_STATUS_INVALID_TITLE:
            text = "Target title mismatch";
            break;
        case VC_TARGET_STATUS_INVALID_VERSION:
            text = "Target version mismatch";
            break;
        case VC_TARGET_STATUS_INVALID_FINGERPRINT:
            text = "Target fingerprint mismatch";
            break;
        case VC_TARGET_STATUS_INVALID_MODULE:
            text = "Target module mismatch";
            break;
        case VC_TARGET_STATUS_INVALID_SEGMENT:
            text = "Target segment invalid";
            break;
        case VC_TARGET_STATUS_INVALID_THREAD:
            text = "Target thread invalid";
            break;
        case VC_TARGET_STATUS_OVERFLOW:
            text = "Target range overflow";
            break;
        case VC_TARGET_STATUS_OVERLAP:
            text = "Target segments overlap";
            break;
        case VC_TARGET_STATUS_DUPLICATE:
            text = "Target facts duplicated";
            break;
        case VC_TARGET_STATUS_LIMIT:
            text = "Target facts exceed limit";
            break;
        case VC_TARGET_STATUS_UNKNOWN_PERMISSION:
            text = "Target permissions invalid";
            break;
        case VC_TARGET_STATUS_PERMISSION_DENIED:
            text = "Target permission denied";
            break;
        case VC_TARGET_STATUS_UNMAPPED_RANGE:
            text = "Target range unmapped";
            break;
        case VC_TARGET_STATUS_CROSS_SEGMENT_RANGE:
            text = "Target range crosses segment";
            break;
        case VC_TARGET_STATUS_ADAPTER_FAILURE:
            text = "Target adapter failed";
            break;
        case VC_TARGET_STATUS_SEQUENCE_ROLLBACK:
            text = "Target sequence rolled back";
            break;
        case VC_TARGET_STATUS_POLICY_MISMATCH:
            text = "Target policy mismatch";
            break;
        case VC_TARGET_STATUS_PROTECTED_THREAD:
            text = "Protected thread rejected";
            break;
        case VC_TARGET_STATUS_CLEANUP_FAILED:
            text = "Target cleanup failed";
            break;
        case VC_TARGET_STATUS_BOUNDED_ERROR:
        default:
            text = "Target bounded state exhausted";
            break;
        }
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
