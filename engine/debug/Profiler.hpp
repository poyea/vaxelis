// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#pragma once

/// @file
/// Scoped timing that writes the Chrome Tracing format.
///
/// The engine has had to answer performance questions by reasoning about the
/// code, because the only numbers it produced came from one example and one
/// benchmark. This records every scope that asks to be recorded and writes a
/// file `chrome://tracing` and Perfetto both open.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vaxelis {

/// One completed scope: a name, a thread, and when it began and ended.
///
/// Microseconds since the profiler started, because that is the unit the trace
/// format wants and converting once here beats converting per event at write
/// time.
struct ProfileEvent {
    /// Points at a string literal or other storage that outlives the profiler.
    /// Scopes are named by literals at their call sites, so this never owns.
    std::string_view name;
    uint64_t start_us{0};
    uint64_t duration_us{0};
    /// Which thread recorded it. Traces separate threads into lanes.
    uint32_t thread_id{0};
};

/// Collects ProfileEvents and writes them out.
///
/// Single instance, reached through instance(). Recording is off until
/// begin_session(), so a build that never asks to profile pays one bool test
/// per scope and allocates nothing.
///
/// Not thread safe. The engine is single-threaded today; when a job system
/// arrives this needs a per-thread buffer rather than a mutex, so the thread id
/// is already in the event.
class Profiler {
  public:
    /// The process-wide profiler.
    static Profiler& instance();

    /// Starts recording. `name` labels the trace. Reserves room for
    /// `expected_events` so a frame does not pay for reallocation mid-capture.
    void begin_session(std::string_view name, size_t expected_events = 4096);

    /// Stops recording and writes the trace to `path`. Returns false if the
    /// file could not be written, which is logged. No-op when no session is
    /// running.
    bool end_session(std::string_view path);

    /// True while a session is recording.
    bool recording() const { return m_recording; }

    /// Records one completed scope. Dropped when no session is running.
    void record(std::string_view name, uint64_t start_us, uint64_t duration_us);

    /// Events captured in the current session.
    const std::vector<ProfileEvent>& events() const { return m_events; }

    /// Microseconds since the session began. Exposed for ScopeTimer and for
    /// tests; reads zero when not recording.
    uint64_t now_us() const;

    /// Serializes the current events as Chrome Tracing JSON. Split out from
    /// end_session so a test can assert on the text without touching a file.
    std::string to_json() const;

  private:
    Profiler() = default;

    bool m_recording{false};
    std::string m_session_name;
    /// Steady-clock reading when the session began, in microseconds.
    uint64_t m_epoch_us{0};
    std::vector<ProfileEvent> m_events;
};

/// Times the scope it is declared in and reports it on the way out.
///
/// @code
///   void Scene::update_world_transforms() {
///       VX_PROFILE_SCOPE("Scene::update_world_transforms");
///       ...
///   }
/// @endcode
///
/// The name must outlive the profiler, so pass a literal. Copying one of these
/// would report the same scope twice, so it does not copy.
class ScopeTimer {
  public:
    explicit ScopeTimer(std::string_view name);
    ~ScopeTimer();

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
    ScopeTimer(ScopeTimer&&) = delete;
    ScopeTimer& operator=(ScopeTimer&&) = delete;

  private:
    std::string_view m_name;
    uint64_t m_start_us{0};
    /// Whether a session was running at construction. A session that ends
    /// mid-scope must not get a half-measured event, and one that starts
    /// mid-scope must not get an event whose start predates its epoch.
    bool m_active{false};
};

/// Two levels, because `a##__LINE__` pastes the token `__LINE__` itself rather
/// than the line number. Without the indirection every scope in a translation
/// unit would declare the same variable name, and two in one block would not
/// compile.
#define VX_PROFILE_CONCAT_INNER(a, b) a##b
#define VX_PROFILE_CONCAT(a, b) VX_PROFILE_CONCAT_INNER(a, b)

/// Times the enclosing scope under `name`, which must outlive the profiler --
/// pass a string literal.
#define VX_PROFILE_SCOPE(name)                                                                     \
    const ::vaxelis::ScopeTimer VX_PROFILE_CONCAT(vx_scope_timer_, __LINE__)(name)

/// Times the enclosing function, named by the compiler.
#define VX_PROFILE_FUNCTION() VX_PROFILE_SCOPE(__func__)

} // namespace vaxelis
