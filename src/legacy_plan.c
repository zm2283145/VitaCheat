#include "vitacheat/legacy_plan.h"

#include <limits.h>
#include <string.h>

#if !defined(UINTPTR_MAX)
#error "VitaCheat legacy planning requires an implementation with uintptr_t"
#endif

typedef char vc_psv_plan_uintptr_must_cover_size_t[
    UINTPTR_MAX >= SIZE_MAX ? 1 : -1];

static bool multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool ranges_overlap(const void *left,
                           size_t left_size,
                           const void *right,
                           size_t right_size)
{
    uintptr_t left_start;
    uintptr_t right_start;

    if (left == NULL || right == NULL || left_size == 0 || right_size == 0) {
        return false;
    }
    left_start = (uintptr_t)left;
    right_start = (uintptr_t)right;
    if (left_start > UINTPTR_MAX - left_size ||
        right_start > UINTPTR_MAX - right_size) {
        return true;
    }
    return left_start < right_start + right_size &&
           right_start < left_start + left_size;
}

static bool valid_width(vc_psv_width width)
{
    return width == VC_PSV_WIDTH_U8 ||
           width == VC_PSV_WIDTH_U16 ||
           width == VC_PSV_WIDTH_U32;
}

static vc_psv_width width_from_index(uint16_t index)
{
    switch (index) {
    case 0:
        return VC_PSV_WIDTH_U8;
    case 1:
        return VC_PSV_WIDTH_U16;
    default:
        return VC_PSV_WIDTH_U32;
    }
}

