cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(
            CMAKE_TOOLCHAIN_FILE
            "$ENV{VITASDK}/share/vita.toolchain.cmake"
            CACHE PATH "VitaSDK toolchain file"
        )
    else()
        message(FATAL_ERROR "VITASDK must name the installed VitaSDK")
    endif()
endif()

project(VitaCheatHardwareGate C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
enable_language(ASM)

option(
    VITACHEAT_HARDWARE_GATE
    "Build the disposable native read hardware gate"
    OFF
)
set(
    VITACHEAT_HARDWARE_GATE_TITLE_ID ""
    CACHE STRING "Exact disposable Vita title ID"
)
set(
    VITACHEAT_HARDWARE_GATE_FIRMWARE ""
    CACHE STRING "Exact target firmware"
)

if(NOT VITACHEAT_HARDWARE_GATE)
    message(FATAL_ERROR
        "Set -DVITACHEAT_HARDWARE_GATE=ON explicitly"
    )
endif()
if(NOT VITACHEAT_HARDWARE_GATE_TITLE_ID STREQUAL "VCHG00001")
    message(FATAL_ERROR
        "The source-owned disposable title must be VCHG00001"
    )
endif()
if(NOT VITACHEAT_HARDWARE_GATE_FIRMWARE STREQUAL "3.65")
    message(FATAL_ERROR
        "This gate is pinned to firmware 3.65 only"
    )
endif()

set(HARDWARE_GATE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(HARDWARE_GATE_INCLUDE "${HARDWARE_GATE_ROOT}/include")
set(HARDWARE_GATE_EXPORTS "${CMAKE_CURRENT_SOURCE_DIR}/exports.yml")
set(HARDWARE_GATE_STUB_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-stubs")
set(HARDWARE_GATE_STUB
    "${CMAKE_CURRENT_BINARY_DIR}/libVitaCheatHardwareGate_stub.a"
)
find_program(
    HARDWARE_GATE_VITA_AS arm-vita-eabi-as
    HINTS "${VITASDK}/bin"
    REQUIRED
)
find_program(
    HARDWARE_GATE_VITA_AR arm-vita-eabi-ar
    HINTS "${VITASDK}/bin"
    REQUIRED
)
find_program(
    HARDWARE_GATE_VITA_RANLIB arm-vita-eabi-ranlib
    HINTS "${VITASDK}/bin"
    REQUIRED
)
find_program(
    HARDWARE_GATE_VITA_NM arm-vita-eabi-nm
    HINTS "${VITASDK}/bin"
    REQUIRED
)

add_executable(VitaCheatHardwareGateKernel
    kernel.c
    "${HARDWARE_GATE_ROOT}/src/hardware_gate.c"
)
target_include_directories(VitaCheatHardwareGateKernel
    PRIVATE "${HARDWARE_GATE_INCLUDE}"
)
target_compile_features(VitaCheatHardwareGateKernel PRIVATE c_std_11)
target_compile_options(VitaCheatHardwareGateKernel PRIVATE
    -O2
    -Wall
    -Wextra
    -Werror
    -fno-builtin
    -fvisibility=hidden
)
target_compile_definitions(VitaCheatHardwareGateKernel PRIVATE
    VITACHEAT_HARDWARE_GATE_ENABLE=1
    VITACHEAT_HARDWARE_GATE_API_AVAILABLE=1
    VITACHEAT_HARDWARE_GATE_TITLE_ID="${VITACHEAT_HARDWARE_GATE_TITLE_ID}"
    VITACHEAT_HARDWARE_GATE_MODULE_NAME="VitaCheatHgClient"
    VITACHEAT_HARDWARE_GATE_FIRMWARE=0x03650000
)
target_link_libraries(VitaCheatHardwareGateKernel
    SceSysmemForDriver_stub
    SceModulemgrForDriver_stub
    SceModulemgrForKernel_363_stub
    SceThreadmgrForDriver_stub
    SceSysrootForKernel_stub
)
set_target_properties(VitaCheatHardwareGateKernel PROPERTIES
    LINK_FLAGS "-nostdlib -Wl,-q"
)

vita_create_self(
    vitacheat-hardware-gate.skprx
    VitaCheatHardwareGateKernel
    CONFIG "${HARDWARE_GATE_EXPORTS}"
    UNSAFE
)

add_custom_command(
    OUTPUT "${HARDWARE_GATE_STUB}"
    COMMAND "${CMAKE_COMMAND}"
        "-DVITA_ELF_EXPORT=${VITA_ELF_EXPORT}"
        "-DVITA_LIBS_GEN=${VITA_LIBS_GEN}"
        "-DVITA_AS=${HARDWARE_GATE_VITA_AS}"
        "-DVITA_AR=${HARDWARE_GATE_VITA_AR}"
        "-DVITA_RANLIB=${HARDWARE_GATE_VITA_RANLIB}"
        "-DKERNEL_ELF=$<TARGET_FILE:VitaCheatHardwareGateKernel>"
        "-DEXPORTS_YML=${HARDWARE_GATE_EXPORTS}"
        "-DSTUB_DIR=${HARDWARE_GATE_STUB_DIR}"
        "-DSTUB_OUTPUT=${HARDWARE_GATE_STUB}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/build_stubs.cmake"
    DEPENDS
        VitaCheatHardwareGateKernel
        "${HARDWARE_GATE_EXPORTS}"
        "${CMAKE_CURRENT_SOURCE_DIR}/build_stubs.cmake"
    VERBATIM
)
add_custom_target(hardware-gate-stubs
    DEPENDS "${HARDWARE_GATE_STUB}"
)

add_executable(VitaCheatHgClient
    client.c
)
add_dependencies(VitaCheatHgClient hardware-gate-stubs)
target_include_directories(VitaCheatHgClient
    PRIVATE "${HARDWARE_GATE_INCLUDE}"
)
target_compile_features(VitaCheatHgClient PRIVATE c_std_11)
target_compile_options(VitaCheatHgClient PRIVATE
    -O2
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
target_compile_definitions(VitaCheatHgClient PRIVATE
    VITACHEAT_HARDWARE_GATE_TITLE_ID="${VITACHEAT_HARDWARE_GATE_TITLE_ID}"
)
target_link_libraries(VitaCheatHgClient
    "${HARDWARE_GATE_STUB}"
    SceKernelModulemgr_stub
    SceKernelThreadMgr_stub
    SceIofilemgr_stub
    SceLibKernel_stub
)

set(VITA_ELF_CREATE_FLAGS "" CACHE STRING "vita-elf-create flags" FORCE)
vita_create_self(
    eboot.bin
    VitaCheatHgClient
    UNSAFE
)
vita_create_vpk(
    vitacheat-hardware-gate-test.vpk
    "${VITACHEAT_HARDWARE_GATE_TITLE_ID}"
    eboot.bin
    VERSION 01.00
    NAME "VitaCheat hardware gate"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/symbol-inventory.txt"
    COMMAND "${CMAKE_COMMAND}"
        "-DVITA_NM=${HARDWARE_GATE_VITA_NM}"
        "-DVITA_AR=${HARDWARE_GATE_VITA_AR}"
        "-DKERNEL_ELF=$<TARGET_FILE:VitaCheatHardwareGateKernel>"
        "-DSTUB=${HARDWARE_GATE_STUB}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/symbol-inventory.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/inspect_symbols.cmake"
    DEPENDS
        VitaCheatHardwareGateKernel
        hardware-gate-stubs
        "${CMAKE_CURRENT_SOURCE_DIR}/inspect_symbols.cmake"
    VERBATIM
)
add_custom_target(hardware-gate-symbol-inspection
    ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/symbol-inventory.txt"
)

add_custom_target(hardware-gate-artifact-inventory ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSKPRX=${CMAKE_CURRENT_BINARY_DIR}/vitacheat-hardware-gate.skprx"
        "-DVPK=${CMAKE_CURRENT_BINARY_DIR}/vitacheat-hardware-gate-test.vpk"
        "-DSTUB=${HARDWARE_GATE_STUB}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/artifact-inventory.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_hashes.cmake"
    DEPENDS
        vitacheat-hardware-gate.skprx-self
        vitacheat-hardware-gate-test.vpk-vpk
        hardware-gate-stubs
        hardware-gate-symbol-inspection
    VERBATIM
)
