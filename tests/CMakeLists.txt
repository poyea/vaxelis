include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(vaxelis_tests test_rhi.cpp test_input.cpp test_scene.cpp test_physics.cpp test_filewatcher.cpp test_tilemap.cpp test_world_transform.cpp)
target_link_libraries(vaxelis_tests PRIVATE vaxelis_engine Catch2::Catch2WithMain)
vaxelis_apply_target_options(vaxelis_tests)

include(Catch)
# DL_PATHS points discovery at SDL3.dll's location on Windows so the test exe
# can launch (it transitively links SDL3 as a shared library).
catch_discover_tests(vaxelis_tests
    DL_PATHS $<TARGET_FILE_DIR:SDL3::SDL3>)

# Also drop SDL3.dll next to the test exe so `ctest` runs work the same way.
if(WIN32)
    add_custom_command(TARGET vaxelis_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:vaxelis_tests>
    )
endif()
