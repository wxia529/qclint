if(NOT DEFINED QCLINT_EXE OR NOT DEFINED INPUT_FILE OR
   NOT DEFINED NORMAL_CONFIG OR NOT DEFINED STRICT_CONFIG OR
   NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "The ORCA extension test is missing required variables")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

foreach(extension IN ITEMS inp in orca)
    set(INPUT_COPY "${TEST_ROOT}/calculation.${extension}")
    file(COPY_FILE "${INPUT_FILE}" "${INPUT_COPY}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${NORMAL_CONFIG}"
                "${QCLINT_EXE}" "${INPUT_COPY}"
        RESULT_VARIABLE check_result
        OUTPUT_VARIABLE check_output
        ERROR_VARIABLE check_error
    )
    if(NOT check_result EQUAL 0 OR NOT check_output STREQUAL "" OR
       NOT check_error MATCHES
           "warning\\[resource.memory-underallocated\\]: requested 218.75 GiB; configured allocation is 224 GiB")
        message(FATAL_ERROR
                "ORCA .${extension} input was not checked successfully")
    endif()
endforeach()

set(FIX_COPY "${TEST_ROOT}/fix.orca")
file(COPY_FILE "${INPUT_FILE}" "${FIX_COPY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" --fix-all "${FIX_COPY}"
    RESULT_VARIABLE fix_result
    ERROR_VARIABLE fix_error
)
file(READ "${FIX_COPY}" fixed_contents)
if(NOT fix_result EQUAL 0 OR
   NOT fixed_contents MATCHES "%maxcore 512" OR
   NOT fixed_contents MATCHES "nprocs 2")
    message(FATAL_ERROR
            "ORCA .orca input did not use the ORCA fixer:\n${fix_error}")
endif()
