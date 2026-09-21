#include "vitacheat/quick_menu_launcher.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

#define TARGET_PID UINT32_C(0x12345678)
#define TARGET_GENERATION UINT64_C(0x0102030405060708)
#define REQUEST_ID UINT64_C(0x2122232425262728)

typedef enum response_fault {
    RESPONSE_VALID = 0,
    RESPONSE_WRONG_OPERATION,
    RESPONSE_WRONG_ID,
    RESPONSE_WRONG_TARGET,
    RESPONSE_BAD_VERSION,
    RESPONSE_TRUNCATED,
    RESPONSE_OVERSIZED
} response_fault;

typedef enum snapshot_mutation {
    SNAPSHOT_MUTATION_NONE = 0,
    SNAPSHOT_MUTATION_GENERATION,
    SNAPSHOT_MUTATION_SEQUENCE,
    SNAPSHOT_MUTATION_TITLE
} snapshot_mutation;

typedef struct fake_platform {
    vc_quick_menu_launcher *launcher;
    vc_launch_foreground_snapshot foreground;
    vc_quick_menu_button_callback_fn button_callback;
    vc_quick_menu_worker_callback_fn worker_callback;
    void *button_context;
    void *worker_context;
    uint8_t last_request[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t retry_request[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint64_t button_generation;
    uint64_t worker_generation;
    uint64_t now_ms;
    uint64_t created_ms;
    uint64_t deadline_ms;
    uint32_t api_version;
    uint32_t response_state;
    vc_quick_menu_transport_result transport_result;
    response_fault response_fault;
    snapshot_mutation mutation;
    unsigned int mutate_on_foreground_call;
    unsigned int fail_registration_step;
    uint64_t fail_cleanup_token;
    bool api_ok;
    bool foreground_ok;
    bool time_ok;
    bool signal_ok;
    bool reenter;
    bool update_ok;
    unsigned int registration_calls;
    unsigned int update_calls;
    unsigned int signal_calls;
    unsigned int foreground_calls;
    unsigned int time_calls;
    unsigned int transport_calls;
    unsigned int reentry_attempts;
    unsigned int cleanup_count;
    uint64_t cleanup_order[16];
} fake_platform;

static void check_adapter_entry(fake_platform *platform)
{
    CHECK(atomic_load_explicit(
              &platform->launcher->transaction_busy,
              memory_order_acquire) == 0u);
    CHECK(atomic_load_explicit(
              &platform->launcher->adapter_active,
              memory_order_acquire) == 1u);
    if (platform->reenter) {
        CHECK(vc_quick_menu_launcher_handle_button(
                  platform->launcher,
                  platform->launcher->generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_BUSY);
        CHECK(vc_quick_menu_launcher_worker_step(
                  platform->launcher,
                  platform->launcher->generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_BUSY);
        ++platform->reentry_attempts;
    }
}

static bool fake_get_api_version(void *context, uint32_t *version)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    if (platform->api_ok) {
        *version = platform->api_version;
    }
    return platform->api_ok;
}

static bool fake_register(fake_platform *platform,
                          unsigned int step,
                          uint64_t token)
{
    check_adapter_entry(platform);
    ++platform->registration_calls;
    return platform->fail_registration_step != step && token != 0;
}

static bool fake_register_texture(void *context, uint64_t *token)
{
    fake_platform *platform = (fake_platform *)context;

    if (!fake_register(platform, 1, 11)) {
        return false;
    }
    *token = 11;
    return true;
}

static bool fake_register_label(void *context, uint64_t *token)
{
    fake_platform *platform = (fake_platform *)context;

    if (!fake_register(platform, 2, 22)) {
        return false;
    }
    *token = 22;
    return true;
}

static bool fake_register_widget(void *context,
                                 uint64_t texture_token,
                                 uint64_t label_token,
                                 uint64_t *token)
{
    fake_platform *platform = (fake_platform *)context;

    CHECK(texture_token == 11);
    CHECK(label_token == 22);
    if (!fake_register(platform, 3, 33)) {
        return false;
    }
    *token = 33;
    return true;
}

static bool fake_register_callback(
    void *context,
    uint64_t widget_token,
    vc_quick_menu_button_callback_fn callback,
    void *callback_context,
    uint64_t generation,
    uint64_t *token)
{
    fake_platform *platform = (fake_platform *)context;

    CHECK(widget_token == 33);
    if (!fake_register(platform, 4, 44)) {
        return false;
    }
    platform->button_callback = callback;
    platform->button_context = callback_context;
    platform->button_generation = generation;
    *token = 44;
    return true;
}

static bool fake_start_worker(
    void *context,
    vc_quick_menu_worker_callback_fn callback,
    void *callback_context,
    uint64_t generation,
    uint64_t *token)
{
    fake_platform *platform = (fake_platform *)context;

    if (!fake_register(platform, 5, 55)) {
        return false;
    }
    platform->worker_callback = callback;
    platform->worker_context = callback_context;
    platform->worker_generation = generation;
    *token = 55;
    return true;
}

static bool fake_update_status(void *context,
                               uint64_t label_token,
                               vc_quick_menu_launcher_status status)
{
    fake_platform *platform = (fake_platform *)context;

    (void)status;
    check_adapter_entry(platform);
    CHECK(label_token == 22);
    ++platform->update_calls;
    if (platform->fail_registration_step == 6 &&
        platform->update_calls == 1) {
        return false;
    }
    return platform->update_ok;
}

static bool fake_signal_worker(void *context, uint64_t worker_token)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    CHECK(worker_token == 55);
    ++platform->signal_calls;
    return platform->signal_ok;
}

static bool fake_cleanup(void *context, uint64_t token)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    CHECK(platform->cleanup_count <
          sizeof(platform->cleanup_order) /
              sizeof(platform->cleanup_order[0]));
    platform->cleanup_order[platform->cleanup_count++] = token;
    return platform->fail_cleanup_token != token;
}

static void mutate_foreground(fake_platform *platform)
{
    switch (platform->mutation) {
    case SNAPSHOT_MUTATION_GENERATION:
        ++platform->foreground.sequence;
        ++platform->foreground.target_generation;
        break;
    case SNAPSHOT_MUTATION_SEQUENCE:
        ++platform->foreground.sequence;
        break;
    case SNAPSHOT_MUTATION_TITLE:
        ++platform->foreground.sequence;
        platform->foreground.title_id[
            platform->foreground.title_id_size - 1u] ^= 1u;
        break;
    case SNAPSHOT_MUTATION_NONE:
    default:
        break;
    }
}

static bool fake_get_foreground(
    void *context,
    vc_launch_foreground_snapshot *snapshot)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    ++platform->foreground_calls;
    if (platform->foreground_calls ==
        platform->mutate_on_foreground_call) {
        mutate_foreground(platform);
    }
    if (platform->foreground_ok) {
        *snapshot = platform->foreground;
    }
    return platform->foreground_ok;
}

