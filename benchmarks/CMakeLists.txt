include(FetchContent)

FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz)

set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(${PROJECT_NAME}_benchmarks bench_mylib.cpp)
target_link_libraries(${PROJECT_NAME}_benchmarks PRIVATE ${PROJECT_NAME}::${PROJECT_NAME} benchmark::benchmark)
cpp_package_boilerplate_apply_target_options(${PROJECT_NAME}_benchmarks)
