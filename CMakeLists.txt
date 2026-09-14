cmake_minimum_required(VERSION 3.16)

project(VitaCheat VERSION 0.1.0 LANGUAGES C)

option(VITACHEAT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(vitacheat_core STATIC
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

include(CTest)

if(BUILD_TESTING)
    add_executable(vitacheat_host_tests
        tests/host/test_search.c
    )

    target_link_libraries(vitacheat_host_tests PRIVATE vitacheat_core)
    target_compile_features(vitacheat_host_tests PRIVATE c_std_11)

    if(MSVC)
        target_compile_options(vitacheat_host_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(vitacheat_host_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    if(VITACHEAT_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(vitacheat_host_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(vitacheat_host_tests PRIVATE -fsanitize=address,undefined)
    endif()

    add_test(NAME vitacheat_host_tests COMMAND vitacheat_host_tests)
endif()

