#include "vitacheat/foreign_target_controller.h"

#include <limits.h>
#include <string.h>

static void controller_store_u32(
    uint8_t *output,
    uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
    output[2] = (uint8_t)(value >> 16u);
    output[3] = (uint8_t)(value >> 24u);
}

static void controller_store_u64(
    uint8_t *output,
    uint64_t value)
{
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        output[index] = (uint8_t)(value >> (index * 8u));
    }
}

static uint32_t controller_load_u32(
    const uint8_t *input)
{
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8u) |
           ((uint32_t)input[2] << 16u) |
           ((uint32_t)input[3] << 24u);
}

static uint64_t controller_load_u64(
    const uint8_t *input)
{
    uint64_t value = 0u;
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        value |= (uint64_t)input[index] << (index * 8u);
    }
    return value;
}

static uint32_t controller_integrity(
    const uint8_t *bytes,
    size_t size)
{
    uint32_t value = UINT32_C(2166136261);

    while (size != 0u) {
        value ^= *bytes++;
        value *= UINT32_C(16777619);
        --size;
    }
    return value;
}

static void controller_store_counts(
    uint8_t *output,
    const vc_ftg_controller_counts *counts)
{
    controller_store_u32(
        output + 0u, counts->create_callback_count);
    controller_store_u32(
        output + 4u, counts->start_callback_count);
    controller_store_u32(
        output + 8u, counts->start_revalidation_count);
    controller_store_u32(
        output + 12u, counts->target_create_match_count);
    controller_store_u32(
        output + 16u,
        counts->target_create_authorized_count);
}

static void controller_load_counts(
    vc_ftg_controller_counts *counts,
    const uint8_t *input)
{
    counts->create_callback_count =
        controller_load_u32(input + 0u);
    counts->start_callback_count =
        controller_load_u32(input + 4u);
    counts->start_revalidation_count =
        controller_load_u32(input + 8u);
    counts->target_create_match_count =
        controller_load_u32(input + 12u);
    counts->target_create_authorized_count =
        controller_load_u32(input + 16u);
}

static bool controller_counts_zero(
    const vc_ftg_controller_counts *counts)
{
    return counts->create_callback_count == 0u &&
           counts->start_callback_count == 0u &&
           counts->start_revalidation_count == 0u &&
           counts->target_create_match_count == 0u &&
           counts->target_create_authorized_count == 0u;
}

bool vc_ftg_controller_generation_profile_valid(
    const vc_ftg_controller_counts *current,
    const vc_ftg_controller_counts *baseline)
{
    uint32_t start_revalidations;

    if (current == NULL ||
        baseline == NULL ||
        current->create_callback_count <
            baseline->create_callback_count ||
        current->start_callback_count <
            baseline->start_callback_count ||
        current->start_revalidation_count <
            baseline->start_revalidation_count ||
        current->target_create_match_count <
            baseline->target_create_match_count ||
        current->target_create_authorized_count <
            baseline->target_create_authorized_count ||
        baseline->create_callback_count == UINT32_MAX ||
        baseline->target_create_match_count == UINT32_MAX ||
        baseline->target_create_authorized_count == UINT32_MAX ||
        current->create_callback_count <
            baseline->create_callback_count + 1u ||
        current->target_create_match_count !=
            baseline->target_create_match_count + 1u ||
        current->target_create_authorized_count !=
            baseline->target_create_authorized_count + 1u) {
        return false;
    }
    start_revalidations =
        current->start_revalidation_count -
        baseline->start_revalidation_count;
    if (current->start_callback_count -
            baseline->start_callback_count <
        start_revalidations) {
        return false;
    }
    if (start_revalidations == 0u) {
        return current->start_callback_count ==
               baseline->start_callback_count;
    }
    return start_revalidations >=
           VC_FTG_CONTROLLER_MIN_REVALIDATIONS;
}

