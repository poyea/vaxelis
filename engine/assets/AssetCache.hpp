#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "engine/rhi/Rhi.hpp"

namespace vaxelis {

class FileWatcher;

// Texture cache keyed by string id (usually the file path). Reloads are
// in-place from the cache's perspective — the public handle stays valid
// across reloads because we destroy the old GPU texture and replace it under
// the same key. Listeners get notified so they can refresh dependent state.
class AssetCache {
public:
    using ReloadListener = std::function<void(std::string_view key, rhi::TextureHandle)>;

    bool init(rhi::IDevice& device, FileWatcher* watcher = nullptr);
    void shutdown();

    // Loads or returns cached. `key` defaults to `path` if empty.
    rhi::TextureHandle load_texture(std::string_view path, std::string_view key = {});

    // Lookup without loading.
    rhi::TextureHandle get_texture(std::string_view key) const;

    // Force-reload a texture (also invoked automatically by the watcher).
    void reload_texture(std::string_view key);

    void add_listener(ReloadListener l) { listeners_.push_back(std::move(l)); }

private:
    struct TexEntry {
        std::string        path;
        rhi::TextureHandle handle{};
    };

    rhi::IDevice*  device_{nullptr};
    FileWatcher*   watcher_{nullptr};
    std::unordered_map<std::string, TexEntry> textures_;
    std::vector<ReloadListener> listeners_;
};

}  // namespace vaxelis
