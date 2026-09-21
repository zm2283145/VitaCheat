cmake_minimum_required(VERSION 3.16)

project(VitaCheat VERSION 0.1.0 LANGUAGES C)

option(VITACHEAT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(VITACHEAT_BUILD_FUZZERS "Build libFuzzer targets" OFF)

add_library(vitacheat_core STATIC
    src/legacy_plan.c
    src/legacy_psv.c
    src/menu_activation.c
    src/pause.c
    src/search.c
)

target_include_directories(vitacheat_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vitacheat_core PUBLIC c_std_11)

if(MSVC)
    target_compile_options(vitacheat_core PRIVATE /W4 /WX)
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

    add_executable(vitacheat_legacy_plan_fuzzer
        tests/fuzz/fuzz_legacy_plan.c
    )

    target_link_libraries(vitacheat_search_fuzzer PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_legacy_plan_fuzzer PRIVATE vitacheat_core)
    target_compile_features(vitacheat_search_fuzzer PRIVATE c_std_11)
    target_compile_features(vitacheat_legacy_plan_fuzzer PRIVATE c_std_11)
    target_compile_options(vitacheat_search_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_search_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_compile_options(vitacheat_legacy_plan_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_legacy_plan_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(vitacheat_host_tests
        tests/host/test_search.c
    )

    add_executable(vitacheat_psv_tests
        tests/host/test_legacy_psv.c
    )

    add_executable(vitacheat_legacy_plan_tests
        tests/host/test_legacy_plan.c
    )

    add_executable(vitacheat_legacy_pointer_plan_tests
        tests/host/test_legacy_pointer_plan.c
    )

    add_executable(vitacheat_activation_tests
        tests/host/test_menu_activation.c
    )

    add_executable(vitacheat_pause_tests
        tests/host/test_pause.c
    )

    target_link_libraries(vitacheat_host_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_psv_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_legacy_plan_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_legacy_pointer_plan_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_activation_tests PRIVATE vitacheat_core)
    target_link_libraries(vitacheat_pause_tests PRIVATE vitacheat_core)
    target_compile_features(vitacheat_host_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_psv_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_legacy_plan_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_legacy_pointer_plan_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_activation_tests PRIVATE c_std_11)
    target_compile_features(vitacheat_pause_tests PRIVATE c_std_11)

    if(MSVC)
        target_compile_options(vitacheat_host_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_psv_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_legacy_plan_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_legacy_pointer_plan_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_activation_tests PRIVATE /W4 /WX)
        target_compile_options(vitacheat_pause_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(vitacheat_host_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_psv_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_legacy_plan_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_legacy_pointer_plan_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_activation_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(vitacheat_pause_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    if(VITACHEAT_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(vitacheat_host_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_host_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_psv_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_psv_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_legacy_plan_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_legacy_plan_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_legacy_pointer_plan_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_legacy_pointer_plan_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_activation_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_activation_tests PRIVATE -fsanitize=address,undefined)
        target_compile_options(vitacheat_pause_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_pause_tests PRIVATE -fsanitize=address,undefined)
    endif()

    add_test(NAME vitacheat_host_tests COMMAND vitacheat_host_tests)
    add_test(NAME vitacheat_psv_tests COMMAND vitacheat_psv_tests)
    add_test(NAME vitacheat_legacy_plan_tests COMMAND vitacheat_legacy_plan_tests)
    add_test(NAME vitacheat_legacy_pointer_plan_tests COMMAND vitacheat_legacy_pointer_plan_tests)
    add_test(NAME vitacheat_activation_tests COMMAND vitacheat_activation_tests)
    add_test(NAME vitacheat_pause_tests COMMAND vitacheat_pause_tests)
endif()
