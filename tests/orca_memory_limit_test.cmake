if(NOT DEFINED QCLINT_EXE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED LIMIT_CONFIG)
    message(FATAL_ERROR "The ORCA memory limit test is missing required variables")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${LIMIT_CONFIG}"
            "${QCLINT_EXE}" "${INPUT_FILE}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 1 OR
   NOT check_output MATCHES "maximum is 204 GiB")
    message(FATAL_ERROR
            "ORCA memory above its configured limit was not rejected:\n"
            "${check_output}${check_error}")
endif()
