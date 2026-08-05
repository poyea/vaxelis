#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "engine/assets/FileWatcher.hpp"
#include "engine/core/StringMap.hpp"

namespace vaxelis {

/// Key-to-asset table shared by the per-type asset caches: it dedups loads,
/// registers file watches for hot reload, fans reload notifications out to
/// listeners, and releases everything it owns at shutdown().
///
/// `T` is the cached value: a small trivially-copyable handle, plus whatever
/// metadata the owning cache needs, exposing `bool valid() const` where invalid
/// means "nothing loaded". Everything type-specific lives in the Ops callbacks,
/// so supporting a new asset type is a registry plus three lambdas.
///
/// Keys default to the path, so loading one path twice yields one asset. An
/// explicit key lets several assets share a file, and adopt() binds a key to an
/// asset with no file behind it at all (procedural textures, generated atlases).
///
/// Neither copyable nor movable: watch callbacks capture `this`.
template <class T> class AssetRegistry {
    static_assert(std::is_trivially_copyable_v<T>,
                  "asset values are passed and stored by value; keep them trivially copyable");

  public:
    /// Creates the asset stored at `path`. Returns an invalid value on failure,
    /// which is not cached.
    using Loader = std::function<T(const std::string& path)>;
    /// Refreshes an already-loaded asset. Returns true when `value` changed and
    /// dependents have to rebind, false when it was refreshed in place or the
    /// reload failed and the previous value still stands.
    using Reloader = std::function<bool(const std::string& path, T& value)>;
    /// Releases an asset the registry owns.
    using Deleter = std::function<void(T value)>;
    /// Notified after a reload replaced the asset bound to `key`.
    using ReloadListener = std::function<void(std::string_view key, T value)>;

    /// The type-specific half of a registry. An empty `reload` disables hot
    /// reload for the type; the other two are required to load and to own.
    struct Ops {
        Loader load;
        Reloader reload;
        Deleter destroy;
    };

    AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    /// Installs the callbacks. Without a `watcher` the registry only reloads
    /// when asked to.
    void init(Ops ops, FileWatcher* watcher = nullptr) {
        ops_ = std::move(ops);
        watcher_ = watcher;
    }

    /// Destroys every owned asset and clears the callbacks. Assets outlive the
    /// registry if this is never called, so call it while the owning subsystem
    /// (device, audio engine) is still alive.
    void shutdown() {
        if (ops_.destroy) {
            for (auto& [_, e] : entries_) {
                if (e.value.valid())
                    ops_.destroy(e.value);
            }
        }
        entries_.clear();
        listeners_.clear();
        ops_ = {};
        watcher_ = nullptr;
    }

    /// Loads `path` once; later calls with the same key return the cached asset.
    /// `key` defaults to `path`.
    T load(std::string_view path, std::string_view key = {}) {
        const std::string_view k = key.empty() ? path : key;
        if (auto it = entries_.find(k); it != entries_.end())
            return it->second.value;
        if (!ops_.load)
            return T{};

        const std::string p(path);
        const T value = ops_.load(p);
        if (!value.valid())
            return T{};
        entries_.emplace(std::string(k), Entry{p, value});
        watch(std::string(k), p);
        return value;
    }

    /// Binds `key` to an asset created elsewhere. The registry owns it from here
    /// and destroys it at shutdown like any other; whatever `key` held before is
    /// destroyed now. Adopted assets have no path, so they never hot-reload.
    T adopt(std::string_view key, T value) {
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            entries_.emplace(std::string(key), Entry{std::string{}, value});
            return value;
        }
        if (ops_.destroy && it->second.value.valid())
            ops_.destroy(it->second.value);
        it->second = Entry{std::string{}, value};
        return value;
    }

    /// Asset bound to `key`, or an invalid value when unknown. Never loads.
    T get(std::string_view key) const {
        auto it = entries_.find(key);
        return it != entries_.end() ? it->second.value : T{};
    }

    /// True when `key` is bound.
    bool contains(std::string_view key) const { return entries_.find(key) != entries_.end(); }

    /// Number of bound keys.
    size_t size() const { return entries_.size(); }

    /// Re-runs the reload hook for `key`; the watcher calls this for you. No-op
    /// for keys that are unknown or have no file. Listeners fire only when the
    /// hook reports that the value changed.
    void reload(std::string_view key) {
        auto it = entries_.find(key);
        if (it == entries_.end() || it->second.path.empty() || !ops_.reload)
            return;
        if (!ops_.reload(it->second.path, it->second.value))
            return;
        for (auto& l : listeners_)
            l(it->first, it->second.value);
    }

    /// Registers a reload listener; see ReloadListener.
    void add_listener(ReloadListener l) { listeners_.push_back(std::move(l)); }

  private:
    struct Entry {
        std::string path; ///< empty once adopt()ed: nothing to reload from
        T value{};
    };

    void watch(std::string key, const std::string& path) {
        if (!watcher_ || path.empty())
            return;
        watcher_->watch(path, [this, k = std::move(key)](const std::string&) { reload(k); });
    }

    StringMap<Entry> entries_;
    std::vector<ReloadListener> listeners_;
    Ops ops_;
    FileWatcher* watcher_{nullptr};
};

} // namespace vaxelis