static bool add_size(size_t left, size_t right, size_t *result)
{
    if (right > SIZE_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static uint32_t required_compatibility(uint32_t operation_flags)
{
    uint32_t required = 0;

    if ((operation_flags & VC_PSV_OPERATION_FLAG_INLINE_COMMENT) != 0) {
        required |= VC_PSV_COMPILE_ALLOW_INLINE_COMMENTS;
    }
    if ((operation_flags &
         VC_PSV_OPERATION_FLAG_NONCANONICAL_REPEAT) != 0) {
        required |= VC_PSV_COMPILE_ALLOW_NONCANONICAL_REPEAT;
    }
    if ((operation_flags &
         VC_PSV_OPERATION_FLAG_NONCANONICAL_PATCH_U8) != 0) {
        required |= VC_PSV_COMPILE_ALLOW_PATCH_U8;
    }
    return required;
}

static bool valid_operation_flags(uint32_t flags)
{
    const uint32_t known = VC_PSV_OPERATION_FLAG_INLINE_COMMENT |
                           VC_PSV_OPERATION_FLAG_NONCANONICAL_REPEAT |
                           VC_PSV_OPERATION_FLAG_NONCANONICAL_PATCH_U8 |
                           VC_PSV_OPERATION_FLAG_LOWERCASE_HEX;
    return (flags & ~known) == 0;
}

static bool valid_compile_compatibility(uint32_t compatibility)
{
    const uint32_t known = VC_PSV_COMPILE_ALLOW_INLINE_COMMENTS |
                           VC_PSV_COMPILE_ALLOW_NONCANONICAL_REPEAT |
                           VC_PSV_COMPILE_ALLOW_PATCH_U8 |
                           VC_PSV_COMPILE_ALLOW_POINTER_LEVELS_6_8 |
                           VC_PSV_COMPILE_ALLOW_POINTER_TERMINAL_MARKERS |
                           VC_PSV_COMPILE_ALLOW_POINTER_U32_GAP |
                           VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH |
                           VC_PSV_COMPILE_ALLOW_NONCANONICAL_HEADER;
    return (compatibility & ~known) == 0;
}

static uint32_t plan_diagnostics_from_import(uint32_t diagnostics)
{
    uint32_t result = VC_PSV_PLAN_DIAGNOSTIC_NONE;

    if ((diagnostics & VC_PSV_DIAGNOSTIC_LOWERCASE_HEX) != 0) {
        result |= VC_PSV_PLAN_DIAGNOSTIC_LOWERCASE_HEX;
    }
    if ((diagnostics &
         VC_PSV_DIAGNOSTIC_RUNTIME_DEPENDENT_ADDRESS) != 0) {
        result |= VC_PSV_PLAN_DIAGNOSTIC_RUNTIME_DEPENDENT_ADDRESS;
    }
    return result;
}

static bool is_canonical_repeat(uint16_t code)
{
    return code == UINT16_C(0x4001) ||
           code == UINT16_C(0x4101) ||
           code == UINT16_C(0x4201);
}

static bool is_compatible_repeat(uint16_t code)
{
    return code == UINT16_C(0x4000) ||
           code == UINT16_C(0x4002) ||
           code == UINT16_C(0x4100) ||
           code == UINT16_C(0x4200) ||
           code == UINT16_C(0x4202) ||
           code == UINT16_C(0x4210);
}

static bool operation_shape_is_valid(const vc_psv_operation *operation)
{
    const uint16_t code = operation->legacy_code;

    switch (operation->kind) {
    case VC_PSV_OPERATION_OPAQUE:
        return true;
    case VC_PSV_OPERATION_WRITE_U8:
        return code == UINT16_C(0x0000);
    case VC_PSV_OPERATION_WRITE_U16:
        return code == UINT16_C(0x0100);
    case VC_PSV_OPERATION_WRITE_U32:
        return code == UINT16_C(0x0200);
    case VC_PSV_OPERATION_SELECT_MODULE_BASE:
        return (code & UINT16_C(0xff00)) == UINT16_C(0xb200);
    case VC_PSV_OPERATION_MOVE:
        return code == UINT16_C(0x5000) ||
               code == UINT16_C(0x5100) ||
               code == UINT16_C(0x5200);
    case VC_PSV_OPERATION_REPEAT:
        return is_canonical_repeat(code) || is_compatible_repeat(code);
    case VC_PSV_OPERATION_REPEAT_CONTINUATION:
    case VC_PSV_OPERATION_POINTER_POSITIONAL:
        return true;
    case VC_PSV_OPERATION_POINTER_WRITE:
        return (code & UINT16_C(0xf000)) == UINT16_C(0x3000);
    case VC_PSV_OPERATION_POINTER_REPEAT:
        return (code & UINT16_C(0xf000)) == UINT16_C(0x7000);
    case VC_PSV_OPERATION_POINTER_MOVE:
        return (code & UINT16_C(0xf000)) == UINT16_C(0x8000);
    case VC_PSV_OPERATION_PATCH:
        return code == UINT16_C(0xa000) ||
               code == UINT16_C(0xa100) ||
               code == UINT16_C(0xa200);
    case VC_PSV_OPERATION_BUTTON_GATE:
        return (code & UINT16_C(0xff00)) == UINT16_C(0xc200);
    case VC_PSV_OPERATION_CONDITION_GATE:
        return (code & UINT16_C(0xf000)) == UINT16_C(0xd000) &&
               ((code >> 8u) & UINT16_C(0x000f)) <= UINT16_C(0x000b);
    default:
        return false;
    }
}

static bool operation_flags_match_shape(const vc_psv_operation *operation)
{
    const bool repeat_flag =
        (operation->flags &
         VC_PSV_OPERATION_FLAG_NONCANONICAL_REPEAT) != 0;
    const bool patch_u8_flag =
        (operation->flags &
         VC_PSV_OPERATION_FLAG_NONCANONICAL_PATCH_U8) != 0;

    if (operation->kind == VC_PSV_OPERATION_REPEAT) {
        if (repeat_flag != is_compatible_repeat(operation->legacy_code)) {
            return false;
        }
    } else if (repeat_flag) {
        return false;
    }
    if (operation->kind == VC_PSV_OPERATION_PATCH) {
        if (patch_u8_flag !=
            (operation->legacy_code == UINT16_C(0xa000))) {
            return false;
        }
    } else if (patch_u8_flag) {
        return false;
    }
    return true;
}

static vc_psv_width operation_width(const vc_psv_operation *operation)
{
    switch (operation->kind) {
    case VC_PSV_OPERATION_WRITE_U8:
        return VC_PSV_WIDTH_U8;
    case VC_PSV_OPERATION_WRITE_U16:
        return VC_PSV_WIDTH_U16;
    case VC_PSV_OPERATION_WRITE_U32:
        return VC_PSV_WIDTH_U32;
    default:
        return width_from_index(
            (uint16_t)((operation->legacy_code >> 8u) & UINT16_C(0x000f)));
    }
}

static vc_psv_plan_address make_address(uint32_t value,
                                        bool relative,
                                        uint8_t module_serial,
                                        uint8_t segment_index)
{
    vc_psv_plan_address result;

    memset(&result, 0, sizeof(result));
    result.mode = relative
                      ? VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE
                      : VC_PSV_ADDRESS_ABSOLUTE;
    result.value = value;
    if (relative) {
        result.module_serial = module_serial;
        result.segment_index = segment_index;
    }
    return result;
}

static bool address_snapshot_matches(const vc_psv_operation *operation,
                                     bool relative,
                                     uint8_t module_serial,
                                     uint8_t segment_index)
{
    if (relative) {
        return operation->address_mode ==
                   VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE &&
               operation->module_serial == module_serial &&
               operation->segment_index == segment_index;
    }
    return operation->address_mode == VC_PSV_ADDRESS_ABSOLUTE &&
           operation->module_serial == 0 &&
           operation->segment_index == 0;
}

static bool is_address_operation(vc_psv_operation_kind kind)
{
    return kind == VC_PSV_OPERATION_WRITE_U8 ||
           kind == VC_PSV_OPERATION_WRITE_U16 ||
           kind == VC_PSV_OPERATION_WRITE_U32 ||
           kind == VC_PSV_OPERATION_MOVE ||
           kind == VC_PSV_OPERATION_REPEAT ||
           kind == VC_PSV_OPERATION_POINTER_WRITE ||
           kind == VC_PSV_OPERATION_POINTER_REPEAT ||
           kind == VC_PSV_OPERATION_POINTER_MOVE ||
           kind == VC_PSV_OPERATION_PATCH ||
           kind == VC_PSV_OPERATION_CONDITION_GATE;
}

static bool button_mode_is_valid(uint32_t mode)
{
    return mode == UINT32_C(0) ||
           mode == UINT32_C(1) ||
           mode == UINT32_C(2) ||
           mode == UINT32_C(4) ||
           mode == UINT32_C(8);
}

static bool operation_physical_span(const vc_psv_operation *operation,
                                    size_t remaining,
                                    size_t *span)
{
    size_t levels;

    switch (operation->kind) {
    case VC_PSV_OPERATION_REPEAT:
        *span = 2;
        break;
    case VC_PSV_OPERATION_POINTER_WRITE:
        levels = (size_t)(operation->legacy_code & UINT16_C(0x000f));
        if (levels == 0) {
            return false;
        }
        *span = levels + 1u;
        break;
    case VC_PSV_OPERATION_POINTER_REPEAT:
        levels = (size_t)(operation->legacy_code & UINT16_C(0x000f));
        if (levels == 0) {
            return false;
        }
        *span = levels + 2u;
        break;
    case VC_PSV_OPERATION_POINTER_MOVE:
        levels = (size_t)(operation->legacy_code & UINT16_C(0x000f));
        if (levels == 0) {
            return false;
        }
        *span = (levels + 1u) * 2u;
        break;
    default:
        *span = 1;
        break;
    }
    return *span <= remaining;
}

static bool gate_target_is_valid(const vc_psv_operation *operations,
                                 size_t first,
                                 size_t physical_count,
                                 size_t index,
                                 uint8_t skip_records)
{
    size_t target;
    size_t cursor = 0;

    if (!add_size(index, (size_t)skip_records + 1u, &target) ||
        target > physical_count) {
        return false;
    }
    while (cursor < physical_count) {
        size_t span;

        if (!operation_physical_span(&operations[first + cursor],
                                     physical_count - cursor, &span)) {
            return false;
        }
        if (target > cursor && target < cursor + span) {
            return false;
        }
        cursor += span;
    }
    return true;
}

static vc_psv_compile_status accept_positional_record(
    const vc_psv_operation *operation,
    uint32_t compatibility,
    vc_psv_plan_node *node,
    vc_psv_plan *plan)
{
    const uint32_t required = required_compatibility(operation->flags);

    if (operation->kind != VC_PSV_OPERATION_POINTER_POSITIONAL ||
        operation->address_mode != VC_PSV_ADDRESS_NOT_APPLICABLE ||
        operation->module_serial != 0 ||
        operation->segment_index != 0 ||
        !valid_operation_flags(operation->flags) ||
        !operation_flags_match_shape(operation)) {
        return VC_PSV_COMPILE_MALFORMED;
    }
    if ((required & ~compatibility) != 0) {
        return VC_PSV_COMPILE_NONCANONICAL;
    }
    if (required != 0) {
        ++plan->compatibility_diagnostics;
    }
    node->flags |= operation->flags;
    node->diagnostics |=
        plan_diagnostics_from_import(operation->diagnostics);
    plan->diagnostics |=
        plan_diagnostics_from_import(operation->diagnostics);
    return VC_PSV_COMPILE_OK;
}

static vc_psv_compile_status accept_pointer_level(
    size_t levels,
    uint32_t compatibility,
    vc_psv_plan_node *node,
    vc_psv_plan *plan)
{
    if (levels == 0 || levels > VC_PSV_POINTER_MAX_LEVELS) {
        return VC_PSV_COMPILE_MALFORMED;
    }
    if (levels > VC_PSV_POINTER_CANONICAL_MAX_LEVELS) {
        if ((compatibility &
             VC_PSV_COMPILE_ALLOW_POINTER_LEVELS_6_8) == 0) {
            return VC_PSV_COMPILE_NONCANONICAL;
        }
        node->diagnostics |= VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL;
        ++plan->compatibility_diagnostics;
    }
    return VC_PSV_COMPILE_OK;
}

static vc_psv_compile_status build_pointer_path(
    const vc_psv_operation *operations,
    size_t root_index,
    size_t levels,
    uint16_t continuation_code,
    bool relative,
    uint8_t module_serial,
    uint8_t segment_index,
    uint32_t compatibility,
    vc_psv_plan_node *node,
    vc_psv_plan *plan,
    vc_psv_pointer_path *path)
{
    size_t level;

    memset(path, 0, sizeof(*path));
    path->base = make_address(operations[root_index].address, relative,
                              module_serial, segment_index);
    path->level_count = (uint8_t)levels;
    path->reads[0].width = VC_PSV_WIDTH_U32;
    path->reads[0].offset = operations[root_index].value;
    for (level = 1; level < levels; ++level) {
        const vc_psv_operation *continuation =
            &operations[root_index + level];
        const vc_psv_compile_status status =
            accept_positional_record(continuation, compatibility,
                                     node, plan);

        if (status != VC_PSV_COMPILE_OK) {
            return status;
        }
        if (continuation->legacy_code != continuation_code ||
            continuation->address != UINT32_C(0)) {
            return VC_PSV_COMPILE_MALFORMED;
        }
        path->reads[level].width = VC_PSV_WIDTH_U32;
        path->reads[level].offset = continuation->value;
    }
    return VC_PSV_COMPILE_OK;
}

static void clear_nodes(vc_psv_plan_node *nodes, size_t node_capacity)
{
    if (nodes != NULL && node_capacity != 0) {
        memset(nodes, 0, node_capacity * sizeof(*nodes));
    }
}

vc_psv_compile_status vc_psv_compile_cheat(
    const vc_psv_cheat *cheat,
    const vc_psv_operation *operations,
    size_t operation_count,
    const vc_psv_compile_options *options,
    vc_psv_plan_node *nodes,
    size_t node_capacity,
    vc_psv_plan *plan)
{
    const uint32_t compatibility =
        options != NULL ? options->compatibility : VC_PSV_COMPILE_STRICT;
    size_t action_limit =
        options != NULL ? options->action_limit : VC_PSV_DEFAULT_ACTION_LIMIT;
    size_t operation_end;
    size_t operation_bytes;
    size_t node_bytes;
    size_t index = 0;
    bool relative = false;
    uint8_t module_serial = 0;
    uint8_t segment_index = 0;
    vc_psv_plan result;
    vc_psv_compile_status status = VC_PSV_COMPILE_OK;

    if (cheat == NULL || plan == NULL ||
        (operations == NULL && operation_count != 0) ||
        (nodes == NULL && node_capacity != 0) ||
        !valid_compile_compatibility(compatibility) ||
        !multiply_size(operation_count, sizeof(*operations),
                       &operation_bytes) ||
        !multiply_size(node_capacity, sizeof(*nodes), &node_bytes) ||
        ranges_overlap(operations, operation_bytes, nodes, node_bytes) ||
        ranges_overlap(nodes, node_bytes, cheat, sizeof(*cheat)) ||
        ranges_overlap(nodes, node_bytes, options, sizeof(*options)) ||
        ranges_overlap(nodes, node_bytes, plan, sizeof(*plan)) ||
        ranges_overlap(operations, operation_bytes, plan, sizeof(*plan)) ||
        ranges_overlap(cheat, sizeof(*cheat), plan, sizeof(*plan)) ||
        ranges_overlap(options, sizeof(*options), plan, sizeof(*plan))) {
        return VC_PSV_COMPILE_INVALID_ARGUMENT;
    }
    if (action_limit == 0) {
        action_limit = VC_PSV_DEFAULT_ACTION_LIMIT;
    }
    if (action_limit > VC_PSV_MAX_ACTION_LIMIT ||
        cheat->first_operation_index > operation_count ||
        cheat->operation_count >
            operation_count - cheat->first_operation_index) {
        return VC_PSV_COMPILE_INVALID_ARGUMENT;
    }
    operation_end = cheat->first_operation_index + cheat->operation_count;

    memset(&result, 0, sizeof(result));
    result.schema_version = VC_PSV_PLAN_SCHEMA_VERSION;
    result.physical_record_count = cheat->operation_count;
    if ((cheat->diagnostics &
         VC_PSV_DIAGNOSTIC_NONCANONICAL_HEADER) != 0) {
        result.diagnostics |=
            VC_PSV_PLAN_DIAGNOSTIC_NONCANONICAL_HEADER;
    }

    if (cheat->translation_state == VC_PSV_TRANSLATION_MALFORMED) {
        clear_nodes(nodes, node_capacity);
        *plan = result;
        return VC_PSV_COMPILE_MALFORMED;
    }
    if ((cheat->diagnostics &
         VC_PSV_DIAGNOSTIC_NONCANONICAL_HEADER) != 0) {
        if ((compatibility &
             VC_PSV_COMPILE_ALLOW_NONCANONICAL_HEADER) == 0) {
            clear_nodes(nodes, node_capacity);
            *plan = result;
            return VC_PSV_COMPILE_NONCANONICAL;
        }
        ++result.compatibility_diagnostics;
    }
    while (cheat->first_operation_index + index < operation_end) {
        const vc_psv_operation *operation =
            &operations[cheat->first_operation_index + index];
        vc_psv_plan_node node;
        size_t action_increment = 0;
        size_t read_increment = 0;
        size_t physical_advance = 1;
        const uint32_t required =
            required_compatibility(operation->flags);

        if (!valid_operation_flags(operation->flags) ||
            !operation_shape_is_valid(operation) ||
            !operation_flags_match_shape(operation)) {
            status = VC_PSV_COMPILE_MALFORMED;
            break;
        }
        if ((required & ~compatibility) != 0) {
            status = VC_PSV_COMPILE_NONCANONICAL;
            break;
        }
        if (required != 0) {
            ++result.compatibility_diagnostics;
        }

        if (operation->kind == VC_PSV_OPERATION_OPAQUE) {
            status = VC_PSV_COMPILE_UNSUPPORTED;
            break;
        }
        if (operation->kind == VC_PSV_OPERATION_REPEAT_CONTINUATION ||
            operation->kind == VC_PSV_OPERATION_POINTER_POSITIONAL) {
            status = VC_PSV_COMPILE_MALFORMED;
            break;
        }
        if (operation->kind == VC_PSV_OPERATION_SELECT_MODULE_BASE) {
            if (operation->address > UINT32_C(1) ||
                operation->value != UINT32_C(0) ||
                operation->address_mode !=
                    VC_PSV_ADDRESS_NOT_APPLICABLE) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            relative = true;
            module_serial =
                (uint8_t)(operation->legacy_code & UINT16_C(0x00ff));
            segment_index = (uint8_t)operation->address;
            ++index;
            continue;
        }
        if (!is_address_operation(operation->kind) &&
            (operation->address_mode != VC_PSV_ADDRESS_NOT_APPLICABLE ||
             operation->module_serial != 0 ||
             operation->segment_index != 0)) {
            status = VC_PSV_COMPILE_MALFORMED;
            break;
        }
        if (is_address_operation(operation->kind) &&
            !address_snapshot_matches(operation, relative, module_serial,
                                      segment_index)) {
            status = VC_PSV_COMPILE_MALFORMED;
            break;
        }

        memset(&node, 0, sizeof(node));
        node.width = operation_width(operation);
        node.flags = operation->flags;
        node.diagnostics =
            plan_diagnostics_from_import(operation->diagnostics);
        result.diagnostics |= node.diagnostics;
        node.physical_first = index;
        node.physical_count = 1;

        switch (operation->kind) {
        case VC_PSV_OPERATION_WRITE_U8:
        case VC_PSV_OPERATION_WRITE_U16:
        case VC_PSV_OPERATION_WRITE_U32:
            node.kind = VC_PSV_PLAN_WRITE;
            node.destination =
                make_address(operation->address, relative, module_serial,
                             segment_index);
            node.value = operation->value;
            action_increment = 1;
            break;
        case VC_PSV_OPERATION_MOVE:
            node.kind = VC_PSV_PLAN_MOVE;
            node.destination =
                make_address(operation->address, relative, module_serial,
                             segment_index);
            node.source =
                make_address(operation->value, relative, module_serial,
                             segment_index);
            action_increment = 1;
            read_increment = 1;
            break;
        case VC_PSV_OPERATION_REPEAT: {
            const vc_psv_operation *continuation;
            uint32_t continuation_required;

            if (index + 1u >= cheat->operation_count) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            continuation =
                &operations[cheat->first_operation_index + index + 1u];
            if (continuation->kind !=
                    VC_PSV_OPERATION_REPEAT_CONTINUATION ||
                !valid_operation_flags(continuation->flags) ||
                !operation_flags_match_shape(continuation) ||
                continuation->address_mode !=
                    VC_PSV_ADDRESS_NOT_APPLICABLE ||
                continuation->module_serial != 0 ||
                continuation->segment_index != 0) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            continuation_required =
                required_compatibility(continuation->flags);
            if ((continuation_required & ~compatibility) != 0) {
                status = VC_PSV_COMPILE_NONCANONICAL;
                break;
            }
            if (continuation_required != 0) {
                ++result.compatibility_diagnostics;
            }
            node.kind = VC_PSV_PLAN_REPEAT;
            node.destination =
                make_address(operation->address, relative, module_serial,
                             segment_index);
            node.value = operation->value;
            node.repeat_count = continuation->legacy_code;
            node.address_gap = continuation->address;
            node.value_increment = continuation->value;
            node.physical_count = 2;
            node.flags |= continuation->flags;
            node.diagnostics |=
                plan_diagnostics_from_import(
                    continuation->diagnostics);
            action_increment = (size_t)node.repeat_count;
            physical_advance = 2;
            break;
        }
        case VC_PSV_OPERATION_POINTER_WRITE: {
            const size_t levels =
                (size_t)(operation->legacy_code & UINT16_C(0x000f));
            const uint16_t width_index =
                (uint16_t)((operation->legacy_code >> 8u) &
                           UINT16_C(0x000f));
            const uint16_t continuation_code =
                (uint16_t)(UINT16_C(0x3000) |
                           (uint16_t)(width_index << 8u));
            const vc_psv_operation *terminal;

            if ((operation->legacy_code & UINT16_C(0x00f0)) != 0 ||
                width_index > UINT16_C(2)) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            status = accept_pointer_level(levels, compatibility,
                                          &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            if (levels + 1u > cheat->operation_count - index) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            status = build_pointer_path(
                operations, cheat->first_operation_index + index,
                levels, continuation_code, relative, module_serial,
                segment_index, compatibility, &node, &result,
                &node.destination_pointer);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            terminal =
                &operations[cheat->first_operation_index + index + levels];
            status = accept_positional_record(
                terminal, compatibility, &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            if (terminal->address != UINT32_C(0)) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            if (terminal->legacy_code != UINT16_C(0x3300)) {
                if ((terminal->legacy_code != UINT16_C(0x3302) &&
                     terminal->legacy_code != UINT16_C(0x9000)) ||
                    (compatibility &
                     VC_PSV_COMPILE_ALLOW_POINTER_TERMINAL_MARKERS) == 0) {
                    status =
                        terminal->legacy_code == UINT16_C(0x3302) ||
                                terminal->legacy_code == UINT16_C(0x9000)
                            ? VC_PSV_COMPILE_NONCANONICAL
                            : VC_PSV_COMPILE_MALFORMED;
                    break;
                }
                node.diagnostics |=
                    VC_PSV_PLAN_DIAGNOSTIC_POINTER_TERMINAL_MARKER;
                ++result.compatibility_diagnostics;
            }
            node.kind = VC_PSV_PLAN_POINTER_WRITE;
            node.width = width_from_index(width_index);
            node.value = terminal->value;
            node.physical_count = levels + 1u;
            action_increment = 1;
            read_increment = levels;
            physical_advance = node.physical_count;
            break;
        }
        case VC_PSV_OPERATION_POINTER_REPEAT: {
            const size_t levels =
                (size_t)(operation->legacy_code & UINT16_C(0x000f));
            const uint16_t width_index =
                (uint16_t)((operation->legacy_code >> 8u) &
                           UINT16_C(0x000f));
            const uint16_t continuation_code =
                (uint16_t)(UINT16_C(0x7000) |
                           (uint16_t)(width_index << 8u));
            const vc_psv_operation *marker;
            const vc_psv_operation *count;
            uint16_t selector;

            if ((operation->legacy_code & UINT16_C(0x00f0)) != 0 ||
                width_index > UINT16_C(2)) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            status = accept_pointer_level(levels, compatibility,
                                          &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            if (levels + 2u > cheat->operation_count - index) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            status = build_pointer_path(
                operations, cheat->first_operation_index + index,
                levels, continuation_code, relative, module_serial,
                segment_index, compatibility, &node, &result,
                &node.destination_pointer);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            marker =
                &operations[cheat->first_operation_index + index + levels];
            status = accept_positional_record(
                marker, compatibility, &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            if (marker->address != UINT32_C(0)) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            if ((marker->legacy_code & UINT16_C(0xff00)) ==
                UINT16_C(0x7700)) {
                selector =
                    (uint16_t)(marker->legacy_code & UINT16_C(0x00ff));
            } else if (marker->legacy_code == UINT16_C(0x7402)) {
                if ((compatibility &
                     VC_PSV_COMPILE_ALLOW_POINTER_TERMINAL_MARKERS) == 0) {
                    status = VC_PSV_COMPILE_NONCANONICAL;
                    break;
                }
                selector = UINT16_C(2);
                node.diagnostics |=
                    VC_PSV_PLAN_DIAGNOSTIC_POINTER_REPEAT_MARKER;
                ++result.compatibility_diagnostics;
            } else {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            if ((size_t)selector > levels) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            count = &operations[cheat->first_operation_index + index +
                                levels + 1u];
            status = accept_positional_record(
                count, compatibility, &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            if (count->address > UINT32_C(0xffff)) {
                if ((compatibility &
                     VC_PSV_COMPILE_ALLOW_POINTER_U32_GAP) == 0) {
                    status = VC_PSV_COMPILE_NONCANONICAL;
                    break;
                }
                node.diagnostics |=
                    VC_PSV_PLAN_DIAGNOSTIC_POINTER_U32_GAP;
                ++result.compatibility_diagnostics;
            }
            node.kind = VC_PSV_PLAN_POINTER_REPEAT;
            node.width = width_from_index(width_index);
            node.value = marker->value;
            node.repeat_count = count->legacy_code;
            node.address_gap = count->address;
            node.value_increment = count->value;
            node.pointer_increment_selector = (uint8_t)selector;
            node.physical_count = levels + 2u;
            action_increment = (size_t)node.repeat_count;
            if (!multiply_size(action_increment, levels,
                               &read_increment)) {
                status = VC_PSV_COMPILE_ACTION_LIMIT;
                break;
            }
            physical_advance = node.physical_count;
            break;
        }
        case VC_PSV_OPERATION_POINTER_MOVE: {
            const size_t levels =
                (size_t)(operation->legacy_code & UINT16_C(0x000f));
            const uint16_t width_index =
                (uint16_t)((operation->legacy_code >> 8u) &
                           UINT16_C(0x000f));
            const uint16_t destination_continuation =
                (uint16_t)(UINT16_C(0x8000) |
                           (uint16_t)(width_index << 8u));
            const vc_psv_operation *destination_marker;
            const vc_psv_operation *source_root;
            const vc_psv_operation *source_marker;
            size_t source_levels;
            uint16_t source_width_index;
            uint16_t source_continuation;

            if ((operation->legacy_code & UINT16_C(0x00f0)) != 0 ||
                width_index > UINT16_C(2)) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            status = accept_pointer_level(levels, compatibility,
                                          &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            if ((levels + 1u) * 2u >
                cheat->operation_count - index) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            status = build_pointer_path(
                operations, cheat->first_operation_index + index,
                levels, destination_continuation, relative,
                module_serial, segment_index, compatibility, &node,
                &result, &node.destination_pointer);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            destination_marker =
                &operations[cheat->first_operation_index + index + levels];
            status = accept_positional_record(
                destination_marker, compatibility, &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            source_root =
                &operations[cheat->first_operation_index + index +
                            levels + 1u];
            status = accept_positional_record(
                source_root, compatibility, &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            source_width_index =
                (uint16_t)((source_root->legacy_code >> 8u) &
                           UINT16_C(0x000f));
            source_levels =
                (size_t)(source_root->legacy_code & UINT16_C(0x000f));
            if ((source_root->legacy_code & UINT16_C(0xf0f0)) !=
                    UINT16_C(0x8000) ||
                source_width_index < UINT16_C(4) ||
                source_width_index > UINT16_C(6) ||
                source_levels == 0 ||
                source_levels > VC_PSV_POINTER_MAX_LEVELS) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            if (source_width_index != width_index + UINT16_C(4)) {
                if ((compatibility &
                     VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH) == 0) {
                    status = VC_PSV_COMPILE_NONCANONICAL;
                    break;
                }
                node.diagnostics |=
                    VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_WIDTH;
                ++result.compatibility_diagnostics;
            }
            if (source_levels != levels) {
                if ((compatibility &
                     VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH) == 0) {
                    status = VC_PSV_COMPILE_NONCANONICAL;
                    break;
                }
                node.diagnostics |=
                    VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_LEVEL;
                ++result.compatibility_diagnostics;
            }
            if (destination_marker->address != UINT32_C(0) ||
                destination_marker->value != UINT32_C(0)) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            if (destination_marker->legacy_code != UINT16_C(0x8800)) {
                if (destination_marker->legacy_code != UINT16_C(0x8900) ||
                    (compatibility &
                     VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH) == 0) {
                    status =
                        destination_marker->legacy_code ==
                                UINT16_C(0x8900)
                            ? VC_PSV_COMPILE_NONCANONICAL
                            : VC_PSV_COMPILE_MALFORMED;
                    break;
                }
                node.diagnostics |=
                    VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_MARKER;
                ++result.compatibility_diagnostics;
            }
            source_continuation =
                (uint16_t)(UINT16_C(0x8000) |
                           (uint16_t)(source_width_index << 8u));
            status = build_pointer_path(
                operations,
                cheat->first_operation_index + index + levels + 1u,
                levels, source_continuation, relative, module_serial,
                segment_index, compatibility, &node, &result,
                &node.source_pointer);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            source_marker =
                &operations[cheat->first_operation_index + index +
                            levels * 2u + 1u];
            status = accept_positional_record(
                source_marker, compatibility, &node, &result);
            if (status != VC_PSV_COMPILE_OK) {
                break;
            }
            if (source_marker->address != UINT32_C(0) ||
                source_marker->value != UINT32_C(0)) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            if (source_marker->legacy_code != UINT16_C(0x8900)) {
                if (source_marker->legacy_code != UINT16_C(0x8800) ||
                    (compatibility &
                     VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH) == 0) {
                    status =
                        source_marker->legacy_code == UINT16_C(0x8800)
                            ? VC_PSV_COMPILE_NONCANONICAL
                            : VC_PSV_COMPILE_MALFORMED;
                    break;
                }
                node.diagnostics |=
                    VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_MARKER;
                ++result.compatibility_diagnostics;
            }
            node.kind = VC_PSV_PLAN_POINTER_MOVE;
            node.width = width_from_index(width_index);
            node.observed_source_width_index =
                (uint8_t)source_width_index;
            node.observed_source_level_count = (uint8_t)source_levels;
            node.physical_count = (levels + 1u) * 2u;
            action_increment = 1;
            read_increment = levels * 2u + 1u;
            physical_advance = node.physical_count;
            break;
        }
        case VC_PSV_OPERATION_PATCH:
            node.kind = VC_PSV_PLAN_PATCH;
            node.destination =
                make_address(operation->address, relative, module_serial,
                             segment_index);
            node.value = operation->value;
            action_increment = 1;
            read_increment = 1;
            break;
        case VC_PSV_OPERATION_BUTTON_GATE:
            if ((operation->address & UINT32_C(0xffff0000)) != 0 ||
                !button_mode_is_valid(operation->address) ||
                (operation->value & ~UINT32_C(0x0000f3f9)) != 0 ||
                !gate_target_is_valid(operations,
                                      cheat->first_operation_index,
                                      cheat->operation_count, index,
                                      (uint8_t)(operation->legacy_code &
                                                UINT16_C(0x00ff)))) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            node.kind = VC_PSV_PLAN_BUTTON_GATE;
            node.button_mode = operation->address;
            node.button_mask = operation->value;
            node.skip_records =
                (uint8_t)(operation->legacy_code & UINT16_C(0x00ff));
            break;
        case VC_PSV_OPERATION_CONDITION_GATE: {
            const uint16_t condition =
                (uint16_t)((operation->legacy_code >> 8u) &
                           UINT16_C(0x000f));
            if (!gate_target_is_valid(operations,
                                      cheat->first_operation_index,
                                      cheat->operation_count, index,
                                      (uint8_t)(operation->legacy_code &
                                                UINT16_C(0x00ff)))) {
                status = VC_PSV_COMPILE_MALFORMED;
                break;
            }
            node.kind = VC_PSV_PLAN_CONDITION_GATE;
            node.width = width_from_index((uint16_t)(condition % 3u));
            node.relation = (vc_psv_relation)(condition / 3u);
            node.destination =
                make_address(operation->address, relative, module_serial,
                             segment_index);
            node.value = operation->value;
            node.skip_records =
                (uint8_t)(operation->legacy_code & UINT16_C(0x00ff));
            read_increment = 1;
            break;
        }
        default:
            status = VC_PSV_COMPILE_MALFORMED;
            break;
        }
        if (status != VC_PSV_COMPILE_OK) {
            break;
        }
        result.diagnostics |= node.diagnostics;
        if (!add_size(result.maximum_actions, action_increment,
                      &result.maximum_actions) ||
            result.maximum_actions > action_limit) {
            status = VC_PSV_COMPILE_ACTION_LIMIT;
            break;
        }
        if (!add_size(result.maximum_memory_reads, read_increment,
                      &result.maximum_memory_reads) ||
            result.maximum_memory_reads > VC_PSV_MAX_READ_LIMIT) {
            status = VC_PSV_COMPILE_ACTION_LIMIT;
            break;
        }
        if (result.node_count < node_capacity) {
            nodes[result.node_count] = node;
        }
        ++result.node_count;
        index += physical_advance;
    }

    if (status == VC_PSV_COMPILE_OK &&
        result.node_count > node_capacity) {
        status = VC_PSV_COMPILE_OUTPUT_TOO_SMALL;
    }
    if (status != VC_PSV_COMPILE_OK) {
        clear_nodes(nodes, node_capacity);
    }
    *plan = result;
    return status;
}

static uint32_t width_mask(vc_psv_width width)
{
    switch (width) {
    case VC_PSV_WIDTH_U8:
        return UINT32_C(0xff);
    case VC_PSV_WIDTH_U16:
        return UINT32_C(0xffff);
    default:
        return UINT32_MAX;
    }
}

static void encode_little_endian(uint32_t value,
                                 vc_psv_width width,
                                 uint8_t bytes[4])
{
    size_t index;

    memset(bytes, 0, 4);
    for (index = 0; index < (size_t)width; ++index) {
        bytes[index] = (uint8_t)(value >> (index * CHAR_BIT));
    }
}

static uint32_t decode_little_endian(const uint8_t bytes[4],
                                     vc_psv_width width)
{
    uint32_t value = 0;
    size_t index;

    for (index = 0; index < (size_t)width; ++index) {
        value |= (uint32_t)bytes[index] << (index * CHAR_BIT);
    }
    return value;
}

static bool valid_plan_address(const vc_psv_plan_address *address)
{
    if (address->mode == VC_PSV_ADDRESS_ABSOLUTE) {
        return address->module_serial == 0 &&
               address->segment_index == 0;
    }
    return address->mode == VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE &&
           address->segment_index <= 1;
}

static bool valid_pointer_path(const vc_psv_pointer_path *path)
{
    size_t index;

    if (!valid_plan_address(&path->base) ||
        path->level_count == 0 ||
        path->level_count > VC_PSV_POINTER_MAX_LEVELS) {
        return false;
    }
    for (index = 0; index < VC_PSV_POINTER_MAX_LEVELS; ++index) {
        if (index < path->level_count) {
            if (path->reads[index].width != VC_PSV_WIDTH_U32) {
                return false;
            }
        } else if (path->reads[index].width != 0 ||
                   path->reads[index].offset != UINT32_C(0)) {
            return false;
        }
    }
    return true;
}

static bool pointer_path_is_zero(const vc_psv_pointer_path *path)
{
    size_t index;

    if (path->base.mode != VC_PSV_ADDRESS_NOT_APPLICABLE ||
        path->base.value != UINT32_C(0) ||
        path->base.module_serial != 0 ||
        path->base.segment_index != 0 ||
        path->level_count != 0) {
        return false;
    }
    for (index = 0; index < VC_PSV_POINTER_MAX_LEVELS; ++index) {
        if (path->reads[index].width != 0 ||
            path->reads[index].offset != UINT32_C(0)) {
            return false;
        }
    }
    return true;
}

static bool node_has_destination(vc_psv_plan_node_kind kind)
{
    return kind == VC_PSV_PLAN_WRITE ||
           kind == VC_PSV_PLAN_MOVE ||
           kind == VC_PSV_PLAN_REPEAT ||
           kind == VC_PSV_PLAN_PATCH ||
           kind == VC_PSV_PLAN_CONDITION_GATE;
}

static bool target_splits_multiline(const vc_psv_plan_node *nodes,
                                    size_t node_count,
                                    size_t target)
{
    size_t index;

    for (index = 0; index < node_count; ++index) {
        if (nodes[index].physical_count > 1 &&
            target == nodes[index].physical_first + 1u) {
            return true;
        }
        if (nodes[index].physical_count > 2 &&
            target > nodes[index].physical_first &&
            target < nodes[index].physical_first +
                         nodes[index].physical_count) {
            return true;
        }
    }
    return false;
}

static bool validate_plan(const vc_psv_plan *plan,
                          const vc_psv_plan_node *nodes,
                          const vc_psv_evaluation_callbacks *callbacks,
                          vc_psv_action *actions,
                          size_t action_capacity,
                          vc_psv_evaluation_report *report)
{
    size_t node_bytes;
    size_t action_bytes;
    size_t index;
    size_t previous_end = 0;
    size_t maximum_actions = 0;
    size_t maximum_reads = 0;
    bool needs_resolve = false;
    bool needs_read = false;
    bool needs_buttons = false;
    const uint32_t known_plan_diagnostics =
        VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL |
        VC_PSV_PLAN_DIAGNOSTIC_POINTER_TERMINAL_MARKER |
        VC_PSV_PLAN_DIAGNOSTIC_POINTER_REPEAT_MARKER |
        VC_PSV_PLAN_DIAGNOSTIC_POINTER_U32_GAP |
        VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_WIDTH |
        VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_LEVEL |
        VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_MARKER |
        VC_PSV_PLAN_DIAGNOSTIC_NONCANONICAL_HEADER |
        VC_PSV_PLAN_DIAGNOSTIC_LOWERCASE_HEX |
        VC_PSV_PLAN_DIAGNOSTIC_RUNTIME_DEPENDENT_ADDRESS;

    if (plan == NULL || callbacks == NULL || report == NULL ||
        plan->schema_version != VC_PSV_PLAN_SCHEMA_VERSION ||
        (plan->diagnostics & ~known_plan_diagnostics) != 0 ||
        plan->maximum_actions > VC_PSV_MAX_ACTION_LIMIT ||
        plan->maximum_memory_reads > VC_PSV_MAX_READ_LIMIT ||
        (nodes == NULL && plan->node_count != 0) ||
        (actions == NULL && action_capacity != 0) ||
        action_capacity < plan->maximum_actions ||
        !multiply_size(plan->node_count, sizeof(*nodes), &node_bytes) ||
        !multiply_size(action_capacity, sizeof(*actions), &action_bytes) ||
        ranges_overlap(nodes, node_bytes, actions, action_bytes) ||
        ranges_overlap(actions, action_bytes, plan, sizeof(*plan)) ||
        ranges_overlap(actions, action_bytes, callbacks,
                       sizeof(*callbacks)) ||
        ranges_overlap(actions, action_bytes, report, sizeof(*report)) ||
        ranges_overlap(nodes, node_bytes, report, sizeof(*report)) ||
        ranges_overlap(plan, sizeof(*plan), report, sizeof(*report)) ||
        ranges_overlap(callbacks, sizeof(*callbacks), report,
                       sizeof(*report))) {
        return false;
    }

    for (index = 0; index < plan->node_count; ++index) {
        const vc_psv_plan_node *node = &nodes[index];
        size_t node_end;
        size_t action_increment = 0;
        size_t read_increment = 0;
        size_t expected_physical_count = 1;
        const uint32_t general_diagnostics =
            VC_PSV_PLAN_DIAGNOSTIC_LOWERCASE_HEX |
            VC_PSV_PLAN_DIAGNOSTIC_RUNTIME_DEPENDENT_ADDRESS;
        const uint32_t known_diagnostics =
            VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL |
            VC_PSV_PLAN_DIAGNOSTIC_POINTER_TERMINAL_MARKER |
            VC_PSV_PLAN_DIAGNOSTIC_POINTER_REPEAT_MARKER |
            VC_PSV_PLAN_DIAGNOSTIC_POINTER_U32_GAP |
            VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_WIDTH |
            VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_LEVEL |
            VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_MARKER |
            general_diagnostics;

        if (!valid_width(node->width) ||
            !valid_operation_flags(node->flags) ||
            (node->diagnostics & ~known_diagnostics) != 0 ||
            node->physical_count == 0 ||
            node->physical_first < previous_end ||
            !add_size(node->physical_first, node->physical_count, &node_end) ||
            node_end > plan->physical_record_count) {
            return false;
        }
        switch (node->kind) {
        case VC_PSV_PLAN_REPEAT:
            expected_physical_count = 2;
            break;
        case VC_PSV_PLAN_POINTER_WRITE:
            if (!valid_pointer_path(&node->destination_pointer)) {
                return false;
            }
            expected_physical_count =
                (size_t)node->destination_pointer.level_count + 1u;
            break;
        case VC_PSV_PLAN_POINTER_REPEAT:
            if (!valid_pointer_path(&node->destination_pointer)) {
                return false;
            }
            expected_physical_count =
                (size_t)node->destination_pointer.level_count + 2u;
            break;
        case VC_PSV_PLAN_POINTER_MOVE:
            if (!valid_pointer_path(&node->destination_pointer) ||
                !valid_pointer_path(&node->source_pointer) ||
                node->source_pointer.level_count !=
                    node->destination_pointer.level_count) {
                return false;
            }
            expected_physical_count =
                ((size_t)node->destination_pointer.level_count + 1u) * 2u;
            break;
        default:
            break;
        }
        if (node->physical_count != expected_physical_count) {
            return false;
        }
        if (node->kind <= VC_PSV_PLAN_CONDITION_GATE) {
            if ((node->diagnostics & ~general_diagnostics) != 0 ||
                !pointer_path_is_zero(&node->destination_pointer) ||
                !pointer_path_is_zero(&node->source_pointer) ||
                node->pointer_increment_selector != 0 ||
                node->observed_source_width_index != 0 ||
                node->observed_source_level_count != 0) {
                return false;
            }
        } else if (node->kind == VC_PSV_PLAN_POINTER_WRITE) {
            const uint32_t allowed =
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL |
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_TERMINAL_MARKER |
                general_diagnostics;

            if ((node->diagnostics & ~allowed) != 0 ||
                (node->destination_pointer.level_count >
                     VC_PSV_POINTER_CANONICAL_MAX_LEVELS) !=
                    ((node->diagnostics &
                      VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL) != 0) ||
                node->observed_source_width_index != 0 ||
                node->observed_source_level_count != 0) {
                return false;
            }
        } else if (node->kind == VC_PSV_PLAN_POINTER_REPEAT) {
            const uint32_t allowed =
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL |
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_REPEAT_MARKER |
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_U32_GAP |
                general_diagnostics;

            if ((node->diagnostics & ~allowed) != 0 ||
                (node->destination_pointer.level_count >
                     VC_PSV_POINTER_CANONICAL_MAX_LEVELS) !=
                    ((node->diagnostics &
                      VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL) != 0) ||
                (node->address_gap > UINT32_C(0xffff)) !=
                    ((node->diagnostics &
                      VC_PSV_PLAN_DIAGNOSTIC_POINTER_U32_GAP) != 0) ||
                node->observed_source_width_index != 0 ||
                node->observed_source_level_count != 0) {
                return false;
            }
        } else if (node->kind == VC_PSV_PLAN_POINTER_MOVE) {
            const uint32_t allowed =
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL |
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_WIDTH |
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_LEVEL |
                VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_MARKER |
                general_diagnostics;
            const uint8_t expected_source_width =
                node->width == VC_PSV_WIDTH_U8
                    ? UINT8_C(4)
                    : node->width == VC_PSV_WIDTH_U16
                          ? UINT8_C(5)
                          : UINT8_C(6);

            if ((node->diagnostics & ~allowed) != 0 ||
                (node->destination_pointer.level_count >
                     VC_PSV_POINTER_CANONICAL_MAX_LEVELS) !=
                    ((node->diagnostics &
                      VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL) != 0) ||
                (node->observed_source_width_index !=
                     expected_source_width) !=
                    ((node->diagnostics &
                      VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_WIDTH) != 0) ||
                (node->observed_source_level_count !=
                     node->destination_pointer.level_count) !=
                    ((node->diagnostics &
                      VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_LEVEL) != 0) ||
                node->pointer_increment_selector != 0) {
                return false;
            }
        }
        if (node_has_destination(node->kind)) {
            if (!valid_plan_address(&node->destination)) {
                return false;
            }
            if (node->kind != VC_PSV_PLAN_REPEAT ||
                node->repeat_count != 0) {
                needs_resolve |=
                    node->destination.mode ==
                    VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE;
            }
        }
        switch (node->kind) {
        case VC_PSV_PLAN_WRITE:
            action_increment = 1;
            break;
        case VC_PSV_PLAN_PATCH:
            action_increment = 1;
            read_increment = 1;
            needs_read = true;
            break;
        case VC_PSV_PLAN_MOVE:
            if (!valid_plan_address(&node->source)) {
                return false;
            }
            needs_resolve |=
                node->source.mode ==
                VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE;
            needs_read = true;
            action_increment = 1;
            read_increment = 1;
            break;
        case VC_PSV_PLAN_REPEAT:
            action_increment = (size_t)node->repeat_count;
            break;
        case VC_PSV_PLAN_POINTER_WRITE:
            if (!pointer_path_is_zero(&node->source_pointer) ||
                node->pointer_increment_selector != 0) {
                return false;
            }
            needs_resolve |=
                node->destination_pointer.base.mode ==
                VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE;
            needs_read = true;
            action_increment = 1;
            read_increment =
                (size_t)node->destination_pointer.level_count;
            break;
        case VC_PSV_PLAN_POINTER_REPEAT:
            if (!pointer_path_is_zero(&node->source_pointer) ||
                node->pointer_increment_selector >
                    node->destination_pointer.level_count) {
                return false;
            }
            if (node->repeat_count != 0) {
                needs_resolve |=
                    node->destination_pointer.base.mode ==
                    VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE;
                needs_read = true;
            }
            action_increment = (size_t)node->repeat_count;
            if (!multiply_size(action_increment,
                               node->destination_pointer.level_count,
                               &read_increment)) {
                return false;
            }
            break;
        case VC_PSV_PLAN_POINTER_MOVE:
            if (node->observed_source_width_index < UINT8_C(4) ||
                node->observed_source_width_index > UINT8_C(6) ||
                node->observed_source_level_count == 0 ||
                node->observed_source_level_count >
                    VC_PSV_POINTER_MAX_LEVELS) {
                return false;
            }
            needs_resolve |=
                node->destination_pointer.base.mode ==
                    VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE ||
                node->source_pointer.base.mode ==
                    VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE;
            needs_read = true;
            action_increment = 1;
            read_increment =
                (size_t)node->destination_pointer.level_count * 2u + 1u;
            break;
        case VC_PSV_PLAN_BUTTON_GATE: {
            size_t target;
            if (!button_mode_is_valid(node->button_mode) ||
                (node->button_mask & ~UINT32_C(0x0000f3f9)) != 0 ||
                !add_size(node->physical_first,
                          (size_t)node->skip_records + 1u, &target) ||
                target > plan->physical_record_count ||
                target_splits_multiline(nodes, plan->node_count, target)) {
                return false;
            }
            needs_buttons = true;
            break;
        }
        case VC_PSV_PLAN_CONDITION_GATE: {
            size_t target;
            if (node->relation > VC_PSV_RELATION_UNSIGNED_LESS ||
                !add_size(node->physical_first,
                          (size_t)node->skip_records + 1u, &target) ||
                target > plan->physical_record_count ||
                target_splits_multiline(nodes, plan->node_count, target)) {
                return false;
            }
            needs_read = true;
            read_increment = 1;
            break;
        }
        default:
            return false;
        }
        if (!add_size(maximum_actions, action_increment,
                      &maximum_actions) ||
            maximum_actions > VC_PSV_MAX_ACTION_LIMIT) {
            return false;
        }
        if (!add_size(maximum_reads, read_increment, &maximum_reads) ||
            maximum_reads > VC_PSV_MAX_READ_LIMIT) {
            return false;
        }
        previous_end = node_end;
    }
    if (maximum_actions != plan->maximum_actions ||
        maximum_reads != plan->maximum_memory_reads ||
        (needs_resolve && callbacks->resolve_module_base == NULL) ||
        (needs_read && callbacks->read_memory == NULL) ||
        (needs_buttons && callbacks->read_buttons == NULL)) {
        return false;
    }
    return true;
}

typedef enum resolve_status {
    RESOLVE_OK = 0,
    RESOLVE_CALLBACK_FAILED,
    RESOLVE_OVERFLOW,
    RESOLVE_INVALID_POINTER
} resolve_status;

static vc_psv_evaluate_status evaluate_status_from_resolve(
    resolve_status status)
{
    if (status == RESOLVE_OVERFLOW) {
        return VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
    }
    if (status == RESOLVE_INVALID_POINTER) {
        return VC_PSV_EVALUATE_INVALID_POINTER;
    }
    return VC_PSV_EVALUATE_CALLBACK_FAILED;
}

static resolve_status resolve_address(
    const vc_psv_plan_address *address,
    const vc_psv_evaluation_callbacks *callbacks,
    uint32_t *resolved,
    vc_psv_evaluation_report *report)
{
    uint32_t base;

    if (address->mode == VC_PSV_ADDRESS_ABSOLUTE) {
        *resolved = address->value;
        return RESOLVE_OK;
    }
    ++report->module_resolutions;
    if (!callbacks->resolve_module_base(callbacks->context,
                                        address->module_serial,
                                        address->segment_index, &base)) {
        return RESOLVE_CALLBACK_FAILED;
    }
    if (base > UINT32_MAX - address->value) {
        return RESOLVE_OVERFLOW;
    }
    *resolved = base + address->value;
    return RESOLVE_OK;
}

static void clear_actions(vc_psv_action *actions, size_t action_count)
{
    if (actions != NULL && action_count != 0) {
        memset(actions, 0, action_count * sizeof(*actions));
    }
}

static vc_psv_evaluate_status append_value_action(
    vc_psv_action *actions,
    size_t *action_count,
    vc_psv_action_kind kind,
    vc_psv_width width,
    uint32_t address,
    uint32_t value,
    size_t source_node_index)
{
    vc_psv_action *action = &actions[*action_count];

    memset(action, 0, sizeof(*action));
    action->kind = kind;
    action->width = width;
    action->address = address;
    action->source_node_index = source_node_index;
    encode_little_endian(value & width_mask(width), width, action->bytes);
    ++*action_count;
    return VC_PSV_EVALUATE_OK;
}

static bool relation_matches(vc_psv_relation relation,
                             uint32_t actual,
                             uint32_t expected)
{
    switch (relation) {
    case VC_PSV_RELATION_EQUAL:
        return actual == expected;
    case VC_PSV_RELATION_NOT_EQUAL:
        return actual != expected;
    case VC_PSV_RELATION_UNSIGNED_GREATER:
        return actual > expected;
    case VC_PSV_RELATION_UNSIGNED_LESS:
        return actual < expected;
    default:
        return false;
    }
}

static bool address_range_is_valid(uint32_t address, vc_psv_width width)
{
    return address <= UINT32_MAX - ((uint32_t)width - UINT32_C(1));
}

static bool staged_byte(const vc_psv_action *actions,
                        size_t action_count,
                        uint32_t address,
                        uint8_t *value)
{
    size_t index;

    for (index = action_count; index != 0; --index) {
        const vc_psv_action *action = &actions[index - 1u];
        const uint64_t start = action->address;
        const uint64_t end = start + (size_t)action->width;

        if ((uint64_t)address >= start && (uint64_t)address < end) {
            *value = action->bytes[(size_t)((uint64_t)address - start)];
            return true;
        }
    }
    return false;
}

static bool read_logical_memory(
    const vc_psv_evaluation_callbacks *callbacks,
    const vc_psv_action *actions,
    size_t action_count,
    uint32_t address,
    vc_psv_width width,
    uint8_t bytes[4],
    vc_psv_evaluation_report *report)
{
    bool covered[4] = {false, false, false, false};
    bool all_covered = true;
    size_t index;

    memset(bytes, 0, 4);
    for (index = 0; index < (size_t)width; ++index) {
        covered[index] = staged_byte(actions, action_count,
                                     address + (uint32_t)index,
                                     &bytes[index]);
        all_covered &= covered[index];
    }
    if (!all_covered) {
        uint8_t current[4] = {0, 0, 0, 0};

        ++report->memory_reads;
        if (!callbacks->read_memory(callbacks->context, address, current,
                                    (size_t)width)) {
            return false;
        }
        for (index = 0; index < (size_t)width; ++index) {
            if (!covered[index]) {
                bytes[index] = current[index];
            }
        }
    }
    return true;
}

static resolve_status resolve_pointer_path(
    const vc_psv_pointer_path *path,
    uint8_t increment_selector,
    uint32_t increment,
    const vc_psv_evaluation_callbacks *callbacks,
    const vc_psv_action *actions,
    size_t action_count,
    uint32_t *resolved,
    vc_psv_evaluation_report *report)
{
    uint32_t cursor;
    size_t level;
    resolve_status status =
        resolve_address(&path->base, callbacks, &cursor, report);

    if (status != RESOLVE_OK) {
        return status;
    }
    if (increment_selector == 0) {
        cursor = (uint32_t)(cursor + increment);
    }
    for (level = 0; level < path->level_count; ++level) {
        uint8_t bytes[4] = {0, 0, 0, 0};
        uint32_t pointer;
        uint32_t offset = path->reads[level].offset;

        if (cursor == UINT32_C(0)) {
            return RESOLVE_INVALID_POINTER;
        }
        if (!address_range_is_valid(cursor, VC_PSV_WIDTH_U32)) {
            return RESOLVE_OVERFLOW;
        }
        if (!read_logical_memory(callbacks, actions, action_count,
                                 cursor, VC_PSV_WIDTH_U32,
                                 bytes, report)) {
            return RESOLVE_CALLBACK_FAILED;
        }
        if (increment_selector == level + 1u) {
            offset = (uint32_t)(offset + increment);
        }
        pointer = decode_little_endian(bytes, VC_PSV_WIDTH_U32);
        if (pointer == UINT32_C(0)) {
            return RESOLVE_INVALID_POINTER;
        }
        cursor = (uint32_t)(pointer + offset);
    }
    *resolved = cursor;
    return RESOLVE_OK;
}

vc_psv_evaluate_status vc_psv_evaluate_plan(
    const vc_psv_plan *plan,
    const vc_psv_plan_node *nodes,
    const vc_psv_evaluation_callbacks *callbacks,
    vc_psv_action *actions,
    size_t action_capacity,
    vc_psv_evaluation_report *report)
{
    vc_psv_evaluation_report result;
    size_t physical_cursor = 0;
    size_t node_index;
    vc_psv_evaluate_status status = VC_PSV_EVALUATE_OK;

    if (plan == NULL || callbacks == NULL || report == NULL ||
        (nodes == NULL && plan->node_count != 0) ||
        (actions == NULL && action_capacity != 0)) {
        return VC_PSV_EVALUATE_INVALID_ARGUMENT;
    }
    if (plan->schema_version == VC_PSV_PLAN_SCHEMA_VERSION &&
        plan->maximum_actions <= VC_PSV_MAX_ACTION_LIMIT &&
        action_capacity < plan->maximum_actions) {
        return VC_PSV_EVALUATE_OUTPUT_TOO_SMALL;
    }
    if (!validate_plan(plan, nodes, callbacks, actions, action_capacity,
                       report)) {
        return VC_PSV_EVALUATE_INVALID_PLAN;
    }
    memset(&result, 0, sizeof(result));

    for (node_index = 0; node_index < plan->node_count; ++node_index) {
        const vc_psv_plan_node *node = &nodes[node_index];
        uint32_t address;
        resolve_status resolved;

        if (node->physical_first < physical_cursor) {
            continue;
        }
        switch (node->kind) {
        case VC_PSV_PLAN_WRITE:
            resolved = resolve_address(&node->destination, callbacks,
                                       &address, &result);
            if (resolved != RESOLVE_OK) {
                status = evaluate_status_from_resolve(resolved);
                break;
            }
            if (!address_range_is_valid(address, node->width)) {
                status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                break;
            }
            status = append_value_action(actions, &result.action_count,
                                         VC_PSV_ACTION_WRITE, node->width,
                                         address, node->value, node_index);
            break;
        case VC_PSV_PLAN_MOVE: {
            uint32_t source_address;
            vc_psv_action *action;

            resolved = resolve_address(&node->source, callbacks,
                                       &source_address, &result);
            if (resolved != RESOLVE_OK) {
                status = evaluate_status_from_resolve(resolved);
                break;
            }
            if (!address_range_is_valid(source_address, node->width)) {
                status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                break;
            }
            resolved = resolve_address(&node->destination, callbacks,
                                       &address, &result);
            if (resolved != RESOLVE_OK) {
                status = evaluate_status_from_resolve(resolved);
                break;
            }
            if (!address_range_is_valid(address, node->width)) {
                status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                break;
            }
            action = &actions[result.action_count];
            memset(action, 0, sizeof(*action));
            if (!read_logical_memory(callbacks, actions,
                                     result.action_count, source_address,
                                     node->width, action->bytes, &result)) {
                status = VC_PSV_EVALUATE_CALLBACK_FAILED;
                break;
            }
            action->kind = VC_PSV_ACTION_WRITE;
            action->width = node->width;
            action->address = address;
            action->source_node_index = node_index;
            ++result.action_count;
            break;
        }
        case VC_PSV_PLAN_REPEAT: {
            uint32_t start_address;
            uint32_t repeat_index;

            if (node->repeat_count == 0) {
                break;
            }
            resolved = resolve_address(&node->destination, callbacks,
                                       &start_address, &result);
            if (resolved != RESOLVE_OK) {
                status = evaluate_status_from_resolve(resolved);
                break;
            }
            for (repeat_index = 0;
                 repeat_index < node->repeat_count;
                 ++repeat_index) {
                const uint32_t address_delta =
                    (uint32_t)((uint64_t)repeat_index *
                               (uint64_t)node->address_gap);
                const uint32_t value_delta =
                    (uint32_t)((uint64_t)repeat_index *
                               (uint64_t)node->value_increment);
                const uint32_t repeated_address =
                    (uint32_t)(start_address + address_delta);
                const uint32_t repeated_value =
                    (uint32_t)(node->value + value_delta);

                if (!address_range_is_valid(repeated_address,
                                            node->width)) {
                    status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                    break;
                }
                status = append_value_action(
                    actions, &result.action_count, VC_PSV_ACTION_WRITE,
                    node->width, repeated_address, repeated_value,
                    node_index);
                if (status != VC_PSV_EVALUATE_OK) {
                    break;
                }
            }
            break;
        }
        case VC_PSV_PLAN_POINTER_WRITE:
            resolved = resolve_pointer_path(
                &node->destination_pointer, 0, UINT32_C(0),
                callbacks, actions, result.action_count, &address,
                &result);
            if (resolved != RESOLVE_OK) {
                status = evaluate_status_from_resolve(resolved);
                break;
            }
            if (address == UINT32_C(0)) {
                status = VC_PSV_EVALUATE_INVALID_POINTER;
                break;
            }
            if (!address_range_is_valid(address, node->width)) {
                status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                break;
            }
            status = append_value_action(
                actions, &result.action_count, VC_PSV_ACTION_WRITE,
                node->width, address, node->value, node_index);
            break;
        case VC_PSV_PLAN_POINTER_REPEAT: {
            uint32_t repeat_index;

            for (repeat_index = 0;
                 repeat_index < node->repeat_count;
                 ++repeat_index) {
                const uint32_t address_delta =
                    (uint32_t)((uint64_t)repeat_index *
                               (uint64_t)node->address_gap);
                const uint32_t value_delta =
                    (uint32_t)((uint64_t)repeat_index *
                               (uint64_t)node->value_increment);
                const uint32_t repeated_value =
                    (uint32_t)(node->value + value_delta);

                resolved = resolve_pointer_path(
                    &node->destination_pointer,
                    node->pointer_increment_selector,
                    address_delta, callbacks, actions,
                    result.action_count, &address, &result);
                if (resolved != RESOLVE_OK) {
                    status = evaluate_status_from_resolve(resolved);
                    break;
                }
                if (address == UINT32_C(0)) {
                    status = VC_PSV_EVALUATE_INVALID_POINTER;
                    break;
                }
                if (!address_range_is_valid(address, node->width)) {
                    status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                    break;
                }
                status = append_value_action(
                    actions, &result.action_count, VC_PSV_ACTION_WRITE,
                    node->width, address, repeated_value, node_index);
                if (status != VC_PSV_EVALUATE_OK) {
                    break;
                }
            }
            break;
        }
        case VC_PSV_PLAN_POINTER_MOVE: {
            uint32_t source_address;
            vc_psv_action *action;

            resolved = resolve_pointer_path(
                &node->destination_pointer, 0, UINT32_C(0),
                callbacks, actions, result.action_count, &address,
                &result);
            if (resolved != RESOLVE_OK) {
                status = evaluate_status_from_resolve(resolved);
                break;
            }
            resolved = resolve_pointer_path(
                &node->source_pointer, 0, UINT32_C(0),
                callbacks, actions, result.action_count,
                &source_address, &result);
            if (resolved != RESOLVE_OK) {
                status = evaluate_status_from_resolve(resolved);
                break;
            }
            if (address == UINT32_C(0) ||
                source_address == UINT32_C(0)) {
                status = VC_PSV_EVALUATE_INVALID_POINTER;
                break;
            }
            if (!address_range_is_valid(address, node->width) ||
                !address_range_is_valid(source_address, node->width)) {
                status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                break;
            }
            action = &actions[result.action_count];
            memset(action, 0, sizeof(*action));
            if (!read_logical_memory(
                    callbacks, actions, result.action_count,
                    source_address, node->width, action->bytes,
                    &result)) {
                status = VC_PSV_EVALUATE_CALLBACK_FAILED;
                break;
            }
            action->kind = VC_PSV_ACTION_WRITE;
            action->width = node->width;
            action->address = address;
            action->source_node_index = node_index;
            ++result.action_count;
            break;
        }
        case VC_PSV_PLAN_PATCH: {
            vc_psv_action *action;

            resolved = resolve_address(&node->destination, callbacks,
                                       &address, &result);
            if (resolved != RESOLVE_OK) {
                status = resolved == RESOLVE_OVERFLOW
                             ? VC_PSV_EVALUATE_ADDRESS_OVERFLOW
                             : VC_PSV_EVALUATE_CALLBACK_FAILED;
                break;
            }
            if (!address_range_is_valid(address, node->width)) {
                status = VC_PSV_EVALUATE_ADDRESS_OVERFLOW;
                break;
            }
            action = &actions[result.action_count];
            memset(action, 0, sizeof(*action));
            action->kind = VC_PSV_ACTION_PATCH;
            action->width = node->width;
            action->address = address;
            action->source_node_index = node_index;
            encode_little_endian(node->value & width_mask(node->width),
                                 node->width, action->bytes);
            if (!read_logical_memory(callbacks, actions,
                                     result.action_count, address,
                                     node->width, action->original_bytes,
                                     &result)) {
                status = VC_PSV_EVALUATE_CALLBACK_FAILED;
                break;
            }
            ++result.action_count;
            break;
        }
        case VC_PSV_PLAN_BUTTON_GATE: {
            uint32_t current_mask;

            ++result.button_reads;
            if (!callbacks->read_buttons(callbacks->context,
                                         node->button_mode,
                                         &current_mask)) {
                status = VC_PSV_EVALUATE_CALLBACK_FAILED;
                break;
            }
            if (current_mask != node->button_mask) {
                physical_cursor = node->physical_first + 1u +
                                  (size_t)node->skip_records;
                result.skipped_physical_records += node->skip_records;
            }
            break;
        }
        case VC_PSV_PLAN_CONDITION_GATE: {
            uint8_t bytes[4] = {0, 0, 0, 0};
            uint32_t actual = 0;
            const uint32_t expected =
                node->value & width_mask(node->width);
            bool predicate = false;

            resolved = resolve_address(&node->destination, callbacks,
                                       &address, &result);
            if (resolved == RESOLVE_OK &&
                address_range_is_valid(address, node->width)) {
                if (read_logical_memory(callbacks, actions,
                                        result.action_count, address,
                                        node->width, bytes, &result)) {
                    actual = decode_little_endian(bytes, node->width);
                    predicate = relation_matches(node->relation, actual,
                                                 expected);
                }
            }
            if (!predicate) {
                physical_cursor = node->physical_first + 1u +
                                  (size_t)node->skip_records;
                result.skipped_physical_records += node->skip_records;
            }
            break;
        }
        default:
            status = VC_PSV_EVALUATE_INVALID_PLAN;
            break;
        }
        if (status != VC_PSV_EVALUATE_OK) {
            break;
        }
    }

    if (status != VC_PSV_EVALUATE_OK) {
        clear_actions(actions, action_capacity);
        result.action_count = 0;
    }
    *report = result;
    return status;
}

static bool valid_action(const vc_psv_action *action)
{
    return (action->kind == VC_PSV_ACTION_WRITE ||
            action->kind == VC_PSV_ACTION_PATCH) &&
           valid_width(action->width);
}

vc_psv_execute_status vc_psv_apply_actions(
    const vc_psv_action *actions,
    size_t action_count,
    const vc_psv_write_callbacks *callbacks,
    vc_psv_patch_ledger_entry *ledger,
    size_t ledger_capacity,
    vc_psv_apply_report *report)
{
    size_t action_bytes;
    size_t ledger_bytes;
    size_t patch_count = 0;
    size_t index;
    vc_psv_apply_report result;

    if (callbacks == NULL || report == NULL ||
        (actions == NULL && action_count != 0) ||
        (ledger == NULL && ledger_capacity != 0) ||
        (action_count != 0 && callbacks->write_memory == NULL) ||
        !multiply_size(action_count, sizeof(*actions), &action_bytes) ||
        !multiply_size(ledger_capacity, sizeof(*ledger), &ledger_bytes) ||
        ranges_overlap(actions, action_bytes, ledger, ledger_bytes) ||
        ranges_overlap(actions, action_bytes, callbacks,
                       sizeof(*callbacks)) ||
        ranges_overlap(actions, action_bytes, report, sizeof(*report)) ||
        ranges_overlap(ledger, ledger_bytes, callbacks,
                       sizeof(*callbacks)) ||
        ranges_overlap(ledger, ledger_bytes, report, sizeof(*report))) {
        return VC_PSV_EXECUTE_INVALID_ARGUMENT;
    }
    for (index = 0; index < action_count; ++index) {
        if (!valid_action(&actions[index])) {
            return VC_PSV_EXECUTE_INVALID_ACTION;
        }
        if (actions[index].kind == VC_PSV_ACTION_PATCH) {
            ++patch_count;
        }
    }
    if (patch_count > ledger_capacity) {
        return VC_PSV_EXECUTE_LEDGER_TOO_SMALL;
    }

    memset(&result, 0, sizeof(result));
    for (index = 0; index < action_count; ++index) {
        const vc_psv_action *action = &actions[index];

        if (!callbacks->write_memory(callbacks->context, action->address,
                                     action->bytes,
                                     (size_t)action->width)) {
            *report = result;
            return VC_PSV_EXECUTE_CALLBACK_FAILED;
        }
        ++result.applied_actions;
        if (action->kind == VC_PSV_ACTION_PATCH) {
            vc_psv_patch_ledger_entry *entry =
                &ledger[result.active_patch_entries];

            memset(entry, 0, sizeof(*entry));
            entry->address = action->address;
            entry->width = action->width;
            memcpy(entry->original_bytes, action->original_bytes,
                   (size_t)action->width);
            entry->source_node_index = action->source_node_index;
            entry->active = true;
            ++result.active_patch_entries;
        }
    }
    *report = result;
    return VC_PSV_EXECUTE_OK;
}

vc_psv_execute_status vc_psv_rollback_patches(
    vc_psv_patch_ledger_entry *ledger,
    size_t ledger_count,
    const vc_psv_write_callbacks *callbacks,
    vc_psv_rollback_report *report)
{
    size_t ledger_bytes;
    size_t index;
    size_t active_count = 0;
    vc_psv_rollback_report result;

    if (callbacks == NULL || report == NULL ||
        (ledger == NULL && ledger_count != 0) ||
        !multiply_size(ledger_count, sizeof(*ledger), &ledger_bytes) ||
        ranges_overlap(ledger, ledger_bytes, callbacks,
                       sizeof(*callbacks)) ||
        ranges_overlap(ledger, ledger_bytes, report, sizeof(*report))) {
        return VC_PSV_EXECUTE_INVALID_ARGUMENT;
    }
    for (index = 0; index < ledger_count; ++index) {
        if (!valid_width(ledger[index].width)) {
            return VC_PSV_EXECUTE_INVALID_ACTION;
        }
        if (ledger[index].active) {
            ++active_count;
        }
    }
    if (active_count != 0 && callbacks->write_memory == NULL) {
        return VC_PSV_EXECUTE_INVALID_ARGUMENT;
    }

    memset(&result, 0, sizeof(result));
    result.remaining_entries = active_count;
    for (index = ledger_count; index != 0; --index) {
        vc_psv_patch_ledger_entry *entry = &ledger[index - 1u];

        if (!entry->active) {
            continue;
        }
        if (!callbacks->write_memory(callbacks->context, entry->address,
                                     entry->original_bytes,
                                     (size_t)entry->width)) {
            *report = result;
            return VC_PSV_EXECUTE_CALLBACK_FAILED;
        }
        entry->active = false;
        ++result.restored_entries;
        --result.remaining_entries;
    }
    *report = result;
    return VC_PSV_EXECUTE_OK;
}
