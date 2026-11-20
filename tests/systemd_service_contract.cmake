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

foreach(REQUIRED_DIRECTIVE
    "RestrictNamespaces=true"
    "RestrictRealtime=true"
    "SystemCallArchitectures=native"
    "UMask=0077"
    "KeyringMode=private"
    "RemoveIPC=true")
  string(FIND "${SERVICE_CONTENT}" "${REQUIRED_DIRECTIVE}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR "Required service sandbox directive missing: ${REQUIRED_DIRECTIVE}")
  endif()
endforeach()

if(NOT DEFINED POLKIT_POLICY OR NOT EXISTS "${POLKIT_POLICY}")
  message(FATAL_ERROR "Polkit write authorization policy is missing")
endif()
file(READ "${POLKIT_POLICY}" POLKIT_CONTENT)
foreach(REQUIRED_POLICY_VALUE
    "org.iso_flash.write"
    "<allow_any>no</allow_any>"
    "<allow_inactive>auth_admin</allow_inactive>"
    "<allow_active>auth_admin</allow_active>")
  string(FIND "${POLKIT_CONTENT}" "${REQUIRED_POLICY_VALUE}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR "Required restrictive Polkit policy value missing: ${REQUIRED_POLICY_VALUE}")
  endif()
endforeach()
