foreach(required VITA_NM VITA_AR KERNEL_ELF STUB OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

execute_process(
    COMMAND "${VITA_NM}" -g "${KERNEL_ELF}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "arm-vita-eabi-nm failed with ${nm_result}")
endif()

set(expected_imports
    ksceKernelCopyFromUserProc
    ksceKernelCopyToUserProc
    ksceKernelGetModuleFingerprint
    ksceKernelGetModuleIdByPid
    ksceKernelGetModuleInfo
    ksceKernelGetProcessId
    ksceKernelGetSystemSwVersion
    ksceKernelGetSystemTimeWide
    ksceKernelSysrootGetProcessTitleId
)
set(expected_exports
    vchgClose
    vchgGetSelfMainModule
    vchgGetStatus
    vchgOpenSelf
    vchgReadSelfSegment
)

string(REGEX MATCHALL
    "[0-9A-Fa-f]+ T ksceKernel[A-Za-z0-9_]+"
    import_lines "${symbols}"
)
set(actual_imports)
foreach(line IN LISTS import_lines)
    string(REGEX REPLACE "^.* T " "" symbol "${line}")
    list(APPEND actual_imports "${symbol}")
endforeach()
list(REMOVE_DUPLICATES actual_imports)
list(SORT actual_imports)
list(SORT expected_imports)
if(NOT actual_imports STREQUAL expected_imports)
    message(FATAL_ERROR
        "Unexpected kernel imports: ${actual_imports}"
    )
endif()

string(REGEX MATCHALL
    "[0-9A-Fa-f]+ T vchg[A-Za-z0-9_]+"
    export_lines "${symbols}"
)
set(actual_exports)
foreach(line IN LISTS export_lines)
    string(REGEX REPLACE "^.* T " "" symbol "${line}")
    list(APPEND actual_exports "${symbol}")
endforeach()
list(REMOVE_DUPLICATES actual_exports)
list(SORT actual_exports)
list(SORT expected_exports)
if(NOT actual_exports STREQUAL expected_exports)
    message(FATAL_ERROR
        "Unexpected hardware-gate exports: ${actual_exports}"
    )
endif()

execute_process(
    COMMAND "${VITA_AR}" t "${STUB}"
    RESULT_VARIABLE ar_result
    OUTPUT_VARIABLE stub_members
)
if(NOT ar_result EQUAL 0)
    message(FATAL_ERROR "arm-vita-eabi-ar failed with ${ar_result}")
endif()
string(REGEX MATCHALL
    "VitaCheatHardwareGate_VitaCheatHardwareGate_vchg[A-Za-z0-9_]+[.]o"
    stub_member_list "${stub_members}"
)
list(LENGTH stub_member_list stub_member_count)
if(NOT stub_member_count EQUAL 5)
    message(FATAL_ERROR
        "Expected five generated syscall stubs, got ${stub_member_count}"
    )
endif()

string(REPLACE ";" "\n" import_text "${actual_imports}")
string(REPLACE ";" "\n" export_text "${actual_exports}")
file(WRITE "${OUTPUT}"
    "kernel_imports:\n${import_text}\n"
    "gate_exports:\n${export_text}\n"
    "generated_stub_members:\n${stub_members}"
)