static bool fake_get_time(void *context, uint64_t *now_ms)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    ++platform->time_calls;
    if (platform->time_ok) {
        *now_ms = platform->now_ms;
    }
    return platform->time_ok;
}

static int32_t status_for_state(uint32_t state)
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

static vc_quick_menu_transport_result fake_transport(
    void *context,
    const uint8_t *request_wire,
    size_t request_size,
    uint8_t *response_wire,
    size_t response_capacity,
    size_t *response_size)
{
    fake_platform *platform = (fake_platform *)context;
    vc_launch_request request;
    vc_launch_response response;
    size_t encoded_size = 0;

    check_adapter_entry(platform);
    ++platform->transport_calls;
    CHECK(request_size == VC_LAUNCH_REQUEST_WIRE_SIZE);
    CHECK(response_capacity == VC_LAUNCH_RESPONSE_WIRE_SIZE);
    if (platform->transport_calls == 1) {
        memcpy(platform->last_request, request_wire, request_size);
    } else {
        memcpy(platform->retry_request, request_wire, request_size);
    }
    *response_size = 0;
    if (platform->transport_result != VC_QUICK_MENU_TRANSPORT_OK) {
        return platform->transport_result;
    }

    CHECK(vc_launch_request_decode(request_wire, request_size,
                                   &request) == VC_LAUNCH_STATUS_OK);
    memset(&response, 0, sizeof(response));
    response.version = VC_LAUNCH_ABI_VERSION;
    response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response.operation = request.operation;
    response.status = status_for_state(platform->response_state);
    response.launch_state = platform->response_state;
    response.capabilities = request.capabilities;
    response.target_process_id = request.target_process_id;
    response.target_generation = request.target_generation;
    response.request_id =
        request.operation == VC_LAUNCH_OPERATION_SUBMIT
            ? REQUEST_ID
            : request.request_id;
    response.created_ms =
        platform->created_ms != 0 ? platform->created_ms : request.now_ms;
    response.deadline_ms =
        platform->deadline_ms != 0
            ? platform->deadline_ms
            : response.created_ms + VC_LAUNCH_DEFAULT_TTL_MS;
    response.observed_ms = request.now_ms;

    switch (platform->response_fault) {
    case RESPONSE_WRONG_OPERATION:
        response.operation =
            request.operation == VC_LAUNCH_OPERATION_SUBMIT
                ? VC_LAUNCH_OPERATION_STATUS
                : VC_LAUNCH_OPERATION_SUBMIT;
        response.capabilities =
            response.operation == VC_LAUNCH_OPERATION_SUBMIT
                ? VC_LAUNCH_CAPABILITY_LAUNCH
                : VC_LAUNCH_CAPABILITY_STATUS;
        break;
    case RESPONSE_WRONG_ID:
        ++response.request_id;
        break;
    case RESPONSE_WRONG_TARGET:
        ++response.target_process_id;
        break;
    case RESPONSE_VALID:
    case RESPONSE_BAD_VERSION:
    case RESPONSE_TRUNCATED:
    case RESPONSE_OVERSIZED:
    default:
        break;
    }

    CHECK(vc_launch_response_encode(
              &response, response_wire, response_capacity,
              &encoded_size) == VC_LAUNCH_STATUS_OK);
    if (platform->response_fault == RESPONSE_BAD_VERSION) {
        response_wire[0] = 2;
    }
    if (platform->response_fault == RESPONSE_TRUNCATED) {
        *response_size = VC_LAUNCH_RESPONSE_WIRE_SIZE - 1u;
    } else if (platform->response_fault == RESPONSE_OVERSIZED) {
        *response_size = VC_LAUNCH_RESPONSE_WIRE_SIZE + 1u;
    } else {
        *response_size = encoded_size;
    }
    return VC_QUICK_MENU_TRANSPORT_OK;
}

