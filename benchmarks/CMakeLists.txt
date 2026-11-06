# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

# Built by default so they cannot rot; only the bench workflow runs them.
add_executable(vaxelis_ecs_bench ecs_bench.cpp)
# entt is linked here and nowhere else: the engine stores scenes in its own
# archetype world now, and entt survives only as the baseline this benchmark
# measures against. Linking it to the engine again would let a dependency creep
# back in without anyone noticing.
target_link_libraries(vaxelis_ecs_bench PRIVATE vaxelis_engine entt)
vaxelis_apply_target_options(vaxelis_ecs_bench)

# The engine links SDL3 even though nothing here opens a window.
if(WIN32)
    add_custom_command(TARGET vaxelis_ecs_bench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:vaxelis_ecs_bench>
    )
endif()
