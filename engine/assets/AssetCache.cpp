#include "engine/assets/AssetCache.hpp"

#include <cstring>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

#include "engine/assets/FileWatcher.hpp"
#include "engine/core/Log.hpp"

namespace vaxelis {

namespace {

// Returns RGBA8 pixels + dims. Empty on failure.
struct LoadedImage {
    std::vector<uint8_t> px;
    int w{0};
    int h{0};
};

LoadedImage load_rgba8(const std::string& path) {
    LoadedImage out;
    int w = 0, h = 0, n = 0;
    stbi_set_flip_vertically_on_load(0);
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        VX_ERROR("AssetCache: stbi_load failed for {}: {}", path, stbi_failure_reason());
        return out;
    }
    out.w = w; out.h = h;
    out.px.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return out;
}

}  // namespace

bool AssetCache::init(rhi::IDevice& device, FileWatcher* watcher) {
    device_  = &device;
    watcher_ = watcher;
    return true;
}

void AssetCache::shutdown() {
    if (device_) {
        for (auto& [_, t] : textures_) {
            if (t.handle.valid()) device_->destroy(t.handle);
        }
    }
    textures_.clear();
    listeners_.clear();
    device_ = nullptr;
    watcher_ = nullptr;
}

rhi::TextureHandle AssetCache::load_texture(std::string_view path, std::string_view key) {
    if (!device_) return {};
    const std::string k = key.empty() ? std::string(path) : std::string(key);
    if (auto it = textures_.find(k); it != textures_.end()) return it->second.handle;

    auto img = load_rgba8(std::string(path));
    if (img.px.empty()) return {};

    rhi::TextureDesc td{
        .width = static_cast<uint32_t>(img.w),
        .height = static_cast<uint32_t>(img.h),
        .format = rhi::TextureFormat::RGBA8,
        .initial_data = img.px.data(),
    };
    auto tex = device_->create_texture(td);
    if (!tex) return {};

    TexEntry e{std::string(path), *tex};
    auto [it, _] = textures_.emplace(k, std::move(e));
    if (watcher_) {
        std::string key_copy = k;
        watcher_->watch(it->second.path, [this, key_copy](const std::string&) {
            reload_texture(key_copy);
        });
    }
    return it->second.handle;
}

rhi::TextureHandle AssetCache::get_texture(std::string_view key) const {
    auto it = textures_.find(std::string(key));
    return (it != textures_.end()) ? it->second.handle : rhi::TextureHandle{};
}

void AssetCache::reload_texture(std::string_view key) {
    if (!device_) return;
    auto it = textures_.find(std::string(key));
    if (it == textures_.end()) return;

    auto img = load_rgba8(it->second.path);
    if (img.px.empty()) return;

    // No in-place texture update in the RHI yet; destroy + recreate. The
    // listener fan-out lets clients rebind the new handle.
    if (it->second.handle.valid()) device_->destroy(it->second.handle);
    rhi::TextureDesc td{
        .width = static_cast<uint32_t>(img.w),
        .height = static_cast<uint32_t>(img.h),
        .format = rhi::TextureFormat::RGBA8,
        .initial_data = img.px.data(),
    };
    auto tex = device_->create_texture(td);
    if (!tex) { it->second.handle = {}; return; }
    it->second.handle = *tex;
    VX_INFO("AssetCache: reloaded texture '{}'", it->first);
    for (auto& l : listeners_) l(it->first, it->second.handle);
}

}  // namespace vaxelis
