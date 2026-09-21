#ifndef VITACHEAT_TARGET_ATTESTATION_H
#define VITACHEAT_TARGET_ATTESTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_TARGET_TITLE_ID_MAX UINT32_C(16)
#define VC_TARGET_VERSION_MAX UINT32_C(32)
#define VC_TARGET_FINGERPRINT_MAX UINT32_C(64)
#define VC_TARGET_MAX_MODULES UINT32_C(16)
#define VC_TARGET_MAX_SEGMENTS UINT32_C(8)
#define VC_TARGET_MAX_THREADS UINT32_C(64)
#define VC_TARGET_REFRESH_ATTEMPTS UINT32_C(3)
#define VC_TARGET_INITIALIZER {0}

#define VC_TARGET_PERMISSION_READ UINT32_C(0x01)
#define VC_TARGET_PERMISSION_WRITE UINT32_C(0x02)
#define VC_TARGET_PERMISSION_EXECUTE UINT32_C(0x04)
#define VC_TARGET_PERMISSION_ALL                                      \
    (VC_TARGET_PERMISSION_READ | VC_TARGET_PERMISSION_WRITE |         \
     VC_TARGET_PERMISSION_EXECUTE)

/*
 * Role flags are opaque adapter facts. The portable layer never assigns
 * gameplay meaning to them and rejects values outside this bounded namespace.
 */
#define VC_TARGET_THREAD_ROLE_FLAGS_MAX UINT32_C(0x0000ffff)

typedef enum vc_target_status {
    VC_TARGET_STATUS_OK = 0,
    VC_TARGET_STATUS_UNAVAILABLE = 1,
    VC_TARGET_STATUS_INVALID_ARGUMENT = -1,
    VC_TARGET_STATUS_NOT_INITIALIZED = -2,
    VC_TARGET_STATUS_STOPPED = -3,
    VC_TARGET_STATUS_BUSY = -4,
    VC_TARGET_STATUS_STALE_SNAPSHOT = -5,
    VC_TARGET_STATUS_MUTATED_SNAPSHOT = -6,
    VC_TARGET_STATUS_INVALID_IDENTITY = -7,
    VC_TARGET_STATUS_INVALID_TITLE = -8,
    VC_TARGET_STATUS_INVALID_VERSION = -9,
    VC_TARGET_STATUS_INVALID_FINGERPRINT = -10,
    VC_TARGET_STATUS_INVALID_MODULE = -11,
    VC_TARGET_STATUS_INVALID_SEGMENT = -12,
    VC_TARGET_STATUS_INVALID_THREAD = -13,
    VC_TARGET_STATUS_OVERFLOW = -14,
    VC_TARGET_STATUS_OVERLAP = -15,
    VC_TARGET_STATUS_DUPLICATE = -16,
    VC_TARGET_STATUS_LIMIT = -17,
    VC_TARGET_STATUS_UNKNOWN_PERMISSION = -18,
    VC_TARGET_STATUS_PERMISSION_DENIED = -19,
    VC_TARGET_STATUS_UNMAPPED_RANGE = -20,
    VC_TARGET_STATUS_CROSS_SEGMENT_RANGE = -21,
    VC_TARGET_STATUS_ADAPTER_FAILURE = -22,
    VC_TARGET_STATUS_SEQUENCE_ROLLBACK = -23,
    VC_TARGET_STATUS_POLICY_MISMATCH = -24,
    VC_TARGET_STATUS_PROTECTED_THREAD = -25,
    VC_TARGET_STATUS_CLEANUP_FAILED = -26,
    VC_TARGET_STATUS_BOUNDED_ERROR = -27
} vc_target_status;

typedef enum vc_target_phase {
    VC_TARGET_PHASE_UNINITIALIZED = 0,
    VC_TARGET_PHASE_INITIALIZED = 1,
    VC_TARGET_PHASE_RUNNING = 2,
    VC_TARGET_PHASE_STOPPED = 3
} vc_target_phase;

typedef enum vc_target_runtime_status {
    VC_TARGET_RUNTIME_STOPPED = 0,
    VC_TARGET_RUNTIME_UNAVAILABLE = 1,
    VC_TARGET_RUNTIME_REFRESHING = 2,
    VC_TARGET_RUNTIME_AVAILABLE = 3,
    VC_TARGET_RUNTIME_STALE = 4,
    VC_TARGET_RUNTIME_ERROR = 5
} vc_target_runtime_status;

