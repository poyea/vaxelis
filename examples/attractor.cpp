/// @file
/// De Jong attractor: the orbit of
///
///   x' = sin(a y) - cos(b x)
///   y' = sin(c x) - cos(d y)
///
/// accumulated as a density field. A single trajectory is iterated a little
/// further every frame and the picture refines as hits pile up, so what you see
/// is how often the orbit visits each pixel, not where it happens to be now.
///
/// Sliders move a..d, R picks a random set, Space pauses the accumulation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include "engine/assets/AssetCache.hpp"
#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

namespace {

constexpr uint32_t kSize = 512; ///< density field is kSize x kSize
constexpr uint32_t kPixels = kSize * kSize;
constexpr uint32_t kPointsPerFrame = 20000;
constexpr uint32_t kLutSize = 2048;
// The map's range is [-2, 2] on both axes, so this maps it onto the field.
constexpr float kToPixel = static_cast<float>(kSize) / 4.0f;

/// Packs unit floats into the RGBA8 byte order the RHI uploads (little endian:
/// the low byte is red).
uint32_t pack_rgba(float r, float g, float b) {
    const auto to_byte = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return to_byte(r) | (to_byte(g) << 8) | (to_byte(b) << 16) | 0xFF000000u;
}

/// Density ramp: blue lights up first, red last, so thin structure stays
/// visible next to the saturated core. t = 0 is the background.
uint32_t shade(float t) {
    return pack_rgba(t * t, t, std::sqrt(t));
}

class Attractor final : public vaxelis::Application {
  public:
    using Application::Application;

  protected:
    void on_init() override {
        assets_.init(device());
        density_.assign(kPixels, 0u);
        pixels_.assign(kPixels, 0xFF000000u);

        const auto tex = device()
                             .create_texture({.width = kSize,
                                              .height = kSize,
                                              .format = vaxelis::rhi::TextureFormat::RGBA8,
                                              .initial_data = pixels_.data()})
                             .value_or(vaxelis::rhi::TextureHandle{});
        texture_ = assets_.adopt_texture("attractor", tex);

        if (!batch_.init(device()))
            VX_ERROR("SpriteBatch init failed");
        input().bind_action("pause", SDL_SCANCODE_SPACE);
        input().bind_action("randomize", SDL_SCANCODE_R);

        clear_color() = {0.0f, 0.0f, 0.0f, 1.0f};
        restart();
    }

    void on_update(float /*dt*/) override {
        if (input().pressed("pause"))
            paused_ = !paused_;
        if (input().pressed("randomize"))
            randomize();

        // A paused field is already on the GPU: no iteration, no remap, no
        // upload until something actually changes it.
        if (paused_)
            return;
        accumulate();
        tone_map();
        device().update_texture(texture_, {.x = 0,
                                           .y = 0,
                                           .width = kSize,
                                           .height = kSize,
                                           .data = pixels_.data()});
    }

    void on_render() override {
        batch_.begin(device(), width(), height());
        // Square field, so fit it to the short edge instead of stretching. The
        // uv rect carries v_bottom in .y and v_top in .w so row 0 lands on top.
        const float side = static_cast<float>(std::min(width(), height()));
        const vaxelis::vec2 center{static_cast<float>(width()) * 0.5f,
                                   static_cast<float>(height()) * 0.5f};
        batch_.draw(texture_, center, {side, side}, vaxelis::vec4{0.0f, 1.0f, 1.0f, 0.0f},
                    vaxelis::vec4{1.0f});
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("De Jong attractor");
        bool changed = false;
        if (ImGui::SliderFloat("a", &a_, -3.0f, 3.0f))
            changed = true;
        if (ImGui::SliderFloat("b", &b_, -3.0f, 3.0f))
            changed = true;
        if (ImGui::SliderFloat("c", &c_, -3.0f, 3.0f))
            changed = true;
        if (ImGui::SliderFloat("d", &d_, -3.0f, 3.0f))
            changed = true;

        if (ImGui::Button(paused_ ? "Resume" : "Pause"))
            paused_ = !paused_;
        ImGui::SameLine();
        if (ImGui::Button("Randomize"))
            randomize();
        ImGui::Text("%.1f M points  -  peak %u hits", static_cast<double>(points_) * 1e-6,
                    max_density_);
        ImGui::Text("Space pauses  -  R randomizes");
        ImGui::End();

        if (changed)
            restart();
    }

    void on_shutdown() override {
        batch_.shutdown(device());
        assets_.shutdown();
    }

  private:
    /// Clears the field and puts the orbit back at its seed. Any parameter edit
    /// invalidates every hit already counted.
    void restart() {
        std::ranges::fill(density_, 0u);
        max_density_ = 0;
        points_ = 0.0;
        x_ = 0.1f;
        y_ = 0.1f;
        paused_ = false;
    }

    void randomize() {
        std::uniform_real_distribution<float> param(-3.0f, 3.0f);
        a_ = param(rng_);
        b_ = param(rng_);
        c_ = param(rng_);
        d_ = param(rng_);
        restart();
    }

    /// Iterates the map and counts hits. The orbit carries over between frames,
    /// which is what makes the image refine instead of restarting.
    void accumulate() {
        const float a = a_;
        const float b = b_;
        const float c = c_;
        const float d = d_;
        float x = x_;
        float y = y_;
        uint32_t peak = max_density_;

        for (uint32_t i = 0; i < kPointsPerFrame; ++i) {
            const float nx = std::sin(a * y) - std::cos(b * x);
            const float ny = std::sin(c * x) - std::cos(d * y);
            x = nx;
            y = ny;

            // One unsigned compare per axis rejects both negatives (which wrap
            // to huge) and the exact-edge case.
            const auto px = static_cast<uint32_t>(static_cast<int>((x + 2.0f) * kToPixel));
            const auto py = static_cast<uint32_t>(static_cast<int>((y + 2.0f) * kToPixel));
            if (px >= kSize || py >= kSize)
                continue;

            const uint32_t hits = ++density_[py * kSize + px];
            peak = hits > peak ? hits : peak;
        }

        x_ = x;
        y_ = y;
        max_density_ = peak;
        points_ += static_cast<double>(kPointsPerFrame);
    }

    /// Maps densities to colours through a log LUT: one log per palette entry
    /// per frame instead of one per pixel.
    void tone_map() {
        const uint32_t peak = max_density_ > 0u ? max_density_ : 1u;
        const uint32_t top = std::min(peak, kLutSize - 1u);
        const float norm = 1.0f / std::log(1.0f + static_cast<float>(peak));
        for (uint32_t i = 0; i <= top; ++i)
            lut_[i] = shade(std::log(1.0f + static_cast<float>(i)) * norm);

        // Densities past the LUT clamp to its last entry, which is already at
        // full brightness.
        for (uint32_t i = 0; i < kPixels; ++i) {
            const uint32_t hits = density_[i];
            pixels_[i] = lut_[hits < top ? hits : top];
        }
    }

    vaxelis::SpriteBatch batch_;
    vaxelis::AssetCache assets_;
    vaxelis::rhi::TextureHandle texture_{};

    std::vector<uint32_t> density_;
    std::vector<uint32_t> pixels_;
    std::array<uint32_t, kLutSize> lut_{};
    uint32_t max_density_{0};
    double points_{0.0};

    // Peter de Jong's original set.
    float a_{-2.24f};
    float b_{-0.65f};
    float c_{-0.43f};
    float d_{-2.43f};
    float x_{0.1f};
    float y_{0.1f};
    bool paused_{false};

    std::mt19937 rng_{7u};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Attractor app({.title = "Vaxelis - De Jong attractor", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
