# One executable per example. Each example is a single self-contained file that
# depends on nothing but the engine -- no assets, no shared helper headers -- so
# it reads top to bottom as a complete program.
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
