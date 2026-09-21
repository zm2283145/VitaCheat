#ifndef VITACHEAT_LEGACY_PLAN_H
#define VITACHEAT_LEGACY_PLAN_H

#include "vitacheat/legacy_psv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_PSV_PLAN_SCHEMA_VERSION UINT32_C(2)
#define VC_PSV_DEFAULT_ACTION_LIMIT ((size_t)4096)
#define VC_PSV_MAX_ACTION_LIMIT ((size_t)65536)
#define VC_PSV_POINTER_MAX_LEVELS ((size_t)8)
#define VC_PSV_POINTER_CANONICAL_MAX_LEVELS ((size_t)5)
#define VC_PSV_MAX_READ_LIMIT \
    (VC_PSV_MAX_ACTION_LIMIT * \
     (VC_PSV_POINTER_MAX_LEVELS * (size_t)2 + (size_t)1))

typedef enum vc_psv_compile_compatibility {
    VC_PSV_COMPILE_STRICT = 0,
    VC_PSV_COMPILE_ALLOW_INLINE_COMMENTS = 1u << 0,
    VC_PSV_COMPILE_ALLOW_NONCANONICAL_REPEAT = 1u << 1,
    VC_PSV_COMPILE_ALLOW_PATCH_U8 = 1u << 2,
    VC_PSV_COMPILE_ALLOW_POINTER_LEVELS_6_8 = 1u << 3,
    VC_PSV_COMPILE_ALLOW_POINTER_TERMINAL_MARKERS = 1u << 4,
    VC_PSV_COMPILE_ALLOW_POINTER_U32_GAP = 1u << 5,
    VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH = 1u << 6
} vc_psv_compile_compatibility;

typedef enum vc_psv_compile_status {
    VC_PSV_COMPILE_OK = 0,
    VC_PSV_COMPILE_INVALID_ARGUMENT = -1,
    VC_PSV_COMPILE_OUTPUT_TOO_SMALL = -2,
    VC_PSV_COMPILE_UNSUPPORTED = -3,
    VC_PSV_COMPILE_MALFORMED = -4,
    VC_PSV_COMPILE_NONCANONICAL = -5,
    VC_PSV_COMPILE_ACTION_LIMIT = -6
} vc_psv_compile_status;

typedef enum vc_psv_width {
    VC_PSV_WIDTH_U8 = 1,
    VC_PSV_WIDTH_U16 = 2,
    VC_PSV_WIDTH_U32 = 4
} vc_psv_width;

typedef enum vc_psv_plan_node_kind {
    VC_PSV_PLAN_WRITE = 0,
    VC_PSV_PLAN_MOVE,
    VC_PSV_PLAN_REPEAT,
    VC_PSV_PLAN_PATCH,
    VC_PSV_PLAN_BUTTON_GATE,
    VC_PSV_PLAN_CONDITION_GATE,
    VC_PSV_PLAN_POINTER_WRITE,
    VC_PSV_PLAN_POINTER_REPEAT,
    VC_PSV_PLAN_POINTER_MOVE
} vc_psv_plan_node_kind;

typedef enum vc_psv_plan_diagnostic {
    VC_PSV_PLAN_DIAGNOSTIC_NONE = 0,
    VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL = 1u << 0,
    VC_PSV_PLAN_DIAGNOSTIC_POINTER_TERMINAL_MARKER = 1u << 1,
    VC_PSV_PLAN_DIAGNOSTIC_POINTER_REPEAT_MARKER = 1u << 2,
    VC_PSV_PLAN_DIAGNOSTIC_POINTER_U32_GAP = 1u << 3,
    VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_WIDTH = 1u << 4,
    VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_LEVEL = 1u << 5,
    VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_MARKER = 1u << 6
} vc_psv_plan_diagnostic;

typedef enum vc_psv_relation {
    VC_PSV_RELATION_EQUAL = 0,
    VC_PSV_RELATION_NOT_EQUAL,
    VC_PSV_RELATION_UNSIGNED_GREATER,
    VC_PSV_RELATION_UNSIGNED_LESS
} vc_psv_relation;

