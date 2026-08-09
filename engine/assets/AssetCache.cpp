// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/assets/AssetCache.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

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
    out.w = w;
    out.h = h;
    out.px.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return out;
}

} // namespace

bool AssetCache::init(rhi::IDevice& device, FileWatcher* watcher) {
    m_device = &device;
    AssetRegistry<TextureAsset>::Ops ops;
    ops.load = [this](const std::string& path) { return create(path); };
    ops.reload = [this](const std::string& path, TextureAsset& tex) { return refresh(path, tex); };
    ops.destroy = [this](TextureAsset tex) { m_device->destroy(tex.handle); };
    m_textures.init(std::move(ops), watcher);
    return true;
}

void AssetCache::shutdown() {
    m_textures.shutdown();
    m_device = nullptr;
}

rhi::TextureHandle AssetCache::load_texture(std::string_view path, std::string_view key) {
    return m_textures.load(path, key).handle;
}

rhi::TextureHandle AssetCache::get_texture(std::string_view key) const {
    return m_textures.get(key).handle;
}

rhi::TextureHandle AssetCache::adopt_texture(std::string_view key, rhi::TextureHandle handle) {
    return m_textures.adopt(key, TextureAsset{.handle = handle}).handle;
}

void AssetCache::reload_texture(std::string_view key) {
    m_textures.reload(key);
}

void AssetCache::add_listener(ReloadListener l) {
    m_textures.add_listener(
        [cb = std::move(l)](std::string_view key, TextureAsset tex) { cb(key, tex.handle); });
}

TextureAsset AssetCache::create(const std::string& path) {
    if (!m_device)
        return {};
    auto img = load_rgba8(path);
    if (img.px.empty())
        return {};

    const auto w = static_cast<uint32_t>(img.w);
    const auto h = static_cast<uint32_t>(img.h);
    rhi::TextureDesc td{
        .width = w,
        .height = h,
        .format = rhi::TextureFormat::RGBA8,
        .initial_data = img.px.data(),
    };
    auto tex = m_device->create_texture(td);
    if (!tex)
        return {};
    return TextureAsset{.handle = *tex, .width = w, .height = h};
}

bool AssetCache::refresh(const std::string& path, TextureAsset& tex) {
    if (!m_device)
        return false;
    auto img = load_rgba8(path);
    if (img.px.empty())
        return false;

    const auto w = static_cast<uint32_t>(img.w);
    const auto h = static_cast<uint32_t>(img.h);

    // Fast path: same dimensions; upload in place so the handle and GPU object
    // stay stable. Dependents (e.g. SpriteComponent ids) need no rebinding and
    // listeners are not disturbed.
    if (tex.handle.valid() && w == tex.width && h == tex.height) {
        m_device->update_texture(tex.handle, rhi::TextureUpdate{
                                                 .x = 0,
                                                 .y = 0,
                                                 .width = w,
                                                 .height = h,
                                                 .data = img.px.data(),
                                             });
        VX_INFO("AssetCache: reloaded texture '{}' in place", path);
        return false;
    }

    // Dimensions changed (or nothing was loaded before): recreate. Reporting the
    // change is what makes the registry fan out to listeners, so clients rebind
    // the new handle -- including when creation failed and it is now null.
    if (tex.handle.valid())
        m_device->destroy(tex.handle);
    rhi::TextureDesc td{
        .width = w,
        .height = h,
        .format = rhi::TextureFormat::RGBA8,
        .initial_data = img.px.data(),
    };
    auto fresh = m_device->create_texture(td);
    tex = fresh ? TextureAsset{.handle = *fresh, .width = w, .height = h} : TextureAsset{};
    VX_INFO("AssetCache: reloaded texture '{}'", path);
    return true;
}

} // namespace vaxelis
