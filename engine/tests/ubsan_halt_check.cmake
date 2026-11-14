# Runs ubsan_probe (a signed overflow) and checks that UBSan reports it and ends the process with a failure,
# so a UBSan finding fails ctest instead of only printing to the log (SEED-797). Needs PROBE.
execute_process(COMMAND "${PROBE}" RESULT_VARIABLE code OUTPUT_VARIABLE text ERROR_VARIABLE text)
if(NOT text MATCHES "runtime error: signed integer overflow")
    message(FATAL_ERROR "ubsan_probe ran without a UBSan report (exit ${code}):\n${text}")
endif()
if(code EQUAL 0)
    message(FATAL_ERROR "UBSan reported the overflow but the process exited 0, so ctest would pass:\n${text}")
endif()
