include(FetchContent)
find_package(Git REQUIRED)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "Dependency checkout root")
file(READ "${CMAKE_SOURCE_DIR}/third_party/dependencies.lock.json" lock)
string(JSON count LENGTH "${lock}" dependencies)
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
    foreach(field name repository tag commit)
        string(JSON ${field} GET "${lock}" dependencies ${index} ${field})
    endforeach()
    FetchContent_Declare(${name}
        GIT_REPOSITORY "${repository}"
        GIT_TAG "${tag}"
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/${name}-src"
        SOURCE_SUBDIR _do_not_add_upstream_build)
    FetchContent_MakeAvailable(${name})
    # Verify the pinned revision BEFORE patching, so local patches (applied below)
    # never make this check look like a version mismatch.
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${${name}_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE actual OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT actual STREQUAL commit)
        message(FATAL_ERROR "${name}: expected ${commit}, got ${actual} (git exit ${result})")
    endif()
endforeach()

# Apply this project's vendor patches. Some parts of the firmware cannot be
# configured from outside the vendor tree -- notably the main/ISR stack size, which
# CMSIS startup files hard-code. Patches live in third_party/patches/ and stay
# tracked in git even though third_party/*-src/ is ignored.
#
# These edits are required, not cosmetic: without the Keil patch below the ARM
# target silently runs on a 1 KB stack instead of the intended 4 KB. See
# projects/stm32f407/KEIL.md.
set(FW_PATCH_DIR "${CMAKE_SOURCE_DIR}/third_party/patches")
if(EXISTS "${FW_PATCH_DIR}")
    file(GLOB FW_PATCHES "${FW_PATCH_DIR}/*.patch")
    foreach(patch ${FW_PATCHES})
        get_filename_component(patch_name "${patch}" NAME)
        # Map "<dependency>-<anything>.patch" to the dependency source directory.
        string(REGEX REPLACE "[-.].*$" "" patch_dep "${patch_name}")
        set(patch_root "${CMAKE_SOURCE_DIR}/third_party/${patch_dep}-src")
        if(NOT IS_DIRECTORY "${patch_root}")
            message(FATAL_ERROR "patch ${patch_name}: no dependency directory ${patch_root}")
        endif()
        # Idempotent: a re-configure must not fail just because the patch is in place.
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${patch_root}" apply --reverse --check "${patch}"
            RESULT_VARIABLE already_applied
            ERROR_QUIET OUTPUT_QUIET)
        if(already_applied EQUAL 0)
            message(STATUS "Dependency patch already applied: ${patch_name}")
        else()
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${patch_root}" apply --check "${patch}"
                RESULT_VARIABLE can_apply
                ERROR_VARIABLE apply_error OUTPUT_QUIET)
            if(NOT can_apply EQUAL 0)
                message(FATAL_ERROR "patch ${patch_name} does not apply to ${patch_dep}-src:\n${apply_error}")
            endif()
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${patch_root}" apply "${patch}"
                RESULT_VARIABLE applied
                ERROR_VARIABLE apply_error)
            if(NOT applied EQUAL 0)
                message(FATAL_ERROR "failed to apply ${patch_name}:\n${apply_error}")
            endif()
            message(STATUS "Applied dependency patch: ${patch_name}")
        endif()
    endforeach()
endif()