static void set_foreground(fake_platform *platform,
                           uint64_t sequence,
                           uint32_t process_id,
                           uint64_t generation,
                           const char *title_id)
{
    size_t title_size = title_id != NULL ? strlen(title_id) : 0;

    memset(&platform->foreground, 0, sizeof(platform->foreground));
    platform->foreground.sequence = sequence;
    if (title_id == NULL) {
        return;
    }
    CHECK(title_size <= VC_LAUNCH_SERVICE_TITLE_ID_MAX);
    platform->foreground.present = 1;
    platform->foreground.target_process_id = process_id;
    platform->foreground.target_generation = generation;
    platform->foreground.title_id_size = (uint32_t)title_size;
    memcpy(platform->foreground.title_id, title_id, title_size);
}

static vc_quick_menu_launcher_dependencies dependencies_for(
    fake_platform *platform)
{
    vc_quick_menu_launcher_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.get_api_version = fake_get_api_version;
    dependencies.register_texture = fake_register_texture;
    dependencies.register_label = fake_register_label;
    dependencies.register_widget = fake_register_widget;
    dependencies.register_callback = fake_register_callback;
    dependencies.start_worker = fake_start_worker;
    dependencies.update_status = fake_update_status;
    dependencies.signal_worker = fake_signal_worker;
    dependencies.stop_worker = fake_cleanup;
    dependencies.unregister_callback = fake_cleanup;
    dependencies.unregister_widget = fake_cleanup;
    dependencies.unregister_label = fake_cleanup;
    dependencies.unregister_texture = fake_cleanup;
    dependencies.get_foreground = fake_get_foreground;
    dependencies.get_time = fake_get_time;
    dependencies.transport = fake_transport;
    dependencies.context = platform;
    dependencies.expected_api_version =
        VC_QUICK_MENU_LAUNCHER_ADAPTER_API_VERSION;
    return dependencies;
}

