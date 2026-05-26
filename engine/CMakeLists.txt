add_library(vaxelis_engine STATIC
    audio/Audio.cpp
    core/Log.cpp
    core/Time.cpp
    core/Application.cpp
    input/Input.cpp
    rhi/Rhi.cpp
    rhi/gl/GLLoader.cpp
    rhi/gl/GLDevice.cpp
    rhi/vk/VkDevice.cpp
    renderer/SpriteRenderer.cpp
    debug/ImGuiLayer.cpp
)

target_include_directories(vaxelis_engine PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vaxelis_engine
    PUBLIC
        SDL3::SDL3
        glm::glm
        spdlog::spdlog
        imgui   # transitively used by debug overlay code in clients
    PRIVATE
        stb
        miniaudio
)

if(WIN32)
    target_link_libraries(vaxelis_engine PUBLIC opengl32)
elseif(APPLE)
    target_link_libraries(vaxelis_engine PUBLIC "-framework OpenGL" "-framework CoreAudio" "-framework AudioToolbox")
else()
    target_link_libraries(vaxelis_engine PUBLIC GL pthread dl m)
endif()

vaxelis_apply_target_options(vaxelis_engine)
