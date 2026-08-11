# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(vaxelis_tests test_rhi.cpp test_input.cpp test_scene.cpp test_physics.cpp test_filewatcher.cpp test_tilemap.cpp test_world_transform.cpp test_script.cpp test_uuid.cpp test_assets.cpp test_sprite_batch.cpp test_archetype.cpp test_world.cpp test_camera.cpp)
target_link_libraries(vaxelis_tests PRIVATE vaxelis_engine GTest::gtest_main)
vaxelis_apply_target_options(vaxelis_tests)

include(GoogleTest)
# PRE_TEST defers discovery until after the POST_BUILD copy below.
gtest_discover_tests(vaxelis_tests
    DL_PATHS $<TARGET_FILE_DIR:SDL3::SDL3>
    DISCOVERY_MODE PRE_TEST)

# Also drop SDL3.dll next to the test exe so `ctest` runs work the same way.
if(WIN32)
    add_custom_command(TARGET vaxelis_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:vaxelis_tests>
    )
endif()
