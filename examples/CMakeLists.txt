# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

# One executable per example; each is a single self-contained file.
function(vaxelis_add_example name)
    set(target vaxelis_example_${name})
    add_executable(${target} ${name}.cpp)
    target_link_libraries(${target} PRIVATE vaxelis_engine)
    vaxelis_apply_target_options(${target})

    # Copy SDL3 next to the exe so it launches straight from the build tree.
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()
endfunction()

vaxelis_add_example(mandelbrot)
vaxelis_add_example(harmonograph)
vaxelis_add_example(orbits)
vaxelis_add_example(attractor)
vaxelis_add_example(stress)
