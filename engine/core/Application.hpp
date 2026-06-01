#pragma once

#include <memory>
#include <string>

#include "engine/audio/Audio.hpp"
#include "engine/core/Time.hpp"
#include "engine/debug/ImGuiLayer.hpp"
#include "engine/input/Input.hpp"
#include "engine/math/Math.hpp"
#include "engine/rhi/Rhi.hpp"

struct SDL_Window;

namespace vaxelis {

struct AppConfig {
    std::string title{"Vaxelis"};
    uint32_t width{1280};
    uint32_t height{720};
    rhi::Backend backend{rhi::Backend::OpenGL};
};

// Owns window + GL context + device + ImGui. Subclass and override the hooks.
//
// Two-phase init: the constructor is nothrow and only stores config. Call
// `init()` to spin up SDL/GL/RHI/ImGui — it returns false on failure with the
// reason logged. `run()` returns non-zero if init failed.
class Application {
public:
    explicit Application(AppConfig cfg) noexcept;
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] bool init();
    int run();  // returns process exit code

protected:
    virtual void on_init()                                  {}
    virtual void on_fixed_update(float /*fixed_dt*/)        {}  // deterministic, called 0..N times/frame
    virtual void on_update(float /*frame_dt*/)              {}  // variable, once per frame
    virtual void on_render()                                {}
    virtual void on_imgui()                                 {}
    virtual void on_shutdown()                              {}

    rhi::IDevice&  device()         { return *device_; }
    Input&         input()          { return input_; }
    Audio&         audio()          { return audio_; }
    uint32_t       width()    const { return fb_width_; }
    uint32_t       height()   const { return fb_height_; }
    vec4&          clear_color()    { return clear_color_; }

    static constexpr float kFixedDt = 1.0f / 60.0f;

private:
    void shutdown_subsystems() noexcept;
    void refresh_drawable_size();
    void step_frame();  // one iteration of the main loop (shared desktop/web)

    AppConfig cfg_;
    SDL_Window* window_{nullptr};
    void* gl_ctx_{nullptr};
    std::unique_ptr<rhi::IDevice> device_;
    ImGuiLayer imgui_;
    Input  input_;
    Audio  audio_;
    bool imgui_inited_{false};
    bool audio_inited_{false};
    Clock clock_;
    double accumulator_{0.0};
    vec4 clear_color_{0.1f, 0.12f, 0.15f, 1.0f};
    uint32_t fb_width_{0};
    uint32_t fb_height_{0};
    bool running_{true};
    bool inited_{false};
};

}  // namespace vaxelis
