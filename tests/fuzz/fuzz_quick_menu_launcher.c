#include "vitacheat/quick_menu_launcher.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VC_QUICK_MENU_LAUNCHER_FUZZ_MAX_STEPS = 256
};

typedef struct fuzz_platform {
    vc_launch_foreground_snapshot foreground;
    uint64_t now_ms;
    uint32_t response_state;
    uint32_t api_version;
    vc_quick_menu_transport_result transport_result;
    bool api_ok;
    bool foreground_ok;
    bool time_ok;
    bool resource_ok;
    bool update_ok;
    bool signal_ok;
} fuzz_platform;

static void fuzz_check(int condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool fuzz_get_api_version(void *context, uint32_t *version)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->api_ok) {
        *version = platform->api_version;
    }
    return platform->api_ok;
}

static bool fuzz_register(void *context, uint64_t *token)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->resource_ok) {
        *token = 1;
    }
    return platform->resource_ok;
}

static bool fuzz_register_widget(void *context,
                                 uint64_t texture_token,
                                 uint64_t label_token,
                                 uint64_t *token)
{
    (void)texture_token;
    (void)label_token;
    return fuzz_register(context, token);
}

static bool fuzz_register_callback(
    void *context,
    uint64_t widget_token,
    vc_quick_menu_button_callback_fn callback,
    void *callback_context,
    uint64_t generation,
    uint64_t *token)
{
    (void)widget_token;
    (void)callback;
    (void)callback_context;
    (void)generation;
    return fuzz_register(context, token);
}

static bool fuzz_start_worker(
    void *context,
    vc_quick_menu_worker_callback_fn callback,
    void *callback_context,
    uint64_t generation,
    uint64_t *token)
{
    (void)callback;
    (void)callback_context;
    (void)generation;
    return fuzz_register(context, token);
}

static bool fuzz_update(void *context,
                        uint64_t label_token,
                        vc_quick_menu_launcher_status status)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    (void)label_token;
    (void)status;
    return platform->update_ok;
}

static bool fuzz_signal(void *context, uint64_t worker_token)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    (void)worker_token;
    return platform->signal_ok;
}

static bool fuzz_cleanup(void *context, uint64_t token)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    (void)token;
    return platform->resource_ok;
}

static bool fuzz_foreground(
    void *context,
    vc_launch_foreground_snapshot *foreground)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->foreground_ok) {
        *foreground = platform->foreground;
    }
    return platform->foreground_ok;
}

static bool fuzz_time(void *context, uint64_t *now_ms)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->time_ok) {
        *now_ms = platform->now_ms;
    }
    return platform->time_ok;
}

static int32_t fuzz_status_for_state(uint32_t state)
{
    switch ((vc_launch_state)state) {
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
    case VC_LAUNCH_STATE_ABSENT:
    default:
        return VC_LAUNCH_STATUS_ABSENT;
    }
}

static vc_quick_menu_transport_result fuzz_transport(
    void *context,
    const uint8_t *request_wire,
    size_t request_size,
    uint8_t *response_wire,
    size_t response_capacity,
    size_t *response_size)
{
    fuzz_platform *platform = (fuzz_platform *)context;
    vc_launch_request request;
    vc_launch_response response;
    size_t encoded_size = 0;

    *response_size = 0;
    if (platform->transport_result != VC_QUICK_MENU_TRANSPORT_OK) {
        return platform->transport_result;
    }
    if (vc_launch_request_decode(
            request_wire, request_size, &request) !=
        VC_LAUNCH_STATUS_OK) {
        return VC_QUICK_MENU_TRANSPORT_FAILED;
    }

    memset(&response, 0, sizeof(response));
    response.version = VC_LAUNCH_ABI_VERSION;
    response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response.operation = request.operation;
    response.status = fuzz_status_for_state(platform->response_state);
    response.launch_state = platform->response_state;
    response.capabilities = request.capabilities;
    response.target_process_id = request.target_process_id;
    response.target_generation = request.target_generation;
    response.request_id =
        request.operation == VC_LAUNCH_OPERATION_SUBMIT
            ? UINT64_C(1)
            : request.request_id;
    response.created_ms = request.now_ms;
    response.deadline_ms =
        request.now_ms <= UINT64_MAX - VC_LAUNCH_DEFAULT_TTL_MS
            ? request.now_ms + VC_LAUNCH_DEFAULT_TTL_MS
            : UINT64_MAX;
    response.observed_ms = request.now_ms;
    if (vc_launch_response_encode(
            &response, response_wire, response_capacity,
            &encoded_size) != VC_LAUNCH_STATUS_OK) {
        return VC_QUICK_MENU_TRANSPORT_FAILED;
    }
    *response_size = encoded_size;
    return VC_QUICK_MENU_TRANSPORT_OK;
}

static vc_quick_menu_launcher_dependencies fuzz_dependencies(
    fuzz_platform *platform)
{
    vc_quick_menu_launcher_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.get_api_version = fuzz_get_api_version;
    dependencies.register_texture = fuzz_register;
    dependencies.register_label = fuzz_register;
    dependencies.register_widget = fuzz_register_widget;
    dependencies.register_callback = fuzz_register_callback;
    dependencies.start_worker = fuzz_start_worker;
    dependencies.update_status = fuzz_update;
    dependencies.signal_worker = fuzz_signal;
    dependencies.stop_worker = fuzz_cleanup;
    dependencies.unregister_callback = fuzz_cleanup;
    dependencies.unregister_widget = fuzz_cleanup;
    dependencies.unregister_label = fuzz_cleanup;
    dependencies.unregister_texture = fuzz_cleanup;
    dependencies.get_foreground = fuzz_foreground;
    dependencies.get_time = fuzz_time;
    dependencies.transport = fuzz_transport;
    dependencies.context = platform;
    dependencies.expected_api_version =
        VC_QUICK_MENU_LAUNCHER_ADAPTER_API_VERSION;
    return dependencies;
}

