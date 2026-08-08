/// @file
/// Mandelbrot explorer: an escape-time fractal rasterized on the CPU into one
/// RGBA8 texture, stretched over the window as a single quad.
///
/// Pan with arrows/WASD, zoom with Q/E, change the iteration budget with [ and
/// ], reset with R.

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include "engine/assets/AssetCache.hpp"
#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

namespace {

// The fractal is rasterized at a fixed internal resolution and stretched to the
// window, so the frame cost never depends on how large the window is. 512x288
// is 147k pixels: a full re-render fits comfortably inside one frame.
constexpr uint32_t kWidth = 512;
constexpr uint32_t kHeight = 288;
constexpr float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
constexpr uint32_t kMinIter = 32;
constexpr uint32_t kMaxIter = 4096;

/// Cycle-detection epsilon. Floats carry about seven digits, so this is as
/// tight as the test can get while still catching a settled orbit.
constexpr float kCycleEps = 1e-6f;
/// How often the periodicity check re-anchors its reference point.
constexpr uint32_t kRefInterval = 20;

/// Escape-time iteration count for c = (cr, ci), or `max_iter` when the orbit
/// stays bounded.
///
/// Three things keep interior pixels from burning the whole budget: closed-form
/// tests for the main cardioid and the period-2 bulb, and a periodicity check
/// for the interior those two miss. An interior orbit settles into a cycle, so
/// once it comes back to a remembered reference point there is nothing left to
/// learn from iterating. The loop carries z^2 across iterations, so each step is
/// three multiplies and no redundant squaring.
uint32_t escape_iterations(float cr, float ci, uint32_t max_iter) {
    const float ci2 = ci * ci;
    const float dx = cr - 0.25f;
    const float q = dx * dx + ci2;
    if (q * (q + dx) <= 0.25f * ci2)
        return max_iter;
    if ((cr + 1.0f) * (cr + 1.0f) + ci2 <= 0.0625f)
        return max_iter;

    float zr = 0.0f;
    float zi = 0.0f;
    float zr2 = 0.0f;
    float zi2 = 0.0f;
    float ref_r = 0.0f;
    float ref_i = 0.0f;
    uint32_t since_ref = 0;

    uint32_t i = 0;
    for (; i < max_iter && zr2 + zi2 <= 4.0f; ++i) {
        zi = 2.0f * zr * zi + ci;
        zr = zr2 - zi2 + cr;
        zr2 = zr * zr;
        zi2 = zi * zi;

        // Integer and compare work here rides alongside the serial float
        // dependency chain above, so it costs far less than it reads.
        if (std::fabs(zr - ref_r) < kCycleEps && std::fabs(zi - ref_i) < kCycleEps)
            return max_iter; // orbit closed on itself: interior
        if (++since_ref >= kRefInterval) {
            since_ref = 0;
            ref_r = zr;
            ref_i = zi;
        }
    }
    return i;
}

class Mandelbrot final : public vaxelis::Application {
  public:
    using Application::Application;

  protected:
    void on_init() override {
        m_assets.init(device());
        m_pixels.resize(static_cast<size_t>(kWidth) * kHeight);
        build_palette();

        const auto tex = device()
                             .create_texture({.width = kWidth,
                                              .height = kHeight,
                                              .format = vaxelis::rhi::TextureFormat::RGBA8,
                                              .initial_data = nullptr})
                             .value_or(vaxelis::rhi::TextureHandle{});
        m_texture = m_assets.adopt_texture("fractal", tex);
        if (!m_batch.init(device()))
            VX_ERROR("SpriteBatch init failed");

        input().bind_action("pan_left", {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});
        input().bind_action("pan_right", {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT});
        input().bind_action("pan_up", {SDL_SCANCODE_W, SDL_SCANCODE_UP});
        input().bind_action("pan_down", {SDL_SCANCODE_S, SDL_SCANCODE_DOWN});
        input().bind_action("zoom_in", SDL_SCANCODE_E);
        input().bind_action("zoom_out", SDL_SCANCODE_Q);
        input().bind_action("iter_down", SDL_SCANCODE_LEFTBRACKET);
        input().bind_action("iter_up", SDL_SCANCODE_RIGHTBRACKET);
        input().bind_action("reset", SDL_SCANCODE_R);

        rasterize();
    }

