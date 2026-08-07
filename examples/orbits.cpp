/// @file
/// N-body orbits: a heavy star and a swarm of planets under mutual gravity,
/// integrated at a fixed timestep and drawn through the scene tree.
///
/// Every body pulls on every other, so orbits precess, resonances build up and
/// the occasional planet gets flung out. Q/E zoom, R reseeds the system.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <entt/entt.hpp>
#include <imgui.h>

#include "engine/assets/AssetCache.hpp"
#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/scene/Camera2D.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

namespace {

constexpr uint32_t kPlanets = 48;
constexpr uint32_t kTrail = 48; ///< trail samples per body (0.8s at 60Hz)
constexpr float kG = 4000.0f;
constexpr float kStarMass = 4000.0f;
constexpr float kSoftening = 144.0f; ///< eps^2, keeps close passes finite
constexpr float kTau = 6.2831853f;

/// Simulation state that Transform2D does not already carry.
struct Body {
    vaxelis::vec2 vel{0.0f, 0.0f};
    float mass{1.0f};
};

vaxelis::vec4 ramp(float u) {
    return {0.55f + 0.45f * std::cos(kTau * (u + 0.00f)),
            0.55f + 0.45f * std::cos(kTau * (u + 0.33f)),
            0.55f + 0.45f * std::cos(kTau * (u + 0.67f)), 1.0f};
}

class Orbits final : public vaxelis::Application {
  public:
    using Application::Application;

  protected:
    void on_init() override {
        assets_.init(device());

        constexpr uint32_t white = 0xFFFFFFFFu;
        const auto tex = device()
                             .create_texture({.width = 1,
                                              .height = 1,
                                              .format = vaxelis::rhi::TextureFormat::RGBA8,
                                              .initial_data = &white})
                             .value_or(vaxelis::rhi::TextureHandle{});
        texture_ = assets_.adopt_texture("dot", tex);

        if (!batch_.init(device()))
            VX_ERROR("SpriteBatch init failed");
        input().bind_action("zoom_in", SDL_SCANCODE_E);
        input().bind_action("zoom_out", SDL_SCANCODE_Q);
        input().bind_action("reseed", SDL_SCANCODE_R);

        clear_color() = {0.02f, 0.02f, 0.05f, 1.0f};
        // Outermost orbit sits at r = 520, so 0.6 frames the whole system on a
        // 720-tall window with room to spare.
        camera_.zoom = 0.6f;
        spawn();
    }

    void on_fixed_update(float dt) override {
        auto& reg = scene_.registry();
        const size_t n = bodies_.size();

        // Gather the hot state into flat arrays once per step: the O(N^2) pass
        // below then runs over contiguous memory instead of doing two component
        // lookups per pair. The vectors keep their capacity between steps.
        pos_.clear();
        vel_.clear();
        mass_.clear();
        for (const entt::entity e : bodies_) {
            pos_.push_back(reg.get<vaxelis::Transform2D>(e).position);
            const Body& b = reg.get<Body>(e);
            vel_.push_back(b.vel);
            mass_.push_back(b.mass);
        }

        acc_.assign(n, vaxelis::vec2{0.0f, 0.0f});
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                const vaxelis::vec2 d = pos_[j] - pos_[i];
                // Softened inverse-square: one rsqrt feeds both directions, and
                // Newton's third law means each pair is only evaluated once.
                const float r2 = d.x * d.x + d.y * d.y + kSoftening;
                const float inv_r = 1.0f / std::sqrt(r2);
                const vaxelis::vec2 pull = d * (kG * inv_r * inv_r * inv_r);
                acc_[i] += pull * mass_[j];
                acc_[j] -= pull * mass_[i];
            }
        }

