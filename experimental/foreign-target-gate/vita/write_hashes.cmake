foreach(required SKPRX TARGET_VPK CONTROLLER_VPK STUB)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Artifact not found: ${${required}}")
    endif()
endforeach()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "Missing OUTPUT")
endif()

file(SHA256 "${SKPRX}" skprx_sha256)
file(SHA256 "${TARGET_VPK}" target_vpk_sha256)
file(SHA256 "${CONTROLLER_VPK}" controller_vpk_sha256)
file(SHA256 "${STUB}" stub_sha256)
file(WRITE "${OUTPUT}"
    "sha256  artifact\n"
    "${skprx_sha256}  vitacheat-foreign-target-gate.skprx\n"
    "${target_vpk_sha256}  vitacheat-foreign-target-fixture.vpk\n"
    "${controller_vpk_sha256}  vitacheat-foreign-target-controller.vpk\n"
    "${stub_sha256}  libVitaCheatForeignGate_stub.a\n"
)
