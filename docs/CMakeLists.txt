# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

find_package(Doxygen REQUIRED)

# Graphviz is optional. When it is present Doxygen draws the class hierarchies;
# without it the build still succeeds, just without diagrams.
if(TARGET Doxygen::dot)
    set(VAXELIS_DOXYGEN_HAVE_DOT YES)
else()
    set(VAXELIS_DOXYGEN_HAVE_DOT NO)
endif()

set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

add_custom_target(
    docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)
