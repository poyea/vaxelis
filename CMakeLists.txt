# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

cmake_minimum_required(VERSION 3.30)

project(
    vaxelis
    VERSION 0.1.0
    DESCRIPTION "Vaxelis game engine"
    LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 26 CACHE STRING "C++ language standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VAXELIS_BUILD_RUNTIME "Build the runtime executable" ON)
option(VAXELIS_BUILD_EXAMPLES "Build the example programs" ON)
option(VAXELIS_BUILD_BENCHMARKS "Build the headless benchmarks" ON)
option(VAXELIS_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})
option(VAXELIS_ENABLE_SANITIZERS "Enable ASan/UBSan in debug builds" OFF)
option(VAXELIS_BUILD_DOCS "Build API documentation with Doxygen" OFF)

# The Catch2 host tests have no runner in the browser; force them off for web.
# The examples are desktop-only too: each would need its own shell page and
# preload wiring, which only the runtime target carries.
if(EMSCRIPTEN)
    set(VAXELIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(VAXELIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(VAXELIS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR "GCC 14 or newer required for C++26")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "Clang 18 or newer required for C++26")
elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.40)
    message(FATAL_ERROR "MSVC 19.40 or newer required for C++26")
endif()

# MSVC spells C++26 "/std:c++latest"; CMake doesn't know the mapping yet.
if(MSVC)
    set(CMAKE_CXX26_STANDARD_COMPILE_OPTION "-std:c++latest")
    set(CMAKE_CXX26_EXTENSION_COMPILE_OPTION "-std:c++latest")
endif()

include(cmake/CompilerOptions.cmake)

add_subdirectory(third_party)
add_subdirectory(engine)

if(VAXELIS_BUILD_RUNTIME)
    add_subdirectory(runtime)
endif()

if(VAXELIS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(VAXELIS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(VAXELIS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(VAXELIS_BUILD_DOCS)
    add_subdirectory(docs)
endif()
