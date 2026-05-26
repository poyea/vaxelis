#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace vaxelis {

struct SoundHandle { uint32_t id{0}; constexpr bool valid() const { return id != 0; } };

// Thin wrapper around miniaudio's ma_engine. Lifetime-owns loaded ma_sound
// instances; the engine destructor releases them in shutdown().
//
// init() may fail (no audio device, driver issue) — the caller should treat
// that as a soft failure and continue silently rather than aborting.
class Audio {
public:
    Audio();
    ~Audio();

    Audio(const Audio&)            = delete;
    Audio& operator=(const Audio&) = delete;

    bool init();
    void shutdown();
    bool ready() const { return inited_; }

    // Loads a sound from disk. Currently keeps the whole sound resident; for
    // long files we'll switch to streaming later.
    SoundHandle load(std::string_view path);
    void unload(SoundHandle);

    // Fire-and-forget play. Restarts the sound from the beginning.
    void play(SoundHandle);
    void stop(SoundHandle);

    void  set_master_volume(float v);
    float master_volume() const { return master_volume_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool  inited_{false};
    float master_volume_{1.0f};
};

}  // namespace vaxelis
