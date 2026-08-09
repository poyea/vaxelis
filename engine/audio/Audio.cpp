// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/audio/Audio.hpp"

#include <string>
#include <unordered_map>

// Silence miniaudio's noisy warnings; it's a third-party single-header lib.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "engine/core/Log.hpp"

namespace vaxelis {

struct Audio::Impl {
    ma_engine engine{};
    std::unordered_map<uint32_t, std::unique_ptr<ma_sound>> sounds;
    uint32_t next_id{1};
};

Audio::Audio() : m_impl(std::make_unique<Impl>()) {
}
Audio::~Audio() {
    shutdown();
}

bool Audio::init() {
    if (m_inited)
        return true;
    if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS) {
        VX_ERROR("Audio: ma_engine_init failed");
        return false;
    }
    m_inited = true;
    set_master_volume(m_master_volume);
    VX_INFO("Audio: initialized");
    return true;
}

void Audio::shutdown() {
    if (!m_inited)
        return;
    for (auto& [_, s] : m_impl->sounds)
        ma_sound_uninit(s.get());
    m_impl->sounds.clear();
    ma_engine_uninit(&m_impl->engine);
    m_inited = false;
}

SoundHandle Audio::load(std::string_view path) {
    if (!m_inited)
        return {};
    auto sound = std::make_unique<ma_sound>();
    const std::string p(path);
    if (ma_sound_init_from_file(&m_impl->engine, p.c_str(), 0, nullptr, nullptr, sound.get()) !=
        MA_SUCCESS) {
        VX_ERROR("Audio: failed to load {}", p);
        return {};
    }
    const uint32_t id = m_impl->next_id++;
    m_impl->sounds.emplace(id, std::move(sound));
    return SoundHandle{id};
}

void Audio::unload(SoundHandle h) {
    if (!m_inited || !h.valid())
        return;
    auto it = m_impl->sounds.find(h.id);
    if (it == m_impl->sounds.end())
        return;
    ma_sound_uninit(it->second.get());
    m_impl->sounds.erase(it);
}

void Audio::play(SoundHandle h) {
    if (!m_inited)
        return;
    auto it = m_impl->sounds.find(h.id);
    if (it == m_impl->sounds.end())
        return;
    ma_sound_seek_to_pcm_frame(it->second.get(), 0);
    ma_sound_start(it->second.get());
}

void Audio::stop(SoundHandle h) {
    if (!m_inited)
        return;
    auto it = m_impl->sounds.find(h.id);
    if (it == m_impl->sounds.end())
        return;
    ma_sound_stop(it->second.get());
}

void Audio::set_master_volume(float v) {
    m_master_volume = v;
    if (m_inited)
        ma_engine_set_volume(&m_impl->engine, v);
}

} // namespace vaxelis
