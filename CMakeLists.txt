cmake_minimum_required(VERSION 3.19)
project(sysrepo-ietf-alarms LANGUAGES CXX)

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
prepare_git_version(SYSREPO_IETF_ALARMS_VERSION "1")

find_package(Doxygen)
option(WITH_DOCS "Create and install internal documentation (needs Doxygen)" ${DOXYGEN_FOUND})

find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(date REQUIRED) # FIXME: Remove when we have STL with __cpp_lib_chrono >= 201907 (gcc 14)
find_package(Boost 1.78 REQUIRED CONFIG)
find_package(fmt "9.0.0" REQUIRED)

pkg_check_modules(DOCOPT REQUIRED IMPORTED_TARGET docopt)
pkg_check_modules(SYSREPO REQUIRED IMPORTED_TARGET sysrepo-cpp>=5 sysrepo)
pkg_check_modules(LIBYANG REQUIRED IMPORTED_TARGET libyang-cpp>=3)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

include(GNUInstallDirs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(YANG_SRCS
    yang/ietf-alarms@2019-09-11.yang
    yang/sysrepo-ietf-alarms@2022-02-17.yang
    )

# Targets
# - utils
add_library(alarms-utils STATIC
    src/utils/benchmark.cpp
    src/utils/benchmark.h
    src/utils/exceptions.cpp
    src/utils/exceptions.h
    src/utils/journal.cpp
    src/utils/journal.h
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
    src/utils/waitUntilSignalled.cpp
    src/utils/waitUntilSignalled.h
    )
target_link_libraries(alarms-utils PUBLIC spdlog::spdlog fmt::fmt PkgConfig::LIBYANG PkgConfig::SYSREPO)

add_library(alarms STATIC
    src/alarms/Key.h
    src/alarms/Daemon.cpp
    src/alarms/Daemon.h
    src/alarms/Key.cpp
    src/alarms/Key.h
    src/alarms/Filters.cpp
    src/alarms/Filters.h
    src/alarms/ShelfMatch.cpp
    src/alarms/ShelfMatch.h
    )
target_link_libraries(alarms PUBLIC alarms-utils Boost::headers PRIVATE date::date-tz)

add_executable(sysrepo-ietf-alarmsd src/main.cpp)
add_dependencies(sysrepo-ietf-alarmsd target-SYSREPO_IETF_ALARMS_VERSION)
target_link_libraries(sysrepo-ietf-alarmsd PUBLIC alarms PkgConfig::DOCOPT PRIVATE PkgConfig::SYSTEMD)

# Testing
include(CTest)
if(BUILD_TESTING)
    find_package(trompeloeil 45 REQUIRED)
    find_package(doctest 2.4.11 REQUIRED)

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
    target_link_libraries(Testing PRIVATE spdlog::spdlog PkgConfig::LIBYANG PkgConfig::SYSREPO alarms-utils date::date-tz)
    target_include_directories(Testing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/)

    include(cmake/SysrepoTest.cmake)

    set(YANG_TEST_SRCS
        tests/yang/alarms-test.yang
        )

    function(ietfalarms_test)
        cmake_parse_arguments(TEST "" "NAME" "" ${ARGN})
        sysrepo_test(NAME ${TEST_NAME} ${TEST_UNPARSED_ARGUMENTS})
        target_link_libraries(test-${TEST_NAME} DoctestIntegration Testing alarms)
    endfunction()

    set(fixture-alarms_testing
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-alarms@2019-09-11.yang
            --enable-feature alarm-history
            --enable-feature alarm-shelving
            --enable-feature alarm-summary
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/sysrepo-ietf-alarms@2022-02-17.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/alarms-test.yang
        )

    ietfalarms_test(NAME alarm_publish FIXTURE fixture-alarms_testing)
    ietfalarms_test(NAME alarm_purge FIXTURE fixture-alarms_testing)
    ietfalarms_test(NAME alarm_compress FIXTURE fixture-alarms_testing)
    ietfalarms_test(NAME alarm_notifications FIXTURE fixture-alarms_testing)
    ietfalarms_test(NAME alarm_shelving FIXTURE fixture-alarms_testing)
    ietfalarms_test(NAME alarm_summary FIXTURE fixture-alarms_testing)
    ietfalarms_test(NAME benchmark FIXTURE fixture-alarms_testing)

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

set(YANG_DIR ${CMAKE_INSTALL_PREFIX}/share/yang/modules/sysrepo-ietf-alarms)
install(FILES ${YANG_SRCS} DESTINATION ${YANG_DIR})
install(TARGETS sysrepo-ietf-alarmsd RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