        // Symplectic Euler: kick, then drift with the updated velocity. Explicit
        // Euler would pump energy in and spiral every orbit outwards.
        vaxelis::vec2 weighted{0.0f, 0.0f};
        float total_mass = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            vel_[i] += acc_[i] * dt;
            pos_[i] += vel_[i] * dt;
            weighted += pos_[i] * mass_[i];
            total_mass += mass_[i];
        }
        barycentre_ = total_mass > 0.0f ? weighted / total_mass : vaxelis::vec2{0.0f, 0.0f};

        for (size_t i = 0; i < n; ++i) {
            reg.get<vaxelis::Transform2D>(bodies_[i]).position = pos_[i];
            reg.get<Body>(bodies_[i]).vel = vel_[i];
            trail_[i * kTrail + head_] = pos_[i];
        }
        head_ = (head_ + 1) % kTrail;
    }

    void on_update(float dt) override {
        if (input().pressed("reseed"))
            reseed();

        float zoom = 0.0f;
        zoom += input().down("zoom_in") ? dt : 0.0f;
        zoom -= input().down("zoom_out") ? dt : 0.0f;
        if (zoom != 0.0f)
            camera_.zoom = std::clamp(camera_.zoom * std::exp(zoom), 0.1f, 8.0f);

        // Follow the barycentre so the system stays framed as the star recoils.
        camera_.position = barycentre_;
    }

    void on_render() override {
        batch_.begin(device(), camera_.projection(width(), height()));
        draw_trails();
        scene_.render_sprites(batch_);
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("Orbits");
        ImGui::Text("bodies %zu  -  quads %u", bodies_.size(), batch_.quads());
        ImGui::Text("zoom   %.2f", static_cast<double>(camera_.zoom));
        ImGui::Text("FPS    %.1f", static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::Separator();
        ImGui::Text("Q/E zoom  -  R reseed");
        ImGui::End();
    }

    void on_shutdown() override {
        batch_.shutdown(device());
        assets_.shutdown();
    }

  private:
    /// Creates the star and the planets once; reseeding only rewrites their
    /// state, so no entity is ever churned at runtime.
    void spawn() {
        auto& reg = scene_.registry();
        bodies_.reserve(kPlanets + 1);

        const entt::entity star = scene_.create_node("Star");
        auto& star_sprite = reg.emplace<vaxelis::SpriteComponent>(star);
        star_sprite.texture = texture_;
        star_sprite.size = {22.0f, 22.0f};
        star_sprite.color = {1.0f, 0.93f, 0.65f, 1.0f};
        star_sprite.z_order = 1;
        reg.emplace<Body>(star).mass = kStarMass;
        bodies_.push_back(star);

        for (uint32_t i = 0; i < kPlanets; ++i) {
            const entt::entity e = scene_.create_node("Planet");
            auto& sprite = reg.emplace<vaxelis::SpriteComponent>(e);
            sprite.texture = texture_;
            sprite.size = {5.0f, 5.0f};
            sprite.z_order = 1;
            reg.emplace<Body>(e);
            bodies_.push_back(e);
        }

        trail_.assign(bodies_.size() * kTrail, vaxelis::vec2{0.0f, 0.0f});
        pos_.reserve(bodies_.size());
        vel_.reserve(bodies_.size());
        mass_.reserve(bodies_.size());
        reseed();
    }

    void reseed() {
        auto& reg = scene_.registry();
        reg.get<vaxelis::Transform2D>(bodies_[0]).position = {0.0f, 0.0f};
        reg.get<Body>(bodies_[0]).vel = {0.0f, 0.0f};

        std::uniform_real_distribution<float> radius(110.0f, 520.0f);
        std::uniform_real_distribution<float> angle(0.0f, kTau);
        std::uniform_real_distribution<float> mass(1.0f, 5.0f);

        for (size_t i = 1; i < bodies_.size(); ++i) {
            const entt::entity e = bodies_[i];
            const float r = radius(rng_);
            const float a = angle(rng_);
            const vaxelis::vec2 dir{std::cos(a), std::sin(a)};

            reg.get<vaxelis::Transform2D>(e).position = dir * r;
            auto& body = reg.get<Body>(e);
            // Circular-orbit speed for the star alone, perpendicular to the
            // radius; the other planets are what perturb it from there.
            body.vel = vaxelis::vec2{-dir.y, dir.x} * std::sqrt(kG * kStarMass / r);
            body.mass = mass(rng_);
            reg.get<vaxelis::SpriteComponent>(e).color = ramp((r - 110.0f) / 410.0f);
        }

        // Seed every trail with the spawn position so no stale streak survives.
        for (size_t i = 0; i < bodies_.size(); ++i) {
            const vaxelis::vec2 p = reg.get<vaxelis::Transform2D>(bodies_[i]).position;
            std::fill_n(trail_.data() + i * kTrail, kTrail, p);
        }
        head_ = 0;
        barycentre_ = {0.0f, 0.0f};
    }

    /// Ring-buffer trails, oldest sample first so alpha ramps up to the head.
    void draw_trails() {
        auto& reg = scene_.registry();
        const vaxelis::vec2 size{2.0f, 2.0f};
        const float inv_age = 1.0f / static_cast<float>(kTrail - 1);

        for (size_t i = 0; i < bodies_.size(); ++i) {
            vaxelis::vec4 tint = reg.get<vaxelis::SpriteComponent>(bodies_[i]).color;
            for (uint32_t k = 0; k < kTrail; ++k) {
                const uint32_t slot = (head_ + k) % kTrail;
                tint.a = 0.45f * static_cast<float>(k) * inv_age;
                batch_.draw(texture_, trail_[i * kTrail + slot], size, tint);
            }
        }
    }

    vaxelis::SpriteBatch batch_;
    vaxelis::AssetCache assets_;
    vaxelis::Scene scene_;
    vaxelis::Camera2D camera_;
    vaxelis::rhi::TextureHandle texture_{};

    std::vector<entt::entity> bodies_;
    std::vector<vaxelis::vec2> pos_;
    std::vector<vaxelis::vec2> vel_;
    std::vector<vaxelis::vec2> acc_;
    std::vector<float> mass_;
    std::vector<vaxelis::vec2> trail_;
    uint32_t head_{0};
    vaxelis::vec2 barycentre_{0.0f, 0.0f};

    std::mt19937 rng_{2024u};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Orbits app({.title = "Vaxelis - Orbits", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
