#include "vitacheat/target_attestation.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VC_TARGET_FUZZ_MAX_STEPS = 128
};

typedef struct fuzz_adapter {
    vc_target_attestation *attestation;
    vc_target_snapshot source;
    uint64_t token;
    bool fail_begin;
    bool fail_module;
    bool fail_thread;
    bool fail_end;
    bool mutate;
} fuzz_adapter;

static void fuzz_check(int condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t fuzz_u32(
    const uint8_t *data,
    size_t size,
    size_t offset)
{
    uint32_t value = 0;
    size_t index;

    for (index = 0; index < 4; ++index) {
        if (offset + index < size) {
            value |=
                (uint32_t)data[offset + index] <<
                (unsigned int)(index * 8u);
        }
    }
    return value;
}

static uint64_t fuzz_u64(
    const uint8_t *data,
    size_t size,
    size_t offset)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        if (offset + index < size) {
            value |=
                (uint64_t)data[offset + index] <<
                (unsigned int)(index * 8u);
        }
    }
    return value;
}

static uint8_t fuzz_byte(
    const uint8_t *data,
    size_t size,
    size_t offset)
{
    return offset < size ? data[offset] : 0;
}

static void fuzz_callback_boundary(fuzz_adapter *adapter)
{
    fuzz_check(adapter->attestation != NULL);
    fuzz_check(atomic_load_explicit(
                   &adapter->attestation->adapter_active,
                   memory_order_acquire) != 0u);
    fuzz_check(atomic_load_explicit(
                   &adapter->attestation->transaction_busy,
                   memory_order_acquire) == 0u);
}

static bool fuzz_begin(
    void *context,
    vc_target_identity *identity,
    uint32_t *module_count,
    uint32_t *thread_count,
    uint64_t *mutation_token)
{
    fuzz_adapter *adapter = (fuzz_adapter *)context;

    fuzz_callback_boundary(adapter);
    if (adapter->fail_begin) {
        return false;
    }
    *identity = adapter->source.identity;
    *module_count = adapter->source.module_count;
    *thread_count = adapter->source.thread_count;
    *mutation_token = adapter->token;
    return true;
}

static bool fuzz_module(
    void *context,
    uint64_t mutation_token,
    uint32_t module_index,
    vc_target_module *module)
{
    fuzz_adapter *adapter = (fuzz_adapter *)context;

    fuzz_callback_boundary(adapter);
    fuzz_check(mutation_token == adapter->token);
    if (adapter->fail_module) {
        return false;
    }
    fuzz_check(module_index < VC_TARGET_MAX_MODULES);
    *module = adapter->source.modules[module_index];
    return true;
}

static bool fuzz_thread(
    void *context,
    uint64_t mutation_token,
    uint32_t thread_index,
    vc_target_thread *thread)
{
    fuzz_adapter *adapter = (fuzz_adapter *)context;

    fuzz_callback_boundary(adapter);
    fuzz_check(mutation_token == adapter->token);
    if (adapter->fail_thread) {
        return false;
    }
    fuzz_check(thread_index < VC_TARGET_MAX_THREADS);
    *thread = adapter->source.threads[thread_index];
    return true;
}

static bool fuzz_end(
    void *context,
    uint64_t mutation_token,
    uint64_t *completion_token)
{
    fuzz_adapter *adapter = (fuzz_adapter *)context;

    fuzz_callback_boundary(adapter);
    if (adapter->fail_end) {
        return false;
    }
    *completion_token =
        adapter->mutate ? mutation_token + 1u
                        : mutation_token;
    return true;
}

static bool fuzz_cleanup(void *context)
{
    fuzz_adapter *adapter = (fuzz_adapter *)context;

    fuzz_callback_boundary(adapter);
    return true;
}

