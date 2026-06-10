#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/StringMap.hpp"
#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

class FileWatcher;

/// Texture cache keyed by string id (usually the file path). Reloads keep the
/// public handle valid: when dimensions are unchanged the pixels are uploaded
/// in place (handle and GPU object stay stable, no listener churn); when they
/// differ we destroy + recreate under the same key and notify listeners so they
/// can rebind dependent state.
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

    /// Force-reload a texture (also invoked automatically by the watcher).
    void reload_texture(std::string_view key);

    /// Registers a reload listener; see ReloadListener.
    void add_listener(ReloadListener l) { listeners_.push_back(std::move(l)); }

  private:
    struct TexEntry {
        std::string path;
        rhi::TextureHandle handle{};
        uint32_t width{0};
        uint32_t height{0};
    };

    rhi::IDevice* device_{nullptr};
    FileWatcher* watcher_{nullptr};
    StringMap<TexEntry> textures_;
    std::vector<ReloadListener> listeners_;
};

} // namespace vaxelis
