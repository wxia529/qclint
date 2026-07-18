if(NOT DEFINED QCLINT_EXE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED NORMAL_CONFIG OR NOT DEFINED STRICT_CONFIG)
    message(FATAL_ERROR "The CLI failure test is missing required variables")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${NORMAL_CONFIG}"
            "${QCLINT_EXE}" --multiplicity 2 "${INPUT_FILE}"
    RESULT_VARIABLE state_result
    OUTPUT_VARIABLE state_output
    ERROR_VARIABLE state_error
)
if(NOT state_result EQUAL 1 OR
   NOT state_error MATCHES
       ": error\\[chem.multiplicity\\]: declared multiplicity 1; expected 2")
    message(FATAL_ERROR "Wrong multiplicity did not produce a lint failure")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" "${INPUT_FILE}"
    RESULT_VARIABLE resource_result
    OUTPUT_VARIABLE resource_output
    ERROR_VARIABLE resource_error
)
if(NOT resource_result EQUAL 1 OR
   NOT resource_error MATCHES
       ":2: error\\[resource.cores\\]: requested 4 cores; maximum is 2" OR
   NOT resource_error MATCHES
       ":3: error\\[resource.memory\\]: requested 2 GiB; maximum is 1 GiB")
    message(FATAL_ERROR "Resource limits did not produce a lint failure")
endif()
