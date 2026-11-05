// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

#include <memory>
#include <string>

#include "engine/audio/Audio.hpp"
#include "engine/core/Time.hpp"
#include "engine/debug/ImGuiLayer.hpp"
#include "engine/input/Input.hpp"
#include "engine/math/Math.hpp"
#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/rhi/Rhi.hpp"

struct SDL_Window;

namespace vaxelis {

/// Startup options for Application: window title, size, and RHI backend.
struct AppConfig {
    std::string title{"Vaxelis"};
    uint32_t width{1280};
    uint32_t height{720};
    rhi::Backend backend{rhi::Backend::OpenGL};

    /// Optional fixed-size offscreen pass. When both are non-zero, on_render()
    /// draws into a render target of exactly this size and Application upscales
    /// it to the window, by the largest integer factor that fits so the pixel
    /// grid stays square, letterboxing what is left over. width() and height()
    /// then report the target's size, so a projection built from them is the
    /// right one. ImGui is unaffected: it draws to the window after the upscale,
    /// at full resolution.
    uint32_t render_width{0};
    uint32_t render_height{0};
};

/// Owns window + GL context + device + ImGui. Subclass and override the hooks.
///
/// Two-phase init: the constructor is nothrow and only stores config. Call
/// `init()` to spin up SDL/GL/RHI/ImGui; it returns false on failure with the
/// reason logged. `run()` returns non-zero if init failed.
class Application {
  public:
    /// Stores the config only; nothing is created until init(). Never throws,
    /// so a failure to start is reported by init() returning false.
    explicit Application(AppConfig cfg) noexcept;
    /// Shuts down any subsystem still running.
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

    rhi::IDevice& device() { return *m_device; }
    Input& input() { return m_input; }
    Audio& audio() { return m_audio; }
    /// Width in pixels of whatever on_render() draws into: the offscreen target
    /// when AppConfig asked for one, otherwise the window's drawable size (which
    /// may differ from its logical size on hidpi).
    uint32_t width() const { return m_offscreen ? m_cfg.render_width : m_fb_width; }
    /// Height in pixels of whatever on_render() draws into. See width().
    uint32_t height() const { return m_offscreen ? m_cfg.render_height : m_fb_height; }
    /// The window's drawable width, regardless of any offscreen pass. Needed
    /// only by code that has to reason about real pixels, such as hit-testing a
    /// mouse position against the upscale.
    uint32_t window_width() const { return m_fb_width; }
    /// The window's drawable height, regardless of any offscreen pass.
    uint32_t window_height() const { return m_fb_height; }
    /// Mutable; the next frame clears with this color.
    vec4& clear_color() { return m_clear_color; }

    /// Fixed-update timestep in seconds (60 Hz).
    static constexpr float kFixedDt = 1.0f / 60.0f;

  private:
    void shutdown_subsystems() noexcept;
    void refresh_drawable_size();
    void step_frame(); // one iteration of the main loop (shared desktop/web)

    /// Creates the offscreen target and the batch that upscales it. Returns
    /// false if either fails, in which case rendering stays direct-to-window.
    bool init_offscreen();
    /// Draws the offscreen colour attachment over the window.
    void blit_offscreen();

    AppConfig m_cfg;
    SDL_Window* m_window{nullptr};
    void* m_gl_ctx{nullptr};
    std::unique_ptr<rhi::IDevice> m_device;
    /// Null unless AppConfig asked for an offscreen pass.
    rhi::RenderTargetHandle m_target{};
    /// Draws the one quad that upscales m_target; separate from any batch the
    /// application owns, so an offscreen pass cannot disturb its state.
    SpriteBatch m_blit;
    bool m_offscreen{false};
    ImGuiLayer m_imgui;
    Input m_input;
    Audio m_audio;
    bool m_imgui_inited{false};
    bool m_audio_inited{false};
    Clock m_clock;
    double m_accumulator{0.0};
    vec4 m_clear_color{0.1f, 0.12f, 0.15f, 1.0f};
    uint32_t m_fb_width{0};
    uint32_t m_fb_height{0};
    bool m_running{true};
    bool m_inited{false};
};

} // namespace vaxelis
