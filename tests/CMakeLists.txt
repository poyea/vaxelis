include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(vaxelis_tests test_rhi.cpp test_input.cpp test_scene.cpp test_physics.cpp test_filewatcher.cpp)
target_link_libraries(vaxelis_tests PRIVATE vaxelis_engine Catch2::Catch2WithMain)
vaxelis_apply_target_options(vaxelis_tests)

include(Catch)
catch_discover_tests(vaxelis_tests)