static bool controller_checkpoint_fields_valid(
    const vc_ftg_controller_checkpoint *checkpoint)
{
    if (checkpoint == NULL ||
        checkpoint->magic !=
            VC_FTG_CONTROLLER_CHECKPOINT_MAGIC ||
        checkpoint->version !=
            VC_FTG_CONTROLLER_CHECKPOINT_VERSION ||
        checkpoint->struct_size !=
            VC_FTG_CONTROLLER_CHECKPOINT_SIZE ||
        checkpoint->transaction_id == 0u ||
        checkpoint->previous_run_id == 0u ||
        checkpoint->reserved0 != 0u ||
        checkpoint->reserved1 != 0u) {
        return false;
    }
    if (checkpoint->baseline_counts
                .start_revalidation_count != 0u ||
        checkpoint->baseline_counts
                .target_create_match_count != 0u ||
        checkpoint->baseline_counts
                .target_create_authorized_count != 0u) {
        return false;
    }
    switch (checkpoint->phase) {
    case VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_1:
    case VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_1:
        return checkpoint->previous_run_id ==
                   checkpoint->transaction_id &&
               checkpoint->first_handle == 0u &&
               checkpoint->restart_count == 1u &&
               controller_counts_zero(
                   &checkpoint->exit_counts);
    case VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_1:
        return checkpoint->previous_run_id !=
                   checkpoint->transaction_id &&
               checkpoint->first_handle == 0u &&
               checkpoint->restart_count == 2u &&
               controller_counts_zero(
                   &checkpoint->exit_counts);
    case VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_2:
    case VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_2:
        return checkpoint->previous_run_id !=
                   checkpoint->transaction_id &&
               checkpoint->first_handle != 0u &&
               checkpoint->restart_count == 2u &&
               vc_ftg_controller_generation_profile_valid(
                   &checkpoint->exit_counts,
                   &checkpoint->baseline_counts);
    case VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_2:
        return checkpoint->previous_run_id !=
                   checkpoint->transaction_id &&
               checkpoint->first_handle != 0u &&
               checkpoint->restart_count == 3u &&
               vc_ftg_controller_generation_profile_valid(
                   &checkpoint->exit_counts,
                   &checkpoint->baseline_counts);
    default:
        return false;
    }
}

static void controller_checkpoint_encode_fields(
    const vc_ftg_controller_checkpoint *checkpoint,
    uint8_t output[VC_FTG_CONTROLLER_CHECKPOINT_SIZE])
{
    memset(output, 0, VC_FTG_CONTROLLER_CHECKPOINT_SIZE);
    controller_store_u32(output + 0u, checkpoint->magic);
    controller_store_u32(output + 4u, checkpoint->version);
    controller_store_u32(
        output + 8u, checkpoint->struct_size);
    controller_store_u32(output + 12u, checkpoint->phase);
    controller_store_u64(
        output + 16u, checkpoint->transaction_id);
    controller_store_u64(
        output + 24u, checkpoint->previous_run_id);
    controller_store_u64(
        output + 32u, checkpoint->first_handle);
    controller_store_counts(
        output + 40u, &checkpoint->baseline_counts);
    controller_store_counts(
        output + 60u, &checkpoint->exit_counts);
    controller_store_u32(
        output + 80u, checkpoint->restart_count);
    controller_store_u32(
        output + 84u, checkpoint->reserved0);
    controller_store_u32(
        output + 88u, checkpoint->reserved1);
}

bool vc_ftg_controller_checkpoint_init_generation_1(
    vc_ftg_controller_checkpoint *checkpoint,
    uint64_t transaction_id,
    uint64_t controller_run_id,
    const vc_ftg_controller_counts *baseline)
{
    if (checkpoint == NULL ||
        baseline == NULL ||
        transaction_id == 0u ||
        controller_run_id != transaction_id) {
        return false;
    }
    memset(checkpoint, 0, sizeof(*checkpoint));
    checkpoint->magic =
        VC_FTG_CONTROLLER_CHECKPOINT_MAGIC;
    checkpoint->version =
        VC_FTG_CONTROLLER_CHECKPOINT_VERSION;
    checkpoint->struct_size =
        VC_FTG_CONTROLLER_CHECKPOINT_SIZE;
    checkpoint->phase =
        VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_1;
    checkpoint->transaction_id = transaction_id;
    checkpoint->previous_run_id = controller_run_id;
    checkpoint->baseline_counts = *baseline;
    checkpoint->restart_count = 1u;
    return controller_checkpoint_fields_valid(checkpoint);
}

bool vc_ftg_controller_checkpoint_advance_generation_2(
    vc_ftg_controller_checkpoint *checkpoint,
    uint64_t controller_run_id,
    uint64_t first_handle,
    const vc_ftg_controller_counts *exit_counts)
{
    if (!vc_ftg_controller_checkpoint_validate(checkpoint) ||
        checkpoint->phase !=
            VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_1 ||
        controller_run_id == 0u ||
        controller_run_id !=
            checkpoint->previous_run_id ||
        first_handle == 0u ||
        exit_counts == NULL ||
        !vc_ftg_controller_generation_profile_valid(
            exit_counts, &checkpoint->baseline_counts)) {
        return false;
    }
    checkpoint->phase =
        VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_2;
    checkpoint->first_handle = first_handle;
    checkpoint->exit_counts = *exit_counts;
    checkpoint->restart_count = 2u;
    checkpoint->integrity = 0u;
    return controller_checkpoint_fields_valid(checkpoint);
}

bool vc_ftg_controller_checkpoint_commit_launch(
    vc_ftg_controller_checkpoint *checkpoint)
{
    if (!vc_ftg_controller_checkpoint_validate(checkpoint)) {
        return false;
    }
    if (checkpoint->phase ==
        VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_1) {
        checkpoint->phase =
            VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_1;
    } else if (checkpoint->phase ==
               VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_2) {
        checkpoint->phase =
            VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_2;
    } else {
        return false;
    }
    checkpoint->integrity = 0u;
    return controller_checkpoint_fields_valid(checkpoint);
}