static void fuzz_make_source(
    fuzz_adapter *adapter,
    const uint8_t *data,
    size_t size)
{
    vc_target_snapshot *source = &adapter->source;
    uint32_t index;

    memset(source, 0, sizeof(*source));
    source->identity.process_id =
        fuzz_u32(data, size, 0);
    source->identity.process_generation =
        fuzz_u64(data, size, 4);
    source->identity.foreground_sequence =
        fuzz_u64(data, size, 12);
    source->identity.title_id_size =
        (uint32_t)(fuzz_byte(data, size, 20) %
                   (VC_TARGET_TITLE_ID_MAX + 2u));
    for (index = 0;
         index < VC_TARGET_TITLE_ID_MAX; ++index) {
        source->identity.title_id[index] =
            (uint8_t)('A' +
                      (fuzz_byte(data, size, 21u + index) %
                       26u));
    }
    source->identity.version_size =
        (uint32_t)(fuzz_byte(data, size, 37) %
                   (VC_TARGET_VERSION_MAX + 2u));
    for (index = 0;
         index < VC_TARGET_VERSION_MAX; ++index) {
        source->identity.version[index] =
            (uint8_t)('0' +
                      (fuzz_byte(data, size, 38u + index) %
                       10u));
    }
    source->identity.fingerprint_algorithm =
        fuzz_u32(data, size, 70);
    source->identity.fingerprint_size =
        (uint32_t)(fuzz_byte(data, size, 74) %
                   (VC_TARGET_FINGERPRINT_MAX + 2u));
    for (index = 0;
         index < VC_TARGET_FINGERPRINT_MAX; ++index) {
        source->identity.fingerprint[index] =
            fuzz_byte(data, size, 75u + index);
    }

    source->module_count =
        (uint32_t)(fuzz_byte(data, size, 139) %
                   (VC_TARGET_MAX_MODULES + 2u));
    for (index = 0;
         index < VC_TARGET_MAX_MODULES; ++index) {
        vc_target_module *module =
            &source->modules[index];

        module->module_id =
            fuzz_u32(data, size, 140u + index);
        module->load_generation =
            fuzz_u64(data, size, 156u + index);
        module->segment_count =
            (uint32_t)(fuzz_byte(
                data, size, 172u + index) %
                (VC_TARGET_MAX_SEGMENTS + 2u));
        for (uint32_t segment_index = 0;
             segment_index < VC_TARGET_MAX_SEGMENTS;
             ++segment_index) {
            vc_target_segment *segment =
                &module->segments[segment_index];
            size_t offset =
                188u + index * 7u + segment_index;

            segment->segment_index =
                fuzz_byte(data, size, offset);
            segment->base =
                fuzz_u32(data, size, offset + 1u);
            segment->size =
                fuzz_u32(data, size, offset + 2u);
            segment->permissions =
                fuzz_byte(data, size, offset + 3u);
            segment->flags =
                fuzz_byte(data, size, offset + 4u);
        }
    }

    source->thread_count =
        (uint32_t)(fuzz_byte(data, size, 300) %
                   (VC_TARGET_MAX_THREADS + 2u));
    for (index = 0;
         index < VC_TARGET_MAX_THREADS; ++index) {
        vc_target_thread *thread =
            &source->threads[index];

        thread->thread_id =
            (int32_t)fuzz_u32(data, size, 301u + index);
        thread->process_id =
            (fuzz_byte(data, size, 365u + index) & 1u) != 0
                ? source->identity.process_id
                : fuzz_u32(data, size, 366u + index);
        thread->process_generation =
            (fuzz_byte(data, size, 430u + index) & 1u) != 0
                ? source->identity.process_generation
                : fuzz_u64(data, size, 431u + index);
        thread->role_flags =
            fuzz_u32(data, size, 495u + index);
    }

    adapter->token = fuzz_u64(data, size, 560);
    adapter->fail_begin =
        (fuzz_byte(data, size, 568) & 1u) != 0;
    adapter->fail_module =
        (fuzz_byte(data, size, 568) & 2u) != 0;
    adapter->fail_thread =
        (fuzz_byte(data, size, 568) & 4u) != 0;
    adapter->fail_end =
        (fuzz_byte(data, size, 568) & 8u) != 0;
    adapter->mutate =
        (fuzz_byte(data, size, 568) & 16u) != 0;
}

