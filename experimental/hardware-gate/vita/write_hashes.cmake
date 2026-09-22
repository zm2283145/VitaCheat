foreach(required SKPRX VPK STUB)
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
file(SHA256 "${VPK}" vpk_sha256)
file(SHA256 "${STUB}" stub_sha256)
file(WRITE "${OUTPUT}"
    "sha256  artifact\n"
    "${skprx_sha256}  vitacheat-hardware-gate.skprx\n"
    "${vpk_sha256}  vitacheat-hardware-gate-test.vpk\n"
    "${stub_sha256}  libVitaCheatHardwareGate_stub.a\n"
)
