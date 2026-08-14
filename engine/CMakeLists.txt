# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Law

add_library(vaxelis_engine STATIC
    assets/AssetCache.cpp
    assets/FileWatcher.cpp
    audio/Audio.cpp
    core/Log.cpp
    core/Time.cpp
    core/Uuid.cpp
    core/Application.cpp
    ecs/ComponentType.cpp
    ecs/Archetype.cpp
    ecs/World.cpp
    input/Input.cpp
    physics/Physics2D.cpp
    rhi/Rhi.cpp
    rhi/gl/GLLoader.cpp
    rhi/gl/GLDevice.cpp
    rhi/vk/VkDevice.cpp
    renderer/SpriteRenderer.cpp
    scene/Scene.cpp
    scene/SceneSerializer.cpp
    script/Script.cpp
    text/Font.cpp
    tilemap/TiledMap.cpp
    debug/ImGuiLayer.cpp
    debug/SceneInspector.cpp
)

target_include_directories(vaxelis_engine PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vaxelis_engine
    PUBLIC
        SDL3::SDL3
        glm::glm
        spdlog::spdlog
        imgui   # transitively used by debug overlay code in clients
        entt    # public — components live in headers clients include
        box2d   # public — physics components expose b2*Id handles
    PRIVATE
        stb
        miniaudio
        nlohmann_json
        sol2
)

if(EMSCRIPTEN)
    # emcc provides GLES3/WebGL2 and the SDL3 port itself; no system GL lib.
elseif(WIN32)
    target_link_libraries(vaxelis_engine PUBLIC opengl32)
elseif(APPLE)
    target_link_libraries(vaxelis_engine PUBLIC "-framework OpenGL" "-framework CoreAudio" "-framework AudioToolbox")
else()
    target_link_libraries(vaxelis_engine PUBLIC GL pthread dl m)
endif()

vaxelis_apply_target_options(vaxelis_engine)
