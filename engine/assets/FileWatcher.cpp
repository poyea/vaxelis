// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/assets/FileWatcher.hpp"

#include <system_error>

namespace vaxelis {

void FileWatcher::watch(std::string path, Callback cb) {
    m_entries[path] = Entry{std::move(cb), {}, false};
}

void FileWatcher::unwatch(const std::string& path) {
    m_entries.erase(path);
}

int FileWatcher::tick(float dt) {
    m_accumulator += dt;
    if (m_accumulator < m_interval)
        return 0;
    m_accumulator = 0.0f;

    int changed = 0;
    for (auto& [path, e] : m_entries) {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(path, ec);
        if (ec)
            continue; // file missing; ignore until it reappears
        if (!e.seen) {
            e.mtime = t;
            e.seen = true;
            continue;
        }
        if (t != e.mtime) {
            e.mtime = t;
            if (e.cb)
                e.cb(path);
            ++changed;
        }
    }
    return changed;
}

} // namespace vaxelis
