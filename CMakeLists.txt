project(sysrepo-ietf-alarms LANGUAGES CXX)
cmake_minimum_required(VERSION 3.19)

# Build options
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Werror ${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic -Woverloaded-virtual -Wimplicit-fallthrough ${CMAKE_CXX_FLAGS}")
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "-Wsuggest-override ${CMAKE_CXX_FLAGS}")
endif()

add_custom_target(sysrepo-ietf-alarms-version-cmake-ide
    cmake/ProjectGitVersion.cmake
    cmake/ProjectGitVersionRunner.cmake
    )
include(cmake/ProjectGitVersion.cmake)
prepare_git_version(SYSREPO_IETF_ALARMS_VERSION "0.0")

find_package(Doxygen)
option(WITH_DOCS "Create and install internal documentation (needs Doxygen)" ${DOXYGEN_FOUND})

find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(date REQUIRED)

pkg_check_modules(SYSREPO REQUIRED IMPORTED_TARGET sysrepo-cpp>=1.1.0 sysrepo)
pkg_check_modules(LIBYANG REQUIRED IMPORTED_TARGET libyang-cpp>=1.1.0)

include(GNUInstallDirs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)

set(YANG_SRCS
    yang/ietf-alarms.yang
    yang/sysrepo-ietf-alarms.yang
    )

# Targets
# - utils
add_library(alarms-utils STATIC
    src/utils/libyang.cpp
    src/utils/libyang.h
    src/utils/log.h
    src/utils/log-fwd.h
    src/utils/log-init.cpp
    src/utils/log-init.h
    src/utils/string.cpp
    src/utils/string.h
    src/utils/sysrepo.cpp
    src/utils/sysrepo.h
    src/utils/time.cpp
    src/utils/time.h
    )
target_link_libraries(alarms-utils PUBLIC spdlog::spdlog PRIVATE fmt::fmt PkgConfig::LIBYANG PkgConfig::SYSREPO date::date-tz)

add_library(alarms STATIC
    src/alarms/Key.h
    src/alarms/Daemon.cpp
    src/alarms/Daemon.h
    src/alarms/Key.cpp
    src/alarms/Key.h
    src/alarms/PurgeFilter.cpp
    src/alarms/PurgeFilter.h
    src/alarms/ShelfMatch.cpp
    src/alarms/ShelfMatch.h
    )
target_link_libraries(alarms PUBLIC alarms-utils PRIVATE PkgConfig::LIBYANG)

# Testing
include(CTest)
if(BUILD_TESTING)
    find_package(trompeloeil 33 REQUIRED)
    find_package(doctest 2.3.1 REQUIRED)

    add_library(DoctestIntegration STATIC
        tests/doctest_integration.cpp
        tests/pretty_printers.h
        tests/trompeloeil_doctest.h
        )
    target_link_libraries(DoctestIntegration doctest::doctest trompeloeil::trompeloeil spdlog::spdlog)
    target_include_directories(DoctestIntegration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/)
    target_compile_definitions(DoctestIntegration PUBLIC DOCTEST_CONFIG_SUPER_FAST_ASSERTS)

    add_library(Testing STATIC
        tests/events.cpp
        tests/events.h
        tests/test_log_setup.h
        tests/test_alarm_helpers.h
        tests/test_sysrepo_helpers.cpp
        tests/test_sysrepo_helpers.h
        tests/test_time_interval.cpp
        tests/test_time_interval.h
        tests/wait-a-bit-longer.cpp
        )
    target_link_libraries(Testing PRIVATE spdlog::spdlog PkgConfig::LIBYANG PkgConfig::SYSREPO alarms-utils)
    target_include_directories(Testing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/)

    find_program(SYSREPOCTL sysrepoctl)
    find_program(SYSREPOCFG sysrepocfg)

    set(YANG_TEST_SRCS
        tests/yang/alarms-test.yang
        )

    function(ietfalarms_test name)
        add_executable(test-${name} ${CMAKE_SOURCE_DIR}/tests/${name}.cpp)
        target_link_libraries(test-${name} DoctestIntegration Testing)
        target_include_directories(test-${name}
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}
            PRIVATE
                ${CMAKE_BINARY_DIR}
        )

        if(NOT CMAKE_CROSSCOMPILING)
            add_test(test-${name} test-${name})
        endif()

        set(test_name_init sysrepo:prep:${name})
        set(test_name_cleanup sysrepo:clean:${name})
        set(fixture_name sysrepo:env:${name})

        # How to do this:
        # Check ARGN for name of fixture
        # then take the args for manage module from variable "fixture{fixture_name}"
        # and then add set env vars for sysrepo
        # set them ALSO for the init/cleanup tests
        if(ARGC GREATER 1)
            set(wanted_fixture ${ARGV1})
            add_test(NAME ${test_name_init} COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepoctl-manage-module.sh ${SYSREPOCTL} ${SYSREPOCFG} prepare ${${wanted_fixture}})
            set_tests_properties(${test_name_init} PROPERTIES FIXTURES_SETUP ${fixture_name})
            add_test(NAME ${test_name_cleanup} COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepoctl-manage-module.sh ${SYSREPOCTL} ${SYSREPOCFG} uninstall ${${wanted_fixture}})
            set_tests_properties(${test_name_cleanup} PROPERTIES FIXTURES_CLEANUP ${fixture_name})

            set_tests_properties(test-${name} PROPERTIES FIXTURES_REQUIRED ${fixture_name})
            set_property(TEST test-${name} ${test_name_init} ${test_name_cleanup} APPEND PROPERTY ENVIRONMENT
                "SYSREPO_REPOSITORY_PATH=${CMAKE_CURRENT_BINARY_DIR}/test_repositories/test_${name}"
                "SYSREPO_SHM_PREFIX=alarms_test_${name}"
                )

        endif()
    endfunction()

    ietfalarms_test(time)

    set(fixture-alarms_testing
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-alarms.yang FEATURE alarm-shelving
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/sysrepo-ietf-alarms.yang
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/alarms-test.yang
        )

    ietfalarms_test(alarm_publish fixture-alarms_testing)
    target_link_libraries(test-alarm_publish alarms)

    ietfalarms_test(alarm_purge fixture-alarms_testing)
    target_link_libraries(test-alarm_purge alarms)

    ietfalarms_test(alarm_notifications fixture-alarms_testing)
    target_link_libraries(test-alarm_notifications alarms)

    ietfalarms_test(alarm_shelving fixture-alarms_testing)
    target_link_libraries(test-alarm_shelving alarms)

    find_program(YANGLINT_PATH yanglint)
    if (NOT YANGLINT_PATH)
        message(FATAL_ERROR "Cannot find libyang's yanglint")
    endif()
    foreach(yang_file ${YANG_SRCS} ${YANG_TEST_SRCS})
        add_test(NAME yang-tree:${yang_file} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/yang COMMAND ${YANGLINT_PATH} -f tree ${CMAKE_CURRENT_SOURCE_DIR}/${yang_file})
    endforeach()
endif()

if(WITH_DOCS)
    set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${doxyfile_in} ${doxyfile} @ONLY)
    add_custom_target(doc
            COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
            SOURCES ${doxyfile_in}
            )
endif()

set(YANG_DIR ${CMAKE_INSTALL_PREFIX}/share/sysrepo-ietf-alarms/yang)
install(FILES ${YANG_SRCS} DESTINATION ${YANG_DIR})