typedef struct vc_target_identity {
    uint32_t process_id;
    uint32_t title_id_size;
    uint64_t process_generation;
    uint64_t foreground_sequence;
    uint32_t version_size;
    uint32_t fingerprint_algorithm;
    uint32_t fingerprint_size;
    uint32_t reserved0;
    uint8_t title_id[VC_TARGET_TITLE_ID_MAX];
    uint8_t version[VC_TARGET_VERSION_MAX];
    uint8_t fingerprint[VC_TARGET_FINGERPRINT_MAX];
} vc_target_identity;

typedef struct vc_target_segment {
    uint32_t segment_index;
    uint32_t base;
    uint32_t size;
    uint32_t permissions;
    uint32_t flags;
    uint32_t reserved0;
} vc_target_segment;

typedef struct vc_target_module {
    uint32_t module_id;
    uint32_t segment_count;
    uint64_t load_generation;
    vc_target_segment segments[VC_TARGET_MAX_SEGMENTS];
} vc_target_module;

typedef struct vc_target_thread {
    int32_t thread_id;
    uint32_t process_id;
    uint64_t process_generation;
    uint32_t role_flags;
    uint32_t reserved0;
} vc_target_thread;

typedef struct vc_target_snapshot {
    uint64_t revision;
    uint64_t lifecycle_generation;
    vc_target_identity identity;
    uint32_t module_count;
    uint32_t thread_count;
    vc_target_module modules[VC_TARGET_MAX_MODULES];
    vc_target_thread threads[VC_TARGET_MAX_THREADS];
} vc_target_snapshot;

typedef struct vc_target_module_binding {
    uint32_t module_id;
    uint32_t reserved0;
    uint64_t load_generation;
} vc_target_module_binding;

typedef struct vc_target_runtime_binding {
    uint32_t process_id;
    uint32_t title_id_size;
    uint64_t process_generation;
    uint64_t foreground_sequence;
    uint32_t module_id;
    uint32_t reserved0;
    uint64_t module_load_generation;
    uint8_t title_id[VC_TARGET_TITLE_ID_MAX];
} vc_target_runtime_binding;

/*
 * A zero version_size means that version display metadata is not constrained.
 * A zero fingerprint_size and algorithm means that fingerprint identity is not
 * constrained. Nonzero fields always require an exact match.
 */
typedef struct vc_target_policy {
    uint32_t title_id_size;
    uint32_t version_size;
    uint32_t fingerprint_algorithm;
    uint32_t fingerprint_size;
    uint32_t module_count;
    uint32_t reserved0;
    uint8_t title_id[VC_TARGET_TITLE_ID_MAX];
    uint8_t version[VC_TARGET_VERSION_MAX];
    uint8_t fingerprint[VC_TARGET_FINGERPRINT_MAX];
    vc_target_module_binding modules[VC_TARGET_MAX_MODULES];
} vc_target_policy;

/*
 * Symbolic output for a checked range. It deliberately omits absolute
 * addresses and contains no dereferenceable pointer.
 */
typedef struct vc_target_range {
    uint64_t snapshot_revision;
    uint32_t module_id;
    uint32_t segment_index;
    uint64_t module_load_generation;
    uint32_t segment_offset;
    uint32_t length;
    uint32_t permissions;
    uint32_t reserved0;
} vc_target_range;

/*
 * begin_snapshot starts one trusted, coherent collection and returns a
 * nonzero mutation token plus bounded counts. read_module/read_thread may be
 * called only for indices below those returned counts. end_snapshot must
 * always be called after a successful begin, even when a read or validation
 * fails. A different completion token means the adapter mutated during
 * collection and permits only VC_TARGET_REFRESH_ATTEMPTS total attempts.
 */
typedef bool (*vc_target_begin_snapshot_fn)(
    void *context,
    vc_target_identity *identity,
    uint32_t *module_count,
    uint32_t *thread_count,
    uint64_t *mutation_token);

typedef bool (*vc_target_read_module_fn)(
    void *context,
    uint64_t mutation_token,
    uint32_t module_index,
    vc_target_module *module);

typedef bool (*vc_target_read_thread_fn)(
    void *context,
    uint64_t mutation_token,
    uint32_t thread_index,
    vc_target_thread *thread);

typedef bool (*vc_target_end_snapshot_fn)(
    void *context,
    uint64_t mutation_token,
    uint64_t *completion_token);

typedef bool (*vc_target_cleanup_fn)(void *context);

typedef struct vc_target_dependencies {
    vc_target_begin_snapshot_fn begin_snapshot;
    vc_target_read_module_fn read_module;
    vc_target_read_thread_fn read_thread;
    vc_target_end_snapshot_fn end_snapshot;
    vc_target_cleanup_fn cleanup;
    void *context;
} vc_target_dependencies;

