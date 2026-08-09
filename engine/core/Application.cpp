// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/core/Application.hpp"

#include <SDL3/SDL.h>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "engine/core/Log.hpp"

namespace vaxelis {

Application::Application(AppConfig cfg) noexcept : m_cfg(std::move(cfg)) {
}

Application::~Application() {
    shutdown_subsystems();
}

bool Application::init() {
    init_logging();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        VX_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

#ifdef __EMSCRIPTEN__
    // WebGL2 is exposed as an OpenGL ES 3.0 context.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    m_window = SDL_CreateWindow(
        m_cfg.title.c_str(), static_cast<int>(m_cfg.width), static_cast<int>(m_cfg.height),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) {
        VX_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    m_gl_ctx = SDL_GL_CreateContext(m_window);
    if (!m_gl_ctx) {
        VX_ERROR("SDL_GL_CreateContext failed: {}", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(m_window, static_cast<SDL_GLContext>(m_gl_ctx));
    SDL_GL_SetSwapInterval(1);

    auto dev = rhi::create_device(m_cfg.backend);
    if (!dev) {
        VX_ERROR("RHI device creation failed: {}", rhi::to_string(dev.error()));
        return false;
    }
    m_device = std::move(*dev);

    m_imgui.init(m_window, m_gl_ctx);
    m_imgui_inited = true;

    if (!m_audio.init()) {
        VX_WARN("Audio init failed; continuing without sound");
    } else {
        m_audio_inited = true;
    }

    refresh_drawable_size();
    m_inited = true;
    return true;
}

void Application::shutdown_subsystems() noexcept {
    if (m_audio_inited) {
        m_audio.shutdown();
        m_audio_inited = false;
    }
    if (m_imgui_inited) {
        m_imgui.shutdown();
        m_imgui_inited = false;
    }
    m_device.reset();
    if (m_gl_ctx) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_gl_ctx));
        m_gl_ctx = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

void Application::refresh_drawable_size() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    m_fb_width = static_cast<uint32_t>(w);
    m_fb_height = static_cast<uint32_t>(h);
}

void Application::step_frame() {
    m_input.begin_frame();

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        m_imgui.process_event(ev);
        m_input.on_event(ev);
        if (ev.type == SDL_EVENT_QUIT)
            m_running = false;
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
            m_running = false;
        if (ev.type == SDL_EVENT_WINDOW_RESIZED || ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            refresh_drawable_size();
        }
    }

    float frame_dt = m_clock.tick();
    // Clamp absurd deltas (paused under debugger) to avoid huge accumulator.
    if (frame_dt > 0.25f)
        frame_dt = 0.25f;

    // Fixed-timestep with accumulator. Caps catch-up steps so a long pause
    // (debugger break, alt-tab) doesn't trigger the spiral of death.
    constexpr int kMaxStepsPerFrame = 8;
    m_accumulator += frame_dt;
    int steps = 0;
    while (m_accumulator >= kFixedDt && steps < kMaxStepsPerFrame) {
        on_fixed_update(kFixedDt);
        m_accumulator -= kFixedDt;
        ++steps;
    }
    if (steps == kMaxStepsPerFrame)
        m_accumulator = 0.0; // drop residual on overrun
    on_update(frame_dt);

    m_device->begin_frame(m_clear_color, m_fb_width, m_fb_height);
    on_render();

    m_imgui.begin_frame();
    on_imgui();
    m_imgui.end_frame();

    m_device->end_frame();
    SDL_GL_SwapWindow(m_window);
}

int Application::run() {
    if (!m_inited) {
        VX_ERROR("Application::run() called before init()");
        return 1;
    }

    on_init();
    m_clock.tick(); // discard the construction-to-first-frame delta

#ifdef __EMSCRIPTEN__
    // The browser drives the loop via requestAnimationFrame; a blocking while
    // would starve it. simulate_infinite_loop=true unwinds the C++ stack, so
    // code after this never runs and on_shutdown() is left to page teardown.
    emscripten_set_main_loop_arg([](void* self) { static_cast<Application*>(self)->step_frame(); },
                                 this, 0, /*simulate_infinite_loop=*/true);
    return 0; // not reached
#else
    while (m_running) {
        step_frame();
    }
    on_shutdown();
    return 0;
#endif
}

} // namespace vaxelis
