cmake_minimum_required(VERSION 3.28)

project(
    vaxelis
    VERSION 0.1.0
    DESCRIPTION "Vaxelis game engine"
    LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ language standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VAXELIS_BUILD_RUNTIME "Build the runtime executable" ON)
option(VAXELIS_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})
option(VAXELIS_ENABLE_SANITIZERS "Enable ASan/UBSan in debug builds" OFF)
option(VAXELIS_BUILD_DOCS "Build API documentation with Doxygen" OFF)

# The Catch2 host tests have no runner in the browser; force them off for web.
if(EMSCRIPTEN)
    set(VAXELIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR "GCC 13 or newer required for C++23")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "Clang 18 or newer required for C++23")
elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.38)
    message(FATAL_ERROR "MSVC 19.38 or newer required for C++23")
endif()

include(cmake/CompilerOptions.cmake)

add_subdirectory(third_party)
add_subdirectory(engine)

if(VAXELIS_BUILD_RUNTIME)
    add_subdirectory(runtime)
endif()

if(VAXELIS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(VAXELIS_BUILD_DOCS)
    add_subdirectory(docs)
endif()
