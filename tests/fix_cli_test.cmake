if(NOT DEFINED QCLINT_EXE OR NOT DEFINED TEST_ROOT OR
   NOT DEFINED GAUSSIAN_INPUT OR NOT DEFINED GAUSSIAN_CPU_INPUT OR
   NOT DEFINED ORCA_INPUT OR
   NOT DEFINED ORCA_COMMENTS_INPUT OR NOT DEFINED ORCA_INVALID_INPUT OR
   NOT DEFINED NORMAL_CONFIG OR NOT DEFINED STRICT_CONFIG OR
   NOT DEFINED ORCA_CONFIG)
    message(FATAL_ERROR "The fix CLI test is missing required variables")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

set(GAUSSIAN_UNDER_COPY "${TEST_ROOT}/water.gjf")
file(COPY_FILE "${GAUSSIAN_INPUT}" "${GAUSSIAN_UNDER_COPY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${NORMAL_CONFIG}"
            "${QCLINT_EXE}" --fix memory "${GAUSSIAN_UNDER_COPY}"
    RESULT_VARIABLE gaussian_under_result
    ERROR_VARIABLE gaussian_under_error
)
file(READ "${GAUSSIAN_UNDER_COPY}" gaussian_under_contents)
if(NOT gaussian_under_result EQUAL 0 OR
   NOT gaussian_under_contents MATCHES "%mem=4GiB")
    message(FATAL_ERROR
            "Gaussian underallocated memory was not raised to the configured allocation:\n${gaussian_under_error}")
endif()

set(ORCA_UNDER_COPY "${TEST_ROOT}/underallocated.inp")
file(COPY_FILE "${ORCA_INPUT}" "${ORCA_UNDER_COPY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${ORCA_CONFIG}"
            "${QCLINT_EXE}" --fix memory "${ORCA_UNDER_COPY}"
    RESULT_VARIABLE orca_under_result
    ERROR_VARIABLE orca_under_error
)
file(READ "${ORCA_UNDER_COPY}" orca_under_contents)
if(NOT orca_under_result EQUAL 0 OR
   NOT orca_under_contents MATCHES "%maxcore 3584")
    message(FATAL_ERROR
            "ORCA underallocated memory was not raised to the configured allocation:\n${orca_under_error}")
endif()

set(GAUSSIAN_CPU_COPY "${TEST_ROOT}/cpu.gjf")
file(COPY_FILE "${GAUSSIAN_CPU_INPUT}" "${GAUSSIAN_CPU_COPY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" --fix-all "${GAUSSIAN_CPU_COPY}"
    RESULT_VARIABLE gaussian_cpu_result
    OUTPUT_VARIABLE gaussian_cpu_output
)
file(READ "${GAUSSIAN_CPU_COPY}" gaussian_cpu_contents)
if(NOT gaussian_cpu_result EQUAL 0 OR
   NOT gaussian_cpu_contents MATCHES "%chk=cpu.chk" OR
   NOT gaussian_cpu_contents MATCHES "%CPU=0-1" OR
   NOT gaussian_cpu_contents MATCHES "%mem=1GiB")
    message(FATAL_ERROR "Gaussian CPU fix failed:\n${gaussian_cpu_output}")
endif()

set(GAUSSIAN_COPY "${TEST_ROOT}/renamed.gjf")
file(COPY_FILE "${GAUSSIAN_INPUT}" "${GAUSSIAN_COPY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" --fix-all "${GAUSSIAN_COPY}"
    RESULT_VARIABLE gaussian_result
    OUTPUT_VARIABLE gaussian_output
    ERROR_VARIABLE gaussian_error
)
file(READ "${GAUSSIAN_COPY}" gaussian_contents)
if(NOT gaussian_result EQUAL 0 OR
   NOT gaussian_contents MATCHES "%chk=renamed.chk" OR
   NOT gaussian_contents MATCHES "%nprocshared=2" OR
   NOT gaussian_contents MATCHES "%mem=1GiB" OR
   NOT gaussian_contents MATCHES "0 1")
    message(FATAL_ERROR "Gaussian --fix-all failed:\n${gaussian_output}\n${gaussian_error}")
