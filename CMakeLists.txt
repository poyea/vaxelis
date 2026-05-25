cmake_minimum_required(VERSION 3.28)

project(
    cpp_package_boilerplate
    VERSION 0.1.0
    DESCRIPTION "Modern C++ package boilerplate"
    LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ language standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPP_PACKAGE_BOILERPLATE_BUILD_LIBRARY "Build the library target" ON)
option(CPP_PACKAGE_BOILERPLATE_BUILD_CLI "Build the CLI target" ON)
option(CPP_PACKAGE_BOILERPLATE_HEADER_ONLY "Build the library as a header-only target" OFF)
option(CPP_PACKAGE_BOILERPLATE_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})
option(CPP_PACKAGE_BOILERPLATE_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(CPP_PACKAGE_BOILERPLATE_BUILD_DOCS "Build API documentation" OFF)
option(CPP_PACKAGE_BOILERPLATE_ENABLE_IPO "Enable interprocedural optimization for release builds" ON)
option(CPP_PACKAGE_BOILERPLATE_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan in debug builds" ON)

if(NOT CPP_PACKAGE_BOILERPLATE_BUILD_LIBRARY AND NOT CPP_PACKAGE_BOILERPLATE_BUILD_CLI)
    message(FATAL_ERROR "Enable at least one target: library or CLI.")
endif()

if(CPP_PACKAGE_BOILERPLATE_HEADER_ONLY AND NOT CPP_PACKAGE_BOILERPLATE_BUILD_LIBRARY)
    message(FATAL_ERROR "CPP_PACKAGE_BOILERPLATE_HEADER_ONLY requires CPP_PACKAGE_BOILERPLATE_BUILD_LIBRARY=ON.")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR "GCC 13 or newer is required for the default C++23 configuration.")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "Clang 18 or newer is required for the default C++23 configuration.")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR "AppleClang 16 or newer is required for the default C++23 configuration.")
elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.38)
    message(FATAL_ERROR "MSVC 19.38 or newer is required for the default C++23 configuration.")
endif()

include(cmake/CompilerOptions.cmake)

add_subdirectory(src)

if(CPP_PACKAGE_BOILERPLATE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(CPP_PACKAGE_BOILERPLATE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(CPP_PACKAGE_BOILERPLATE_BUILD_DOCS)
    add_subdirectory(docs)
endif()
