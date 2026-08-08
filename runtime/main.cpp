#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include "engine/assets/AssetCache.hpp"
#include "engine/assets/AssetRegistry.hpp"
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

// 64x64 RGBA tile atlas, gids 1..4.
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
        m_assets.init(device(), &m_watcher);
        register_procedural("atlas", 64, 64, make_atlas());
        register_procedural("white", 8, 8, make_solid(8, 8, 255, 255, 255));
        register_procedural("enemy", 8, 8, make_solid(8, 8, 230, 70, 90));
        register_procedural("goal", 8, 8, make_solid(8, 8, 250, 230, 80));

        if (!m_batch.init(device())) {
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
        // Sharing the level watcher means edited .wav files reload like textures.
        vaxelis::AssetRegistry<vaxelis::SoundHandle>::Ops ops;
        ops.load = [this](const std::string& p) { return audio().load(p); };
        ops.reload = [this](const std::string& p, vaxelis::SoundHandle& h) {
            const auto fresh = audio().load(p);
            if (!fresh.valid())
                return false;
            audio().unload(h);
            h = fresh;
            return true;
        };
        ops.destroy = [this](vaxelis::SoundHandle h) { audio().unload(h); };
        m_cues.init(std::move(ops), &m_watcher);
        for (const char* name : {"jump", "squash", "level-complete", "death", "win-game"}) {
            m_cues.load("assets/audio/" + std::string(name) + ".wav", name);
        }

        load_level(1);
        VX_INFO("Platformer: ready");
    }

    void on_update(float dt) override {
        m_watcher.tick(dt);
        if (m_player != entt::null) {
            const auto& t = m_scene.registry().get<vaxelis::Transform2D>(m_player);
            const float follow = 1.0f - std::exp(-8.0f * dt);
            m_camera.position += (t.position - m_camera.position) * follow;
        }
        // Levels are single-screen; zoom so the level fills the framebuffer,
        // which tracks the window (and on web, the browser viewport).
        const vaxelis::vec2 world = m_map.world_size();
        if (world.x > 0.0f && world.y > 0.0f) {
            m_camera.zoom = std::min(static_cast<float>(width()) / world.x,
                                     static_cast<float>(height()) / world.y);
        }
        m_camera.apply_bounds(width(), height());
    }

    void on_fixed_update(float dt) override {
        // Global controls that work in any state.
        if (input().pressed("restart")) {
            load_level(m_current_level);
            return;
        }
        if (m_state == GameState::LevelComplete && input().pressed("advance")) {
            load_level(m_current_level + 1);
            return;
        }
        if (m_state == GameState::Won && input().pressed("advance")) {
            m_current_level = 1;
            load_level(1);
            return;
        }
        if (m_state != GameState::Playing) {
            m_physics.step(dt); // keep world ticking so visuals don't freeze weirdly
            m_physics.sync_to_scene(m_scene);
            return;
        }

        drive_player(dt);
        update_enemies(dt);
        m_physics.sync_to_scene(m_scene);
        m_physics.step(dt);
        m_physics.sync_to_scene(m_scene);
        check_triggers();
    }

    void on_render() override {
        m_batch.begin(device(), m_camera.projection(width(), height()));
        vaxelis::tiled::render(m_map, m_batch, m_camera.visible_bounds(width(), height()));
        m_scene.render_sprites(m_batch);
        m_batch.end();
    }

    void on_imgui() override {
        ImGui::Begin("HUD");
        ImGui::Text("Level %d / %d", m_current_level, kNumLevels);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Move: A/D or arrows  -  Jump: Space  -  Restart: R");
        ImGui::End();

        // Centered overlay for transient states. Modal with manual positioning
        // so we don't depend on docking.
        if (m_state != GameState::Playing) {
            const ImVec2 vp = ImGui::GetMainViewport()->Size;
            ImGui::SetNextWindowPos({vp.x * 0.5f, vp.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::Begin("##overlay", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
            switch (m_state) {
            case GameState::Dead:
                ImGui::Text("You died.");
                ImGui::Text("Press R to restart.");
                break;
            case GameState::LevelComplete:
                ImGui::Text("Level %d complete!", m_current_level);
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
        m_physics.shutdown();
        m_batch.shutdown(device());
        m_cues.shutdown();
        m_assets.shutdown();
    }

  private:
    // Uploads pixels we generated ourselves and hands the texture to the cache,
    // which owns it from here (no separate teardown, one lookup path).
    void register_procedural(const char* key, uint32_t w, uint32_t h,
                             const std::vector<uint8_t>& px) {
        auto handle = device()
                          .create_texture({.width = w,
                                           .height = h,
                                           .format = vaxelis::rhi::TextureFormat::RGBA8,
                                           .initial_data = px.data()})
                          .value_or(vaxelis::rhi::TextureHandle{});
        m_assets.adopt_texture(key, handle);
    }

    vaxelis::rhi::TextureHandle texture_for(const std::string& key) const {
        return m_assets.get_texture(key);
    }

    void tear_down_level() {
        m_physics.shutdown();
        m_scene = vaxelis::Scene{};
        m_map = vaxelis::TiledMap{};
        m_hazards.clear();
        m_goal = {};
        m_enemies.clear();
        m_player = entt::null;
    }

    void load_level(int n) {
        if (n > kNumLevels) {
            m_state = GameState::Won;
            play_cue("win-game");
            return;
        }
        tear_down_level();
        m_current_level = n;
        m_state = GameState::Playing;

        m_physics.init({.gravity = {0.0f, 1400.0f}, .pixels_per_meter = 100.0f, .sub_steps = 4});
        m_physics.register_with(m_scene);

        const auto resolve = [this](const std::string& img) { return texture_for(img); };
        if (!vaxelis::tiled::load_file(m_map, level_path(n), resolve)) {
            VX_ERROR("Could not load {}", level_path(n));
            return;
        }

        build_static_colliders();
        build_entities_from_objects();
        resolve_textures();

        m_camera.bounds_min = {0.0f, 0.0f};
        m_camera.bounds_max = m_map.world_size();
        m_camera.zoom = 1.0f;
        if (m_player != entt::null) {
            m_camera.position = m_scene.registry().get<vaxelis::Transform2D>(m_player).position;
        }
        VX_INFO("Loaded level {}", n);
    }

    void build_static_colliders() {
        for (const auto& layer : m_map.tile_layers) {
            if (layer.name != "world")
                continue;
            for (int y = 0; y < layer.height; ++y) {
                for (int x = 0; x < layer.width; ++x) {
                    const uint32_t gid = layer.gids[static_cast<size_t>(y) * layer.width + x];
                    if (gid == 0)
                        continue;
                    auto e = m_scene.create_node("Tile");
                    auto& tr = m_scene.registry().get<vaxelis::Transform2D>(e);
                    tr.position = {(x + 0.5f) * m_map.tile_w, (y + 0.5f) * m_map.tile_h};
                    m_scene.registry().emplace<vaxelis::RigidBody2D>(e).type =
                        vaxelis::BodyType::Static;
                    m_scene.registry().emplace<vaxelis::BoxCollider2D>(e).half_extents = {
                        m_map.tile_w * 0.5f, m_map.tile_h * 0.5f};
                }
            }
        }
    }

    void build_entities_from_objects() {
        for (const auto& g : m_map.object_groups) {
            for (const auto& obj : g.objects) {
                if (obj.type == "spawn" || obj.name == "player") {
                    spawn_player(obj.pos);
                } else if (obj.type == "enemy") {
                    spawn_enemy(obj.pos);
                } else if (obj.type == "hazard") {
                    m_hazards.push_back({obj.pos, obj.pos + obj.size});
                } else if (obj.type == "goal") {
                    m_goal = {obj.pos, obj.pos + obj.size};
                    auto e = m_scene.create_node("Goal");
                    m_scene.registry().get<vaxelis::Transform2D>(e).position =
                        (obj.pos + obj.size) * 0.5f;
                    auto& s = m_scene.registry().emplace<vaxelis::SpriteComponent>(e);
                    s.texture_key = "goal";
                    s.size = obj.size;
                    s.color = {1.0f, 1.0f, 0.4f, 1.0f};
                    s.z_order = 5;
                }
            }
        }
    }

    void spawn_player(vaxelis::vec2 pos) {
        m_player = m_scene.create_node("Player");
        m_scene.registry().get<vaxelis::Transform2D>(m_player).position = pos;
        auto& s = m_scene.registry().emplace<vaxelis::SpriteComponent>(m_player);
        s.texture_key = "white";
        s.size = {28.0f, 44.0f};
        s.color = {0.95f, 0.3f, 0.25f, 1.0f};
        s.z_order = 10;
        auto& rb = m_scene.registry().emplace<vaxelis::RigidBody2D>(m_player);
        rb.fixed_rotation = true;
        rb.linear_damping = 0.5f;
        m_scene.registry().emplace<vaxelis::BoxCollider2D>(m_player).half_extents = {14.0f, 22.0f};
    }

    void spawn_enemy(vaxelis::vec2 pos) {
        auto e = m_scene.create_node("Enemy");
        m_scene.registry().get<vaxelis::Transform2D>(e).position = pos;
        auto& s = m_scene.registry().emplace<vaxelis::SpriteComponent>(e);
        s.texture_key = "enemy";
        s.size = {28.0f, 28.0f};
        s.color = {1.0f, 1.0f, 1.0f, 1.0f};
        s.z_order = 8;
        Enemy en;
        en.origin_x = pos.x;
        m_scene.registry().emplace<Enemy>(e, en);
        m_enemies.push_back(e);
    }

    void resolve_textures() {
        auto view = m_scene.registry().view<vaxelis::SpriteComponent>();
        for (auto e : view) {
            auto& s = view.get<vaxelis::SpriteComponent>(e);
            s.texture = texture_for(s.texture_key);
        }
    }

    void drive_player(float dt) {
        if (m_player == entt::null)
            return;
        const auto& rb = m_scene.registry().get<vaxelis::RigidBody2D>(m_player);
        if (B2_IS_NULL(rb.body))
            return;
        const float ppm = m_physics.pixels_per_meter();

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
            b2Body_ApplyLinearImpulseToCenter(rb.body, b2Vec2{0.0f, -65.0f / ppm}, true);
            play_cue("jump");
        }
        (void) dt;
    }

    void update_enemies(float dt) {
        auto& reg = m_scene.registry();
        for (auto e : m_enemies) {
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
        const auto& t = m_scene.registry().get<vaxelis::Transform2D>(e);
        const auto& s = m_scene.registry().get<vaxelis::SpriteComponent>(e);
        const auto hx = s.size.x * 0.5f, hy = s.size.y * 0.5f;
        return {t.position - vaxelis::vec2{hx, hy}, t.position + vaxelis::vec2{hx, hy}};
    }

    void check_triggers() {
        if (m_player == entt::null)
            return;
        const auto pl = sprite_aabb(m_player);

        // Hazards: any overlap kills.
        for (const auto& h : m_hazards) {
            if (aabb_overlap(pl.min, pl.max, h.min, h.max)) {
                kill_player();
                return;
            }
        }

        // Enemies: from-above contact squashes; otherwise dies.
        auto& reg = m_scene.registry();
        const auto& player_rb = reg.get<vaxelis::RigidBody2D>(m_player);
        const b2Vec2 pv =
            B2_IS_NULL(player_rb.body) ? b2Vec2{0, 0} : b2Body_GetLinearVelocity(player_rb.body);
        for (auto e : m_enemies) {
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
        if (aabb_overlap(pl.min, pl.max, m_goal.min, m_goal.max)) {
            m_state = GameState::LevelComplete;
            play_cue("level-complete");
        }
    }

    void kill_player() {
        m_state = GameState::Dead;
        play_cue("death");
    }

    // Plays the SFX registered for `name` (loaded in on_init). Still logs the
    // cue so events stay visible in the console; an unmapped/failed cue is a
    // silent no-op.
    void play_cue(const char* name) {
        VX_INFO("[cue] {}", name);
        audio().play(m_cues.get(name));
    }

    vaxelis::SpriteBatch m_batch;
    vaxelis::Scene m_scene;
    vaxelis::SceneInspector m_inspector; // available but not drawn during gameplay
    vaxelis::Physics2D m_physics;
    vaxelis::FileWatcher m_watcher;
    vaxelis::AssetCache m_assets;
    vaxelis::TiledMap m_map;
    vaxelis::Camera2D m_camera;

    int m_current_level{1};
    GameState m_state{GameState::Playing};
    entt::entity m_player{entt::null};
    std::vector<entt::entity> m_enemies;
    std::vector<AABB> m_hazards;
    AABB m_goal{};

    vaxelis::AssetRegistry<vaxelis::SoundHandle> m_cues;
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Platformer app({.title = "Vaxelis - Platformer", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
