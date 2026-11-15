# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Helpers for the regular E2E base-test category.
# Fuzz targets intentionally do not use this helper.

include_guard(GLOBAL)

# Discover category/<suite>/all_tests.c manifests, build one static library per
# suite, and generate a dispatcher that references every suite entry point.
#
# Required arguments:
#   CATEGORY             category name used in target names and diagnostics
#   CATEGORY_ROOT        directory containing the suite directories
#   SUITE_TARGET_PREFIX  prefix for per-suite static library targets
#   SUITE_ENTRY_SUFFIX   suffix in run_<suite>_<suffix>()
#   DISPATCH_FUNCTION    category dispatcher function name
#   DISPATCH_SOURCE_VAR  caller-scope variable receiving generated source path
#   SUITE_LIBS_VAR        caller-scope variable receiving static library names
#
# Optional arguments:
#   EXCLUDE_DIRS          suite-directory names containing category support code
#   EXTRA_INCLUDE_DIRS    include directories applied to every suite target
#   EXTRA_LINK_LIBS       libraries linked privately by every suite target
function(base_test_add_category)
    set(_options)
    set(_one_value_args
        CATEGORY
        CATEGORY_ROOT
        SUITE_TARGET_PREFIX
        SUITE_ENTRY_SUFFIX
        DISPATCH_FUNCTION
        DISPATCH_SOURCE_VAR
        SUITE_LIBS_VAR
    )
    set(_multi_value_args EXCLUDE_DIRS EXTRA_INCLUDE_DIRS EXTRA_LINK_LIBS)
    cmake_parse_arguments(_CATEGORY "${_options}" "${_one_value_args}"
        "${_multi_value_args}" ${ARGN})

    foreach(_required IN ITEMS CATEGORY CATEGORY_ROOT SUITE_TARGET_PREFIX
            SUITE_ENTRY_SUFFIX DISPATCH_FUNCTION DISPATCH_SOURCE_VAR SUITE_LIBS_VAR)
        if(NOT DEFINED _CATEGORY_${_required} OR
           "${_CATEGORY_${_required}}" STREQUAL "")
            message(FATAL_ERROR
                "base_test_add_category requires ${_required}")
        endif()
    endforeach()
    if(_CATEGORY_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "base_test_add_category received unknown arguments: "
            "${_CATEGORY_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT IS_DIRECTORY "${_CATEGORY_CATEGORY_ROOT}")
        message(FATAL_ERROR
            "${_CATEGORY_CATEGORY} category directory does not exist: "
            "${_CATEGORY_CATEGORY_ROOT}")
    endif()

    # A category is made up of direct child directories.  Keep support
    # directories (for example test/e2e/common) explicit so a directory with C
    # sources cannot silently become an unregistered suite.
    file(GLOB _category_children CONFIGURE_DEPENDS LIST_DIRECTORIES true
        "${_CATEGORY_CATEGORY_ROOT}/*")
    list(SORT _category_children)

    set(_suite_dirs)
    foreach(_child IN LISTS _category_children)
        if(NOT IS_DIRECTORY "${_child}")
            get_filename_component(_child_name "${_child}" NAME)
            if(_child MATCHES "\\.c$")
                message(FATAL_ERROR
                    "${_CATEGORY_CATEGORY} category source '${_child_name}' "
                    "must be under <suite>/all_tests.c")
            endif()
            continue()
        endif()

        get_filename_component(_child_name "${_child}" NAME)
        list(FIND _CATEGORY_EXCLUDE_DIRS "${_child_name}" _excluded)
        if(NOT _excluded EQUAL -1)
            continue()
        endif()

        file(GLOB _child_c_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
            "${_child}/*.c")
        list(SORT _child_c_sources)
        if(NOT _child_c_sources)
            # Empty directories do not define suites; this keeps an empty
            # category valid while still validating directories with sources.
            continue()
        endif()

        if(NOT EXISTS "${_child}/all_tests.c")
            message(FATAL_ERROR
                "${_CATEGORY_CATEGORY} suite directory '${_child_name}' "
                "contains C sources but no all_tests.c manifest")
        endif()
        list(APPEND _suite_dirs "${_child}")
    endforeach()

    set(_manifests)
    set(_suite_names)
    foreach(_suite_dir IN LISTS _suite_dirs)
        get_filename_component(_suite "${_suite_dir}" NAME)
        if(NOT _suite MATCHES "^[a-z0-9_]+$")
            message(FATAL_ERROR
                "${_CATEGORY_CATEGORY} suite directory '${_suite}' is not a "
                "valid suite name; use lowercase letters, digits, and underscores")
        endif()

        list(FIND _suite_names "${_suite}" _duplicate)
        if(NOT _duplicate EQUAL -1)
            message(FATAL_ERROR
                "Duplicate ${_CATEGORY_CATEGORY} suite name '${_suite}'")
        endif()
        list(APPEND _suite_names "${_suite}")
        list(APPEND _manifests "${_suite_dir}/all_tests.c")

        # A manifest must provide the symbol consumed by the generated
        # dispatcher.  Checking this at configure time catches a misplaced or
        # stale manifest before a much less useful linker error.
        file(READ "${_suite_dir}/all_tests.c" _manifest_content)
        set(_entry_symbol "run_${_suite}_${_CATEGORY_SUITE_ENTRY_SUFFIX}")
        string(REGEX MATCH
            "void[ \t\r\n]+${_entry_symbol}[ \t\r\n]*\\([^{}]*\\)[ \t\r\n]*\\{"
            _entry_match "${_manifest_content}")
        if(NOT _entry_match)
            message(FATAL_ERROR
                "${_CATEGORY_CATEGORY} suite '${_suite}' manifest must define "
                "${_entry_symbol}(...)")
        endif()
        unset(_entry_match)
        unset(_manifest_content)
        unset(_entry_symbol)
        unset(_entry_position)
    endforeach()

    # _suite_dirs is already deterministic, but sort the names explicitly so
    # dispatcher and archive ordering remain stable if discovery changes.
    list(SORT _suite_names)
    set(_sorted_suite_dirs)
    foreach(_suite IN LISTS _suite_names)
        list(APPEND _sorted_suite_dirs
            "${_CATEGORY_CATEGORY_ROOT}/${_suite}")
    endforeach()
    set(_suite_dirs "${_sorted_suite_dirs}")

    set(_dispatcher "// Generated by regular_test_category.cmake.\n")
    string(APPEND _dispatcher
        "// Do not edit; add an all_tests.c manifest under the category instead.\n\n")
    foreach(_suite IN LISTS _suite_names)
        string(APPEND _dispatcher
            "void run_${_suite}_${_CATEGORY_SUITE_ENTRY_SUFFIX}(void);\n")
    endforeach()
    string(APPEND _dispatcher
        "\nvoid ${_CATEGORY_DISPATCH_FUNCTION}(void)\n{\n")
    foreach(_suite IN LISTS _suite_names)
        string(APPEND _dispatcher
            "    run_${_suite}_${_CATEGORY_SUITE_ENTRY_SUFFIX}();\n")
    endforeach()
    string(APPEND _dispatcher "}\n")

    set(_dispatch_source
        "${CMAKE_CURRENT_BINARY_DIR}/${_CATEGORY_CATEGORY}_category_dispatch.c")
    # Write at configure time rather than using file(GENERATE).  The generated
    # dispatcher is a source input, so it must still exist after a clean build
    # removes the binary directory contents.
    file(WRITE "${_dispatch_source}" "${_dispatcher}")

    set(_suite_libs)
    foreach(_suite IN LISTS _suite_names)
        set(_suite_dir "${_CATEGORY_CATEGORY_ROOT}/${_suite}")
        file(GLOB _suite_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
            "${_suite_dir}/*.c")
        list(SORT _suite_sources)

        set(_target "${_CATEGORY_SUITE_TARGET_PREFIX}_${_suite}_all")
        add_library("${_target}" STATIC ${_suite_sources})
        target_include_directories("${_target}" PRIVATE
            "${_suite_dir}"
            ${_CATEGORY_EXTRA_INCLUDE_DIRS})
        target_link_libraries("${_target}" PRIVATE
            common_test_dep
            ${_CATEGORY_EXTRA_LINK_LIBS})
        list(APPEND _suite_libs "${_target}")
    endforeach()

    set(${_CATEGORY_DISPATCH_SOURCE_VAR} "${_dispatch_source}" PARENT_SCOPE)
    set(${_CATEGORY_SUITE_LIBS_VAR} "${_suite_libs}" PARENT_SCOPE)
endfunction()
