/// @file
/// Scene stress harness: builds a scene of a chosen size and hierarchy depth,
/// then times the three passes a frame actually spends its budget on.
///
///   animate    a flat `view<Transform2D>` sweep -- the sparse set's dense
///              array walked in memory order, the fast path
///   transform  Scene::update_world_transforms(), a recursive DFS over
///              Hierarchy::children with random component access per node
///   render     Scene::render_sprites(), which gathers, sorts by (z, texture)
///              and submits
///   archetype  the same work as `animate`, over ecs::World columns instead
///
/// Two ratios come out of that. `dfs/flat` is the cost of the scene graph:
/// same entities, same count, one walked linearly and one walked as a tree.
/// `arch/flat` is archetype columns against entt's sparse set on identical
/// work, which is the number that says whether migrating storage is worth
/// anything. Drag the sliders until they separate.
///
/// The archetype world mirrors only the entity count and Transform2D -- it has
/// no hierarchy and no sprites -- so `arch/flat` compares storage layout, not
/// engines. Read it as an upper bound on what migrating could buy.
///
/// Note render_sprites() refreshes world transforms itself, so its number
/// includes a second transform pass; subtract `transform` to read the
/// gather/sort/submit cost on its own.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <entt/entt.hpp>
#include <imgui.h>

#include "engine/assets/AssetCache.hpp"
#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/ecs/World.hpp"
#include "engine/renderer/SpriteRenderer.hpp"
#include "engine/scene/Camera2D.hpp"
#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

namespace {

/// u16 indices cap one batch at 16383 quads; past that the batcher flushes
/// again, which is itself worth seeing in the draw-call counter.
constexpr uint32_t kBatchQuads = 16383;
constexpr int kMaxNodes = 20000;
constexpr int kMaxDepth = 8;
/// Frames averaged in the readout, so the numbers hold still enough to read.
constexpr size_t kWindow = 60;

using SteadyClock = std::chrono::steady_clock;

/// Milliseconds elapsed since `t0`.
float ms_since(SteadyClock::time_point t0) {
    return std::chrono::duration<float, std::milli>(SteadyClock::now() - t0).count();
}

/// Fixed-size rolling mean; one slot per frame in the window.
class Rolling {
  public:
    void push(float v) {
        m_samples[m_next] = v;
        m_next = (m_next + 1) % kWindow;
    }

    float mean() const {
        return std::accumulate(m_samples.begin(), m_samples.end(), 0.0f) /
               static_cast<float>(kWindow);
    }

  private:
    std::array<float, kWindow> m_samples{};
    size_t m_next{0};
};

class Stress final : public vaxelis::Application {
  public:
    using Application::Application;

  protected:
    void on_init() override {
        m_assets.init(device());

        constexpr uint32_t white = 0xFFFFFFFFu;
        const auto tex = device()
                             .create_texture({.width = 1,
                                              .height = 1,
                                              .format = vaxelis::rhi::TextureFormat::RGBA8,
                                              .initial_data = &white})
                             .value_or(vaxelis::rhi::TextureHandle{});
        m_texture = m_assets.adopt_texture("dot", tex);

        if (!m_batch.init(device(), kBatchQuads))
            VX_ERROR("SpriteBatch init failed");
        input().bind_action("rebuild", SDL_SCANCODE_R);

        clear_color() = {0.05f, 0.05f, 0.07f, 1.0f};
        rebuild();
    }

    void on_update(float dt) override {
        if (input().pressed("rebuild"))
            rebuild();

        // Phase 1: flat sweep over the sparse set. view::each is entt's fast
        // path -- iterating the view and calling get() per entity would add a
        // sparse->dense lookup each step and quietly slander it.
        const auto t_anim = SteadyClock::now();
        m_scene.registry().view<vaxelis::Transform2D>().each(
            [dt](vaxelis::Transform2D& tr) { tr.rotation += dt; });
        m_anim.push(ms_since(t_anim));

        // Phase 2: the same entities, reached through the tree instead.
        const auto t_tree = SteadyClock::now();
        m_scene.update_world_transforms();
        m_transform.push(ms_since(t_tree));

        // Phase 3: identical work over archetype columns. Same component type
        // and same entity count as phase 1, so the difference is the storage
        // layout and nothing else.
        const auto t_arch = SteadyClock::now();
        m_ecs->each<vaxelis::Transform2D>([dt](vaxelis::Transform2D& tr) { tr.rotation += dt; });
        m_archetype.push(ms_since(t_arch));
    }

    void on_render() override {
        const auto t_render = SteadyClock::now();
        m_batch.begin(device(), m_camera.projection(width(), height()));
        m_scene.render_sprites(m_batch);
        m_batch.end();
        m_render.push(ms_since(t_render));
    }

