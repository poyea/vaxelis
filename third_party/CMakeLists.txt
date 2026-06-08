# Third-party dependencies. All warnings suppressed, EXCLUDE_FROM_ALL.

# --- glm (header-only target glm::glm) ---
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(glm EXCLUDE_FROM_ALL)

# --- spdlog ---
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

# --- SDL3 ---
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
# Emscripten has no shared libraries, so SDL forces SDL_SHARED off there; build
# it static to avoid "SDL_SHARED and SDL_STATIC cannot both be disabled". Desktop
# keeps the shared build (Windows/tests copy SDL3.dll next to the exe).
if(EMSCRIPTEN)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(SDL EXCLUDE_FROM_ALL)

# --- stb (header-only INTERFACE) ---
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# --- miniaudio (header-only; implementation compiled in engine/audio/Audio.cpp) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)
# The Emscripten WebAudio backend embeds EM_ASM blocks using $0/$1 placeholders,
# which trip -Wdollar-in-identifier-extension. Suppress it for consumers (the
# engine builds with -Werror); propagates after the engine's own warning flags.
if(EMSCRIPTEN)
    target_compile_options(miniaudio INTERFACE -Wno-dollar-in-identifier-extension)
endif()

# --- entt (header-only, single-include) ---
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include)

# --- nlohmann/json (header-only, single-include) ---
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/json/single_include)

# --- Box2D v3 ---
set(BOX2D_SAMPLES      OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS   OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS   OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS         OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATE     OFF CACHE BOOL "" FORCE)
add_subdirectory(box2d EXCLUDE_FROM_ALL)

# --- Lua 5.4 (upstream has no CMakeLists; build via single-file onelua.c) ---
add_library(lua STATIC lua/onelua.c)
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua)
target_compile_definitions(lua PRIVATE MAKE_LIB)
if(WIN32)
    target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
    target_compile_options(lua PRIVATE /W0)
else()
    target_compile_options(lua PRIVATE -w)
endif()

# --- sol2 (header-only C++ binding for Lua) ---
add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sol2/include)
target_link_libraries(sol2 INTERFACE lua)

# --- Dear ImGui (no shipped CMakeLists). Build core + SDL3/OpenGL3 backends. ---
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# Silence third-party warnings.
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
else()
    target_compile_options(imgui PRIVATE -w)
endif()

# --- GL loader: use a vendored single-file glad (generated for GL 4.5 core). ---
# We embed a minimal hand-rolled loader instead to avoid another submodule.
# See engine/rhi/gl/GLLoader.{hpp,cpp}.
