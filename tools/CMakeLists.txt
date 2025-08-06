# COPYRIGHT (C) 2025 INDUCTOHEAT, INC. ALL RIGHTS RESERVED.
#
# THIS SOURCE CODE DISTRIBUTION IS THE SOLE PROPERTY OF INDUCTOHEAT, INC. ANY
# REPRODUCTION IN PART OR AS A WHOLE IS STRICTLY PROHIBITED WITHOUT THE WRITTEN
# PERMISSION OF INDUCTOHEAT, INC.

cmake_minimum_required(VERSION 3.20.0)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Set default value for CANOPEN_DIR if it wasn't set externally
if(NOT DEFINED CANOPEN_DIR)
    set(CANOPEN_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

set(EDS2C_SCRIPT "${CANOPEN_DIR}/eds2c_wrapper.py")
set(EDS_UTILS_PY "eds-utils==0.3.1")
set(EDS_FILE "${CANOPEN_DIR}/ismart-fanout-board.eds")
set(GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/OD.h")
set(GENERATED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/OD.c")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# Check if eds-utils is installed
execute_process(
    COMMAND pip show eds-utils
    RESULT_VARIABLE EDS_UTILS_FOUND
    OUTPUT_QUIET
    ERROR_QUIET
)

if(NOT EDS_UTILS_FOUND EQUAL 0)
    message(STATUS "eds-utils not found. Installing with pip...")
    execute_process(
        COMMAND pip install ${EDS_UTILS_PY}
        RESULT_VARIABLE PIP_INSTALL_RESULT
    )
    if(NOT PIP_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to install eds-utils via pip")
    endif()
endif()

# Generate if EDS file changed or outputs missing
add_custom_command(
    OUTPUT ${GENERATED_HEADER} ${GENERATED_SOURCE}
    COMMAND ${Python3_EXECUTABLE} ${EDS2C_SCRIPT} ${EDS_FILE} -o ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${EDS_FILE}
    COMMENT "Generating CANopenNode object dictionary from ${EDS_FILE} using eds-utils"
    VERBATIM
)

add_custom_target(generate_od
    DEPENDS ${GENERATED_HEADER} ${GENERATED_SOURCE}
)

# Export variables so parent scope can use
set(CO_OD_H ${GENERATED_HEADER} PARENT_SCOPE)
set(CO_OD_C ${GENERATED_SOURCE} PARENT_SCOPE)
set(CO_OD_DIR ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
