#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include "engine/assets/AssetCache.hpp"
#include "engine/assets/FileWatcher.hpp"
#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/debug/SceneInspector.hpp"
#include "engine/physics/Components.hpp"
#include "engine/physics/Physics2D.hpp"
#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/scene/Camera2D.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/Script.hpp"
#include "engine/tilemap/TiledMap.hpp"

namespace {

// 64x64 RGBA atlas laid out as 2x2 cells of 32x32 — gids 1..4 in the tilemap.
// Colors picked so each gid is distinct on screen.
std::vector<uint8_t> make_atlas() {
    constexpr uint32_t W = 64, H = 64, C = 32;
    std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 0);
    auto fill = [&](uint32_t cx, uint32_t cy, uint8_t r, uint8_t g, uint8_t b) {
        for (uint32_t y = cy; y < cy + C; ++y) {
            for (uint32_t x = cx; x < cx + C; ++x) {
                size_t i = (static_cast<size_t>(y) * W + x) * 4;
                px[i + 0] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
            }
        }
    };
    fill( 0,  0,  90, 130, 200);  // gid 1: sky/empty marker (unused)
    fill(32,  0, 160,  90,  90);  // gid 2: ground top (reddish)
    fill( 0, 32,  90,  70,  50);  // gid 3: ground deep (brown)
    fill(32, 32, 220, 180,  60);  // gid 4: platform (yellow)
    return px;
}

std::vector<uint8_t> make_solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
    }
    return px;
}