typedef struct vc_target_state {
    uint32_t phase;
    vc_target_runtime_status status;
    vc_target_status last_result;
    bool snapshot_available;
    uint64_t snapshot_revision;
    uint64_t lifecycle_generation;
} vc_target_state;

typedef struct vc_target_attestation {
    vc_target_dependencies dependencies;
    vc_target_snapshot snapshot;
    atomic_uint transaction_busy;
    atomic_uint adapter_active;
    atomic_uint status;
    atomic_int last_result;
    uint64_t next_revision;
    uint64_t lifecycle_generation;
    uint64_t last_foreground_sequence;
    uint32_t phase;
    uint32_t marker;
    bool snapshot_available;
} vc_target_attestation;

/*
 * The object must start as VC_TARGET_INITIALIZER. Repeating init with the
 * identical dependency set is idempotent; a different set is rejected.
 */
vc_target_status vc_target_attestation_init(
    vc_target_attestation *attestation,
    const vc_target_dependencies *dependencies);

vc_target_status vc_target_attestation_start(
    vc_target_attestation *attestation);

/*
 * Copies adapter output into private storage, completes and validates the
 * entire candidate, then publishes one new revision. Failure never partially
 * changes the last valid snapshot.
 */
vc_target_status vc_target_attestation_refresh(
    vc_target_attestation *attestation);

vc_target_status vc_target_attestation_get_snapshot(
    vc_target_attestation *attestation,
    vc_target_snapshot *snapshot);

vc_target_status vc_target_attestation_match_policy(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    const vc_target_policy *policy);

vc_target_status vc_target_attestation_find_segment(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    uint32_t module_id,
    uint64_t module_load_generation,
    uint32_t segment_index,
    vc_target_segment *segment);

vc_target_status vc_target_attestation_resolve_range(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    uint32_t module_id,
    uint64_t module_load_generation,
    uint32_t address,
    uint32_t length,
    uint32_t required_permissions,
    vc_target_range *range);

/*
 * Both sets are explicit inputs. They must be sorted, unique, positive IDs.
 * No descriptor name, role flag, priority, index, or ordering heuristic is
 * used to select gameplay threads.
 */
vc_target_status vc_target_attestation_validate_threads(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    const int32_t *gameplay_thread_ids,
    size_t gameplay_thread_count,
    const int32_t *protected_thread_ids,
    size_t protected_thread_count);

/*
 * One serialized query for the menu coordinator's optional adapter bridge.
 * It exact-matches launch/runtime identity and module instance before
 * validating the explicit gameplay and protected thread sets.
 */
vc_target_status vc_target_attestation_validate_allowlist(
    vc_target_attestation *attestation,
    uint64_t snapshot_revision,
    const vc_target_runtime_binding *binding,
    const int32_t *gameplay_thread_ids,
    size_t gameplay_thread_count,
    const int32_t *protected_thread_ids,
    size_t protected_thread_count);

vc_target_status vc_target_attestation_foreground_changed(
    vc_target_attestation *attestation,
    uint64_t foreground_sequence);

vc_target_status vc_target_attestation_foreground_lost(
    vc_target_attestation *attestation,
    uint64_t foreground_sequence);

vc_target_status vc_target_attestation_process_exit(
    vc_target_attestation *attestation,
    uint32_t process_id,
    uint64_t process_generation);

vc_target_status vc_target_attestation_module_changed(
    vc_target_attestation *attestation,
    uint32_t module_id,
    uint64_t previous_load_generation);

vc_target_status vc_target_attestation_plugin_unload(
    vc_target_attestation *attestation);

vc_target_status vc_target_attestation_reset(
    vc_target_attestation *attestation);

/*
 * Stop scrubs every published identity, fingerprint, module, segment, and
 * thread fact before optional adapter cleanup. Cleanup failure is returned
 * after the service is already stopped. Repeated stop calls are safe.
 */
vc_target_status vc_target_attestation_stop(
    vc_target_attestation *attestation);

vc_target_status vc_target_attestation_destroy(
    vc_target_attestation *attestation);

vc_target_runtime_status vc_target_attestation_get_status(
    const vc_target_attestation *attestation);

vc_target_status vc_target_attestation_get_state(
    vc_target_attestation *attestation,
    vc_target_state *state);

/*
 * Returns the complete text length excluding NUL. Output is bounded and never
 * contains addresses, PIDs, generations, title/build/fingerprint bytes,
 * thread IDs, module IDs, segment indices, or revision values.
 */
size_t vc_target_attestation_format_status(
    const vc_target_attestation *attestation,
    char *buffer,
    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
