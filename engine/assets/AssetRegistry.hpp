// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

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
/// `T` is a trivially-copyable handle exposing `bool valid() const`, where
/// invalid means "nothing loaded". Everything type-specific lives in the Ops
/// callbacks, so a new asset type is a registry plus three lambdas.
///
/// Keys default to the path, so one path loads once. An explicit key lets
/// several assets share a file; adopt() binds a key to an asset with no file.
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
        m_ops = std::move(ops);
        m_watcher = watcher;
    }

    /// Destroys every owned asset and clears the callbacks. Assets outlive the
    /// registry if this is never called, so call it while the owning subsystem
    /// (device, audio engine) is still alive.
    void shutdown() {
        if (m_ops.destroy) {
            for (auto& [_, e] : m_entries) {
                if (e.value.valid())
                    m_ops.destroy(e.value);
            }
        }
        m_entries.clear();
        m_listeners.clear();
        m_ops = {};
        m_watcher = nullptr;
    }

    /// Loads `path` once; later calls with the same key return the cached asset.
    /// `key` defaults to `path`.
    T load(std::string_view path, std::string_view key = {}) {
        const std::string_view k = key.empty() ? path : key;
        if (auto it = m_entries.find(k); it != m_entries.end())
            return it->second.value;
        if (!m_ops.load)
            return T{};

        const std::string p(path);
        const T value = m_ops.load(p);
        if (!value.valid())
            return T{};
        m_entries.emplace(std::string(k), Entry{p, value});
        watch(std::string(k), p);
        return value;
    }

    /// Binds `key` to an asset created elsewhere. The registry owns it from here
    /// and destroys it at shutdown like any other; whatever `key` held before is
    /// destroyed now. Adopted assets have no path, so they never hot-reload.
    T adopt(std::string_view key, T value) {
        auto it = m_entries.find(key);
        if (it == m_entries.end()) {
            m_entries.emplace(std::string(key), Entry{std::string{}, value});
            return value;
        }
        if (m_ops.destroy && it->second.value.valid())
            m_ops.destroy(it->second.value);
        it->second = Entry{std::string{}, value};
        return value;
    }

    /// Asset bound to `key`, or an invalid value when unknown. Never loads.
    T get(std::string_view key) const {
        auto it = m_entries.find(key);
        return it != m_entries.end() ? it->second.value : T{};
    }

    /// True when `key` is bound.
    bool contains(std::string_view key) const { return m_entries.find(key) != m_entries.end(); }

    /// Number of bound keys.
    size_t size() const { return m_entries.size(); }

    /// Re-runs the reload hook for `key`; the watcher calls this for you. No-op
    /// for keys that are unknown or have no file. Listeners fire only when the
    /// hook reports that the value changed.
    void reload(std::string_view key) {
        auto it = m_entries.find(key);
        if (it == m_entries.end() || it->second.path.empty() || !m_ops.reload)
            return;
        if (!m_ops.reload(it->second.path, it->second.value))
            return;
        for (auto& l : m_listeners)
            l(it->first, it->second.value);
    }

    /// Registers a reload listener; see ReloadListener.
    void add_listener(ReloadListener l) { m_listeners.push_back(std::move(l)); }

  private:
    struct Entry {
        std::string path; ///< empty once adopt()ed: nothing to reload from
        T value{};
    };

    void watch(std::string key, const std::string& path) {
        if (!m_watcher || path.empty())
            return;
        m_watcher->watch(path, [this, k = std::move(key)](const std::string&) { reload(k); });
    }

    StringMap<Entry> m_entries;
    std::vector<ReloadListener> m_listeners;
    Ops m_ops;
    FileWatcher* m_watcher{nullptr};
};

} // namespace vaxelis
