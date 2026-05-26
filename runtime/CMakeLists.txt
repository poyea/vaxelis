add_executable(vaxelis_runtime main.cpp)
target_link_libraries(vaxelis_runtime PRIVATE vaxelis_engine)
vaxelis_apply_target_options(vaxelis_runtime)

# Copy SDL3 shared lib next to the runtime exe on Windows.
if(WIN32)
    add_custom_command(TARGET vaxelis_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:vaxelis_runtime>
    )
endif()
