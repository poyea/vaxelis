#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace vaxelis {

/// Polling-based file watcher. Cross-platform (mtime), low frequency. Each
/// watched path holds a callback that fires when the file's last_write_time
/// changes vs. the previous tick.
///
/// Designed for editor/dev use, not for production hot paths. Default tick
/// interval is 250ms; call tick(dt) every frame.
class FileWatcher {
  public:
    using Callback = std::function<void(const std::string& path)>;

    /// Starts watching `path`; `cb` fires on each detected change.
    void watch(std::string path, Callback cb);
    /// Stops watching `path`.
    void unwatch(const std::string& path);

    /// Polls watched files once the interval has elapsed.
    /// @return the number of files that changed this tick.
    int tick(float dt);

    /// Sets the polling interval in seconds.
    void set_interval(float seconds) { m_interval = seconds; }

  private:
    struct Entry {
        Callback cb;
        std::filesystem::file_time_type mtime{};
        bool seen{false};
    };
    std::unordered_map<std::string, Entry> m_entries;
    float m_accumulator{0.0f};
    float m_interval{0.25f};
};

} // namespace vaxelis
