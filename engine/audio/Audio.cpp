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

Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() { shutdown(); }

bool Audio::init() {
    if (inited_) return true;
    if (ma_engine_init(nullptr, &impl_->engine) != MA_SUCCESS) {
        VX_ERROR("Audio: ma_engine_init failed");
        return false;
    }
    inited_ = true;
    set_master_volume(master_volume_);
    VX_INFO("Audio: initialized");
    return true;
}

void Audio::shutdown() {
    if (!inited_) return;
    for (auto& [_, s] : impl_->sounds) ma_sound_uninit(s.get());
    impl_->sounds.clear();
    ma_engine_uninit(&impl_->engine);
    inited_ = false;
}

SoundHandle Audio::load(std::string_view path) {
    if (!inited_) return {};
    auto sound = std::make_unique<ma_sound>();
    const std::string p(path);
    if (ma_sound_init_from_file(&impl_->engine, p.c_str(), 0, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
        VX_ERROR("Audio: failed to load {}", p);
        return {};
    }
    const uint32_t id = impl_->next_id++;
    impl_->sounds.emplace(id, std::move(sound));
    return SoundHandle{id};
}

void Audio::unload(SoundHandle h) {
    if (!inited_ || !h.valid()) return;
    auto it = impl_->sounds.find(h.id);
    if (it == impl_->sounds.end()) return;
    ma_sound_uninit(it->second.get());
    impl_->sounds.erase(it);
}

void Audio::play(SoundHandle h) {
    if (!inited_) return;
    auto it = impl_->sounds.find(h.id);
    if (it == impl_->sounds.end()) return;
    ma_sound_seek_to_pcm_frame(it->second.get(), 0);
    ma_sound_start(it->second.get());
}

void Audio::stop(SoundHandle h) {
    if (!inited_) return;
    auto it = impl_->sounds.find(h.id);
    if (it == impl_->sounds.end()) return;
    ma_sound_stop(it->second.get());
}

void Audio::set_master_volume(float v) {
    master_volume_ = v;
    if (inited_) ma_engine_set_volume(&impl_->engine, v);
}

}  // namespace vaxelis
