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

project(VitaCheatForeignTargetGate C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
enable_language(ASM)

option(
    VITACHEAT_FOREIGN_TARGET_GATE
    "Build the disposable source-owned foreign-target gate"
    OFF
)
set(
    VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID ""
    CACHE STRING "Exact source-owned target title ID"
)
set(
    VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID ""
    CACHE STRING "Exact source-owned controller title ID"
)
set(
    VITACHEAT_FOREIGN_GATE_FIRMWARE ""
    CACHE STRING "Exact target firmware"
)
set(
    VITACHEAT_FOREIGN_GATE_LIFECYCLE ""
    CACHE STRING "Exact source-owned fixture lifecycle"
)

if(NOT VITACHEAT_FOREIGN_TARGET_GATE)
    message(FATAL_ERROR
        "Set -DVITACHEAT_FOREIGN_TARGET_GATE=ON explicitly"
    )
endif()
if(NOT VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID STREQUAL "VCFT00001")
    message(FATAL_ERROR
        "The source-owned target title must be VCFT00001"
    )
endif()
if(NOT VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID STREQUAL "VCFC00001")
    message(FATAL_ERROR
        "The source-owned controller title must be VCFC00001"
    )
endif()
if(NOT VITACHEAT_FOREIGN_GATE_FIRMWARE STREQUAL "3.65")
    message(FATAL_ERROR
        "This gate is pinned to firmware 3.65 only"
    )
endif()
if(NOT VITACHEAT_FOREIGN_GATE_LIFECYCLE STREQUAL
       "SUSPENDED_SOURCE_FIXTURE")
    message(FATAL_ERROR
        "Set the lifecycle to SUSPENDED_SOURCE_FIXTURE explicitly"
    )
endif()

set(FOREIGN_GATE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FOREIGN_GATE_INCLUDE "${FOREIGN_GATE_ROOT}/include")
set(FOREIGN_GATE_EXPORTS "${CMAKE_CURRENT_SOURCE_DIR}/exports.yml")
set(FOREIGN_GATE_STUB_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated-stubs"
)
set(FOREIGN_GATE_STUB
    "${CMAKE_CURRENT_BINARY_DIR}/libVitaCheatForeignGate_stub.a"
)
find_program(
    FOREIGN_GATE_VITA_AS arm-vita-eabi-as
    HINTS "${VITASDK}/bin"
    REQUIRED
)
find_program(
    FOREIGN_GATE_VITA_AR arm-vita-eabi-ar
    HINTS "${VITASDK}/bin"
    REQUIRED
)
find_program(
    FOREIGN_GATE_VITA_RANLIB arm-vita-eabi-ranlib
    HINTS "${VITASDK}/bin"
    REQUIRED
)
find_program(
    FOREIGN_GATE_VITA_NM arm-vita-eabi-nm
    HINTS "${VITASDK}/bin"
    REQUIRED
)

add_executable(VitaCheatForeignGateKernel
    kernel.c
    "${FOREIGN_GATE_ROOT}/src/foreign_target_gate.c"
)
target_include_directories(VitaCheatForeignGateKernel
    PRIVATE "${FOREIGN_GATE_INCLUDE}"
)
target_compile_features(VitaCheatForeignGateKernel PRIVATE c_std_11)
target_compile_options(VitaCheatForeignGateKernel PRIVATE
    -O2
    -Wall
    -Wextra
    -Werror
    -fno-builtin
    -fvisibility=hidden
)
target_compile_definitions(VitaCheatForeignGateKernel PRIVATE
    VITACHEAT_FOREIGN_GATE_ENABLE=1
    VITACHEAT_FOREIGN_GATE_API_AVAILABLE=1
    VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID="${VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID}"
    VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID="${VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID}"
    VITACHEAT_FOREIGN_GATE_TARGET_MODULE="VitaCheatFtTarget"
    VITACHEAT_FOREIGN_GATE_FIRMWARE=0x03650000
)
target_link_libraries(VitaCheatForeignGateKernel
    SceSysmemForDriver_stub
    SceModulemgrForDriver_stub
    SceModulemgrForKernel_363_stub
    SceThreadmgrForDriver_stub
    SceSysrootForKernel_stub
    SceProcEventForDriver_stub
)
set_target_properties(VitaCheatForeignGateKernel PROPERTIES
    LINK_FLAGS "-nostdlib -Wl,-q"
)

vita_create_self(
    vitacheat-foreign-target-gate.skprx
    VitaCheatForeignGateKernel
    CONFIG "${FOREIGN_GATE_EXPORTS}"
    UNSAFE
)

add_custom_command(
    OUTPUT "${FOREIGN_GATE_STUB}"
    COMMAND "${CMAKE_COMMAND}"
        "-DVITA_ELF_EXPORT=${VITA_ELF_EXPORT}"
        "-DVITA_LIBS_GEN=${VITA_LIBS_GEN}"
        "-DVITA_AS=${FOREIGN_GATE_VITA_AS}"
        "-DVITA_AR=${FOREIGN_GATE_VITA_AR}"
        "-DVITA_RANLIB=${FOREIGN_GATE_VITA_RANLIB}"
        "-DKERNEL_ELF=$<TARGET_FILE:VitaCheatForeignGateKernel>"
        "-DEXPORTS_YML=${FOREIGN_GATE_EXPORTS}"
        "-DSTUB_DIR=${FOREIGN_GATE_STUB_DIR}"
        "-DSTUB_OUTPUT=${FOREIGN_GATE_STUB}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/build_stubs.cmake"
    DEPENDS
        VitaCheatForeignGateKernel
        "${FOREIGN_GATE_EXPORTS}"
        "${CMAKE_CURRENT_SOURCE_DIR}/build_stubs.cmake"
    VERBATIM
)
add_custom_target(foreign-gate-stubs
    DEPENDS "${FOREIGN_GATE_STUB}"
)

add_executable(VitaCheatFtTarget
    target.c
)
add_dependencies(VitaCheatFtTarget foreign-gate-stubs)
target_include_directories(VitaCheatFtTarget
    PRIVATE "${FOREIGN_GATE_INCLUDE}"
)
target_compile_features(VitaCheatFtTarget PRIVATE c_std_11)
target_compile_options(VitaCheatFtTarget PRIVATE
    -O2
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -ffunction-sections
    -fdata-sections
)
target_compile_definitions(VitaCheatFtTarget PRIVATE
    VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID="${VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID}"
)
target_link_libraries(VitaCheatFtTarget
    "${FOREIGN_GATE_STUB}"
    SceAppMgr_stub
    SceCtrl_stub
    SceIofilemgr_stub
    SceKernelModulemgr_stub
    SceKernelThreadMgr_stub
    SceLibKernel_stub
)
target_link_options(VitaCheatFtTarget PRIVATE
    -Wl,--gc-sections
)

add_executable(VitaCheatFtController
    controller.c
)
add_dependencies(VitaCheatFtController foreign-gate-stubs)
target_include_directories(VitaCheatFtController
    PRIVATE "${FOREIGN_GATE_INCLUDE}"
)
target_compile_features(VitaCheatFtController PRIVATE c_std_11)
target_compile_options(VitaCheatFtController PRIVATE
    -O2
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -ffunction-sections
    -fdata-sections
)
target_compile_definitions(VitaCheatFtController PRIVATE
    VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID="${VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID}"
)
target_link_libraries(VitaCheatFtController
    "${FOREIGN_GATE_STUB}"
    SceAppMgr_stub
    SceIofilemgr_stub
    SceKernelThreadMgr_stub
    SceLibKernel_stub
)
target_link_options(VitaCheatFtController PRIVATE
    -Wl,--gc-sections
)

set(VITA_ELF_CREATE_FLAGS "" CACHE STRING "vita-elf-create flags" FORCE)
vita_create_self(
    target-eboot.bin
    VitaCheatFtTarget
    UNSAFE
)
vita_create_self(
    controller-eboot.bin
    VitaCheatFtController
    UNSAFE
)

function(vcfg_create_reproducible_vpk
         output title_id display_name eboot_target eboot_path)
    set(root "${CMAKE_CURRENT_BINARY_DIR}/${output}-root")
    set(sfo "${root}/sce_sys/param.sfo")
    set(output_path "${CMAKE_CURRENT_BINARY_DIR}/${output}")
    file(MAKE_DIRECTORY "${root}/sce_sys")
    add_custom_command(
        OUTPUT "${output_path}"
        COMMAND "${VITA_MKSFOEX}"
            -s "APP_VER=01.00"
            -s "TITLE_ID=${title_id}"
            "${display_name}"
            "${sfo}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${eboot_path}" "${root}/eboot.bin"
        COMMAND "${CMAKE_COMMAND}" -E rm -f
            "${output_path}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "SOURCE_DATE_EPOCH=315532800"
            "${CMAKE_COMMAND}" -E tar cf
            "${output_path}"
            --format=zip
            "sce_sys/param.sfo"
            "eboot.bin"
        DEPENDS
            "${eboot_target}"
            "${eboot_path}"
        WORKING_DIRECTORY "${root}"
        VERBATIM
    )
    add_custom_target("${output}-vpk" ALL
        DEPENDS "${output_path}"
    )
endfunction()

vcfg_create_reproducible_vpk(
    "vitacheat-foreign-target-fixture.vpk"
    "${VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID}"
    "VitaCheat foreign fixture"
    "target-eboot.bin-self"
    "${CMAKE_CURRENT_BINARY_DIR}/target-eboot.bin"
)
vcfg_create_reproducible_vpk(
    "vitacheat-foreign-target-controller.vpk"
    "${VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID}"
    "VitaCheat foreign controller"
    "controller-eboot.bin-self"
    "${CMAKE_CURRENT_BINARY_DIR}/controller-eboot.bin"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/symbol-inventory.txt"
    COMMAND "${CMAKE_COMMAND}"
        "-DVITA_NM=${FOREIGN_GATE_VITA_NM}"
        "-DVITA_AR=${FOREIGN_GATE_VITA_AR}"
        "-DKERNEL_ELF=$<TARGET_FILE:VitaCheatForeignGateKernel>"
        "-DTARGET_ELF=$<TARGET_FILE:VitaCheatFtTarget>"
        "-DCONTROLLER_ELF=$<TARGET_FILE:VitaCheatFtController>"
        "-DSTUB=${FOREIGN_GATE_STUB}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/symbol-inventory.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/inspect_symbols.cmake"
    DEPENDS
        VitaCheatForeignGateKernel
        VitaCheatFtTarget
        VitaCheatFtController
        foreign-gate-stubs
        "${CMAKE_CURRENT_SOURCE_DIR}/inspect_symbols.cmake"
    VERBATIM
)
add_custom_target(foreign-gate-symbol-inspection
    ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/symbol-inventory.txt"
)

add_custom_target(foreign-gate-artifact-inventory ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSKPRX=${CMAKE_CURRENT_BINARY_DIR}/vitacheat-foreign-target-gate.skprx"
        "-DTARGET_VPK=${CMAKE_CURRENT_BINARY_DIR}/vitacheat-foreign-target-fixture.vpk"
        "-DCONTROLLER_VPK=${CMAKE_CURRENT_BINARY_DIR}/vitacheat-foreign-target-controller.vpk"
        "-DSTUB=${FOREIGN_GATE_STUB}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/artifact-inventory.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_hashes.cmake"
    DEPENDS
        vitacheat-foreign-target-gate.skprx-self
        vitacheat-foreign-target-fixture.vpk-vpk
        vitacheat-foreign-target-controller.vpk-vpk
        foreign-gate-stubs
        foreign-gate-symbol-inspection
    VERBATIM
)
