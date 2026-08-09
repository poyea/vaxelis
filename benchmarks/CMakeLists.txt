# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

# Headless timing targets. Built by default so they cannot rot, but only ever
# executed by the bench workflow -- a shared CI runner's numbers are far too
# noisy to gate a merge on.
add_executable(vaxelis_ecs_bench ecs_bench.cpp)
target_link_libraries(vaxelis_ecs_bench PRIVATE vaxelis_engine)
vaxelis_apply_target_options(vaxelis_ecs_bench)

# The engine links SDL3 even though nothing here opens a window.
if(WIN32)
    add_custom_command(TARGET vaxelis_ecs_bench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:vaxelis_ecs_bench>
    )
endif()
