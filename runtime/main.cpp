#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/debug/SceneInspector.hpp"
#include "engine/physics/Components.hpp"
#include "engine/physics/Physics2D.hpp"
#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/Script.hpp"

namespace {

std::vector<uint8_t> make_solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
    }
    return px;
}

class M4Demo final : public vaxelis::Application {
public:
    using Application::Application;

protected:
    void on_init() override {
        auto tex_from = [&](const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
            return device().create_texture({.width = w, .height = h,
                                            .format = vaxelis::rhi::TextureFormat::RGBA8,
                                            .initial_data = px.data()}).value_or(vaxelis::rhi::TextureHandle{});
        };
        textures_["white"]  = tex_from(make_solid(8, 8, 255, 255, 255), 8, 8);
        textures_["ground"] = tex_from(make_solid(8, 8, 90, 110, 130),  8, 8);
        textures_["box"]    = tex_from(make_solid(8, 8, 220, 160, 60),  8, 8);

        if (!batch_.init(device())) { VX_ERROR("SpriteBatch init failed"); return; }

        input().bind_action("move_left",  {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});
        input().bind_action("move_right", {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT});
        input().bind_action("jump",       SDL_SCANCODE_SPACE);

        physics_.init({.gravity = {0.0f, 980.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4});
        scripts_.init(scene_, input());

        build_scene();
        resolve_textures();
        VX_INFO("M4Demo: ready");
    }

    void on_fixed_update(float dt) override {
        // Jump impulse: bumps the player's body upward when SPACE pressed.
        if (input().pressed("jump") && player_ != entt::null) {
            const auto& rb = scene_.registry().get<vaxelis::RigidBody2D>(player_);
            if (!B2_IS_NULL(rb.body)) {
                const float ppm = physics_.pixels_per_meter();
                b2Body_ApplyLinearImpulseToCenter(rb.body, b2Vec2{0.0f, -350.0f / ppm}, true);
            }
        }
        scripts_.update(dt, scene_);
        physics_.sync_to_scene(scene_);   // also lazily creates bodies
        physics_.step(dt);
        physics_.sync_to_scene(scene_);   // write back post-step
    }

    void on_render() override {
        batch_.begin(device(), width(), height());
        scene_.render_sprites(batch_);
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("Vaxelis M4");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Batch: %u quads / %u draws", batch_.quads(), batch_.draw_calls());
        if (player_ != entt::null) {
            const auto& t = scene_.registry().get<vaxelis::Transform2D>(player_);
            ImGui::Text("Player: (%.1f, %.1f)", t.position.x, t.position.y);
        }
        ImGui::End();
        inspector_.draw(scene_);
    }

    void on_shutdown() override {
        physics_.shutdown();
        batch_.shutdown(device());
        for (auto& [_, h] : textures_) if (h.valid()) device().destroy(h);
        textures_.clear();
    }

private:
    entt::entity spawn_sprite(const char* name, vaxelis::vec2 pos, vaxelis::vec2 size,
                              const char* tex_key, vaxelis::vec4 color = vaxelis::vec4(1.0f)) {
        auto e = scene_.create_node(name);
        scene_.registry().get<vaxelis::Transform2D>(e).position = pos;
        auto& s = scene_.registry().emplace<vaxelis::SpriteComponent>(e);
        s.texture_key = tex_key;
        s.size  = size;
        s.color = color;
        return e;
    }

    void build_scene() {
        const float W = static_cast<float>(width());
        const float H = static_cast<float>(height());

        // Ground: static body spanning the bottom.
        auto ground = spawn_sprite("Ground", {W * 0.5f, H - 32.0f}, {W, 64.0f}, "ground");
        scene_.registry().emplace<vaxelis::RigidBody2D>(ground).type = vaxelis::BodyType::Static;
        scene_.registry().emplace<vaxelis::BoxCollider2D>(ground).half_extents = {W * 0.5f, 32.0f};

        // A few dynamic boxes that fall under gravity.
        for (int i = 0; i < 6; ++i) {
            auto e = spawn_sprite("Box", {W * 0.35f + i * 80.0f, 80.0f + i * 20.0f},
                                  {48.0f, 48.0f}, "box");
            scene_.registry().emplace<vaxelis::RigidBody2D>(e);  // dynamic by default
            scene_.registry().emplace<vaxelis::BoxCollider2D>(e).half_extents = {24.0f, 24.0f};
        }

        // Player: dynamic, fixed rotation, scripted.
        player_ = spawn_sprite("Player", {W * 0.5f, 200.0f}, {48.0f, 64.0f}, "white",
                               {1.0f, 0.4f, 0.2f, 1.0f});
        auto& rb = scene_.registry().emplace<vaxelis::RigidBody2D>(player_);
        rb.fixed_rotation = true;
        rb.linear_damping = 0.5f;
        scene_.registry().emplace<vaxelis::BoxCollider2D>(player_).half_extents = {24.0f, 32.0f};
        auto& script = scene_.registry().emplace<vaxelis::ScriptComponent>(player_);
        script.path = "assets/scripts/player.lua";
    }

    void resolve_textures() {
        auto view = scene_.registry().view<vaxelis::SpriteComponent>();
        for (auto e : view) {
            auto& s = view.get<vaxelis::SpriteComponent>(e);
            auto it = textures_.find(s.texture_key);
            s.texture = (it != textures_.end()) ? it->second : vaxelis::rhi::TextureHandle{};
        }
    }

    vaxelis::SpriteBatch     batch_;
    vaxelis::Scene           scene_;
    vaxelis::SceneInspector  inspector_;
    vaxelis::Physics2D       physics_;
    vaxelis::ScriptHost      scripts_;
    entt::entity             player_{entt::null};
    std::unordered_map<std::string, vaxelis::rhi::TextureHandle> textures_;
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    M4Demo app({.title = "Vaxelis - M4", .width = 1280, .height = 720});
    if (!app.init()) return 1;
    return app.run();
}
