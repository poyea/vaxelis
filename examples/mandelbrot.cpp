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

/// Escape-time iteration count for c = (cr, ci), or `max_iter` when the orbit
/// stays bounded.
///
/// The two closed-form tests up front reject the main cardioid and the period-2
/// bulb, which is where interior pixels would otherwise burn the entire budget;
/// that is the difference between a view of the set costing microseconds and
/// costing milliseconds. The loop itself carries the squares across iterations,
/// so each step is three multiplies and no redundant squaring.
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
    uint32_t i = 0;
    for (; i < max_iter && zr2 + zi2 <= 4.0f; ++i) {
        zi = 2.0f * zr * zi + ci;
        zr = zr2 - zi2 + cr;
        zr2 = zr * zr;
        zi2 = zi * zi;
    }
    return i;
}

class Mandelbrot final : public vaxelis::Application {
  public:
    using Application::Application;

  protected:
    void on_init() override {
        assets_.init(device());
        pixels_.resize(static_cast<size_t>(kWidth) * kHeight);
        build_palette();

        const auto tex = device()
                             .create_texture({.width = kWidth,
                                              .height = kHeight,
                                              .format = vaxelis::rhi::TextureFormat::RGBA8,
                                              .initial_data = nullptr})
                             .value_or(vaxelis::rhi::TextureHandle{});
        texture_ = assets_.adopt_texture("fractal", tex);
        if (!batch_.init(device()))
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
        const float step = 0.9f * dt * half_height_;
        vaxelis::vec2 pan{0.0f, 0.0f};
        pan.x -= input().down("pan_left") ? step : 0.0f;
        pan.x += input().down("pan_right") ? step : 0.0f;
        pan.y -= input().down("pan_up") ? step : 0.0f;
        pan.y += input().down("pan_down") ? step : 0.0f;
        if (pan.x != 0.0f || pan.y != 0.0f) {
            center_ += pan;
            changed = true;
        }

        // Exponential zoom keeps every keypress worth the same relative step.
        float zoom = 0.0f;
        zoom -= input().down("zoom_in") ? 1.2f * dt : 0.0f;
        zoom += input().down("zoom_out") ? 1.2f * dt : 0.0f;
        if (zoom != 0.0f) {
            half_height_ *= std::exp(zoom);
            changed = true;
        }

        if (input().pressed("iter_up") && max_iter_ < kMaxIter) {
            max_iter_ *= 2;
            changed = true;
        }
        if (input().pressed("iter_down") && max_iter_ > kMinIter) {
            max_iter_ /= 2;
            changed = true;
        }
        if (input().pressed("reset")) {
            center_ = {-0.6f, 0.0f};
            half_height_ = 1.2f;
            max_iter_ = 256;
            changed = true;
        }

        // A still view costs nothing: the texture already holds the image.
        if (changed)
            rasterize();
    }

    void on_render() override {
        batch_.begin(device(), width(), height());
        const vaxelis::vec2 size{static_cast<float>(width()), static_cast<float>(height())};
        // v_bottom in .y and v_top in .w, so texture row 0 lands on the top
        // scanline; SpriteBatch's default uv rect would flip it vertically.
        batch_.draw(texture_, size * 0.5f, size, vaxelis::vec4{0.0f, 1.0f, 1.0f, 0.0f},
                    vaxelis::vec4{1.0f});
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("Mandelbrot");
        ImGui::Text("center  %.9f, %.9f", static_cast<double>(center_.x),
                    static_cast<double>(center_.y));
        ImGui::Text("scale   %.3e", static_cast<double>(half_height_));
        ImGui::Text("iters   %u", max_iter_);
        ImGui::Text("buffer  %ux%u", kWidth, kHeight);
        ImGui::Separator();
        ImGui::Text("WASD/arrows pan  -  Q/E zoom  -  [ ] iters  -  R reset");
        ImGui::End();
    }

    void on_shutdown() override {
        batch_.shutdown(device());
        assets_.shutdown();
    }

  private:
    /// Cyclic cosine gradient, evaluated once so the inner loop is a lookup.
    void build_palette() {
        for (uint32_t i = 0; i < palette_.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(palette_.size());
            const float r = 0.5f + 0.5f * std::cos(6.2831853f * (t + 0.00f));
            const float g = 0.5f + 0.5f * std::cos(6.2831853f * (t + 0.33f));
            const float b = 0.5f + 0.5f * std::cos(6.2831853f * (t + 0.67f));
            palette_[i] = pack_rgba(r, g, b);
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
        const float half_w = half_height_ * kAspect;
        const float dx = (2.0f * half_w) / static_cast<float>(kWidth);
        const float dy = (2.0f * half_height_) / static_cast<float>(kHeight);
        const float left = center_.x - half_w;
        const float top = center_.y - half_height_;
        // Escaped pixels spread over the whole palette whatever the budget, so
        // the banding rescales with the detail. Reciprocal once, not a division
        // per pixel.
        const float to_palette = 255.0f / static_cast<float>(max_iter_);

        for (uint32_t py = 0; py < kHeight; ++py) {
            const float ci = top + static_cast<float>(py) * dy;
            uint32_t* row = pixels_.data() + static_cast<size_t>(py) * kWidth;
            for (uint32_t px = 0; px < kWidth; ++px) {
                const float cr = left + static_cast<float>(px) * dx;
                const uint32_t it = escape_iterations(cr, ci, max_iter_);
                if (it >= max_iter_) {
                    row[px] = 0xFF000000u; // interior
                    continue;
                }
                row[px] = palette_[static_cast<uint32_t>(static_cast<float>(it) * to_palette)];
            }
        }

        device().update_texture(texture_, {.x = 0,
                                           .y = 0,
                                           .width = kWidth,
                                           .height = kHeight,
                                           .data = pixels_.data()});
    }

    vaxelis::SpriteBatch batch_;
    vaxelis::AssetCache assets_;
    vaxelis::rhi::TextureHandle texture_{};

    std::vector<uint32_t> pixels_;
    std::array<uint32_t, 256> palette_{};

    vaxelis::vec2 center_{-0.6f, 0.0f};
    float half_height_{1.2f};
    uint32_t max_iter_{256};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Mandelbrot app({.title = "Vaxelis - Mandelbrot", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
