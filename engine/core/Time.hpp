#pragma once

#include <chrono>

namespace vaxelis {

/// Frame timer over std::chrono::steady_clock.
class Clock {
  public:
    using clock = std::chrono::steady_clock;

    Clock() : start_(clock::now()), last_(start_) {}

    /// Returns seconds since the previous call to tick().
    float tick() {
        auto now = clock::now();
        std::chrono::duration<float> dt = now - last_;
        last_ = now;
        return dt.count();
    }

    /// Returns seconds since construction.
    float elapsed() const {
        std::chrono::duration<float> e = clock::now() - start_;
        return e.count();
    }

  private:
    clock::time_point start_;
    clock::time_point last_;
};

} // namespace vaxelis
