if(NOT DEFINED QCLINT_EXE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED LIMIT_CONFIG OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "The Gaussian memory limit test is missing required variables")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(INPUT_COPY "${TEST_ROOT}/water.gjf")
file(COPY_FILE "${INPUT_FILE}" "${INPUT_COPY}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${LIMIT_CONFIG}"
            "${QCLINT_EXE}" "${INPUT_COPY}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 1 OR
   NOT check_error MATCHES
       "error\\[resource.memory\\]: requested 2 GB; maximum is 1 GB")
    message(FATAL_ERROR "Gaussian custom memory limit was not enforced")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${LIMIT_CONFIG}"
            "${QCLINT_EXE}" --fix memory "${INPUT_COPY}"
    RESULT_VARIABLE fix_result
    OUTPUT_VARIABLE fix_output
    ERROR_VARIABLE fix_error
)
file(READ "${INPUT_COPY}" fixed_contents)
if(NOT fix_result EQUAL 0 OR
   NOT fixed_contents MATCHES "%mem=1GB")
    message(FATAL_ERROR
            "Gaussian custom memory limit was not fixed safely:\n"
            "${fix_output}${fix_error}")
endif()
