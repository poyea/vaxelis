#include <cmath>
#include <cstdint>
#include <string>
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
#include "engine/tilemap/TiledMap.hpp"

namespace {

// 64x64 RGBA tile atlas — gids 1..4.
std::vector<uint8_t> make_atlas() {
    constexpr uint32_t W = 64, H = 64, C = 32;
    std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 0);
    auto fill = [&](uint32_t cx, uint32_t cy, uint8_t r, uint8_t g, uint8_t b) {
        for (uint32_t y = cy; y < cy + C; ++y)
            for (uint32_t x = cx; x < cx + C; ++x) {
                size_t i = (static_cast<size_t>(y) * W + x) * 4;
                px[i + 0] = r;
                px[i + 1] = g;
                px[i + 2] = b;
                px[i + 3] = 255;
            }
    };
    fill(0, 0, 60, 90, 140);    // gid 1 (unused, sky tint)
    fill(32, 0, 110, 180, 90);  // gid 2 ground top
    fill(0, 32, 90, 70, 50);    // gid 3 ground deep
    fill(32, 32, 220, 180, 60); // gid 4 platform
    return px;
}

std::vector<uint8_t> make_solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

bool aabb_overlap(vaxelis::vec2 amin, vaxelis::vec2 amax, vaxelis::vec2 bmin, vaxelis::vec2 bmax) {
    return amin.x < bmax.x && amax.x > bmin.x && amin.y < bmax.y && amax.y > bmin.y;
}

struct AABB {
    vaxelis::vec2 min, max;
};

// Enemy is rendered + ticked by the game; it lives outside the physics world
// so contacts are checked via AABB overlap (simpler and tunable for squash).
struct Enemy {
    float speed{70.0f};
    float origin_x{0.0f};
    float range{96.0f};
    int dir{1};
    bool alive{true};
};

enum class GameState { Playing, Dead, LevelComplete, Won };

constexpr int kNumLevels = 3;
const char* level_path(int idx) {
    switch (idx) {
    case 1:
        return "assets/maps/level1.tmj";
    case 2:
        return "assets/maps/level2.tmj";
    case 3:
        return "assets/maps/level3.tmj";
    }
    return "assets/maps/level1.tmj";
}

class Platformer final : public vaxelis::Application {
  public:
    using Application::Application;

  protected:
    void on_init() override {
        assets_.init(device(), &watcher_);
        register_procedural("atlas", 64, 64, make_atlas());
        register_procedural("white", 8, 8, make_solid(8, 8, 255, 255, 255));
        register_procedural("enemy", 8, 8, make_solid(8, 8, 230, 70, 90));
        register_procedural("goal", 8, 8, make_solid(8, 8, 250, 230, 80));

        if (!batch_.init(device())) {
            VX_ERROR("SpriteBatch init failed");
            return;
        }

        input().bind_action("move_left", {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});
        input().bind_action("move_right", {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT});
        input().bind_action("jump", {SDL_SCANCODE_SPACE, SDL_SCANCODE_W, SDL_SCANCODE_UP});
        input().bind_action("restart", SDL_SCANCODE_R);
        input().bind_action("advance", SDL_SCANCODE_RETURN);

        // Load the SFX cues. Keys match the names passed to play_cue(); a
        // missing/failed file just yields an invalid handle that play() ignores.
        for (const char* name : {"jump", "squash", "level-complete", "death", "win-game"}) {
            cues_[name] = audio().load("assets/audio/" + std::string(name) + ".wav");
        }

        load_level(1);
        VX_INFO("Platformer: ready");
    }

    void on_update(float dt) override {
        watcher_.tick(dt);
        if (player_ != entt::null) {
            const auto& t = scene_.registry().get<vaxelis::Transform2D>(player_);
            const float follow = 1.0f - std::exp(-8.0f * dt);
            camera_.position += (t.position - camera_.position) * follow;
        }
        camera_.apply_bounds(width(), height());
    }

