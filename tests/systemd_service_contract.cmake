if(NOT EXISTS "${SERVICE_FILE}")
  message(FATAL_ERROR "Systemd service unit is missing")
endif()

file(READ "${SERVICE_FILE}" SERVICE_CONTENT)

foreach(REQUIRED_DIRECTIVE
    "NoNewPrivileges=true"
    "ProtectSystem=strict"
    "ProtectProc=invisible"
    "PrivateDevices=false")
  string(FIND "${SERVICE_CONTENT}" "${REQUIRED_DIRECTIVE}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR "Required service hardening directive missing: ${REQUIRED_DIRECTIVE}")
  endif()
endforeach()

string(FIND "${SERVICE_CONTENT}" "ProcSubset=pid" PROC_SUBSET_POSITION)
if(NOT PROC_SUBSET_POSITION EQUAL -1)
  message(FATAL_ERROR "ProcSubset=pid hides /proc/swaps required by DeviceGuard")
endif()