class M6Demo final : public vaxelis::Application {
public:
    using Application::Application;

protected:
    void on_init() override {
        assets_.init(device(), &watcher_);
        register_procedural("white", 8, 8, make_solid(8, 8, 255, 255, 255));
        register_procedural("atlas", 64, 64, make_atlas());

        if (!batch_.init(device())) { VX_ERROR("SpriteBatch init failed"); return; }

        input().bind_action("move_left",  {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});
        input().bind_action("move_right", {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT});
        input().bind_action("jump",       SDL_SCANCODE_SPACE);

        physics_.init({.gravity = {0.0f, 980.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4});
        scripts_.init(scene_, input());

        if (!load_map("assets/maps/demo.tmj")) {
            VX_ERROR("Failed to load tilemap; aborting init");
        }
        build_player_from_spawn();
        resolve_textures();
        watch_scripts();

        camera_.bounds_min = {0.0f, 0.0f};
        camera_.bounds_max = map_.world_size();
        camera_.zoom = 1.0f;

        VX_INFO("M6Demo: ready");
    }

    void on_update(float dt) override {
        watcher_.tick(dt);

        // Camera follows the player smoothly. apply_bounds keeps the view
        // inside the tilemap so the player can walk to the edges without
        // showing void.
        if (player_ != entt::null) {
            const auto& t = scene_.registry().get<vaxelis::Transform2D>(player_);
            const float follow = 1.0f - std::exp(-8.0f * dt);  // exp-smoothing
            camera_.position += (t.position - camera_.position) * follow;
        }
        camera_.apply_bounds(width(), height());
    }

    void on_fixed_update(float dt) override {
        if (input().pressed("jump") && player_ != entt::null) {
            const auto& rb = scene_.registry().get<vaxelis::RigidBody2D>(player_);
            if (!B2_IS_NULL(rb.body)) {
                const float ppm = physics_.pixels_per_meter();
                b2Body_ApplyLinearImpulseToCenter(rb.body, b2Vec2{0.0f, -350.0f / ppm}, true);
            }
        }
        scripts_.update(dt, scene_);
        physics_.sync_to_scene(scene_);
        physics_.step(dt);
        physics_.sync_to_scene(scene_);
    }

    void on_render() override {
        batch_.begin(device(), camera_.projection(width(), height()));
        vaxelis::tiled::render(map_, batch_);
        scene_.render_sprites(batch_);
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("Vaxelis M6");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Batch: %u quads / %u draws", batch_.quads(), batch_.draw_calls());
        ImGui::Text("Map: %dx%d tiles", map_.width, map_.height);
        ImGui::Text("Camera: (%.0f, %.0f) zoom %.2f", camera_.position.x, camera_.position.y, camera_.zoom);
        ImGui::SliderFloat("Zoom", &camera_.zoom, 0.5f, 3.0f);
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
        assets_.shutdown();
        for (auto& [_, h] : procedural_) if (h.valid()) device().destroy(h);
        procedural_.clear();
    }

private:
    void register_procedural(const char* key, uint32_t w, uint32_t h,
                             const std::vector<uint8_t>& px) {
        auto handle = device().create_texture({.width = w, .height = h,
                                               .format = vaxelis::rhi::TextureFormat::RGBA8,
                                               .initial_data = px.data()})
                                 .value_or(vaxelis::rhi::TextureHandle{});
        procedural_[key] = handle;
    }

    vaxelis::rhi::TextureHandle texture_for(const std::string& key) const {
        if (auto it = procedural_.find(key); it != procedural_.end()) return it->second;
        return assets_.get_texture(key);
    }

    bool load_map(const char* path) {
        if (!vaxelis::tiled::load_file(map_, path,
                [this](const std::string& image) { return texture_for(image); })) {
            return false;
        }

        // Build static colliders from any tile layer named "world" — every
        // non-zero tile becomes a solid AABB. Good enough for a demo; a real
        // game would tag solid gids in the tileset.
        for (const auto& layer : map_.tile_layers) {
            if (layer.name != "world") continue;
            for (int y = 0; y < layer.height; ++y) {
                for (int x = 0; x < layer.width; ++x) {
                    const uint32_t gid = layer.gids[static_cast<size_t>(y) * layer.width + x];
                    if (gid == 0) continue;
                    auto e = scene_.create_node("Tile");
                    auto& tr = scene_.registry().get<vaxelis::Transform2D>(e);
                    tr.position = { (x + 0.5f) * map_.tile_w, (y + 0.5f) * map_.tile_h };
                    scene_.registry().emplace<vaxelis::RigidBody2D>(e).type = vaxelis::BodyType::Static;
                    scene_.registry().emplace<vaxelis::BoxCollider2D>(e).half_extents =
                        { map_.tile_w * 0.5f, map_.tile_h * 0.5f };
                }
            }
        }
        return true;
    }

    void build_player_from_spawn() {
        vaxelis::vec2 spawn{200.0f, 200.0f};
        for (const auto& g : map_.object_groups) {
            for (const auto& obj : g.objects) {
                if (obj.type == "spawn" || obj.name == "player") {
                    spawn = obj.pos;
                    break;
                }
            }
        }
        player_ = scene_.create_node("Player");
        auto& t = scene_.registry().get<vaxelis::Transform2D>(player_);
        t.position = spawn;
        auto& s = scene_.registry().emplace<vaxelis::SpriteComponent>(player_);
        s.texture_key = "white";
        s.size  = {32.0f, 48.0f};
        s.color = {1.0f, 0.4f, 0.2f, 1.0f};
        s.z_order = 10;
        auto& rb = scene_.registry().emplace<vaxelis::RigidBody2D>(player_);
        rb.fixed_rotation = true;
        rb.linear_damping = 0.5f;
        scene_.registry().emplace<vaxelis::BoxCollider2D>(player_).half_extents = {16.0f, 24.0f};
        auto& script = scene_.registry().emplace<vaxelis::ScriptComponent>(player_);
        script.path = "assets/scripts/player.lua";
    }

    void resolve_textures() {
        auto view = scene_.registry().view<vaxelis::SpriteComponent>();
        for (auto e : view) {
            auto& s = view.get<vaxelis::SpriteComponent>(e);
            s.texture = texture_for(s.texture_key);
        }
    }

    void watch_scripts() {
        auto view = scene_.registry().view<vaxelis::ScriptComponent>();
        for (auto e : view) {
            auto& sc = view.get<vaxelis::ScriptComponent>(e);
            if (sc.path.empty()) continue;
            const auto target = e;
            watcher_.watch(sc.path, [this, target](const std::string& path) {
                if (!scene_.registry().valid(target)) return;
                if (auto* s = scene_.registry().try_get<vaxelis::ScriptComponent>(target)) {
                    s->loaded = false;
                    VX_INFO("Script reload: {}", path);
                }
            });
        }
    }

    vaxelis::SpriteBatch     batch_;
    vaxelis::Scene           scene_;
    vaxelis::SceneInspector  inspector_;
    vaxelis::Physics2D       physics_;
    vaxelis::ScriptHost      scripts_;
    vaxelis::FileWatcher     watcher_;
    vaxelis::AssetCache      assets_;
    vaxelis::TiledMap        map_;
    vaxelis::Camera2D        camera_;
    entt::entity             player_{entt::null};
    std::unordered_map<std::string, vaxelis::rhi::TextureHandle> procedural_;
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    M6Demo app({.title = "Vaxelis - M6", .width = 1280, .height = 720});
    if (!app.init()) return 1;
    return app.run();
}
