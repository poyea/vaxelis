#include "engine/core/Application.hpp"

#include <SDL3/SDL.h>
#include <utility>

#include "engine/core/Log.hpp"

namespace vaxelis {

Application::Application(AppConfig cfg) noexcept : cfg_(std::move(cfg)) {}

Application::~Application() { shutdown_subsystems(); }

bool Application::init() {
    init_logging();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        VX_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window_ = SDL_CreateWindow(cfg_.title.c_str(),
                               static_cast<int>(cfg_.width),
                               static_cast<int>(cfg_.height),
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
    refresh_drawable_size();
    inited_ = true;
    return true;
}

void Application::shutdown_subsystems() noexcept {
    if (imgui_inited_) { imgui_.shutdown(); imgui_inited_ = false; }
    device_.reset();
    if (gl_ctx_) { SDL_GL_DestroyContext(static_cast<SDL_GLContext>(gl_ctx_)); gl_ctx_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    SDL_Quit();
}

void Application::refresh_drawable_size() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    fb_width_  = static_cast<uint32_t>(w);
    fb_height_ = static_cast<uint32_t>(h);
}

int Application::run() {
    if (!inited_) {
        VX_ERROR("Application::run() called before init()");
        return 1;
    }

    on_init();
    clock_.tick();  // discard the construction-to-first-frame delta

    while (running_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            imgui_.process_event(ev);
            if (ev.type == SDL_EVENT_QUIT) running_ = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) running_ = false;
            if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
                ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                refresh_drawable_size();
            }
        }

        float dt = clock_.tick();
        on_update(dt);

        device_->begin_frame(clear_color_, fb_width_, fb_height_);
        on_render();

        imgui_.begin_frame();
        on_imgui();
        imgui_.end_frame();

        device_->end_frame();
        SDL_GL_SwapWindow(window_);
    }
    on_shutdown();
    return 0;
}

}  // namespace vaxelis
