cmake_minimum_required(VERSION 3.21)
project(embedded_framework VERSION 0.1.0 LANGUAGES C ASM)

# C99, deliberately not C11. The Keil chain is fixed at C99 (--c99 in
# tools/build-keil-ac5.mjs, and the uVision project's own C options); AC5 has no C11
# mode at all. Pinning the shared sources to the lower common standard is what keeps
# the two chains honest: with -std=c11 here, a C11-only construct (say _Static_assert)
# would pass GCC and every host test, then break the Keil build -- and no automated
# step currently watches the Keil build. See docs/architecture.md.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(FW_TARGET "host" CACHE STRING "host or stm32f407")
set_property(CACHE FW_TARGET PROPERTY STRINGS host stm32f407)

# The core libraries' file lists live in exactly one place: the Keil manifest.
# The same manifest drives tools/build-keil-ac5.mjs, so adding a source there updates
# both chains instead of drifting between two hand-kept lists (see docs/architecture.md).
# tools/check-sources.mjs additionally guards the third copy, the uVision project.
file(READ "${CMAKE_SOURCE_DIR}/projects/stm32f407/manifest.json" FW_MANIFEST)

function(fw_core_library name)
    string(JSON count LENGTH "${FW_MANIFEST}" coreLibraries ${name})
    math(EXPR last "${count} - 1")
    set(sources)
    foreach(index RANGE ${last})
        string(JSON item GET "${FW_MANIFEST}" coreLibraries ${name} ${index})
        list(APPEND sources "${CMAKE_SOURCE_DIR}/${item}")
    endforeach()
    add_library(${name} STATIC ${sources})
endfunction()

fw_core_library(fw_core)
target_include_directories(fw_core PUBLIC framework/include)
fw_core_library(fw_protocol)
target_include_directories(fw_protocol PUBLIC framework/components/protocol/include)
target_link_libraries(fw_protocol PUBLIC fw_core)
fw_core_library(fw_app)
target_include_directories(fw_app PUBLIC framework/app/include)
target_link_libraries(fw_app PUBLIC fw_protocol)

function(fw_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror;-Wpedantic>)
    endif()
endfunction()
foreach(target fw_core fw_protocol fw_app)
    fw_warnings(${target})
endforeach()

if(FW_TARGET STREQUAL "host")
    add_library(fw_host STATIC framework/ports/host/host.c)
    target_include_directories(fw_host PUBLIC framework/ports/host/include)
    target_link_libraries(fw_host PUBLIC fw_core)
    fw_warnings(fw_host)
    # Interface conformance suite. Depends only on fw_core, so any port can link it;
    # tests/conformance/host_adapter.c is the worked example of attaching one.
    add_library(fw_conformance STATIC tests/conformance/conformance.c)
    target_include_directories(fw_conformance PUBLIC tests/conformance/include)
    target_link_libraries(fw_conformance PUBLIC fw_core)
    fw_warnings(fw_conformance)
    add_executable(fw_demo projects/host/main.c)
    target_link_libraries(fw_demo PRIVATE fw_app fw_host)
    fw_warnings(fw_demo)
    enable_testing()
    add_executable(fw_tests tests/host/test_main.c)
    target_link_libraries(fw_tests PRIVATE fw_app fw_host)
    fw_warnings(fw_tests)
    add_executable(fw_conformance_tests tests/conformance/main.c tests/conformance/host_adapter.c)
    target_include_directories(fw_conformance_tests PRIVATE tests/conformance)
    target_link_libraries(fw_conformance_tests PRIVATE fw_conformance fw_host)
    fw_warnings(fw_conformance_tests)
    add_test(NAME framework_contracts COMMAND fw_tests)
    add_test(NAME host_demo COMMAND fw_demo)
    add_test(NAME interface_conformance COMMAND fw_conformance_tests)
    find_program(FW_NODE_EXECUTABLE node)
    if(FW_NODE_EXECUTABLE)
        # The core source list, the Keil manifest and the uVision project are three
        # hand-maintained copies of the same thing. This test is what stops them
        # from drifting apart without anyone noticing.
        add_test(NAME source_manifest_sync
                 COMMAND ${FW_NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check-sources.mjs)
        add_test(NAME docs_links
                 COMMAND ${FW_NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check-docs.mjs)
    else()
        message(STATUS "node not found: source-manifest consistency test is skipped")
    endif()
elseif(FW_TARGET STREQUAL "stm32f407")
    if(NOT CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Use cmake/toolchains/arm-none-eabi.cmake")
    endif()
    include(cmake/dependencies.cmake)
    add_subdirectory(projects/stm32f407)
else()
    message(FATAL_ERROR "Unsupported FW_TARGET=${FW_TARGET}")
endif()