    void on_imgui() override {
        ImGui::Begin("Scene stress");
        ImGui::SliderInt("nodes", &m_wanted_nodes, 100, kMaxNodes);
        ImGui::SliderInt("depth", &m_wanted_depth, 1, kMaxDepth);
        const bool pending = m_wanted_nodes != m_nodes || m_wanted_depth != m_depth;
        if (ImGui::Button("Rebuild"))
            rebuild();
        if (pending) {
            ImGui::SameLine();
            ImGui::TextUnformatted("<- pending");
        }

        ImGui::Separator();
        ImGui::Text("nodes      %d over %d levels", m_nodes, m_depth);
        ImGui::Text("build      %.1f ms scene / %.1f ms ecs (one off)",
                    static_cast<double>(m_build_ms), static_cast<double>(m_ecs_build_ms));
        ImGui::Separator();
        ImGui::Text("animate    %.3f ms   flat view<Transform2D>",
                    static_cast<double>(m_anim.mean()));
        ImGui::Text("transform  %.3f ms   hierarchy DFS",
                    static_cast<double>(m_transform.mean()));
        ImGui::Text("render     %.3f ms   gather + sort + submit",
                    static_cast<double>(m_render.mean()));
        ImGui::Text("archetype  %.3f ms   ecs::World each<Transform2D>",
                    static_cast<double>(m_archetype.mean()));
        ImGui::Separator();
        ImGui::Text("dfs/flat   %.2fx     scene graph vs sparse-set sweep",
                    static_cast<double>(ratio(m_transform, m_anim)));
        ImGui::Text("arch/flat  %.2fx     archetype vs sparse set (<1 wins)",
                    static_cast<double>(ratio(m_archetype, m_anim)));
        ImGui::Separator();
        ImGui::Text("quads      %u in %u draw calls", m_batch.quads(), m_batch.draw_calls());
        ImGui::Text("FPS        %.1f", static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::Text("R rebuilds");
        ImGui::End();
    }

    void on_shutdown() override {
        m_batch.shutdown(device());
        m_assets.shutdown();
    }

  private:
    /// `lhs` relative to `rhs`; 0 until the rolling window has data.
    static float ratio(const Rolling& lhs, const Rolling& rhs) {
        const float base = rhs.mean();
        return base > 0.0f ? lhs.mean() / base : 0.0f;
    }

    /// Rebuilds the scene as a tree `depth` levels deep, spreading the node
    /// budget evenly across levels and parenting each level round-robin onto
    /// the one above, so the DFS has real branching to walk.
    void rebuild() {
        m_nodes = m_wanted_nodes;
        m_depth = m_wanted_depth;

        const auto t0 = SteadyClock::now();
        m_scene = vaxelis::Scene{};
        auto& reg = m_scene.registry();

        const int per_level = std::max(1, m_nodes / m_depth);
        const float span = std::sqrt(static_cast<float>(per_level)) + 1.0f;
        const float spacing = 14.0f;

        std::vector<entt::entity> prev{m_scene.root()};
        std::vector<entt::entity> cur;
        cur.reserve(static_cast<size_t>(per_level));

        int made = 0;
        for (int level = 0; level < m_depth && made < m_nodes; ++level) {
            cur.clear();
            for (int i = 0; i < per_level && made < m_nodes; ++i, ++made) {
                const entt::entity parent = prev[static_cast<size_t>(i) % prev.size()];
                const entt::entity e = m_scene.create_node("n", parent);

                auto& tr = reg.get<vaxelis::Transform2D>(e);
                if (level == 0) {
                    // Level 0 lays out the grid; deeper levels are small local
                    // offsets, so each subtree renders as a visible cluster.
                    const auto col = static_cast<float>(i % static_cast<int>(span));
                    const auto row = static_cast<float>(i / static_cast<int>(span));
                    tr.position = {(col - span * 0.5f) * spacing,
                                   (row - span * 0.5f) * spacing};
                } else {
                    tr.position = {6.0f, 0.0f};
                    tr.scale = {0.96f, 0.96f};
                }

                auto& sprite = reg.emplace<vaxelis::SpriteComponent>(e);
                sprite.texture = m_texture;
                sprite.size = {3.0f, 3.0f};
                const float shade = static_cast<float>(level) / static_cast<float>(kMaxDepth);
                sprite.color = {1.0f - shade * 0.6f, 0.5f + shade * 0.4f, 0.4f + shade * 0.6f,
                                1.0f};
                cur.push_back(e);
            }
            prev = cur;
        }

        m_build_ms = ms_since(t0);

        // Mirror the node count into an archetype world. One add() per entity
        // means one migration each, empty archetype -> {Transform2D}.
        const auto t_ecs = SteadyClock::now();
        m_ecs = std::make_unique<vaxelis::ecs::World>();
        for (int i = 0; i < m_nodes; ++i)
            m_ecs->add<vaxelis::Transform2D>(m_ecs->create());
        m_ecs_build_ms = ms_since(t_ecs);
        m_camera.position = {0.0f, 0.0f};
        m_camera.zoom = 640.0f / (span * spacing + 1.0f);
        m_camera.zoom = std::clamp(m_camera.zoom, 0.05f, 4.0f);
        VX_INFO("stress: {} nodes over {} levels in {:.1f} ms", m_nodes, m_depth, m_build_ms);
    }

    vaxelis::SpriteBatch m_batch;
    vaxelis::AssetCache m_assets;
    vaxelis::Scene m_scene;
    vaxelis::Camera2D m_camera;
    vaxelis::rhi::TextureHandle m_texture{};

    /// Same entity count as the scene, holding Transform2D only, so phase 3
    /// measures column iteration rather than scene-graph bookkeeping. Held by
    /// pointer because ecs::World is neither copyable nor movable.
    std::unique_ptr<vaxelis::ecs::World> m_ecs;

    Rolling m_anim;
    Rolling m_transform;
    Rolling m_render;
    Rolling m_archetype;
    float m_build_ms{0.0f};
    float m_ecs_build_ms{0.0f};

    int m_wanted_nodes{2000};
    int m_wanted_depth{4};
    int m_nodes{0};
    int m_depth{0};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    Stress app({.title = "Vaxelis - Scene stress", .width = 1280, .height = 720});
    if (!app.init())
        return 1;
    return app.run();
}
