if(NOT DEFINED QCLINT_EXE OR NOT DEFINED TEST_ROOT OR NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "The config CLI test is missing required variables")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
set(CONFIG_FILE "${TEST_ROOT}/qclint/config")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${CONFIG_FILE}"
            "${QCLINT_EXE}" "${INPUT_FILE}"
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
)
if(NOT missing_result EQUAL 2 OR
   NOT missing_error MATCHES "User configuration not found")
    message(FATAL_ERROR "Missing configuration did not produce the expected error")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${CONFIG_FILE}"
            "${QCLINT_EXE}" config init
    RESULT_VARIABLE init_result
)
if(NOT init_result EQUAL 0 OR NOT EXISTS "${CONFIG_FILE}")
    message(FATAL_ERROR "config init did not create the configuration")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${CONFIG_FILE}"
            "${QCLINT_EXE}" config show
    RESULT_VARIABLE show_result
    OUTPUT_VARIABLE show_output
)
if(NOT show_result EQUAL 0 OR
   NOT show_output MATCHES "max_cores = 32" OR
   NOT show_output MATCHES "gaussian_max_memory = 64" OR
   NOT show_output MATCHES "orca_max_memory = 51")
    message(FATAL_ERROR "config show did not report effective values")
endif()

file(WRITE "${CONFIG_FILE}" "max_cores = 7\n")
file(WRITE "${TEST_ROOT}/no.txt" "n\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${CONFIG_FILE}"
            "${QCLINT_EXE}" config init
    INPUT_FILE "${TEST_ROOT}/no.txt"
    RESULT_VARIABLE decline_result
)
file(READ "${CONFIG_FILE}" declined_contents)
if(NOT decline_result EQUAL 0 OR NOT declined_contents STREQUAL "max_cores = 7\n")
    message(FATAL_ERROR "Declining overwrite changed the configuration")
endif()

file(WRITE "${TEST_ROOT}/yes.txt" "yes\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${CONFIG_FILE}"
            "${QCLINT_EXE}" config init
    INPUT_FILE "${TEST_ROOT}/yes.txt"
    RESULT_VARIABLE confirm_result
)
file(READ "${CONFIG_FILE}" confirmed_contents)
if(NOT confirm_result EQUAL 0 OR
   NOT confirmed_contents MATCHES "max_cores = 32" OR
   NOT confirmed_contents MATCHES "gaussian_max_memory = 64" OR
   NOT confirmed_contents MATCHES "orca_max_memory = 51")
    message(FATAL_ERROR "Confirming overwrite did not replace the configuration")
endif()
