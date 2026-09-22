#ifndef VITACHEAT_FOREIGN_TARGET_CONTROLLER_H
#define VITACHEAT_FOREIGN_TARGET_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VC_FTG_CONTROLLER_CHECKPOINT_MAGIC UINT32_C(0x43544731)
#define VC_FTG_CONTROLLER_CHECKPOINT_VERSION UINT32_C(1)
#define VC_FTG_CONTROLLER_CHECKPOINT_SIZE 96u
#define VC_FTG_CONTROLLER_MIN_REVALIDATIONS UINT32_C(19)
#define VC_FTG_CONTROLLER_PHASE_COUNT UINT32_C(3)
#define VC_FTG_CONTROLLER_PHASE_1_TEST_OFFSET UINT32_C(0)
#define VC_FTG_CONTROLLER_PHASE_1_TEST_COUNT UINT32_C(8)
#define VC_FTG_CONTROLLER_PHASE_2_TEST_OFFSET UINT32_C(8)
#define VC_FTG_CONTROLLER_PHASE_2_TEST_COUNT UINT32_C(20)
#define VC_FTG_CONTROLLER_PHASE_3_TEST_OFFSET UINT32_C(28)
#define VC_FTG_CONTROLLER_PHASE_3_TEST_COUNT UINT32_C(7)

typedef enum vc_ftg_controller_phase {
    VC_FTG_CONTROLLER_PHASE_INVALID = 0,
    VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_1 = 1,
    VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_2 = 2,
    VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_1 = 3,
    VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_1 = 4,
    VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_2 = 5,
    VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_2 = 6
} vc_ftg_controller_phase;

typedef enum vc_ftg_controller_checkpoint_observation {
    VC_FTG_CONTROLLER_CHECKPOINT_EMPTY = 0,
    VC_FTG_CONTROLLER_CHECKPOINT_VALID = 1,
    VC_FTG_CONTROLLER_CHECKPOINT_INVALID = 2
} vc_ftg_controller_checkpoint_observation;

typedef enum vc_ftg_controller_host_step {
    VC_FTG_CONTROLLER_HOST_EXPECT_PHASE_1 = 1,
    VC_FTG_CONTROLLER_HOST_EXPECT_PROMPT_1 = 2,
    VC_FTG_CONTROLLER_HOST_EXPECT_PHASE_2 = 3,
    VC_FTG_CONTROLLER_HOST_EXPECT_PROMPT_2 = 4,
    VC_FTG_CONTROLLER_HOST_EXPECT_PHASE_3 = 5,
    VC_FTG_CONTROLLER_HOST_COMPLETE = 6,
    VC_FTG_CONTROLLER_HOST_FAILED = 7
} vc_ftg_controller_host_step;

typedef enum vc_ftg_controller_launch_step {
    VC_FTG_CONTROLLER_LAUNCH_PREPARING = 1,
    VC_FTG_CONTROLLER_LAUNCH_CHECKPOINT_VERIFIED = 2,
    VC_FTG_CONTROLLER_LAUNCH_RESULTS_VERIFIED = 3,
    VC_FTG_CONTROLLER_LAUNCH_FAILED = 4
} vc_ftg_controller_launch_step;

typedef struct vc_ftg_controller_counts {
    uint32_t create_callback_count;
    uint32_t start_callback_count;
    uint32_t start_revalidation_count;
    uint32_t target_create_match_count;
    uint32_t target_create_authorized_count;
} vc_ftg_controller_counts;

typedef struct vc_ftg_controller_checkpoint {
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint32_t phase;
    uint64_t transaction_id;
    uint64_t previous_run_id;
    uint64_t first_handle;
    vc_ftg_controller_counts baseline_counts;
    vc_ftg_controller_counts exit_counts;
    uint32_t restart_count;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t integrity;
} vc_ftg_controller_checkpoint;

typedef struct vc_ftg_controller_phase_evidence {
    uint64_t run_id;
    uint64_t transaction_id;
    uint64_t previous_run_id;
    uint32_t phase_index;
    uint32_t test_offset;
    uint32_t test_count;
    bool result_cleared;
    bool phase_complete;
    bool launch_committed;
} vc_ftg_controller_phase_evidence;

