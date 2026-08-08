#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "engine/assets/AssetRegistry.hpp"
#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

/// A cached texture: the handle plus the dimensions a reload compares against to
/// decide between an in-place upload and a recreate.
struct TextureAsset {
    rhi::TextureHandle handle{};
    uint32_t width{0};
    uint32_t height{0};

    constexpr bool valid() const { return handle.valid(); }
};

/// Texture cache keyed by string id (usually the file path). Reloads keep the
/// public handle valid: when dimensions are unchanged the pixels are uploaded
/// in place (handle and GPU object stay stable, no listener churn); when they
/// differ we destroy + recreate under the same key and notify listeners so they
/// can rebind dependent state.
///
/// The keying, dedup, watch registration and ownership live in AssetRegistry;
/// this is the texture-specific face over it (stb decode, GPU upload).
class AssetCache {
  public:
    /// Callback invoked when a reload recreated the texture under a new handle.
    using ReloadListener = std::function<void(std::string_view key, rhi::TextureHandle)>;

    /// Stores the device (and optional watcher for hot-reload).
    bool init(rhi::IDevice& device, FileWatcher* watcher = nullptr);
    /// Destroys all cached textures and clears listeners.
    void shutdown();

    /// Loads or returns cached. `key` defaults to `path` if empty.
    rhi::TextureHandle load_texture(std::string_view path, std::string_view key = {});

    /// Lookup without loading.
    rhi::TextureHandle get_texture(std::string_view key) const;

    /// Hands a texture created elsewhere (procedural pixels, a generated atlas)
    /// to the cache under `key`. The cache destroys it at shutdown like any
    /// other; with no file behind it, it never hot-reloads.
    rhi::TextureHandle adopt_texture(std::string_view key, rhi::TextureHandle);

    /// Force-reload a texture (also invoked automatically by the watcher).
    void reload_texture(std::string_view key);

    /// Registers a reload listener; see ReloadListener.
    void add_listener(ReloadListener l);

  private:
    TextureAsset create(const std::string& path);
    bool refresh(const std::string& path, TextureAsset& tex);

    rhi::IDevice* m_device{nullptr};
    AssetRegistry<TextureAsset> m_textures;
};

} // namespace vaxelis
