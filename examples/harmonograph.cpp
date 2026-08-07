/// @file
/// Harmonograph: the curve traced by four damped pendulums, two per axis.
///
///   x(t) = A sin(f1 t + p1) e^(-d1 t) + A sin(f2 t + p2) e^(-d2 t)
///   y(t) = A sin(f3 t + p3) e^(-d3 t) + A sin(f4 t + p4) e^(-d4 t)
///
/// Near-integer frequency ratios give closed figures; nudging one off by a
/// hundredth makes the curve precess. Tweak the pendulums in the ImGui panel,
/// press Space (or Replay) to draw it again from t = 0.

#include <algorithm>
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

// Samples along the curve. The batcher is sized to hold them all in one draw
// call, which is why this stays under the 16383-quad ceiling of u16 indices.
constexpr uint32_t kSamples = 6000;
constexpr uint32_t kBatchQuads = 8192;
/// Seconds of pendulum time sampled, and how fast the pen walks through them.
constexpr float kSpan = 100.0f;
constexpr float kRevealRate = 2400.0f;
constexpr float kTau = 6.2831853f;

/// One pendulum: amplitude is fixed, the panel drives the rest.
struct Pendulum {
    float freq{2.0f};
    float phase{0.0f};
    float decay{0.018f};
};

/// A sampled point, in unit space, with the colour it is drawn in. Both are
/// cached so an idle frame does no trigonometry at all.
struct Sample {
    vaxelis::vec2 pos{0.0f, 0.0f};
    vaxelis::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

float pendulum_at(const Pendulum& p, float t) {
    return 0.45f * std::sin(p.freq * t + p.phase) * std::exp(-p.decay * t);
}

/// Cyclic cosine gradient over the length of the curve.
vaxelis::vec4 ramp(float u) {
    return {0.55f + 0.45f * std::cos(kTau * (u + 0.00f)),
            0.55f + 0.45f * std::cos(kTau * (u + 0.33f)),
            0.55f + 0.45f * std::cos(kTau * (u + 0.67f)), 1.0f};
}

class Harmonograph final : public vaxelis::Application {
  public:
    using Application::Application;

  protected:
    void on_init() override {
        assets_.init(device());

        // A single white texel: every dot is this texture tinted per-vertex, so
        // the whole curve stays one draw call.
        constexpr uint32_t white = 0xFFFFFFFFu;
        const auto tex = device()
                             .create_texture({.width = 1,
                                              .height = 1,
                                              .format = vaxelis::rhi::TextureFormat::RGBA8,
                                              .initial_data = &white})
                             .value_or(vaxelis::rhi::TextureHandle{});
        texture_ = assets_.adopt_texture("dot", tex);

        if (!batch_.init(device(), kBatchQuads))
            VX_ERROR("SpriteBatch init failed");
        input().bind_action("replay", SDL_SCANCODE_SPACE);
        input().bind_action("randomize", SDL_SCANCODE_R);

        clear_color() = {0.04f, 0.04f, 0.06f, 1.0f};
        samples_.reserve(kSamples);
        resample();
    }

    void on_update(float dt) override {
        if (input().pressed("replay"))
            revealed_ = 0.0f;
        if (input().pressed("randomize"))
            randomize();

        const auto total = static_cast<float>(samples_.size());
        revealed_ = std::min(revealed_ + kRevealRate * dt, total);
    }

    void on_render() override {
        batch_.begin(device(), width(), height());
        const vaxelis::vec2 center{static_cast<float>(width()) * 0.5f,
                                   static_cast<float>(height()) * 0.5f};
        // Samples reach +-0.9 in unit space, so half the short edge (less a
        // margin) frames the whole curve; resizing reframes it without touching
        // the cached samples.
        const float scale = 0.48f * static_cast<float>(std::min(width(), height()));
        const vaxelis::vec2 dot{2.5f, 2.5f};

        const auto count = static_cast<size_t>(revealed_);
        for (size_t i = 0; i < count; ++i) {
            const Sample& s = samples_[i];
            batch_.draw(texture_, center + s.pos * scale, dot, s.color);
        }
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("Harmonograph");
        bool changed = false;
        changed = pendulum_ui("x pendulum 1", x1_) || changed;
        changed = pendulum_ui("x pendulum 2", x2_) || changed;
        changed = pendulum_ui("y pendulum 1", y1_) || changed;
        changed = pendulum_ui("y pendulum 2", y2_) || changed;

        if (ImGui::Button("Replay"))
            revealed_ = 0.0f;
        ImGui::SameLine();
        if (ImGui::Button("Randomize"))
            randomize();
        ImGui::Text("%zu / %u samples", static_cast<size_t>(revealed_), kSamples);
        ImGui::Text("Space replays  -  R randomizes");
        ImGui::End();

        // Resampling here rather than inside the widget loop keeps one rebuild
        // per frame no matter how many sliders the user drags.
        if (changed) {
            resample();
            revealed_ = 0.0f;
        }
    }

    void on_shutdown() override {
        batch_.shutdown(device());
        assets_.shutdown();
    }

  private:
    /// Three sliders for one pendulum; true when any of them moved.
    static bool pendulum_ui(const char* label, Pendulum& p) {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        if (ImGui::SliderFloat("freq", &p.freq, 0.5f, 8.0f))
            changed = true;
        if (ImGui::SliderFloat("phase", &p.phase, 0.0f, kTau))
            changed = true;
        if (ImGui::SliderFloat("decay", &p.decay, 0.0f, 0.05f))
            changed = true;
        ImGui::PopID();
        return changed;
    }

    /// Rebuilds the cached curve. Only runs when a parameter moved, never on a
    /// frame that is merely drawing what is already there.
    void resample() {
        samples_.clear();
        const float step = kSpan / static_cast<float>(kSamples - 1);
        const float inv = 1.0f / static_cast<float>(kSamples - 1);
        for (uint32_t i = 0; i < kSamples; ++i) {
            const float t = static_cast<float>(i) * step;
            const vaxelis::vec2 p{pendulum_at(x1_, t) + pendulum_at(x2_, t),
                                  pendulum_at(y1_, t) + pendulum_at(y2_, t)};
            samples_.push_back({p, ramp(static_cast<float>(i) * inv)});
        }
    }

    void randomize() {
        std::uniform_real_distribution<float> freq(1.0f, 6.0f);
        std::uniform_real_distribution<float> phase(0.0f, kTau);
        std::uniform_real_distribution<float> decay(0.004f, 0.03f);
        for (Pendulum* p : {&x1_, &x2_, &y1_, &y2_}) {
            // Snap most frequencies near a whole number: exact ratios close the
            // figure, the leftover fraction is what makes it precess.
            p->freq = std::round(freq(rng_)) + 0.01f * std::round(freq(rng_));
            p->phase = phase(rng_);
            p->decay = decay(rng_);
        }
        resample();
        revealed_ = 0.0f;
    }

    vaxelis::SpriteBatch batch_;
    vaxelis::AssetCache assets_;
    vaxelis::rhi::TextureHandle texture_{};

    std::vector<Sample> samples_;
    float revealed_{0.0f};

    Pendulum x1_{2.0f, 0.0f, 0.018f};
    Pendulum x2_{2.01f, 1.6f, 0.010f};
    Pendulum y1_{3.0f, 0.8f, 0.014f};
    Pendulum y2_{3.02f, 2.4f, 0.022f};

    std::mt19937 rng_{1337u};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Harmonograph app({.title = "Vaxelis - Harmonograph", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
