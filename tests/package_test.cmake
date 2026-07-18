if(NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "The package test is missing required variables")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
set(INSTALL_ROOT "${TEST_ROOT}/install")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
            --prefix "${INSTALL_ROOT}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "qclint installation failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/package_consumer"
            -B "${TEST_ROOT}/build"
            "-DCMAKE_PREFIX_PATH=${INSTALL_ROOT}"
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Installed package could not be consumed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${TEST_ROOT}/build"
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Package consumer did not build")
endif()

execute_process(
    COMMAND "${TEST_ROOT}/build/qclint_consumer${EXE_SUFFIX}"
    RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Package consumer failed")
endif()
