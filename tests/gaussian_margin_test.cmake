if(NOT DEFINED QCLINT_EXE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED MARGIN_CONFIG OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "The Gaussian margin test is missing required variables")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(INPUT_COPY "${TEST_ROOT}/water.gjf")
file(COPY_FILE "${INPUT_FILE}" "${INPUT_COPY}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${MARGIN_CONFIG}"
            "${QCLINT_EXE}" "${INPUT_COPY}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
)
if(NOT check_result EQUAL 1 OR
   NOT check_output MATCHES
       "safe maximum is 0.80 GiB \\(80% of configured memory\\)")
    message(FATAL_ERROR "Gaussian custom memory percentage was not enforced")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${MARGIN_CONFIG}"
            "${QCLINT_EXE}" --fix memory "${INPUT_COPY}"
    RESULT_VARIABLE fix_result
    OUTPUT_VARIABLE fix_output
)
file(READ "${INPUT_COPY}" fixed_contents)
if(NOT fix_result EQUAL 0 OR
   NOT fixed_contents MATCHES "%mem=819MiB")
    message(FATAL_ERROR
            "Gaussian custom memory percentage was not fixed safely:\n"
            "${fix_output}")
endif()
