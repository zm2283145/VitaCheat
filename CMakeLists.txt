cmake_minimum_required(VERSION 3.16)

project(VitaCheat VERSION 0.1.0 LANGUAGES C)

option(VITACHEAT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(VITACHEAT_BUILD_FUZZERS "Build libFuzzer targets" OFF)

add_library(vitacheat_core STATIC
    src/launch_broker.c
    src/launch_claimant.c
    src/launch_service.c
    src/legacy_psv.c
    src/menu_activation.c
    src/menu_coordinator.c
    src/pause.c
    src/quick_menu_launcher.c
    src/search.c
)

target_include_directories(vitacheat_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vitacheat_core PUBLIC c_std_11)

if(MSVC)
    target_compile_options(vitacheat_core
        PUBLIC /experimental:c11atomics
        PRIVATE /W4 /WX
    )
else()
    target_compile_options(vitacheat_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(VITACHEAT_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vitacheat_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(vitacheat_core PRIVATE -fsanitize=address,undefined)
endif()

if(VITACHEAT_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "VITACHEAT_BUILD_FUZZERS requires Clang with libFuzzer")
    endif()
    if(NOT VITACHEAT_ENABLE_SANITIZERS)
        message(FATAL_ERROR "VITACHEAT_BUILD_FUZZERS requires VITACHEAT_ENABLE_SANITIZERS=ON")
    endif()

    add_executable(vitacheat_search_fuzzer
        tests/fuzz/fuzz_search.c
    )

    add_executable(vitacheat_launch_broker_fuzzer
        tests/fuzz/fuzz_launch_broker.c
    )

    add_executable(vitacheat_launch_service_fuzzer
        tests/fuzz/fuzz_launch_service.c
    )

    add_executable(vitacheat_quick_menu_launcher_fuzzer
        tests/fuzz/fuzz_quick_menu_launcher.c
    )

    add_executable(vitacheat_launch_claimant_fuzzer
        tests/fuzz/fuzz_launch_claimant.c
    )

    add_executable(vitacheat_menu_coordinator_fuzzer
        tests/fuzz/fuzz_menu_coordinator.c
    )

    target_link_libraries(vitacheat_search_fuzzer PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_launch_broker_fuzzer PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_launch_service_fuzzer PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_quick_menu_launcher_fuzzer PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_launch_claimant_fuzzer PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_menu_coordinator_fuzzer PRIVATE vitacheat_core)
    target_compile_features(vitacheat_search_fuzzer PRIVATE c_std_11)
    target_compile_features(vitacheat_launch_broker_fuzzer PRIVATE c_std_11)
    target_compile_features(vitacheat_launch_service_fuzzer PRIVATE c_std_11)
    target_compile_features(vitacheat_quick_menu_launcher_fuzzer PRIVATE c_std_11)
    target_compile_features(vitacheat_launch_claimant_fuzzer PRIVATE c_std_11)
    target_compile_features(vitacheat_menu_coordinator_fuzzer PRIVATE c_std_11)
    target_compile_options(vitacheat_search_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_search_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_compile_options(vitacheat_launch_broker_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_launch_broker_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_compile_options(vitacheat_launch_service_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_launch_service_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_compile_options(vitacheat_quick_menu_launcher_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_quick_menu_launcher_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_compile_options(vitacheat_launch_claimant_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_launch_claimant_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_compile_options(vitacheat_menu_coordinator_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_menu_coordinator_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(vitacheat_host_tests
        tests/host/test_search.c
    )

    add_executable(vitacheat_psv_tests
        tests/host/test_legacy_psv.c
    )

    add_executable(vitacheat_activation_tests
        tests/host/test_menu_activation.c
    )

    add_executable(vitacheat_pause_tests
        tests/host/test_pause.c
    )

    add_executable(vitacheat_launch_broker_tests
        tests/host/test_launch_broker.c
    )

    add_executable(vitacheat_launch_service_tests
        tests/host/test_launch_service.c
    )

    add_executable(vitacheat_quick_menu_launcher_tests
        tests/host/test_quick_menu_launcher.c
    )

    add_executable(vitacheat_launch_claimant_tests
        tests/host/test_launch_claimant.c
    )

    add_executable(vitacheat_menu_coordinator_tests
        tests/host/test_menu_coordinator.c
    )

    target_link_libraries(vitacheat_host_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_psv_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_activation_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_pause_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_launch_broker_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_launch_service_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_quick_menu_launcher_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_launch_claimant_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_menu_coordinator_tests PRIVATE vitacheat_core)
    target_compile_features(vitacheat_host_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_psv_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_activation_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_pause_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_launch_broker_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_launch_service_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_quick_menu_launcher_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_launch_claimant_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_menu_coordinator_tests PRIVATE c_std_11)

    if(MSVC)
        target_compile_options(vitacheat_host_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_psv_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_activation_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_pause_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_launch_broker_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_launch_service_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_quick_menu_launcher_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_launch_claimant_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_menu_coordinator_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(vitacheat_host_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_psv_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_activation_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_pause_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_launch_broker_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_launch_service_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_quick_menu_launcher_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_launch_claimant_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_menu_coordinator_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    if(VITACHEAT_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(vitacheat_host_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_host_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_psv_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_psv_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_activation_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_activation_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_pause_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_pause_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_launch_broker_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_launch_broker_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_launch_service_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_launch_service_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_quick_menu_launcher_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_quick_menu_launcher_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_launch_claimant_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_launch_claimant_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_menu_coordinator_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_menu_coordinator_tests PRIVATE -fsanitize=address,undefined)
    endif()

    add_test(NAME vitacheat_host_tests COMMAND vitacheat_host_tests)
    add_test(NAME vitacheat_psv_tests COMMAND vitacheat_psv_tests)
    add_test(NAME vitacheat_activation_tests COMMAND vitacheat_activation_tests)
    add_test(NAME vitacheat_pause_tests COMMAND vitacheat_pause_tests)
    add_test(NAME vitacheat_launch_broker_tests COMMAND vitacheat_launch_broker_tests)
    add_test(NAME vitacheat_launch_service_tests COMMAND vitacheat_launch_service_tests)
    add_test(NAME vitacheat_quick_menu_launcher_tests COMMAND vitacheat_quick_menu_launcher_tests)
    add_test(NAME vitacheat_launch_claimant_tests COMMAND vitacheat_launch_claimant_tests)
    add_test(NAME vitacheat_menu_coordinator_tests COMMAND vitacheat_menu_coordinator_tests)
endif()
