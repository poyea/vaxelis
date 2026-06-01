#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace vaxelis {

// Polling-based file watcher. Cross-platform (mtime), low frequency. Each
// watched path holds a callback that fires when the file's last_write_time
// changes vs. the previous tick.
//
// Designed for editor/dev use — not for production hot paths. Default tick
// interval is 250ms; call tick(dt) every frame.
class FileWatcher {
  public:
    using Callback = std::function<void(const std::string& path)>;

    void watch(std::string path, Callback cb);
    void unwatch(const std::string& path);

    // Returns the number of files that changed this tick.
    int tick(float dt);

    void set_interval(float seconds) { interval_ = seconds; }

  private:
    struct Entry {
        Callback cb;
        std::filesystem::file_time_type mtime{};
        bool seen{false};
    };
    std::unordered_map<std::string, Entry> entries_;
    float accumulator_{0.0f};
    float interval_{0.25f};
};

} // namespace vaxelis
