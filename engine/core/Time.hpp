#pragma once

#include <chrono>

namespace vaxelis {

/// Frame timer over std::chrono::steady_clock.
class Clock {
  public:
    /// Monotonic source, so the clock is unaffected by wall-clock adjustments.
    using clock = std::chrono::steady_clock;

    /// Starts the clock; both the elapsed and the per-tick timer begin now.
    Clock() : m_start(clock::now()), m_last(m_start) {}

    /// Returns seconds since the previous call to tick().
    float tick() {
        auto now = clock::now();
        std::chrono::duration<float> dt = now - m_last;
        m_last = now;
        return dt.count();
    }

    /// Returns seconds since construction.
    float elapsed() const {
        std::chrono::duration<float> e = clock::now() - m_start;
        return e.count();
    }

  private:
    clock::time_point m_start;
    clock::time_point m_last;
};

} // namespace vaxelis