    void on_fixed_update(float dt) override {
        // Global controls that work in any state.
        if (input().pressed("restart")) {
            load_level(current_level_);
            return;
        }
        if (state_ == GameState::LevelComplete && input().pressed("advance")) {
            load_level(current_level_ + 1);
            return;
        }
        if (state_ == GameState::Won && input().pressed("advance")) {
            current_level_ = 1;
            load_level(1);
            return;
        }
        if (state_ != GameState::Playing) {
            physics_.step(dt); // keep world ticking so visuals don't freeze weirdly
            physics_.sync_to_scene(scene_);
            return;
        }

        drive_player(dt);
        update_enemies(dt);
        physics_.sync_to_scene(scene_);
        physics_.step(dt);
        physics_.sync_to_scene(scene_);
        check_triggers();
    }

    void on_render() override {
        batch_.begin(device(), camera_.projection(width(), height()));
        vaxelis::tiled::render(map_, batch_);
        scene_.render_sprites(batch_);
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("HUD");
        ImGui::Text("Level %d / %d", current_level_, kNumLevels);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Move: A/D or arrows  -  Jump: Space  -  Restart: R");
        ImGui::End();

        // Centered overlay for transient states. Modal with manual positioning
        // so we don't depend on docking.
        if (state_ != GameState::Playing) {
            const ImVec2 vp = ImGui::GetMainViewport()->Size;
            ImGui::SetNextWindowPos({vp.x * 0.5f, vp.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::Begin("##overlay", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
            switch (state_) {
            case GameState::Dead:
                ImGui::Text("You died.");
                ImGui::Text("Press R to restart.");
                break;
            case GameState::LevelComplete:
                ImGui::Text("Level %d complete!", current_level_);
                ImGui::Text("Press Enter to continue.");
                break;
            case GameState::Won:
                ImGui::Text("You beat all %d levels.", kNumLevels);
                ImGui::Text("Press Enter to play again.");
                break;
            default:
                break;
            }
            ImGui::End();
        }
    }

    void on_shutdown() override {
        tear_down_level();
        physics_.shutdown();
        batch_.shutdown(device());
        assets_.shutdown();
        for (auto& [_, h] : procedural_)
            if (h.valid())
                device().destroy(h);
        procedural_.clear();
    }

  private:
    void register_procedural(const char* key, uint32_t w, uint32_t h,
                             const std::vector<uint8_t>& px) {
        auto handle = device()
                          .create_texture({.width = w,
                                           .height = h,
                                           .format = vaxelis::rhi::TextureFormat::RGBA8,
                                           .initial_data = px.data()})
                          .value_or(vaxelis::rhi::TextureHandle{});
        procedural_[key] = handle;
    }

    vaxelis::rhi::TextureHandle texture_for(const std::string& key) const {
        if (auto it = procedural_.find(key); it != procedural_.end())
            return it->second;
        return assets_.get_texture(key);
    }

    void tear_down_level() {
        physics_.shutdown();
        scene_ = vaxelis::Scene{};
        map_ = vaxelis::TiledMap{};
        hazards_.clear();
        goal_ = {};
        enemies_.clear();
        player_ = entt::null;
    }

    void load_level(int n) {
        if (n > kNumLevels) {
            state_ = GameState::Won;
            play_cue("win-game");
            return;
        }
        tear_down_level();
        current_level_ = n;
        state_ = GameState::Playing;

        physics_.init({.gravity = {0.0f, 1400.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4});
        physics_.register_with(scene_);

        if (!vaxelis::tiled::load_file(
                map_, level_path(n), [this](const std::string& img) { return texture_for(img); })) {
            VX_ERROR("Could not load {}", level_path(n));
            return;
        }

        build_static_colliders();
        build_entities_from_objects();
        resolve_textures();

        camera_.bounds_min = {0.0f, 0.0f};
        camera_.bounds_max = map_.world_size();
        camera_.zoom = 1.0f;
        if (player_ != entt::null) {
            camera_.position = scene_.registry().get<vaxelis::Transform2D>(player_).position;
        }
        VX_INFO("Loaded level {}", n);
    }

    void build_static_colliders() {
        for (const auto& layer : map_.tile_layers) {
            if (layer.name != "world")
                continue;
            for (int y = 0; y < layer.height; ++y) {
                for (int x = 0; x < layer.width; ++x) {
                    const uint32_t gid = layer.gids[static_cast<size_t>(y) * layer.width + x];
                    if (gid == 0)
                        continue;
                    auto e = scene_.create_node("Tile");
                    auto& tr = scene_.registry().get<vaxelis::Transform2D>(e);
                    tr.position = {(x + 0.5f) * map_.tile_w, (y + 0.5f) * map_.tile_h};
                    scene_.registry().emplace<vaxelis::RigidBody2D>(e).type =
                        vaxelis::BodyType::Static;
                    scene_.registry().emplace<vaxelis::BoxCollider2D>(e).half_extents = {
                        map_.tile_w * 0.5f, map_.tile_h * 0.5f};
                }
            }
        }
    }

    void build_entities_from_objects() {
        for (const auto& g : map_.object_groups) {
            for (const auto& obj : g.objects) {
                if (obj.type == "spawn" || obj.name == "player") {
                    spawn_player(obj.pos);
                } else if (obj.type == "enemy") {
                    spawn_enemy(obj.pos);
                } else if (obj.type == "hazard") {
                    hazards_.push_back({obj.pos, obj.pos + obj.size});
                } else if (obj.type == "goal") {
                    goal_ = {obj.pos, obj.pos + obj.size};
                    auto e = scene_.create_node("Goal");
                    scene_.registry().get<vaxelis::Transform2D>(e).position =
                        (obj.pos + obj.size) * 0.5f;
                    auto& s = scene_.registry().emplace<vaxelis::SpriteComponent>(e);
                    s.texture_key = "goal";
                    s.size = obj.size;
                    s.color = {1.0f, 1.0f, 0.4f, 1.0f};
                    s.z_order = 5;
                }
            }
        }
    }

    void spawn_player(vaxelis::vec2 pos) {
        player_ = scene_.create_node("Player");
        scene_.registry().get<vaxelis::Transform2D>(player_).position = pos;
        auto& s = scene_.registry().emplace<vaxelis::SpriteComponent>(player_);
        s.texture_key = "white";
        s.size = {28.0f, 44.0f};
        s.color = {0.95f, 0.3f, 0.25f, 1.0f};
        s.z_order = 10;
        auto& rb = scene_.registry().emplace<vaxelis::RigidBody2D>(player_);
        rb.fixed_rotation = true;
        rb.linear_damping = 0.5f;
        scene_.registry().emplace<vaxelis::BoxCollider2D>(player_).half_extents = {14.0f, 22.0f};
    }

    void spawn_enemy(vaxelis::vec2 pos) {
        auto e = scene_.create_node("Enemy");
        scene_.registry().get<vaxelis::Transform2D>(e).position = pos;
        auto& s = scene_.registry().emplace<vaxelis::SpriteComponent>(e);
        s.texture_key = "enemy";
        s.size = {28.0f, 28.0f};
        s.color = {1.0f, 1.0f, 1.0f, 1.0f};
        s.z_order = 8;
        Enemy en;
        en.origin_x = pos.x;
        scene_.registry().emplace<Enemy>(e, en);
        enemies_.push_back(e);
    }

    void resolve_textures() {
        auto view = scene_.registry().view<vaxelis::SpriteComponent>();
        for (auto e : view) {
            auto& s = view.get<vaxelis::SpriteComponent>(e);
            s.texture = texture_for(s.texture_key);
        }
    }

    void drive_player(float dt) {
        if (player_ == entt::null)
            return;
        const auto& rb = scene_.registry().get<vaxelis::RigidBody2D>(player_);
        if (B2_IS_NULL(rb.body))
            return;
        const float ppm = physics_.pixels_per_meter();

        // Set horizontal velocity directly; preserve vertical (gravity-driven).
        const b2Vec2 v = b2Body_GetLinearVelocity(rb.body);
        float vx = 0.0f;
        if (input().down("move_left"))
            vx -= 240.0f / ppm;
        if (input().down("move_right"))
            vx += 240.0f / ppm;
        b2Body_SetLinearVelocity(rb.body, b2Vec2{vx, v.y});

        // Jump: only when close to standing still vertically (rough ground-check).
        if (input().pressed("jump") && std::abs(v.y) < 0.2f) {
            b2Body_ApplyLinearImpulseToCenter(rb.body, b2Vec2{0.0f, -520.0f / ppm}, true);
            play_cue("jump");
        }
        (void) dt;
    }

    void update_enemies(float dt) {
        auto& reg = scene_.registry();
        for (auto e : enemies_) {
            if (!reg.valid(e))
                continue;
            auto& en = reg.get<Enemy>(e);
            if (!en.alive)
                continue;
            auto& tr = reg.get<vaxelis::Transform2D>(e);
            tr.position.x += en.dir * en.speed * dt;
            if (tr.position.x > en.origin_x + en.range) {
                tr.position.x = en.origin_x + en.range;
                en.dir = -1;
            }
            if (tr.position.x < en.origin_x - en.range) {
                tr.position.x = en.origin_x - en.range;
                en.dir = 1;
            }
        }
    }

    AABB sprite_aabb(entt::entity e) const {
        const auto& t = scene_.registry().get<vaxelis::Transform2D>(e);
        const auto& s = scene_.registry().get<vaxelis::SpriteComponent>(e);
        const auto hx = s.size.x * 0.5f, hy = s.size.y * 0.5f;
        return {t.position - vaxelis::vec2{hx, hy}, t.position + vaxelis::vec2{hx, hy}};
    }

    void check_triggers() {
        if (player_ == entt::null)
            return;
        const auto pl = sprite_aabb(player_);

        // Hazards: any overlap kills.
        for (const auto& h : hazards_) {
            if (aabb_overlap(pl.min, pl.max, h.min, h.max)) {
                kill_player();
                return;
            }
        }

        // Enemies: from-above contact squashes; otherwise dies.
        auto& reg = scene_.registry();
        const auto& player_rb = reg.get<vaxelis::RigidBody2D>(player_);
        const b2Vec2 pv =
            B2_IS_NULL(player_rb.body) ? b2Vec2{0, 0} : b2Body_GetLinearVelocity(player_rb.body);
        for (auto e : enemies_) {
            if (!reg.valid(e))
                continue;
            auto& en = reg.get<Enemy>(e);
            if (!en.alive)
                continue;
            const auto eb = sprite_aabb(e);
            if (!aabb_overlap(pl.min, pl.max, eb.min, eb.max))
                continue;

            const bool falling = pv.y > 1.0f;
            const bool above = pl.max.y < (eb.min.y + (eb.max.y - eb.min.y) * 0.4f);
            if (falling && above) {
                en.alive = false;
                reg.get<vaxelis::SpriteComponent>(e).visible = false;
                play_cue("squash");
                // Pop the player up so it doesn't immediately re-collide.
                if (!B2_IS_NULL(player_rb.body)) {
                    b2Body_SetLinearVelocity(player_rb.body, b2Vec2{pv.x, -3.0f});
                }
            } else {
                kill_player();
                return;
            }
        }

        // Goal: any overlap completes the level.
        if (aabb_overlap(pl.min, pl.max, goal_.min, goal_.max)) {
            state_ = GameState::LevelComplete;
            play_cue("level-complete");
        }
    }

    void kill_player() {
        state_ = GameState::Dead;
        play_cue("death");
    }

    // Plays the SFX registered for `name` (loaded in on_init). Still logs the
    // cue so events stay visible in the console; an unmapped/failed cue is a
    // silent no-op.
    void play_cue(const char* name) {
        VX_INFO("[cue] {}", name);
        if (auto it = cues_.find(name); it != cues_.end())
            audio().play(it->second);
    }

    vaxelis::SpriteBatch batch_;
    vaxelis::Scene scene_;
    vaxelis::SceneInspector inspector_; // available but not drawn during gameplay
    vaxelis::Physics2D physics_;
    vaxelis::FileWatcher watcher_;
    vaxelis::AssetCache assets_;
    vaxelis::TiledMap map_;
    vaxelis::Camera2D camera_;

    int current_level_{1};
    GameState state_{GameState::Playing};
    entt::entity player_{entt::null};
    std::vector<entt::entity> enemies_;
    std::vector<AABB> hazards_;
    AABB goal_{};

    std::unordered_map<std::string, vaxelis::rhi::TextureHandle> procedural_;
    std::unordered_map<std::string, vaxelis::SoundHandle> cues_;
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Platformer app({.title = "Vaxelis - Platformer", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
