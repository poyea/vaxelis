// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

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
        m_assets.init(device());
        m_density.assign(kPixels, 0u);
        m_pixels.assign(kPixels, 0xFF000000u);

        const auto tex = device()
                             .create_texture({.width = kSize,
                                              .height = kSize,
                                              .format = vaxelis::rhi::TextureFormat::RGBA8,
                                              .initial_data = m_pixels.data()})
                             .value_or(vaxelis::rhi::TextureHandle{});
        m_texture = m_assets.adopt_texture("attractor", tex);

        if (!m_batch.init(device()))
            VX_ERROR("SpriteBatch init failed");
        input().bind_action("pause", SDL_SCANCODE_SPACE);
        input().bind_action("randomize", SDL_SCANCODE_R);

        clear_color() = {0.0f, 0.0f, 0.0f, 1.0f};
        restart();
    }

    void on_update(float /*dt*/) override {
        if (input().pressed("pause"))
            m_paused = !m_paused;
        if (input().pressed("randomize"))
            randomize();

        // A paused field is already on the GPU: no iteration, no remap, no
        // upload until something actually changes it.
        if (m_paused)
            return;
        accumulate();
        tone_map();
        device().update_texture(m_texture, {.x = 0,
                                            .y = 0,
                                            .width = kSize,
                                            .height = kSize,
                                            .data = m_pixels.data()});
    }

    void on_render() override {
        m_batch.begin(device(), width(), height());
        // Square field, so fit it to the short edge instead of stretching.
        const float side = static_cast<float>(std::min(width(), height()));
        const vaxelis::vec2 center{static_cast<float>(width()) * 0.5f,
                                   static_cast<float>(height()) * 0.5f};
        m_batch.draw(m_texture, center, {side, side});
        m_batch.end();
    }

    void on_imgui() override {
        ImGui::Begin("De Jong attractor");
        bool changed = false;
        if (ImGui::SliderFloat("a", &m_a, -3.0f, 3.0f))
            changed = true;
        if (ImGui::SliderFloat("b", &m_b, -3.0f, 3.0f))
            changed = true;
        if (ImGui::SliderFloat("c", &m_c, -3.0f, 3.0f))
            changed = true;
        if (ImGui::SliderFloat("d", &m_d, -3.0f, 3.0f))
            changed = true;

        if (ImGui::Button(m_paused ? "Resume" : "Pause"))
            m_paused = !m_paused;
        ImGui::SameLine();
        if (ImGui::Button("Randomize"))
            randomize();
        ImGui::Text("%.1f M points  -  peak %u hits", static_cast<double>(m_points) * 1e-6,
                    m_max_density);
        ImGui::Text("Space pauses  -  R randomizes");
        ImGui::End();

        if (changed)
            restart();
    }

    void on_shutdown() override {
        m_batch.shutdown(device());
        m_assets.shutdown();
    }

  private:
    /// Clears the field and puts the orbit back at its seed. Any parameter edit
    /// invalidates every hit already counted.
    void restart() {
        std::ranges::fill(m_density, 0u);
        m_max_density = 0;
        m_points = 0.0;
        m_x = 0.1f;
        m_y = 0.1f;
        m_paused = false;
    }

    void randomize() {
        std::uniform_real_distribution<float> param(-3.0f, 3.0f);
        m_a = param(m_rng);
        m_b = param(m_rng);
        m_c = param(m_rng);
        m_d = param(m_rng);
        restart();
    }

    /// Iterates the map and counts hits. The orbit carries over between frames,
    /// which is what makes the image refine instead of restarting.
    void accumulate() {
        const float a = m_a;
        const float b = m_b;
        const float c = m_c;
        const float d = m_d;
        float x = m_x;
        float y = m_y;
        uint32_t peak = m_max_density;

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

            const uint32_t hits = ++m_density[py * kSize + px];
            peak = hits > peak ? hits : peak;
        }

        m_x = x;
        m_y = y;
        m_max_density = peak;
        m_points += static_cast<double>(kPointsPerFrame);
    }

    /// Maps densities to colours through a log LUT: one log per palette entry
    /// per frame instead of one per pixel.
    void tone_map() {
        const uint32_t peak = m_max_density > 0u ? m_max_density : 1u;
        const uint32_t top = std::min(peak, kLutSize - 1u);
        const float norm = 1.0f / std::log(1.0f + static_cast<float>(peak));
        for (uint32_t i = 0; i <= top; ++i)
            m_lut[i] = shade(std::log(1.0f + static_cast<float>(i)) * norm);

        // Densities past the LUT clamp to its last entry, which is already at
        // full brightness.
        for (uint32_t i = 0; i < kPixels; ++i) {
            const uint32_t hits = m_density[i];
            m_pixels[i] = m_lut[hits < top ? hits : top];
        }
    }

    vaxelis::SpriteBatch m_batch;
    vaxelis::AssetCache m_assets;
    vaxelis::rhi::TextureHandle m_texture{};

    std::vector<uint32_t> m_density;
    std::vector<uint32_t> m_pixels;
    std::array<uint32_t, kLutSize> m_lut{};
    uint32_t m_max_density{0};
    double m_points{0.0};

    // Peter de Jong's original set.
    float m_a{-2.24f};
    float m_b{-0.65f};
    float m_c{-0.43f};
    float m_d{-2.43f};
    float m_x{0.1f};
    float m_y{0.1f};
    bool m_paused{false};

    std::mt19937 m_rng{7u};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Attractor app({.title = "Vaxelis - De Jong attractor", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