bool vc_ftg_controller_checkpoint_begin_resume(
    vc_ftg_controller_checkpoint *checkpoint,
    uint64_t controller_run_id)
{
    if (!vc_ftg_controller_checkpoint_validate(checkpoint) ||
        controller_run_id == 0u ||
        controller_run_id == checkpoint->transaction_id ||
        controller_run_id == checkpoint->previous_run_id) {
        return false;
    }
    if (checkpoint->phase ==
        VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_1) {
        checkpoint->phase =
            VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_1;
        checkpoint->restart_count = 2u;
    } else if (checkpoint->phase ==
               VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_2) {
        checkpoint->phase =
            VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_2;
        checkpoint->restart_count = 3u;
    } else {
        return false;
    }
    checkpoint->previous_run_id = controller_run_id;
    checkpoint->integrity = 0u;
    return controller_checkpoint_fields_valid(checkpoint);
}

bool vc_ftg_controller_checkpoint_encode(
    vc_ftg_controller_checkpoint *checkpoint,
    uint8_t output[VC_FTG_CONTROLLER_CHECKPOINT_SIZE])
{
    if (checkpoint == NULL ||
        output == NULL ||
        !controller_checkpoint_fields_valid(checkpoint)) {
        return false;
    }
    controller_checkpoint_encode_fields(checkpoint, output);
    checkpoint->integrity =
        controller_integrity(output, 92u);
    controller_store_u32(
        output + 92u, checkpoint->integrity);
    return true;
}

bool vc_ftg_controller_checkpoint_decode(
    const uint8_t *input,
    size_t input_size,
    vc_ftg_controller_checkpoint *checkpoint)
{
    vc_ftg_controller_checkpoint decoded;
    uint32_t expected_integrity;

    if (input == NULL ||
        checkpoint == NULL ||
        input_size != VC_FTG_CONTROLLER_CHECKPOINT_SIZE) {
        return false;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.magic = controller_load_u32(input + 0u);
    decoded.version = controller_load_u32(input + 4u);
    decoded.struct_size = controller_load_u32(input + 8u);
    decoded.phase = controller_load_u32(input + 12u);
    decoded.transaction_id =
        controller_load_u64(input + 16u);
    decoded.previous_run_id =
        controller_load_u64(input + 24u);
    decoded.first_handle =
        controller_load_u64(input + 32u);
    controller_load_counts(
        &decoded.baseline_counts, input + 40u);
    controller_load_counts(
        &decoded.exit_counts, input + 60u);
    decoded.restart_count =
        controller_load_u32(input + 80u);
    decoded.reserved0 =
        controller_load_u32(input + 84u);
    decoded.reserved1 =
        controller_load_u32(input + 88u);
    decoded.integrity =
        controller_load_u32(input + 92u);
    expected_integrity =
        controller_integrity(input, 92u);
    if (decoded.integrity != expected_integrity ||
        !controller_checkpoint_fields_valid(&decoded)) {
        memset(&decoded, 0, sizeof(decoded));
        return false;
    }
    *checkpoint = decoded;
    memset(&decoded, 0, sizeof(decoded));
    return true;
}

vc_ftg_controller_checkpoint_observation
vc_ftg_controller_checkpoint_observe(
    const uint8_t *input,
    size_t input_size,
    vc_ftg_controller_checkpoint *checkpoint)
{
    if (checkpoint == NULL) {
        return VC_FTG_CONTROLLER_CHECKPOINT_INVALID;
    }
    memset(checkpoint, 0, sizeof(*checkpoint));
    if (input_size == 0u) {
        return VC_FTG_CONTROLLER_CHECKPOINT_EMPTY;
    }
    if (input == NULL ||
        !vc_ftg_controller_checkpoint_decode(
            input, input_size, checkpoint)) {
        return VC_FTG_CONTROLLER_CHECKPOINT_INVALID;
    }
    return VC_FTG_CONTROLLER_CHECKPOINT_VALID;
}

bool vc_ftg_controller_checkpoint_validate(
    const vc_ftg_controller_checkpoint *checkpoint)
{
    vc_ftg_controller_checkpoint encoded_checkpoint;
    uint8_t encoded[VC_FTG_CONTROLLER_CHECKPOINT_SIZE];
    bool valid;

    if (checkpoint == NULL) {
        return false;
    }
    encoded_checkpoint = *checkpoint;
    encoded_checkpoint.integrity = 0u;
    valid = vc_ftg_controller_checkpoint_encode(
        &encoded_checkpoint, encoded) &&
        encoded_checkpoint.integrity ==
            checkpoint->integrity;
    memset(&encoded_checkpoint, 0,
           sizeof(encoded_checkpoint));
    memset(encoded, 0, sizeof(encoded));
    return valid;
}
