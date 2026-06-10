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

/// Startup options for Application: window title, size, and RHI backend.
struct AppConfig {
    std::string title{"Vaxelis"};
    uint32_t width{1280};
    uint32_t height{720};
    rhi::Backend backend{rhi::Backend::OpenGL};
};

/// Owns window + GL context + device + ImGui. Subclass and override the hooks.
///
/// Two-phase init: the constructor is nothrow and only stores config. Call
/// `init()` to spin up SDL/GL/RHI/ImGui; it returns false on failure with the
/// reason logged. `run()` returns non-zero if init failed.
class Application {
  public:
    explicit Application(AppConfig cfg) noexcept;
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Spins up SDL/GL/RHI/ImGui. Returns false on failure with the reason logged.
    [[nodiscard]] bool init();
    /// Runs the main loop until quit.
    /// @return process exit code; non-zero if init() failed.
    int run();

  protected:
    /// Called once after init() succeeds, before the first frame.
    virtual void on_init() {}
    /// Fixed-timestep update; deterministic, called 0..N times/frame.
    virtual void on_fixed_update(float /*fixed_dt*/) {}
    /// Variable-timestep update; once per frame.
    virtual void on_update(float /*frame_dt*/) {}
    /// Issue draw calls; the frame has already been begun on the device.
    virtual void on_render() {}
    /// Build debug UI; called inside the ImGui frame.
    virtual void on_imgui() {}
    /// Called once before subsystems shut down.
    virtual void on_shutdown() {}

    rhi::IDevice& device() { return *device_; }
    Input& input() { return input_; }
    Audio& audio() { return audio_; }
    /// Framebuffer width in pixels (drawable size; may differ from window size on hidpi).
    uint32_t width() const { return fb_width_; }
    /// Framebuffer height in pixels.
    uint32_t height() const { return fb_height_; }
    /// Mutable; the next frame clears with this color.
    vec4& clear_color() { return clear_color_; }

    /// Fixed-update timestep in seconds (60 Hz).
    static constexpr float kFixedDt = 1.0f / 60.0f;

  private:
    void shutdown_subsystems() noexcept;
    void refresh_drawable_size();
    void step_frame(); // one iteration of the main loop (shared desktop/web)

    AppConfig cfg_;
    SDL_Window* window_{nullptr};
    void* gl_ctx_{nullptr};
    std::unique_ptr<rhi::IDevice> device_;
    ImGuiLayer imgui_;
    Input input_;
    Audio audio_;
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

} // namespace vaxelis
