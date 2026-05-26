#include <array>
#include <cstdint>
#include <vector>

#include <imgui.h>

#include "engine/core/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/renderer/SpriteRenderer.hpp"

namespace {

// Build a tiny procedural checkerboard so M1 has no on-disk asset dependency.
// stb_image is linked into the engine for later milestones.
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

class HelloSprite final : public vaxelis::Application {
public:
    using Application::Application;

protected:
    void on_init() override {
        auto px = make_checkerboard(128, 128, 16);
        vaxelis::rhi::TextureDesc td{
            .width = 128, .height = 128,
            .format = vaxelis::rhi::TextureFormat::RGBA8,
            .initial_data = px.data(),
        };
        auto tex = device().create_texture(td);
        if (!tex) {
            VX_ERROR("texture create failed: {}", vaxelis::rhi::to_string(tex.error()));
            return;
        }
        texture_ = *tex;
        if (!sprite_.init(device())) {
            VX_ERROR("SpriteRenderer init failed");
            return;
        }
        VX_INFO("HelloSprite: ready");
    }

    void on_render() override {
        vaxelis::vec2 center{width() * 0.5f, height() * 0.5f};
        sprite_.draw(device(), texture_, center, sprite_size_, width(), height());
    }

    void on_imgui() override {
        ImGui::Begin("Vaxelis");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::ColorEdit3("Clear color", &clear_color().x);
        ImGui::SliderFloat("Sprite size", &sprite_size_.x, 32.0f, 1024.0f);
        sprite_size_.y = sprite_size_.x;
        ImGui::End();
    }

    void on_shutdown() override {
        sprite_.shutdown(device());
        if (texture_.valid()) device().destroy(texture_);
    }

private:
    vaxelis::SpriteRenderer sprite_;
    vaxelis::rhi::TextureHandle texture_{};
    vaxelis::vec2 sprite_size_{256.0f, 256.0f};
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    HelloSprite app({.title = "Vaxelis - Hello Sprite", .width = 1280, .height = 720});
    if (!app.init()) return 1;
    return app.run();
}
