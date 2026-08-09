# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

add_executable(vaxelis_runtime main.cpp)
target_link_libraries(vaxelis_runtime PRIVATE vaxelis_engine)
vaxelis_apply_target_options(vaxelis_runtime)

# --- WebAssembly (Emscripten) target ---------------------------------------
# Desktop builds are untouched: everything here is gated on EMSCRIPTEN. Build
# with:  emcmake cmake -B build-web && cmake --build build-web
# then serve build-web/runtime/ over HTTP and open vaxelis_runtime.html.
if(EMSCRIPTEN)
    set_target_properties(vaxelis_runtime PROPERTIES
        SUFFIX ".html"
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
    target_link_options(vaxelis_runtime PRIVATE
        "-sFULL_ES3=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
endif()

# Copy SDL3 shared lib next to the runtime exe on Windows.
if(WIN32)
    add_custom_command(TARGET vaxelis_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:vaxelis_runtime>
    )
endif()

# Mirror runtime/assets/ next to the exe so relative paths in scripts work
# regardless of where the binary is launched from.
add_custom_command(TARGET vaxelis_runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:vaxelis_runtime>/assets
)
