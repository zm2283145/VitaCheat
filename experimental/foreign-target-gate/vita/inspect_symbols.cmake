foreach(required
        VITA_NM
        VITA_AR
        KERNEL_ELF
        TARGET_ELF
        CONTROLLER_ELF
        STUB
        OUTPUT)
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
    ksceKernelRegisterProcEventHandler
    ksceKernelSysrootGetProcessTitleId
    ksceKernelUnregisterProcEventHandler
)
set(expected_exports
    vcfgClose
    vcfgGetStatus
    vcfgOpenExactFixture
    vcfgReadFixtureSegment
)
set(expected_target_symbols
    sceAppMgrLaunchAppByUri
    sceClibMemset
    sceClibPrintf
    sceClibSnprintf
    sceCtrlPeekBufferPositive
    sceCtrlSetSamplingMode
    sceIoClose
    sceIoOpen
    sceIoWrite
    sceKernelAllocMemBlock
    sceKernelCreateLwMutex
    sceKernelCreateMutex
    sceKernelDelayThread
    sceKernelDeleteLwMutex
    sceKernelDeleteMutex
    sceKernelExitProcess
    sceKernelFreeMemBlock
    sceKernelGetMemBlockBase
    sceKernelGetModuleIdByAddr
    sceKernelGetModuleInfo
    sceKernelGetSystemTimeWide
    sceKernelGetTLSAddr
    sceKernelGetThreadId
    sceKernelLockLwMutex
    sceKernelUnlockLwMutex
    vcfgOpenExactFixture
)
set(expected_controller_symbols
    sceAppMgrDestroyAppByName
    sceAppMgrLaunchAppByUri
    sceClibMemcmp
    sceClibMemset
    sceClibPrintf
    sceClibSnprintf
    sceIoClose
    sceIoOpen
    sceIoRead
    sceIoWrite
    sceKernelAllocMemBlock
    sceKernelCreateLwMutex
    sceKernelCreateMutex
    sceKernelDelayThread
    sceKernelDeleteLwMutex
    sceKernelDeleteMutex
    sceKernelExitProcess
    sceKernelFreeMemBlock
    sceKernelGetMemBlockBase
    sceKernelGetSystemTimeWide
    sceKernelGetTLSAddr
    sceKernelGetThreadId
    sceKernelLockLwMutex
    sceKernelUnlockLwMutex
    vcfgClose
    vcfgGetStatus
    vcfgOpenExactFixture
    vcfgReadFixtureSegment
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
    "[0-9A-Fa-f]+ T vcfg[A-Za-z0-9_]+"
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
        "Unexpected foreign-target exports: ${actual_exports}"
    )
endif()

foreach(client IN ITEMS TARGET CONTROLLER)
    execute_process(
        COMMAND "${VITA_NM}" -g "${${client}_ELF}"
        RESULT_VARIABLE client_nm_result
        OUTPUT_VARIABLE client_symbols
    )
    if(NOT client_nm_result EQUAL 0)
        message(FATAL_ERROR
            "arm-vita-eabi-nm failed for ${client}"
        )
    endif()
    string(REGEX MATCHALL
        "[0-9A-Fa-f]+ T (sce|vcfg)[A-Za-z0-9_]+"
        client_symbol_lines "${client_symbols}"
    )
    set(actual_client_symbols)
    foreach(line IN LISTS client_symbol_lines)
        string(REGEX REPLACE "^.* T " "" symbol "${line}")
        list(APPEND actual_client_symbols "${symbol}")
    endforeach()
    list(REMOVE_DUPLICATES actual_client_symbols)
    list(SORT actual_client_symbols)
    string(TOLOWER "${client}" client_lower)
    set(expected_client_symbols
        ${expected_${client_lower}_symbols}
    )
    list(SORT expected_client_symbols)
    if(NOT actual_client_symbols STREQUAL expected_client_symbols)
        message(FATAL_ERROR
            "Unexpected ${client} symbols: ${actual_client_symbols}"
        )
    endif()
    string(REPLACE ";" "\n"
        "${client}_symbol_text" "${actual_client_symbols}")
endforeach()

execute_process(
    COMMAND "${VITA_AR}" t "${STUB}"
    RESULT_VARIABLE ar_result
    OUTPUT_VARIABLE stub_members
)
if(NOT ar_result EQUAL 0)
    message(FATAL_ERROR "arm-vita-eabi-ar failed with ${ar_result}")
endif()
string(REGEX MATCHALL
    "VitaCheatForeignGate_VitaCheatForeignGate_vcfg[A-Za-z0-9_]+[.]o"
    stub_member_list "${stub_members}"
)
list(LENGTH stub_member_list stub_member_count)
if(NOT stub_member_count EQUAL 4)
    message(FATAL_ERROR
        "Expected four generated syscall stubs, got ${stub_member_count}"
    )
endif()

string(REPLACE ";" "\n" import_text "${actual_imports}")
string(REPLACE ";" "\n" export_text "${actual_exports}")
file(WRITE "${OUTPUT}"
    "kernel_imports:\n${import_text}\n"
    "gate_exports:\n${export_text}\n"
    "target_symbols:\n${TARGET_symbol_text}\n"
    "controller_symbols:\n${CONTROLLER_symbol_text}\n"
    "generated_stub_members:\n${stub_members}"
)
