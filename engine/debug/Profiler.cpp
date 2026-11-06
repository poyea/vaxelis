// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/debug/Profiler.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <thread>

#include "engine/core/Log.hpp"

namespace vaxelis {

namespace {

using SteadyClock = std::chrono::steady_clock;

uint64_t steady_now_us() {
    const auto d = SteadyClock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
}

/// The trace format is JSON, so a name carrying a quote or a backslash would
/// produce a file no viewer can read. Names are literals from call sites today,
/// but a caller is free to pass something else.
void write_escaped(std::string& out, std::string_view text) {
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
}

uint32_t current_thread_id() {
    // The trace only needs the lanes to be distinct, not meaningful, so a hash
    // of the opaque id is enough and avoids a platform-specific call.
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

} // namespace

Profiler& Profiler::instance() {
    static Profiler profiler;
    return profiler;
}

void Profiler::begin_session(std::string_view name, size_t expected_events) {
    if (m_recording) {
        VX_WARN("Profiler: begin_session while '{}' is recording; restarting", m_session_name);
        m_events.clear();
    }
    m_session_name.assign(name);
    m_events.clear();
    m_events.reserve(expected_events);
    m_epoch_us = steady_now_us();
    m_recording = true;
}

bool Profiler::end_session(std::string_view path) {
    if (!m_recording)
        return false;
    m_recording = false;

    const std::string json = to_json();
    std::ofstream out{std::string{path}, std::ios::binary | std::ios::trunc};
    if (!out) {
        VX_ERROR("Profiler: cannot open {} for writing; {} events dropped", path, m_events.size());
        return false;
    }
    out << json;
    if (!out) {
        VX_ERROR("Profiler: write to {} failed", path);
        return false;
    }
    VX_INFO("Profiler: wrote {} events to {}", m_events.size(), path);
    return true;
}

uint64_t Profiler::now_us() const {
    if (!m_recording)
        return 0;
    const uint64_t now = steady_now_us();
    // Steady, so this cannot run backwards; the guard is for a session that
    // began after the caller read the clock.
    return now > m_epoch_us ? now - m_epoch_us : 0;
}

void Profiler::record(std::string_view name, uint64_t start_us, uint64_t duration_us) {
    if (!m_recording)
        return;
    m_events.push_back(ProfileEvent{name, start_us, duration_us, current_thread_id()});
}

std::string Profiler::to_json() const {
    // The "complete event" form: one object per scope with a duration, which is
    // what a scoped timer naturally produces.
    std::string out;
    out.reserve(m_events.size() * 128 + 64);
    out += "{\"traceEvents\":[";
    bool first = true;
    for (const ProfileEvent& e : m_events) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"name\":\"";
        write_escaped(out, e.name);
        out += "\",\"cat\":\"function\",\"ph\":\"X\",\"ts\":";
        out += std::to_string(e.start_us);
        out += ",\"dur\":";
        out += std::to_string(e.duration_us);
        out += ",\"pid\":0,\"tid\":";
        out += std::to_string(e.thread_id);
        out += '}';
    }
    out += "],\"displayTimeUnit\":\"ms\",\"otherData\":{\"session\":\"";
    write_escaped(out, m_session_name);
    out += "\"}}";
    return out;
}

ScopeTimer::ScopeTimer(std::string_view name) : m_name(name) {
    Profiler& profiler = Profiler::instance();
    m_active = profiler.recording();
    if (m_active)
        m_start_us = profiler.now_us();
}

ScopeTimer::~ScopeTimer() {
    if (!m_active)
        return;
    Profiler& profiler = Profiler::instance();
    // The session may have ended inside this scope; record() would drop the
    // event anyway, but checking here keeps the intent explicit.
    if (!profiler.recording())
        return;
    const uint64_t end_us = profiler.now_us();
    profiler.record(m_name, m_start_us, end_us > m_start_us ? end_us - m_start_us : 0);
}

} // namespace vaxelis