typedef struct vc_ftg_controller_host_state {
    uint64_t transaction_id;
    uint64_t previous_run_id;
    vc_ftg_controller_host_step step;
    uint32_t input_count;
} vc_ftg_controller_host_state;

typedef struct vc_ftg_controller_launch_guard {
    vc_ftg_controller_launch_step step;
} vc_ftg_controller_launch_guard;

_Static_assert(
    sizeof(vc_ftg_controller_checkpoint) ==
        VC_FTG_CONTROLLER_CHECKPOINT_SIZE,
    "foreign-target controller checkpoint ABI changed");
_Static_assert(
    offsetof(vc_ftg_controller_checkpoint, baseline_counts) == 40u,
    "foreign-target controller baseline offset changed");
_Static_assert(
    offsetof(vc_ftg_controller_checkpoint, exit_counts) == 60u,
    "foreign-target controller exit offset changed");
_Static_assert(
    offsetof(vc_ftg_controller_checkpoint, integrity) == 92u,
    "foreign-target controller integrity offset changed");

bool vc_ftg_controller_generation_profile_valid(
    const vc_ftg_controller_counts *current,
    const vc_ftg_controller_counts *baseline);

bool vc_ftg_controller_checkpoint_init_generation_1(
    vc_ftg_controller_checkpoint *checkpoint,
    uint64_t transaction_id,
    uint64_t controller_run_id,
    const vc_ftg_controller_counts *baseline);

bool vc_ftg_controller_checkpoint_advance_generation_2(
    vc_ftg_controller_checkpoint *checkpoint,
    uint64_t controller_run_id,
    uint64_t first_handle,
    const vc_ftg_controller_counts *exit_counts);

bool vc_ftg_controller_checkpoint_commit_launch(
    vc_ftg_controller_checkpoint *checkpoint);

bool vc_ftg_controller_checkpoint_cancel_launch(
    vc_ftg_controller_checkpoint *checkpoint);

bool vc_ftg_controller_checkpoint_begin_resume(
    vc_ftg_controller_checkpoint *checkpoint,
    uint64_t controller_run_id);

bool vc_ftg_controller_checkpoint_encode(
    vc_ftg_controller_checkpoint *checkpoint,
    uint8_t output[VC_FTG_CONTROLLER_CHECKPOINT_SIZE]);

bool vc_ftg_controller_checkpoint_decode(
    const uint8_t *input,
    size_t input_size,
    vc_ftg_controller_checkpoint *checkpoint);

vc_ftg_controller_checkpoint_observation
vc_ftg_controller_checkpoint_observe(
    const uint8_t *input,
    size_t input_size,
    vc_ftg_controller_checkpoint *checkpoint);

bool vc_ftg_controller_checkpoint_validate(
    const vc_ftg_controller_checkpoint *checkpoint);

void vc_ftg_controller_host_init(
    vc_ftg_controller_host_state *state);

bool vc_ftg_controller_host_accept_phase(
    vc_ftg_controller_host_state *state,
    const vc_ftg_controller_phase_evidence *evidence);

bool vc_ftg_controller_host_can_send_input(
    const vc_ftg_controller_host_state *state,
    uint32_t generation,
    bool fresh_prompt_complete);

bool vc_ftg_controller_host_commit_input(
    vc_ftg_controller_host_state *state,
    uint32_t generation,
    bool fresh_prompt_complete);

bool vc_ftg_controller_host_complete(
    const vc_ftg_controller_host_state *state);

void vc_ftg_controller_launch_guard_init(
    vc_ftg_controller_launch_guard *guard);

bool vc_ftg_controller_launch_guard_checkpoint_verified(
    vc_ftg_controller_launch_guard *guard);

bool vc_ftg_controller_launch_guard_results_verified(
    vc_ftg_controller_launch_guard *guard);

bool vc_ftg_controller_launch_guard_ready(
    const vc_ftg_controller_launch_guard *guard);

#endif
