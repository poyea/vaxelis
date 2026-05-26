#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

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

class M2Demo final : public vaxelis::Application {
public:
    using Application::Application;

protected:
    void on_init() override {
        auto tex_from = [&](const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
            return device().create_texture({.width = w, .height = h,
                                            .format = vaxelis::rhi::TextureFormat::RGBA8,
                                            .initial_data = px.data()}).value_or(vaxelis::rhi::TextureHandle{});
        };

        auto checker = make_checkerboard(128, 128, 16);
        auto white   = make_solid(8, 8, 255, 255, 255);
        bg_tex_      = tex_from(checker, 128, 128);
        player_tex_  = tex_from(white, 8, 8);

        if (!batch_.init(device())) {
            VX_ERROR("SpriteBatch init failed");
            return;
        }

        // Action bindings: WASD / arrows for movement, Space to "jump" (plays a sound).
        input().bind_action("move_left",  {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});
        input().bind_action("move_right", {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT});
        input().bind_action("move_up",    {SDL_SCANCODE_W, SDL_SCANCODE_UP});
        input().bind_action("move_down",  {SDL_SCANCODE_S, SDL_SCANCODE_DOWN});
        input().bind_action("jump",       SDL_SCANCODE_SPACE);

        player_pos_ = {static_cast<float>(width()) * 0.5f, static_cast<float>(height()) * 0.5f};
        VX_INFO("M2Demo: ready");
    }

    void on_fixed_update(float dt) override {
        vaxelis::vec2 dir{0.0f, 0.0f};
        if (input().down("move_left"))  dir.x -= 1.0f;
        if (input().down("move_right")) dir.x += 1.0f;
        if (input().down("move_up"))    dir.y -= 1.0f;
        if (input().down("move_down"))  dir.y += 1.0f;
        if (dir.x != 0.0f || dir.y != 0.0f) dir = glm::normalize(dir);
        constexpr float kSpeedPxPerSec = 320.0f;
        player_pos_ += dir * (kSpeedPxPerSec * dt);
        ++fixed_steps_;
    }

    void on_update(float /*dt*/) override {
        if (input().pressed("jump")) {
            VX_INFO("jump pressed");
        }
    }

    void on_render() override {
        batch_.begin(device(), width(), height());
        // Background — tinted checker, full window.
        batch_.draw(bg_tex_,
                    {width() * 0.5f, height() * 0.5f},
                    {static_cast<float>(width()), static_cast<float>(height())},
                    {0.45f, 0.45f, 0.55f, 1.0f});
        // A few sprites to exercise the batch.
        for (int i = 0; i < 32; ++i) {
            float t = static_cast<float>(i) / 32.0f;
            batch_.draw(player_tex_,
                        {120.0f + t * (width() - 240.0f), height() * 0.5f + std::sin(t * 6.28f) * 80.0f},
                        {16.0f, 16.0f},
                        {0.6f + 0.4f * t, 0.8f, 1.0f - 0.5f * t, 1.0f});
        }
        // Player on top.
        batch_.draw(player_tex_, player_pos_, {48.0f, 48.0f}, {1.0f, 0.4f, 0.2f, 1.0f});
        batch_.end();
    }

    void on_imgui() override {
        ImGui::Begin("Vaxelis M2");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Fixed steps: %u", fixed_steps_);
        ImGui::Text("Batch: %u quads / %u draw calls", batch_.quads(), batch_.draw_calls());
        ImGui::Text("Player: (%.1f, %.1f)", player_pos_.x, player_pos_.y);
        ImGui::ColorEdit3("Clear color", &clear_color().x);
        float vol = audio().master_volume();
        if (ImGui::SliderFloat("Master volume", &vol, 0.0f, 1.0f)) {
            audio().set_master_volume(vol);
        }
        ImGui::End();
    }

    void on_shutdown() override {
        batch_.shutdown(device());
        if (player_tex_.valid()) device().destroy(player_tex_);
        if (bg_tex_.valid())     device().destroy(bg_tex_);
    }

private:
    vaxelis::SpriteBatch        batch_;
    vaxelis::rhi::TextureHandle bg_tex_{};
    vaxelis::rhi::TextureHandle player_tex_{};
    vaxelis::vec2               player_pos_{};
    uint32_t                    fixed_steps_{0};
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    M2Demo app({.title = "Vaxelis - M2", .width = 1280, .height = 720});
    if (!app.init()) return 1;
    return app.run();
}
