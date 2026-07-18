cmake_minimum_required(VERSION 3.16)
set(QCLINT_VERSION "1.2.0" CACHE STRING "qclint release version")
project(qclint VERSION "${QCLINT_VERSION}" LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(qclint_core
    src/charge_multiplicity.cpp
    src/elements.cpp
    src/resource_check.cpp
    src/user_config.cpp
)
target_include_directories(qclint_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(qclint_core PUBLIC cxx_std_17)

add_library(qclint_gaussian src/gaussian_input.cpp)
target_include_directories(qclint_gaussian PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(qclint_gaussian PUBLIC cxx_std_17)
target_link_libraries(qclint_gaussian PUBLIC qclint_core)

add_library(qclint_orca src/orca_input.cpp)
target_include_directories(qclint_orca PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(qclint_orca PUBLIC cxx_std_17)
target_link_libraries(qclint_orca PUBLIC qclint_core)

add_library(qclint_fix src/input_fix.cpp)
target_include_directories(qclint_fix PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(qclint_fix PUBLIC cxx_std_17)
target_link_libraries(qclint_fix PUBLIC
                      qclint_core qclint_gaussian qclint_orca)

add_executable(qclint src/qclint.cpp)
target_link_libraries(qclint PRIVATE qclint_core qclint_gaussian
                                     qclint_orca qclint_fix)
target_compile_definitions(qclint PRIVATE
                           QCLINT_VERSION="${PROJECT_VERSION}")

install(TARGETS qclint qclint_core qclint_gaussian qclint_orca qclint_fix
        EXPORT qclintTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT qclintTargets
        NAMESPACE qclint::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qclint)
configure_package_config_file(
    cmake/qclintConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qclintConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qclint)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qclintConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/qclintConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/qclintConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qclint)

option(QCLINT_BUILD_FUZZER "Build parser fuzz target with libFuzzer" OFF)
if(QCLINT_BUILD_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "QCLINT_BUILD_FUZZER requires Clang")
    endif()
    add_executable(qclint_parser_fuzz tests/parser_fuzz.cpp)
    target_link_libraries(qclint_parser_fuzz PRIVATE
                          qclint_gaussian qclint_orca)
    target_compile_options(qclint_parser_fuzz PRIVATE
                           -fsanitize=fuzzer,address,undefined)
    target_link_options(qclint_parser_fuzz PRIVATE
                        -fsanitize=fuzzer,address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(charge_multiplicity_test tests/charge_multiplicity_test.cpp)
    target_link_libraries(charge_multiplicity_test
                          PRIVATE qclint_core qclint_gaussian qclint_orca)
    add_test(NAME charge_multiplicity COMMAND charge_multiplicity_test)
    add_test(
        NAME qclint_cli_valid
        COMMAND ${CMAKE_COMMAND} -E env
                QCLINT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/config
                $<TARGET_FILE:qclint> --charge 0 --multiplicity 1
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/water.gjf
    )
    add_test(
        NAME qclint_cli_failures
        COMMAND ${CMAKE_COMMAND}
                -DQCLINT_EXE=$<TARGET_FILE:qclint>
                -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/water.gjf
                -DNORMAL_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/config
                -DSTRICT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/strict_config
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_failure_test.cmake
    )
    add_test(
        NAME qclint_cli_output
        COMMAND ${CMAKE_COMMAND}
                -DQCLINT_EXE=$<TARGET_FILE:qclint>
                -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/water.gjf
                -DUNSUPPORTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/README.md
                -DNORMAL_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/config
                -DMISSING_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/missing-output-config
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_cli_test.cmake
    )
    add_test(NAME qclint_version COMMAND $<TARGET_FILE:qclint> --version)
    set_tests_properties(qclint_version PROPERTIES
                         PASS_REGULAR_EXPRESSION "qclint ${PROJECT_VERSION}")
    add_test(
        NAME qclint_orca_simple
        COMMAND ${CMAKE_COMMAND} -E env
                QCLINT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                $<TARGET_FILE:qclint>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_simple.inp
    )
    add_test(
        NAME qclint_orca_multiline
        COMMAND ${CMAKE_COMMAND} -E env
                QCLINT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                $<TARGET_FILE:qclint>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_multiline.inp
    )
    add_test(
        NAME qclint_orca_extensions
        COMMAND ${CMAKE_COMMAND}
                -DQCLINT_EXE=$<TARGET_FILE:qclint>
                -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_simple.inp
                -DNORMAL_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                -DSTRICT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/strict_config
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/orca-extension-test
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/orca_extension_test.cmake
    )
    add_test(
        NAME qclint_orca_memory_limit
        COMMAND ${CMAKE_COMMAND}
                -DQCLINT_EXE=$<TARGET_FILE:qclint>
                -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_simple.inp
                -DLIMIT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/software_memory_config
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/orca_memory_limit_test.cmake
    )
    add_test(
        NAME qclint_orca_xyzfile
        COMMAND ${CMAKE_COMMAND} -E env
                QCLINT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                $<TARGET_FILE:qclint>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_xyzfile.inp
    )
    add_test(
        NAME qclint_orca_new_job
        COMMAND ${CMAKE_COMMAND} -E env
                QCLINT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                $<TARGET_FILE:qclint>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_new_job.inp
    )
    add_test(
        NAME qclint_orca_compound
        COMMAND ${CMAKE_COMMAND} -E env
                QCLINT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                $<TARGET_FILE:qclint>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_compound.inp
    )
    add_test(
        NAME qclint_gaussian_link1
        COMMAND ${CMAKE_COMMAND} -E env
                QCLINT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                $<TARGET_FILE:qclint>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/gaussian_link1.gjf
    )
    add_test(
        NAME qclint_gaussian_memory_limit
        COMMAND ${CMAKE_COMMAND}
                -DQCLINT_EXE=$<TARGET_FILE:qclint>
                -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/water.gjf
                -DLIMIT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/software_memory_config
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/gaussian-memory-limit-test
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gaussian_memory_limit_test.cmake
    )
    add_test(
        NAME qclint_config_cli
        COMMAND ${CMAKE_COMMAND}
                -DQCLINT_EXE=$<TARGET_FILE:qclint>
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/config-cli-test
                -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/water.gjf
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_cli_test.cmake
    )
    add_test(
        NAME qclint_fix_cli
        COMMAND ${CMAKE_COMMAND}
                -DQCLINT_EXE=$<TARGET_FILE:qclint>
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/fix-cli-test
                -DGAUSSIAN_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/water.gjf
                -DGAUSSIAN_CPU_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/gaussian_cpu.gjf
                -DORCA_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_simple.inp
                -DORCA_COMMENTS_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_comments.inp
                -DORCA_INVALID_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_invalid_state.inp
                -DSTRICT_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/strict_config
                -DORCA_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/orca_config
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/fix_cli_test.cmake
    )
    add_test(
        NAME qclint_package
        COMMAND ${CMAKE_COMMAND}
                -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-test
                -DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_test.cmake
    )
endif()
