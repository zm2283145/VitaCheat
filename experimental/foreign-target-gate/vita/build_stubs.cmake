foreach(required
        VITA_ELF_EXPORT
        VITA_LIBS_GEN
        VITA_AS
        VITA_AR
        VITA_RANLIB
        KERNEL_ELF
        EXPORTS_YML
        STUB_DIR
        STUB_OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${STUB_DIR}")
file(MAKE_DIRECTORY "${STUB_DIR}")
set(imports_yml "${STUB_DIR}/imports.yml")
set(generated_dir "${STUB_DIR}/generated")

execute_process(
    COMMAND "${VITA_ELF_EXPORT}"
        kernel "${KERNEL_ELF}" "${EXPORTS_YML}" "${imports_yml}"
    RESULT_VARIABLE export_result
)
if(NOT export_result EQUAL 0)
    message(FATAL_ERROR
        "vita-elf-export failed with ${export_result}"
    )
endif()

execute_process(
    COMMAND "${VITA_LIBS_GEN}" "${imports_yml}" "${generated_dir}"
    RESULT_VARIABLE generator_result
)
if(NOT generator_result EQUAL 0)
    message(FATAL_ERROR
        "vita-libs-gen failed with ${generator_result}"
    )
endif()

file(GLOB assembly_sources "${generated_dir}/*.S")
if(NOT assembly_sources)
    message(FATAL_ERROR "vita-libs-gen produced no stub assembly")
endif()

set(objects)
foreach(source IN LISTS assembly_sources)
    get_filename_component(name "${source}" NAME_WE)
    set(object "${STUB_DIR}/${name}.o")
    execute_process(
        COMMAND "${VITA_AS}"
            --defsym GEN_WEAK_EXPORTS=0 "${source}" -o "${object}"
        RESULT_VARIABLE assembler_result
    )
    if(NOT assembler_result EQUAL 0)
        message(FATAL_ERROR
            "assembler failed for ${source}: ${assembler_result}"
        )
    endif()
    list(APPEND objects "${object}")
endforeach()

execute_process(
    COMMAND "${VITA_AR}" rcsD "${STUB_OUTPUT}" ${objects}
    RESULT_VARIABLE archive_result
)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR "archiver failed with ${archive_result}")
endif()
execute_process(
    COMMAND "${VITA_RANLIB}" -D "${STUB_OUTPUT}"
    RESULT_VARIABLE ranlib_result
)
if(NOT ranlib_result EQUAL 0)
    message(FATAL_ERROR "ranlib failed with ${ranlib_result}")
endif()
