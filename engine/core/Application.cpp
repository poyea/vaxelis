#include "engine/core/Application.hpp"

#include <SDL3/SDL.h>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "engine/core/Log.hpp"

namespace vaxelis {

Application::Application(AppConfig cfg) noexcept : cfg_(std::move(cfg)) {
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

    window_ = SDL_CreateWindow(
        cfg_.title.c_str(), static_cast<int>(cfg_.width), static_cast<int>(cfg_.height),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        VX_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    gl_ctx_ = SDL_GL_CreateContext(window_);
    if (!gl_ctx_) {
        VX_ERROR("SDL_GL_CreateContext failed: {}", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, static_cast<SDL_GLContext>(gl_ctx_));
    SDL_GL_SetSwapInterval(1);

    auto dev = rhi::create_device(cfg_.backend);
    if (!dev) {
        VX_ERROR("RHI device creation failed: {}", rhi::to_string(dev.error()));
        return false;
    }
    device_ = std::move(*dev);

    imgui_.init(window_, gl_ctx_);
    imgui_inited_ = true;

    if (!audio_.init()) {
        VX_WARN("Audio init failed; continuing without sound");
    } else {
        audio_inited_ = true;
    }

    refresh_drawable_size();
    inited_ = true;
    return true;
}

void Application::shutdown_subsystems() noexcept {
    if (audio_inited_) {
        audio_.shutdown();
        audio_inited_ = false;
    }
    if (imgui_inited_) {
        imgui_.shutdown();
        imgui_inited_ = false;
    }
    device_.reset();
    if (gl_ctx_) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(gl_ctx_));
        gl_ctx_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Application::refresh_drawable_size() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    fb_width_ = static_cast<uint32_t>(w);
    fb_height_ = static_cast<uint32_t>(h);
}

void Application::step_frame() {
    input_.begin_frame();

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        imgui_.process_event(ev);
        input_.on_event(ev);
        if (ev.type == SDL_EVENT_QUIT)
            running_ = false;
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
            running_ = false;
        if (ev.type == SDL_EVENT_WINDOW_RESIZED || ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            refresh_drawable_size();
        }
    }

    float frame_dt = clock_.tick();
    // Clamp absurd deltas (paused under debugger) to avoid huge accumulator.
    if (frame_dt > 0.25f)
        frame_dt = 0.25f;

    // Fixed-timestep with accumulator. Caps catch-up steps so a long pause
    // (debugger break, alt-tab) doesn't trigger the spiral of death.
    constexpr int kMaxStepsPerFrame = 8;
    accumulator_ += frame_dt;
    int steps = 0;
    while (accumulator_ >= kFixedDt && steps < kMaxStepsPerFrame) {
        on_fixed_update(kFixedDt);
        accumulator_ -= kFixedDt;
        ++steps;
    }
    if (steps == kMaxStepsPerFrame)
        accumulator_ = 0.0; // drop residual on overrun
    on_update(frame_dt);

    device_->begin_frame(clear_color_, fb_width_, fb_height_);
    on_render();

    imgui_.begin_frame();
    on_imgui();
    imgui_.end_frame();

    device_->end_frame();
    SDL_GL_SwapWindow(window_);
}

int Application::run() {
    if (!inited_) {
        VX_ERROR("Application::run() called before init()");
        return 1;
    }

    on_init();
    clock_.tick(); // discard the construction-to-first-frame delta

#ifdef __EMSCRIPTEN__
    // The browser drives the loop via requestAnimationFrame; a blocking while
    // would starve it. simulate_infinite_loop=true unwinds the C++ stack, so
    // code after this never runs and on_shutdown() is left to page teardown.
    emscripten_set_main_loop_arg([](void* self) { static_cast<Application*>(self)->step_frame(); },
                                 this, 0, /*simulate_infinite_loop=*/true);
    return 0; // not reached
#else
    while (running_) {
        step_frame();
    }
    on_shutdown();
    return 0;
#endif
}

} // namespace vaxelis
