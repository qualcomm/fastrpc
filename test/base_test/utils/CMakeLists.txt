# Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# utils/CMakeLists.txt
#
# Builds two static libraries and exposes them as a single INTERFACE target:
#
#   fastrpc_test_xml_writer
#     xml_writer subsystem (6 source files).
#     Compiled with -D_POSIX_C_SOURCE=200809L (needs strdup) rather than
#     the 200112L used everywhere else.
#
#   fastrpc_test_utils
#     Reporting pipeline + fastrpc test utilities.
#     Compiled with the standard -D_POSIX_C_SOURCE=200112L.
#
# Exports:
#   utils_dep   INTERFACE library target

set(_utils_all_inc
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fastrpc_utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/log_capture"
    "${CMAKE_CURRENT_SOURCE_DIR}/reporting/allure"
    "${CMAKE_CURRENT_SOURCE_DIR}/reporting/unity"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/streaming"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/error"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/logging"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/facade"
    "${FASTRPC_TEST_IDL_INC_DIR}"
    "${FASTRPC_INC_DIR}"
    "${CMAKE_SOURCE_DIR}/vendor/unity/src"
    "${CMAKE_SOURCE_DIR}/vendor/unity/extras/fixture/src"
    "${CMAKE_SOURCE_DIR}/vendor/unity/extras/memory/src"
)

# ----------------------------------------------------------------------------
# fastrpc_test_xml_writer
# ----------------------------------------------------------------------------
add_library(fastrpc_test_xml_writer STATIC
    xml_writer/core/xml_data_model.c
    xml_writer/streaming/xml_stream_writer.c
    xml_writer/error/xml_error_handler.c
    xml_writer/logging/xml_logger.c
    xml_writer/config/xml_config.c
    xml_writer/facade/xml_output_facade.c
)

set_target_properties(fastrpc_test_xml_writer PROPERTIES
    POSITION_INDEPENDENT_CODE OFF
)

target_include_directories(fastrpc_test_xml_writer PRIVATE
    ${_utils_all_inc}
)

target_compile_options(fastrpc_test_xml_writer PRIVATE
    -U_POSIX_C_SOURCE
    -D_POSIX_C_SOURCE=200809L
    -DUNITY_OUTPUT_COLOR
    -DUNITY_INCLUDE_PRINT_FORMATTED
)

# ----------------------------------------------------------------------------
# fastrpc_test_utils
# ----------------------------------------------------------------------------
add_library(fastrpc_test_utils STATIC
    reporting/allure/unity_allure_output.c
    reporting/unity/unity_fixture_file_output.c
    log_capture/log_capture.c
    fastrpc_utils/test_utils.c
)

set_target_properties(fastrpc_test_utils PROPERTIES
    POSITION_INDEPENDENT_CODE OFF
)

target_include_directories(fastrpc_test_utils PRIVATE
    ${_utils_all_inc}
)

target_compile_definitions(fastrpc_test_utils PRIVATE
    ${PLATFORM_C_DEFS}
    ${UNITY_COMPILE_DEFS}
    FASTRPC_TEST_VERSION="${PROJECT_VERSION}"
)

# ----------------------------------------------------------------------------
# utils_dep — exported INTERFACE target
# ----------------------------------------------------------------------------
add_library(utils_dep INTERFACE)

target_link_libraries(utils_dep INTERFACE
    fastrpc_test_utils
    fastrpc_test_xml_writer
)

target_include_directories(utils_dep INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fastrpc_utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/log_capture"
    "${CMAKE_CURRENT_SOURCE_DIR}/reporting/allure"
    "${CMAKE_CURRENT_SOURCE_DIR}/reporting/unity"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/streaming"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/error"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/logging"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/xml_writer/facade"
)