typedef struct vc_psv_plan_address {
    vc_psv_address_mode mode;
    uint32_t value;
    uint8_t module_serial;
    uint8_t segment_index;
} vc_psv_plan_address;

typedef struct vc_psv_pointer_read_dependency {
    vc_psv_width width;
    uint32_t offset;
} vc_psv_pointer_read_dependency;

/*
 * A path is an immutable symbolic dependency chain. Evaluation resolves base,
 * then performs each declared U32 read and adds that dependency's offset with
 * modulo-2^32 legacy arithmetic.
 */
typedef struct vc_psv_pointer_path {
    vc_psv_plan_address base;
    vc_psv_pointer_read_dependency reads[VC_PSV_POINTER_MAX_LEVELS];
    uint8_t level_count;
} vc_psv_pointer_path;

/*
 * A node owns one complete physical-record span. The fields unused by a node
 * kind are zero. B2 selectors are compile-time state and do not become nodes;
 * scalar addresses and pointer starting bases carry the active selector.
 */
typedef struct vc_psv_plan_node {
    vc_psv_plan_node_kind kind;
    vc_psv_width width;
    vc_psv_relation relation;
    uint32_t flags;
    uint32_t diagnostics;
    size_t physical_first;
    size_t physical_count;
    vc_psv_plan_address destination;
    vc_psv_plan_address source;
    uint32_t value;
    uint32_t repeat_count;
    uint32_t address_gap;
    uint32_t value_increment;
    uint32_t button_mode;
    uint32_t button_mask;
    vc_psv_pointer_path destination_pointer;
    vc_psv_pointer_path source_pointer;
    uint8_t pointer_increment_selector;
    uint8_t observed_source_width_index;
    uint8_t observed_source_level_count;
    uint8_t skip_records;
} vc_psv_plan_node;

typedef struct vc_psv_compile_options {
    uint32_t compatibility;
    size_t action_limit;
} vc_psv_compile_options;

typedef struct vc_psv_plan {
    uint32_t schema_version;
    size_t physical_record_count;
    size_t node_count;
    size_t maximum_actions;
    size_t maximum_memory_reads;
    size_t compatibility_diagnostics;
} vc_psv_plan;

/*
 * Compiles one fully imported descriptor without allocation. Malformed spans,
 * unknown records, unapproved compatibility syntax, and gates whose target
 * splits any positional multi-record family reject the whole plan.
 * action_limit is symbolic: scalar and pointer repeats remain one node, but
 * their maximum action and read expansion is bounded before evaluation. A
 * NULL options pointer selects strict mode and VC_PSV_DEFAULT_ACTION_LIMIT.
 *
 * On OUTPUT_TOO_SMALL, plan reports the required node_count and stores none.
 * On every other non-argument result, plan remains a diagnostic summary and
 * nodes are cleared.
 */
vc_psv_compile_status vc_psv_compile_cheat(
    const vc_psv_cheat *cheat,
    const vc_psv_operation *operations,
    size_t operation_count,
    const vc_psv_compile_options *options,
    vc_psv_plan_node *nodes,
    size_t node_capacity,
    vc_psv_plan *plan);

typedef bool (*vc_psv_resolve_module_base_fn)(void *context,
                                               uint8_t module_serial,
                                               uint8_t segment_index,
                                               uint32_t *base);
typedef bool (*vc_psv_read_memory_fn)(void *context,
                                      uint32_t address,
                                      uint8_t *bytes,
                                      size_t size);
typedef bool (*vc_psv_read_buttons_fn)(void *context,
                                       uint32_t mode,
                                       uint32_t *normalized_mask);

typedef struct vc_psv_evaluation_callbacks {
    void *context;
    vc_psv_resolve_module_base_fn resolve_module_base;
    vc_psv_read_memory_fn read_memory;
    vc_psv_read_buttons_fn read_buttons;
} vc_psv_evaluation_callbacks;

typedef enum vc_psv_action_kind {
    VC_PSV_ACTION_WRITE = 0,
    VC_PSV_ACTION_PATCH
} vc_psv_action_kind;

