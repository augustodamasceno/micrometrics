#############################################################################
# micrometrics C++ CMake Project
#
# Copyright (c) 2026, Augusto Damasceno. All rights reserved.
#
# SPDX-License-Identifier: BSD-2-Clause
#
# See (https://github.com/augustodamasceno/micrometrics)
#############################################################################
cmake_minimum_required(VERSION 3.16)
project(micrometrics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel|IntelLLVM")
    add_compile_options(-Wall -Wextra)
else()
    message(WARNING "Unknown compiler: ${CMAKE_CXX_COMPILER_ID}. No warning flags set.")
endif()

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

foreach(SRC_FILE ${ALL_SOURCES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_SOURCE_DIR}" "${SRC_FILE}")
    string(REGEX REPLACE "[/\\\\]" "_" TARGET_NAME "${REL_PATH}")
    string(REGEX REPLACE "\\.cpp$" "" TARGET_NAME "${TARGET_NAME}")

    add_executable(${TARGET_NAME} ${SRC_FILE})

    message(STATUS "Registered target: ${TARGET_NAME}  <-  ${REL_PATH}")
endforeach()