static void fuzz_check_snapshot(
    vc_target_attestation *attestation)
{
    vc_target_snapshot snapshot;
    vc_target_status status =
        vc_target_attestation_get_snapshot(
            attestation, &snapshot);

    if (status == VC_TARGET_STATUS_OK) {
        fuzz_check(snapshot.revision != 0);
        fuzz_check(snapshot.lifecycle_generation != 0);
        fuzz_check(snapshot.identity.process_id != 0);
        fuzz_check(snapshot.identity.process_generation != 0);
        fuzz_check(snapshot.identity.foreground_sequence != 0);
        fuzz_check(snapshot.module_count > 0);
        fuzz_check(snapshot.module_count <=
                   VC_TARGET_MAX_MODULES);
        fuzz_check(snapshot.thread_count > 0);
        fuzz_check(snapshot.thread_count <=
                   VC_TARGET_MAX_THREADS);
    } else {
        fuzz_check(status == VC_TARGET_STATUS_UNAVAILABLE ||
                   status == VC_TARGET_STATUS_STOPPED);
    }
}

int LLVMFuzzerTestOneInput(
    const uint8_t *data,
    size_t size)
{
    vc_target_attestation attestation =
        VC_TARGET_INITIALIZER;
    vc_target_dependencies dependencies;
    fuzz_adapter adapter;
    vc_target_snapshot snapshot;
    vc_target_range range;
    uint32_t step_count;
    uint32_t step;

    if (data == NULL && size != 0) {
        return 0;
    }
    memset(&adapter, 0, sizeof(adapter));
    fuzz_make_source(&adapter, data, size);
    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.begin_snapshot = fuzz_begin;
    dependencies.read_module = fuzz_module;
    dependencies.read_thread = fuzz_thread;
    dependencies.end_snapshot = fuzz_end;
    dependencies.cleanup = fuzz_cleanup;
    dependencies.context = &adapter;
    fuzz_check(vc_target_attestation_init(
                   &attestation, &dependencies) ==
               VC_TARGET_STATUS_OK);
    adapter.attestation = &attestation;
    fuzz_check(vc_target_attestation_start(
                   &attestation) ==
               VC_TARGET_STATUS_OK);

    step_count =
        (uint32_t)(size < VC_TARGET_FUZZ_MAX_STEPS
                       ? size
                       : VC_TARGET_FUZZ_MAX_STEPS);
    for (step = 0; step < step_count; ++step) {
        uint8_t operation = data[step] & 7u;

        if (operation == 0) {
            (void)vc_target_attestation_refresh(
                &attestation);
        } else if (operation == 1) {
            if (vc_target_attestation_get_snapshot(
                    &attestation, &snapshot) ==
                VC_TARGET_STATUS_OK) {
                (void)vc_target_attestation_resolve_range(
                    &attestation, snapshot.revision,
                    fuzz_u32(data, size, step + 1u),
                    fuzz_u64(data, size, step + 5u),
                    fuzz_u32(data, size, step + 13u),
                    fuzz_u32(data, size, step + 17u),
                    fuzz_u32(data, size, step + 21u),
                    &range);
            }
        } else if (operation == 2) {
            (void)vc_target_attestation_foreground_changed(
                &attestation,
                fuzz_u64(data, size, step + 1u));
        } else if (operation == 3) {
            (void)vc_target_attestation_process_exit(
                &attestation,
                fuzz_u32(data, size, step + 1u),
                fuzz_u64(data, size, step + 5u));
        } else if (operation == 4) {
            (void)vc_target_attestation_module_changed(
                &attestation,
                fuzz_u32(data, size, step + 1u),
                fuzz_u64(data, size, step + 5u));
        } else if (operation == 5) {
            (void)vc_target_attestation_reset(
                &attestation);
        } else if (operation == 6) {
            (void)vc_target_attestation_plugin_unload(
                &attestation);
        } else {
            (void)vc_target_attestation_stop(
                &attestation);
            (void)vc_target_attestation_start(
                &attestation);
        }
        fuzz_check_snapshot(&attestation);
    }
    (void)vc_target_attestation_stop(&attestation);
    return 0;
}

#ifdef VC_TARGET_ATTESTATION_FUZZ_STANDALONE
int main(void)
{
    static const uint8_t seeds[][64] = {
        {0},
        {1, 2, 3, 4, 5, 6, 7, 8},
        {0xff, 0xff, 0xff, 0xff, 0x01},
        {'P', 'C', 'S', 'A', '0', '0', '1', '3', '3'}
    };
    size_t index;

    for (index = 0;
         index < sizeof(seeds) / sizeof(seeds[0]);
         ++index) {
        (void)LLVMFuzzerTestOneInput(
            seeds[index], sizeof(seeds[index]));
    }
    return 0;
}
#endif