    void on_update(float dt) override {
        bool changed = false;

        // Pan in view-space units so the apparent speed is zoom-independent.
        const float step = 0.9f * dt * m_half_height;
        vaxelis::vec2 pan{0.0f, 0.0f};
        pan.x -= input().down("pan_left") ? step : 0.0f;
        pan.x += input().down("pan_right") ? step : 0.0f;
        pan.y -= input().down("pan_up") ? step : 0.0f;
        pan.y += input().down("pan_down") ? step : 0.0f;
        if (pan.x != 0.0f || pan.y != 0.0f) {
            m_center += pan;
            changed = true;
        }

        // Exponential zoom keeps every keypress worth the same relative step.
        float zoom = 0.0f;
        zoom -= input().down("zoom_in") ? 1.2f * dt : 0.0f;
        zoom += input().down("zoom_out") ? 1.2f * dt : 0.0f;
        if (zoom != 0.0f) {
            m_half_height *= std::exp(zoom);
            changed = true;
        }

        if (input().pressed("iter_up") && m_max_iter < kMaxIter) {
            m_max_iter *= 2;
            changed = true;
        }
        if (input().pressed("iter_down") && m_max_iter > kMinIter) {
            m_max_iter /= 2;
            changed = true;
        }
        if (input().pressed("reset")) {
            m_center = {-0.6f, 0.0f};
            m_half_height = 1.2f;
            m_max_iter = 256;
            changed = true;
        }

        // A still view costs nothing: the texture already holds the image.
        if (changed)
            rasterize();
    }

    void on_render() override {
        m_batch.begin(device(), width(), height());
        const vaxelis::vec2 size{static_cast<float>(width()), static_cast<float>(height())};
        m_batch.draw(m_texture, size * 0.5f, size);
        m_batch.end();
    }

    void on_imgui() override {
        ImGui::Begin("Mandelbrot");
        ImGui::Text("center  %.9f, %.9f", static_cast<double>(m_center.x),
                    static_cast<double>(m_center.y));
        ImGui::Text("scale   %.3e", static_cast<double>(m_half_height));
        ImGui::Text("iters   %u", m_max_iter);
        ImGui::Text("buffer  %ux%u", kWidth, kHeight);
        ImGui::Separator();
        ImGui::Text("WASD/arrows pan  -  Q/E zoom  -  [ ] iters  -  R reset");
        ImGui::End();
    }

    void on_shutdown() override {
        m_batch.shutdown(device());
        m_assets.shutdown();
    }

  private:
    /// Cyclic cosine gradient, evaluated once so the inner loop is a lookup.
    void build_palette() {
        for (uint32_t i = 0; i < m_palette.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(m_palette.size());
            const float r = 0.5f + 0.5f * std::cos(6.2831853f * (t + 0.00f));
            const float g = 0.5f + 0.5f * std::cos(6.2831853f * (t + 0.33f));
            const float b = 0.5f + 0.5f * std::cos(6.2831853f * (t + 0.67f));
            m_palette[i] = pack_rgba(r, g, b);
        }
    }

    /// Packs unit floats into the RGBA8 byte order the RHI uploads (little
    /// endian: the low byte is red).
    static uint32_t pack_rgba(float r, float g, float b) {
        const auto to_byte = [](float v) {
            return static_cast<uint32_t>(v * 255.0f + 0.5f) & 0xFFu;
        };
        return to_byte(r) | (to_byte(g) << 8) | (to_byte(b) << 16) | 0xFF000000u;
    }

    /// Rasterizes the whole buffer and uploads it in place. Called only when the
    /// view changed, never on an idle frame.
    void rasterize() {
        const float half_w = m_half_height * kAspect;
        const float dx = (2.0f * half_w) / static_cast<float>(kWidth);
        const float dy = (2.0f * m_half_height) / static_cast<float>(kHeight);
        const float left = m_center.x - half_w;
        const float top = m_center.y - m_half_height;
        // Escaped pixels spread over the whole palette whatever the budget, so
        // the banding rescales with the detail. Reciprocal once, not a division
        // per pixel.
        const float to_palette = 255.0f / static_cast<float>(m_max_iter);

        for (uint32_t py = 0; py < kHeight; ++py) {
            const float ci = top + static_cast<float>(py) * dy;
            uint32_t* row = m_pixels.data() + static_cast<size_t>(py) * kWidth;
            for (uint32_t px = 0; px < kWidth; ++px) {
                const float cr = left + static_cast<float>(px) * dx;
                const uint32_t it = escape_iterations(cr, ci, m_max_iter);
                if (it >= m_max_iter) {
                    row[px] = 0xFF000000u; // interior
                    continue;
                }
                row[px] = m_palette[static_cast<uint32_t>(static_cast<float>(it) * to_palette)];
            }
        }

        device().update_texture(m_texture, {.x = 0,
                                            .y = 0,
                                            .width = kWidth,
                                            .height = kHeight,
                                            .data = m_pixels.data()});
    }

    vaxelis::SpriteBatch m_batch;
    vaxelis::AssetCache m_assets;
    vaxelis::rhi::TextureHandle m_texture{};

    std::vector<uint32_t> m_pixels;
    std::array<uint32_t, 256> m_palette{};

    vaxelis::vec2 m_center{-0.6f, 0.0f};
    float m_half_height{1.2f};
    uint32_t m_max_iter{256};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Mandelbrot app({.title = "Vaxelis - Mandelbrot", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
