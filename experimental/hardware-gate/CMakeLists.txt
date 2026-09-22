add_library(vitacheat_hardware_gate_core STATIC
    src/hardware_gate.c
)

target_include_directories(vitacheat_hardware_gate_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(vitacheat_hardware_gate_core PUBLIC c_std_11)

if(MSVC)
    target_compile_options(vitacheat_hardware_gate_core
        PUBLIC /experimental:c11atomics
        PRIVATE /W4 /WX
    )
else()
    target_compile_options(vitacheat_hardware_gate_core
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(VITACHEAT_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vitacheat_hardware_gate_core
        PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_hardware_gate_core
        PRIVATE -fsanitize=address,undefined
    )
endif()

if(VITACHEAT_ENABLE_ANALYZER)
    target_compile_options(vitacheat_hardware_gate_core
        PRIVATE -fanalyzer
    )
endif()

if(BUILD_TESTING)
    add_executable(vitacheat_hardware_gate_tests
        tests/test_hardware_gate.c
    )
    target_link_libraries(vitacheat_hardware_gate_tests
        PRIVATE vitacheat_hardware_gate_core
    )
    target_compile_features(vitacheat_hardware_gate_tests
        PRIVATE c_std_11
    )
    if(MSVC)
        target_compile_options(vitacheat_hardware_gate_tests
            PRIVATE /W4 /WX
        )
    else()
        target_compile_options(vitacheat_hardware_gate_tests
            PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    if(VITACHEAT_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(vitacheat_hardware_gate_tests
            PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(vitacheat_hardware_gate_tests
            PRIVATE -fsanitize=address,undefined
        )
    endif()
    add_test(
        NAME vitacheat_hardware_gate_tests
        COMMAND vitacheat_hardware_gate_tests
    )
endif()

if(VITACHEAT_BUILD_FUZZERS)
    add_executable(vitacheat_hardware_gate_fuzzer
        fuzz/fuzz_hardware_gate.c
    )
    target_link_libraries(vitacheat_hardware_gate_fuzzer
        PRIVATE vitacheat_hardware_gate_core
    )
    target_compile_features(vitacheat_hardware_gate_fuzzer
        PRIVATE c_std_11
    )
    target_compile_options(vitacheat_hardware_gate_fuzzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vitacheat_hardware_gate_fuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
endif()
