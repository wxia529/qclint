if(NOT DEFINED QCLINT_EXE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED UNSUPPORTED_FILE OR NOT DEFINED DIRECTORY_INPUT OR
   NOT DEFINED NORMAL_CONFIG OR
   NOT DEFINED MISSING_CONFIG)
    message(FATAL_ERROR "The output CLI test is missing required variables")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${MISSING_CONFIG}"
            "${QCLINT_EXE}" "${DIRECTORY_INPUT}"
    RESULT_VARIABLE directory_result
    OUTPUT_VARIABLE directory_output
    ERROR_VARIABLE directory_error
)
if(NOT directory_result EQUAL 0 OR NOT directory_output STREQUAL "" OR
   NOT directory_error STREQUAL "")
    message(FATAL_ERROR "A directory argument was scanned instead of skipped")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${NORMAL_CONFIG}"
            "${QCLINT_EXE}" "${INPUT_FILE}"
    RESULT_VARIABLE quiet_result
    OUTPUT_VARIABLE quiet_output
    ERROR_VARIABLE quiet_error
)
if(NOT quiet_result EQUAL 0 OR NOT quiet_output STREQUAL "" OR
   NOT quiet_error MATCHES
       ":2: warning\\[resource.cores-underallocated\\]: requested 4 cores; configured allocation is 8" OR
   NOT quiet_error MATCHES
       ":3: warning\\[resource.memory-underallocated\\]: requested 2 GB; configured allocation is 4 GB")
    message(FATAL_ERROR "Underallocated resources did not produce warnings")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${MISSING_CONFIG}"
            "${QCLINT_EXE}" "${UNSUPPORTED_FILE}"
    RESULT_VARIABLE skipped_result
    OUTPUT_VARIABLE skipped_output
    ERROR_VARIABLE skipped_error
)
if(NOT skipped_result EQUAL 0 OR NOT skipped_output STREQUAL "" OR
   NOT skipped_error STREQUAL "")
    message(FATAL_ERROR
            "An unsupported input was not skipped before configuration loading")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${NORMAL_CONFIG}"
            "${QCLINT_EXE}" --verbose "${UNSUPPORTED_FILE}" "${INPUT_FILE}"
    RESULT_VARIABLE verbose_result
    OUTPUT_VARIABLE verbose_output
    ERROR_VARIABLE verbose_error
)
if(NOT verbose_result EQUAL 0 OR NOT verbose_output STREQUAL "" OR
   NOT verbose_error MATCHES "note\\[input.skipped\\]" OR
   NOT verbose_error MATCHES "note\\[check.ok\\]: passed" OR
   NOT verbose_error MATCHES
       "qclint: checked=1 passed=1 failed=0 skipped=1")
    message(FATAL_ERROR "Verbose output did not report checks and skips")
endif()
