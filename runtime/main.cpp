#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/debug/SceneInspector.hpp"
#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSerializer.hpp"

namespace {

std::vector<uint8_t> make_checkerboard(uint32_t w, uint32_t h, uint32_t cell) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            bool a = ((x / cell) ^ (y / cell)) & 1u;
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            px[i + 0] = a ? 240 : 50;
            px[i + 1] = a ? 80  : 180;
            px[i + 2] = a ? 120 : 220;
            px[i + 3] = 255;
        }
    }
    return px;
}

std::vector<uint8_t> make_solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
    }
    return px;
}

class M3Demo final : public vaxelis::Application {
public:
    using Application::Application;

protected:
    void on_init() override {
        auto tex_from = [&](const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
            return device().create_texture({.width = w, .height = h,
                                            .format = vaxelis::rhi::TextureFormat::RGBA8,
                                            .initial_data = px.data()}).value_or(vaxelis::rhi::TextureHandle{});
        };
        textures_["checker"] = tex_from(make_checkerboard(128, 128, 16), 128, 128);
        textures_["white"]   = tex_from(make_solid(8, 8, 255, 255, 255), 8, 8);

        if (!batch_.init(device())) { VX_ERROR("SpriteBatch init failed"); return; }

        input().bind_action("move_left",  {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});
        input().bind_action("move_right", {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT});
        input().bind_action("move_up",    {SDL_SCANCODE_W, SDL_SCANCODE_UP});
        input().bind_action("move_down",  {SDL_SCANCODE_S, SDL_SCANCODE_DOWN});

        build_default_scene();
        resolve_textures();
        VX_INFO("M3Demo: ready");
    }

    void on_fixed_update(float dt) override {
        if (player_ == entt::null) return;
        auto& t = scene_.registry().get<vaxelis::Transform2D>(player_);
        vaxelis::vec2 dir{0.0f, 0.0f};
        if (input().down("move_left"))  dir.x -= 1.0f;
        if (input().down("move_right")) dir.x += 1.0f;
        if (input().down("move_up"))    dir.y -= 1.0f;
        if (input().down("move_down"))  dir.y += 1.0f;
        if (dir.x != 0.0f || dir.y != 0.0f) {
            dir = glm::normalize(dir);
            constexpr float kSpeedPxPerSec = 320.0f;
            t.position += dir * (kSpeedPxPerSec * dt);
        }
    }

    void on_render() override {
        batch_.begin(device(), width(), height());
        scene_.render_sprites(batch_);
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("Vaxelis M3");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Batch: %u quads / %u draws", batch_.quads(), batch_.draw_calls());
        if (ImGui::Button("Save scene")) {
            vaxelis::scene_io::save_file(scene_, "scene.json");
            VX_INFO("Saved scene.json");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload scene")) {
            scene_ = vaxelis::Scene{};
            if (!vaxelis::scene_io::load_file(scene_, "scene.json")) {
                build_default_scene();
            }
            resolve_textures();
            player_ = find_player();
        }
        ImGui::End();
        inspector_.draw(scene_);
    }

    void on_shutdown() override {
        batch_.shutdown(device());
        for (auto& [_, h] : textures_) if (h.valid()) device().destroy(h);
        textures_.clear();
    }

private:
    void build_default_scene() {
        scene_ = vaxelis::Scene{};

        auto bg = scene_.create_node("Background");
        auto& bg_t = scene_.registry().get<vaxelis::Transform2D>(bg);
        bg_t.position = {width() * 0.5f, height() * 0.5f};
        auto& bg_s = scene_.registry().emplace<vaxelis::SpriteComponent>(bg);
        bg_s.texture_key = "checker";
        bg_s.size  = {static_cast<float>(width()), static_cast<float>(height())};
        bg_s.color = {0.45f, 0.45f, 0.55f, 1.0f};
        bg_s.z_order = -10;

        auto trail = scene_.create_node("Trail");
        for (int i = 0; i < 32; ++i) {
            auto e = scene_.create_node("dot", trail);
            float t = static_cast<float>(i) / 32.0f;
            auto& tf = scene_.registry().get<vaxelis::Transform2D>(e);
            tf.position = {120.0f + t * (width() - 240.0f),
                           height() * 0.5f + std::sin(t * 6.28f) * 80.0f};
            auto& s = scene_.registry().emplace<vaxelis::SpriteComponent>(e);
            s.texture_key = "white";
            s.size = {16.0f, 16.0f};
            s.color = {0.6f + 0.4f * t, 0.8f, 1.0f - 0.5f * t, 1.0f};
        }

        player_ = scene_.create_node("Player");
        auto& pt = scene_.registry().get<vaxelis::Transform2D>(player_);
        pt.position = {width() * 0.5f, height() * 0.5f};
        auto& ps = scene_.registry().emplace<vaxelis::SpriteComponent>(player_);
        ps.texture_key = "white";
        ps.size = {48.0f, 48.0f};
        ps.color = {1.0f, 0.4f, 0.2f, 1.0f};
        ps.z_order = 10;
    }

    void resolve_textures() {
        auto view = scene_.registry().view<vaxelis::SpriteComponent>();
        for (auto e : view) {
            auto& s = view.get<vaxelis::SpriteComponent>(e);
            auto it = textures_.find(s.texture_key);
            s.texture = (it != textures_.end()) ? it->second : vaxelis::rhi::TextureHandle{};
        }
    }

    entt::entity find_player() {
        entt::entity found = entt::null;
        scene_.for_each([&](entt::entity e) {
            if (found != entt::null) return;
            if (scene_.registry().get<vaxelis::Name>(e).value == "Player") found = e;
        });
        return found;
    }

    vaxelis::SpriteBatch     batch_;
    vaxelis::Scene           scene_;
    vaxelis::SceneInspector  inspector_;
    entt::entity             player_{entt::null};
    std::unordered_map<std::string, vaxelis::rhi::TextureHandle> textures_;
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    M3Demo app({.title = "Vaxelis - M3", .width = 1280, .height = 720});
    if (!app.init()) return 1;
    return app.run();
}
