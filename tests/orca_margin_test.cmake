if(NOT DEFINED QCLINT_EXE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED MARGIN_CONFIG)
    message(FATAL_ERROR "The ORCA margin test is missing required variables")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${MARGIN_CONFIG}"
            "${QCLINT_EXE}" "${INPUT_FILE}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 1 OR
   NOT check_output MATCHES
       "safe maximum is 204.80 GiB \\(80% of configured memory\\)")
    message(FATAL_ERROR
            "ORCA memory above the 80% margin was not rejected:\n"
            "${check_output}${check_error}")
endif()