static void fuzz_set_foreground(fuzz_platform *platform,
                                uint32_t process_id,
                                uint64_t generation)
{
    static const uint8_t title[] = {
        'P', 'C', 'S', 'A', '0', '0', '0', '0', '1'
    };

    memset(&platform->foreground, 0,
           sizeof(platform->foreground));
    platform->foreground.sequence = 1;
    platform->foreground.present = 1;
    platform->foreground.target_process_id = process_id;
    platform->foreground.target_generation = generation;
    platform->foreground.title_id_size = sizeof(title);
    memcpy(platform->foreground.title_id, title, sizeof(title));
}

static void fuzz_check_invariants(
    const vc_quick_menu_launcher *launcher)
{
    fuzz_check(launcher->phase <=
               VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED);
    fuzz_check(launcher->action <=
               VC_QUICK_MENU_LAUNCHER_ACTION_STATUS);
    fuzz_check(vc_quick_menu_launcher_get_status(launcher) <=
               VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
    if (launcher->request_id != 0) {
        fuzz_check(launcher->request_snapshot.present == 1u);
        fuzz_check(
            launcher->request_snapshot.target_process_id != 0);
        fuzz_check(
            launcher->request_snapshot.target_generation != 0);
    }
    if (launcher->phase ==
        VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED) {
        fuzz_check(!launcher->callbacks_enabled);
        fuzz_check(!launcher->work_pending);
        fuzz_check(!launcher->in_flight);
        fuzz_check(launcher->request_id == 0);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    vc_quick_menu_launcher_dependencies dependencies;
    fuzz_platform platform;
    uint32_t process_id;
    uint64_t generation;
    size_t cursor;
    size_t step;

    if (data_size < 12) {
        return 0;
    }
    process_id = read_u32(data);
    if (process_id == 0) {
        process_id = 1;
    }
    generation = read_u64(data + 4);
    if (generation == 0) {
        generation = 1;
    }

    memset(&platform, 0, sizeof(platform));
    platform.api_ok = true;
    platform.foreground_ok = true;
    platform.time_ok = true;
    platform.resource_ok = true;
    platform.update_ok = true;
    platform.signal_ok = true;
    platform.api_version =
        VC_QUICK_MENU_LAUNCHER_ADAPTER_API_VERSION;
    platform.response_state = VC_LAUNCH_STATE_PENDING;
    fuzz_set_foreground(&platform, process_id, generation);
    dependencies = fuzz_dependencies(&platform);
    fuzz_check(vc_quick_menu_launcher_init(
                   &launcher, &dependencies) ==
               VC_QUICK_MENU_LAUNCHER_RESULT_OK);

    cursor = 12;
    for (step = 0;
         cursor < data_size &&
         step < VC_QUICK_MENU_LAUNCHER_FUZZ_MAX_STEPS;
         ++step) {
        const uint8_t command = data[cursor++];
        const uint8_t action = command & 7u;

        platform.api_ok = (command & 0x08u) == 0;
        platform.foreground_ok = (command & 0x10u) == 0;
        platform.time_ok = (command & 0x20u) == 0;
        platform.signal_ok = (command & 0x40u) == 0;
        platform.update_ok = (command & 0x80u) == 0;
        platform.resource_ok = (command & 0x04u) == 0;
        platform.transport_result =
            (vc_quick_menu_transport_result)(
                ((command >> 4) & 7u) %
                (VC_QUICK_MENU_TRANSPORT_FAILED + 1u));
        platform.response_state = command %
                                  (VC_LAUNCH_STATE_CLOCK_ROLLBACK + 1u);
        if ((command & 1u) != 0 && platform.now_ms != 0) {
            --platform.now_ms;
        } else if (platform.now_ms != UINT64_MAX) {
            ++platform.now_ms;
        }

        switch (action) {
        case 0:
            (void)vc_quick_menu_launcher_start(&launcher);
            break;
        case 1:
            (void)vc_quick_menu_launcher_handle_button(
                &launcher, launcher.generation);
            break;
        case 2:
            (void)vc_quick_menu_launcher_worker_step(
                &launcher, launcher.generation);
            break;
        case 3:
            (void)vc_quick_menu_launcher_refresh_status(
                &launcher);
            break;
        case 4:
            ++platform.foreground.sequence;
            if ((command & 8u) != 0) {
                memset(&platform.foreground, 0,
                       sizeof(platform.foreground));
                platform.foreground.sequence =
                    (uint64_t)step + 2u;
            } else {
                ++platform.foreground.target_generation;
            }
            break;
        case 5:
            (void)vc_quick_menu_launcher_reset(&launcher);
            break;
        case 6:
            (void)vc_quick_menu_launcher_stop(&launcher);
            break;
        default:
            (void)vc_quick_menu_launcher_stop(&launcher);
            (void)vc_quick_menu_launcher_start(&launcher);
            break;
        }
        fuzz_check_invariants(&launcher);
    }

    platform.resource_ok = true;
    (void)vc_quick_menu_launcher_destroy(&launcher);
    return 0;
}

#ifdef VC_QUICK_MENU_LAUNCHER_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[512];
    uint32_t random_state = UINT32_C(0x7151c9a3);
    unsigned int run;
    size_t index;

    for (run = 0; run < 10000; ++run) {
        for (index = 0; index < sizeof(data); ++index) {
            random_state =
                random_state * UINT32_C(1664525) +
                UINT32_C(1013904223);
            data[index] = (uint8_t)(random_state >> 24);
        }
        (void)LLVMFuzzerTestOneInput(data, sizeof(data));
    }
    return 0;
}
#endif