static void initialize(vc_quick_menu_launcher *launcher,
                       fake_platform *platform)
{
    vc_quick_menu_launcher_dependencies dependencies;

    memset(launcher, 0, sizeof(*launcher));
    memset(platform, 0, sizeof(*platform));
    platform->launcher = launcher;
    platform->api_ok = true;
    platform->api_version =
        VC_QUICK_MENU_LAUNCHER_ADAPTER_API_VERSION;
    platform->foreground_ok = true;
    platform->time_ok = true;
    platform->signal_ok = true;
    platform->update_ok = true;
    platform->now_ms = UINT64_C(0x1112131415161718);
    platform->response_state = VC_LAUNCH_STATE_PENDING;
    platform->transport_result = VC_QUICK_MENU_TRANSPORT_OK;
    set_foreground(platform, 1, TARGET_PID, TARGET_GENERATION,
                   "PCSA00001");
    dependencies = dependencies_for(platform);
    CHECK(vc_quick_menu_launcher_init(launcher, &dependencies) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
}

static void prepare(vc_quick_menu_launcher *launcher,
                    fake_platform *platform)
{
    initialize(launcher, platform);
    CHECK(vc_quick_menu_launcher_start(launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(launcher->phase ==
          VC_QUICK_MENU_LAUNCHER_PHASE_RUNNING);
    CHECK(vc_quick_menu_launcher_get_status(launcher) ==
          VC_QUICK_MENU_LAUNCHER_STATUS_READY);
}

static void queue_and_submit(vc_quick_menu_launcher *launcher,
                             fake_platform *platform)
{
    CHECK(vc_quick_menu_launcher_handle_button(
              launcher, launcher->generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              launcher, launcher->generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(platform->transport_calls == 1);
    CHECK(launcher->request_id == REQUEST_ID);
    CHECK(vc_quick_menu_launcher_get_status(launcher) ==
          VC_QUICK_MENU_LAUNCHER_STATUS_PENDING_CLAIM);
}

static void test_full_lifecycle_and_rollback(void)
{
    unsigned int fail_step;

    for (fail_step = 1; fail_step <= 6; ++fail_step) {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;
        size_t expected_cleanup =
            fail_step == 1 ? 0u :
            fail_step == 2 ? 1u :
            fail_step == 3 ? 2u :
            fail_step == 4 ? 3u :
            fail_step == 5 ? 4u : 5u;
        static const uint64_t reverse_tokens[] = {
            55, 44, 33, 22, 11
        };
        size_t index;

        initialize(&launcher, &platform);
        platform.fail_registration_step = fail_step;
        CHECK(vc_quick_menu_launcher_start(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE);
        CHECK(launcher.phase ==
              VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED);
        CHECK(platform.cleanup_count == expected_cleanup);
        for (index = 0; index < expected_cleanup; ++index) {
            CHECK(platform.cleanup_order[index] ==
                  reverse_tokens[5u - expected_cleanup + index]);
        }
        CHECK(launcher.texture_token == 0);
        CHECK(launcher.label_token == 0);
        CHECK(launcher.widget_token == 0);
        CHECK(launcher.callback_token == 0);
        CHECK(launcher.worker_token == 0);
        CHECK(vc_quick_menu_launcher_destroy(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    }

    {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;
        vc_quick_menu_button_callback_fn old_button;
        vc_quick_menu_worker_callback_fn old_worker;
        void *old_button_context;
        void *old_worker_context;
        uint64_t old_generation;

        prepare(&launcher, &platform);
        CHECK(vc_quick_menu_launcher_start(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        old_button = platform.button_callback;
        old_worker = platform.worker_callback;
        old_button_context = platform.button_context;
        old_worker_context = platform.worker_context;
        old_generation = platform.button_generation;
        platform.fail_cleanup_token = 33;
        CHECK(vc_quick_menu_launcher_stop(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_CLEANUP_PENDING);
        CHECK(launcher.phase ==
              VC_QUICK_MENU_LAUNCHER_PHASE_STOPPED);
        CHECK(launcher.widget_token == 33);
        CHECK(launcher.worker_token == 0);
        CHECK(launcher.callback_token == 0);
        CHECK(launcher.label_token == 0);
        CHECK(launcher.texture_token == 0);
        CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_STATUS_STOPPED);
        CHECK(vc_quick_menu_launcher_start(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_CLEANUP_PENDING);
        platform.fail_cleanup_token = 0;
        CHECK(vc_quick_menu_launcher_stop(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(platform.cleanup_order[platform.cleanup_count - 1u] == 33);

        old_button(old_button_context, old_generation);
        old_worker(old_worker_context, old_generation);
        CHECK(platform.transport_calls == 0);
        CHECK(vc_quick_menu_launcher_start(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        old_button(old_button_context, old_generation);
        old_worker(old_worker_context, old_generation);
        CHECK(platform.transport_calls == 0);
        CHECK(platform.signal_calls == 1);
        CHECK(vc_quick_menu_launcher_stop(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_destroy(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_destroy(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(launcher.marker == 0);
    }
}

static void test_api_fail_closed(void)
{
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    fake_platform platform;

    initialize(&launcher, &platform);
    platform.api_ok = false;
    CHECK(vc_quick_menu_launcher_start(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE);
    CHECK(platform.registration_calls == 0);
    CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_STATUS_UNAVAILABLE);

    platform.api_ok = true;
    platform.api_version++;
    CHECK(vc_quick_menu_launcher_start(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_INCOMPATIBLE);
    CHECK(platform.registration_calls == 0);
    CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_STATUS_INCOMPATIBLE);
    platform.api_version =
        VC_QUICK_MENU_LAUNCHER_ADAPTER_API_VERSION;
    CHECK(vc_quick_menu_launcher_start(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
}

static void test_short_callback_and_golden_request(void)
{
    static const uint8_t expected[VC_LAUNCH_REQUEST_WIRE_SIZE] = {
        0x01, 0x00, 0x40, 0x00, 0x01, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
        0x88, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    fake_platform platform;
    vc_launch_request decoded;

    prepare(&launcher, &platform);
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(platform.signal_calls == 1);
    CHECK(platform.transport_calls == 0);
    CHECK(platform.time_calls == 0);
    CHECK(launcher.work_pending);
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION);
    CHECK(platform.signal_calls == 1);

    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(memcmp(platform.last_request, expected, sizeof(expected)) == 0);
    CHECK(vc_launch_request_decode(
              platform.last_request, sizeof(platform.last_request),
              &decoded) == VC_LAUNCH_STATUS_OK);
    CHECK(decoded.operation == VC_LAUNCH_OPERATION_SUBMIT);
    CHECK(decoded.caller_role == VC_LAUNCH_CALLER_SCE_SHELL);
    CHECK(decoded.capabilities == VC_LAUNCH_CAPABILITY_LAUNCH);
    CHECK(decoded.request_id == 0);
    CHECK(decoded.presentation_ready == 0);
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION);

    CHECK(vc_quick_menu_launcher_refresh_status(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_launch_request_decode(
              platform.retry_request, sizeof(platform.retry_request),
              &decoded) == VC_LAUNCH_STATUS_OK);
    CHECK(decoded.operation == VC_LAUNCH_OPERATION_STATUS);
    CHECK(decoded.caller_role == VC_LAUNCH_CALLER_SCE_SHELL);
    CHECK(decoded.capabilities == VC_LAUNCH_CAPABILITY_STATUS);
    CHECK(decoded.request_id == REQUEST_ID);
    CHECK(decoded.ttl_ms == 0);
    CHECK(decoded.presentation_ready == 0);
}

static void test_idempotence_terminal_states(void)
{
    const uint32_t terminal_states[] = {
        VC_LAUNCH_STATE_CLAIMED,
        VC_LAUNCH_STATE_EXPIRED
    };
    const vc_quick_menu_launcher_status expected_statuses[] = {
        VC_QUICK_MENU_LAUNCHER_STATUS_CONSUMED,
        VC_QUICK_MENU_LAUNCHER_STATUS_EXPIRED
    };
    size_t index;

    for (index = 0;
         index < sizeof(terminal_states) / sizeof(terminal_states[0]);
         ++index) {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;
        unsigned int signal_calls;

        prepare(&launcher, &platform);
        queue_and_submit(&launcher, &platform);
        platform.response_state = terminal_states[index];
        CHECK(vc_quick_menu_launcher_refresh_status(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_worker_step(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
              expected_statuses[index]);
        signal_calls = platform.signal_calls;
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION);
        CHECK(platform.signal_calls == signal_calls);
        CHECK(vc_quick_menu_launcher_reset(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_STATUS_READY);
    }

    {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;

        prepare(&launcher, &platform);
        platform.transport_result = VC_QUICK_MENU_TRANSPORT_FAILED;
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_worker_step(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_TRANSPORT_FAILURE);
        CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION);
        CHECK(vc_quick_menu_launcher_reset(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    }
}

static void test_snapshot_boundaries_fail_closed(void)
{
    unsigned int mutation_call;

    for (mutation_call = 2; mutation_call <= 5; ++mutation_call) {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;

        prepare(&launcher, &platform);
        platform.mutation = SNAPSHOT_MUTATION_GENERATION;
        platform.mutate_on_foreground_call = mutation_call;
        if (mutation_call == 2) {
            CHECK(vc_quick_menu_launcher_handle_button(
                      &launcher, launcher.generation) ==
                  VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET);
        } else {
            CHECK(vc_quick_menu_launcher_handle_button(
                      &launcher, launcher.generation) ==
                  VC_QUICK_MENU_LAUNCHER_RESULT_OK);
            CHECK(vc_quick_menu_launcher_worker_step(
                      &launcher, launcher.generation) ==
                  VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET);
        }
        CHECK(launcher.request_id == 0);
        CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_STATUS_STALE_TITLE);
        CHECK(platform.transport_calls ==
              (mutation_call == 5 ? 1u : 0u));
    }

    {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;

        prepare(&launcher, &platform);
        set_foreground(&platform, 2, 0, 0, NULL);
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE);
        CHECK(platform.transport_calls == 0);
    }

    {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;

        prepare(&launcher, &platform);
        set_foreground(&platform, 2, 0, 0, NULL);
        platform.foreground.title_id[0] = 1;
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET);
        CHECK(platform.transport_calls == 0);
    }

    {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;

        prepare(&launcher, &platform);
        platform.mutation = SNAPSHOT_MUTATION_TITLE;
        platform.mutate_on_foreground_call = 4;
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_worker_step(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET);
        CHECK(platform.transport_calls == 0);
    }

    {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;

        prepare(&launcher, &platform);
        platform.mutation = SNAPSHOT_MUTATION_SEQUENCE;
        platform.mutate_on_foreground_call = 3;
        CHECK(vc_quick_menu_launcher_handle_button(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        CHECK(vc_quick_menu_launcher_worker_step(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET);
    }
}

static void test_status_refresh_snapshot_change(void)
{
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    fake_platform platform;

    prepare(&launcher, &platform);
    queue_and_submit(&launcher, &platform);
    ++platform.foreground.sequence;
    ++platform.foreground.target_generation;
    CHECK(vc_quick_menu_launcher_refresh_status(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET);
    CHECK(platform.transport_calls == 1);
    CHECK(launcher.request_id == 0);

    CHECK(vc_quick_menu_launcher_destroy(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    prepare(&launcher, &platform);
    queue_and_submit(&launcher, &platform);
    platform.mutation = SNAPSHOT_MUTATION_TITLE;
    platform.mutate_on_foreground_call = 10;
    CHECK(vc_quick_menu_launcher_refresh_status(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_STALE_TARGET);
    CHECK(platform.transport_calls == 2);
    CHECK(launcher.request_id == 0);
}

static void test_transport_validation(void)
{
    const response_fault faults[] = {
        RESPONSE_WRONG_OPERATION,
        RESPONSE_WRONG_ID,
        RESPONSE_WRONG_TARGET,
        RESPONSE_BAD_VERSION,
        RESPONSE_TRUNCATED,
        RESPONSE_OVERSIZED
    };
    size_t index;

    for (index = 0; index < sizeof(faults) / sizeof(faults[0]);
         ++index) {
        vc_quick_menu_launcher launcher =
            VC_QUICK_MENU_LAUNCHER_INITIALIZER;
        fake_platform platform;

        prepare(&launcher, &platform);
        if (faults[index] == RESPONSE_WRONG_ID) {
            queue_and_submit(&launcher, &platform);
            platform.response_fault = faults[index];
            CHECK(vc_quick_menu_launcher_refresh_status(&launcher) ==
                  VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        } else {
            platform.response_fault = faults[index];
            CHECK(vc_quick_menu_launcher_handle_button(
                      &launcher, launcher.generation) ==
                  VC_QUICK_MENU_LAUNCHER_RESULT_OK);
        }
        CHECK(vc_quick_menu_launcher_worker_step(
                  &launcher, launcher.generation) ==
              VC_QUICK_MENU_LAUNCHER_RESULT_PROTOCOL_FAILURE);
        CHECK(launcher.request_id == 0);
        CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
              VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);
    }
}

static void test_retry_failures_and_clock(void)
{
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    fake_platform platform;

    prepare(&launcher, &platform);
    platform.transport_result = VC_QUICK_MENU_TRANSPORT_RETRY;
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_RETRY);
    CHECK(platform.transport_calls == 1);
    CHECK(launcher.request_wire_valid);
    platform.transport_result = VC_QUICK_MENU_TRANSPORT_OK;
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(platform.transport_calls == 2);
    CHECK(memcmp(platform.last_request, platform.retry_request,
                 sizeof(platform.last_request)) == 0);

    CHECK(vc_quick_menu_launcher_reset(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    memset(platform.last_request, 0, sizeof(platform.last_request));
    memset(platform.retry_request, 0, sizeof(platform.retry_request));
    platform.transport_calls = 0;
    platform.transport_result = VC_QUICK_MENU_TRANSPORT_BUSY;
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_RETRY);
    CHECK(!launcher.request_wire_valid);
    ++platform.now_ms;
    platform.transport_result = VC_QUICK_MENU_TRANSPORT_OK;
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(memcmp(platform.last_request, platform.retry_request,
                 sizeof(platform.last_request)) != 0);

    CHECK(vc_quick_menu_launcher_reset(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    platform.now_ms--;
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_CLOCK_ROLLBACK);
    CHECK(platform.transport_calls == 2);

    CHECK(vc_quick_menu_launcher_reset(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    platform.now_ms += 10;
    platform.signal_ok = false;
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE);
    CHECK(!launcher.work_pending);
    CHECK(platform.transport_calls == 2);

    platform.signal_ok = true;
    CHECK(vc_quick_menu_launcher_reset(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    platform.transport_result = VC_QUICK_MENU_TRANSPORT_UNAVAILABLE;
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE);
    CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_STATUS_UNAVAILABLE);
    platform.transport_result = VC_QUICK_MENU_TRANSPORT_OK;
    CHECK(vc_quick_menu_launcher_reset(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
}

static void test_adapter_failures(void)
{
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    fake_platform platform;

    prepare(&launcher, &platform);
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    platform.update_ok = false;
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE);
    CHECK(platform.transport_calls == 0);
    CHECK(vc_quick_menu_launcher_get_status(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_STATUS_ERROR);

    CHECK(vc_quick_menu_launcher_destroy(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    prepare(&launcher, &platform);
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    platform.time_ok = false;
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_PLATFORM_FAILURE);
    CHECK(platform.transport_calls == 0);

    CHECK(vc_quick_menu_launcher_destroy(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    prepare(&launcher, &platform);
    platform.foreground_ok = false;
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_UNAVAILABLE);
    CHECK(platform.signal_calls == 0);
    CHECK(platform.transport_calls == 0);
}

static void test_reentrancy_and_bounded_work(void)
{
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    fake_platform platform;
    unsigned int attempts_before;

    initialize(&launcher, &platform);
    platform.reenter = true;
    CHECK(vc_quick_menu_launcher_start(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(platform.reentry_attempts == 7);

    attempts_before = platform.reentry_attempts;
    CHECK(vc_quick_menu_launcher_handle_button(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(platform.reentry_attempts == attempts_before + 3u);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(platform.transport_calls == 1);
    CHECK(platform.reentry_attempts > attempts_before + 3u);
    CHECK(vc_quick_menu_launcher_worker_step(
              &launcher, launcher.generation) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_NO_ACTION);
    CHECK(platform.transport_calls == 1);
    attempts_before = platform.reentry_attempts;
    CHECK(vc_quick_menu_launcher_stop(&launcher) ==
          VC_QUICK_MENU_LAUNCHER_RESULT_OK);
    CHECK(platform.reentry_attempts == attempts_before + 5u);
}

static void test_status_formatting(void)
{
    vc_quick_menu_launcher launcher =
        VC_QUICK_MENU_LAUNCHER_INITIALIZER;
    fake_platform platform;
    char one[1] = {'x'};
    char short_buffer[8];
    char exact[17];
    char large[128];
    size_t length;

    prepare(&launcher, &platform);
    length = vc_quick_menu_launcher_format_status(
        &launcher, NULL, 0);
    CHECK(length == strlen("Ready to request"));
    CHECK(vc_quick_menu_launcher_format_status(
              &launcher, one, sizeof(one)) == length);
    CHECK(one[0] == '\0');
    memset(short_buffer, 'x', sizeof(short_buffer));
    CHECK(vc_quick_menu_launcher_format_status(
              &launcher, short_buffer,
              sizeof(short_buffer)) == length);
    CHECK(short_buffer[sizeof(short_buffer) - 1u] == '\0');
    CHECK(vc_quick_menu_launcher_format_status(
              &launcher, exact, sizeof(exact)) == length);
    CHECK(strcmp(exact, "Ready to request") == 0);

    queue_and_submit(&launcher, &platform);
    CHECK(vc_quick_menu_launcher_format_status(
              &launcher, large, sizeof(large)) ==
          strlen("Request pending; waiting for game claim"));
    CHECK(strcmp(large,
                 "Request pending; waiting for game claim") == 0);
    CHECK(strstr(large, "PCSA") == NULL);
    CHECK(strstr(large, "12345678") == NULL);
    CHECK(strstr(large, "212223") == NULL);
}

int main(void)
{
    test_full_lifecycle_and_rollback();
    test_api_fail_closed();
    test_short_callback_and_golden_request();
    test_idempotence_terminal_states();
    test_snapshot_boundaries_fail_closed();
    test_status_refresh_snapshot_change();
    test_transport_validation();
    test_retry_failures_and_clock();
    test_adapter_failures();
    test_reentrancy_and_bounded_work();
    test_status_formatting();

    if (failures != 0) {
        fprintf(stderr, "%d Quick Menu launcher test(s) failed\n",
                failures);
        return 1;
    }
    puts("Quick Menu launcher tests passed");
    return 0;
}