typedef struct vc_psv_action {
    vc_psv_action_kind kind;
    vc_psv_width width;
    uint32_t address;
    uint8_t bytes[4];
    uint8_t original_bytes[4];
    size_t source_node_index;
} vc_psv_action;

typedef enum vc_psv_evaluate_status {
    VC_PSV_EVALUATE_OK = 0,
    VC_PSV_EVALUATE_INVALID_ARGUMENT = -1,
    VC_PSV_EVALUATE_INVALID_PLAN = -2,
    VC_PSV_EVALUATE_OUTPUT_TOO_SMALL = -3,
    VC_PSV_EVALUATE_CALLBACK_FAILED = -4,
    VC_PSV_EVALUATE_ADDRESS_OVERFLOW = -5,
    VC_PSV_EVALUATE_INVALID_POINTER = -6
} vc_psv_evaluate_status;

typedef struct vc_psv_evaluation_report {
    size_t action_count;
    size_t skipped_physical_records;
    size_t module_resolutions;
    size_t memory_reads;
    size_t button_reads;
} vc_psv_evaluation_report;

/*
 * Resolves gates and address snapshots into concrete little-endian actions.
 * It never writes memory. The complete plan, callback requirements, and worst
 * case output capacity are validated before the first callback. Failed
 * evaluation clears partial actions and reports zero action_count.
 */
vc_psv_evaluate_status vc_psv_evaluate_plan(
    const vc_psv_plan *plan,
    const vc_psv_plan_node *nodes,
    const vc_psv_evaluation_callbacks *callbacks,
    vc_psv_action *actions,
    size_t action_capacity,
    vc_psv_evaluation_report *report);

typedef bool (*vc_psv_write_memory_fn)(void *context,
                                       uint32_t address,
                                       const uint8_t *bytes,
                                       size_t size);

typedef struct vc_psv_write_callbacks {
    void *context;
    vc_psv_write_memory_fn write_memory;
} vc_psv_write_callbacks;

typedef struct vc_psv_patch_ledger_entry {
    uint32_t address;
    vc_psv_width width;
    uint8_t original_bytes[4];
    size_t source_node_index;
    bool active;
} vc_psv_patch_ledger_entry;

typedef enum vc_psv_execute_status {
    VC_PSV_EXECUTE_OK = 0,
    VC_PSV_EXECUTE_INVALID_ARGUMENT = -1,
    VC_PSV_EXECUTE_INVALID_ACTION = -2,
    VC_PSV_EXECUTE_LEDGER_TOO_SMALL = -3,
    VC_PSV_EXECUTE_CALLBACK_FAILED = -4
} vc_psv_execute_status;

typedef struct vc_psv_apply_report {
    size_t applied_actions;
    size_t active_patch_entries;
} vc_psv_apply_report;

typedef struct vc_psv_rollback_report {
    size_t restored_entries;
    size_t remaining_entries;
} vc_psv_rollback_report;

/*
 * Applies pre-evaluated actions in order. Each successful patch records its
 * original bytes in the caller-owned ledger. A callback failure may leave
 * ordinary writes applied; active patch entries remain explicitly restorable.
 * Callback writes must be atomic for the supplied 1, 2, or 4 byte size.
 */
vc_psv_execute_status vc_psv_apply_actions(
    const vc_psv_action *actions,
    size_t action_count,
    const vc_psv_write_callbacks *callbacks,
    vc_psv_patch_ledger_entry *ledger,
    size_t ledger_capacity,
    vc_psv_apply_report *report);

/*
 * Restores active patch entries in reverse application order. Successfully
 * restored entries are deactivated; a failed entry and all earlier entries
 * remain active so the caller can retry.
 */
vc_psv_execute_status vc_psv_rollback_patches(
    vc_psv_patch_ledger_entry *ledger,
    size_t ledger_count,
    const vc_psv_write_callbacks *callbacks,
    vc_psv_rollback_report *report);

#ifdef __cplusplus
}
#endif

#endif
