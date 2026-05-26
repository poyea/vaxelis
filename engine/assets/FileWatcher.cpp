#include "engine/assets/FileWatcher.hpp"

#include <system_error>

namespace vaxelis {

void FileWatcher::watch(std::string path, Callback cb) {
    entries_[path] = Entry{std::move(cb), {}, false};
}

void FileWatcher::unwatch(const std::string& path) {
    entries_.erase(path);
}

int FileWatcher::tick(float dt) {
    accumulator_ += dt;
    if (accumulator_ < interval_) return 0;
    accumulator_ = 0.0f;

    int changed = 0;
    for (auto& [path, e] : entries_) {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(path, ec);
        if (ec) continue;  // file missing; ignore until it reappears
        if (!e.seen) {
            e.mtime = t;
            e.seen  = true;
            continue;
        }
        if (t != e.mtime) {
            e.mtime = t;
            if (e.cb) e.cb(path);
            ++changed;
        }
    }
    return changed;
}

}  // namespace vaxelis