endif()

set(ORCA_COPY "${TEST_ROOT}/simple.inp")
file(COPY_FILE "${ORCA_INPUT}" "${ORCA_COPY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" --fix-all "${ORCA_COPY}"
    RESULT_VARIABLE orca_result
    OUTPUT_VARIABLE orca_output
    ERROR_VARIABLE orca_error
)
file(READ "${ORCA_COPY}" orca_contents)
if(NOT orca_result EQUAL 0 OR
   NOT orca_contents MATCHES "nprocs 2" OR
   NOT orca_contents MATCHES "%maxcore 512" OR
   NOT orca_contents MATCHES "\\* xyz 0 2")
    message(FATAL_ERROR "ORCA --fix-all failed:\n${orca_output}\n${orca_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${ORCA_CONFIG}"
            "${QCLINT_EXE}" --fix charge "${ORCA_COPY}"
    RESULT_VARIABLE forbidden_result
    OUTPUT_VARIABLE forbidden_output
    ERROR_VARIABLE forbidden_error
)
file(READ "${ORCA_COPY}" contents_after_forbidden_fix)
if(NOT forbidden_result EQUAL 2 OR
   NOT forbidden_error MATCHES "unknown fix item: charge" OR
   NOT contents_after_forbidden_fix STREQUAL orca_contents)
    message(FATAL_ERROR "Charge fix was not safely rejected")
endif()

set(COMMENT_COPY "${TEST_ROOT}/comments.inp")
file(COPY_FILE "${ORCA_COMMENTS_INPUT}" "${COMMENT_COPY}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" --fix-all "${COMMENT_COPY}"
    RESULT_VARIABLE comment_result
)
file(READ "${COMMENT_COPY}" comment_contents)
if(NOT comment_result EQUAL 0 OR
   NOT comment_contents MATCHES "# Historical example: %maxcore 9999" OR
   NOT comment_contents MATCHES "# Historical example: nprocs 128" OR
   NOT comment_contents MATCHES "%maxcore 512" OR
   NOT comment_contents MATCHES "nprocs 2")
    message(FATAL_ERROR "Fixing directives modified comments")
endif()

set(INVALID_COPY "${TEST_ROOT}/invalid.inp")
file(COPY_FILE "${ORCA_INVALID_INPUT}" "${INVALID_COPY}")
file(READ "${INVALID_COPY}" invalid_before)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" --fix-all "${INVALID_COPY}"
    RESULT_VARIABLE invalid_result
)
file(READ "${INVALID_COPY}" invalid_after)
if(NOT invalid_result EQUAL 1 OR NOT invalid_after STREQUAL invalid_before)
    message(FATAL_ERROR "A failed transactional fix modified the input")
endif()

set(DRY_RUN_COPY "${TEST_ROOT}/dry-run.inp")
file(COPY_FILE "${ORCA_INPUT}" "${DRY_RUN_COPY}")
file(READ "${DRY_RUN_COPY}" dry_run_before)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "QCLINT_CONFIG=${STRICT_CONFIG}"
            "${QCLINT_EXE}" --fix-all --dry-run "${DRY_RUN_COPY}"
    RESULT_VARIABLE dry_run_result
    OUTPUT_VARIABLE dry_run_output
    ERROR_VARIABLE dry_run_error
)
file(READ "${DRY_RUN_COPY}" dry_run_after)
if(NOT dry_run_result EQUAL 1 OR
   NOT dry_run_error MATCHES "note\\[fix.plan\\]" OR
   NOT dry_run_after STREQUAL dry_run_before)
    message(FATAL_ERROR "Dry-run modified the input")
endif()
