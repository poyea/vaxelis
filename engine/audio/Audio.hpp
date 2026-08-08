#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace vaxelis {

/// Opaque handle to a loaded sound. 0 == null.
struct SoundHandle {
    uint32_t id{0};
    constexpr bool valid() const { return id != 0; }
};

/// Thin wrapper around miniaudio's ma_engine. Lifetime-owns loaded ma_sound
/// instances; the engine destructor releases them in shutdown().
///
/// init() may fail (no audio device, driver issue); the caller should treat
/// that as a soft failure and continue silently rather than aborting.
class Audio {
  public:
    /// Constructs the wrapper without touching the audio device; call init().
    Audio();
    /// Releases any sounds still loaded and stops the engine.
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    /// Starts the miniaudio engine. Returns false on failure (soft; see class note).
    bool init();
    /// Releases all loaded sounds and stops the engine.
    void shutdown();
    /// True after a successful init().
    bool ready() const { return m_inited; }

    /// Loads a sound from disk. Currently keeps the whole sound resident; for
    /// long files we'll switch to streaming later.
    SoundHandle load(std::string_view path);
    /// Releases the sound; the handle becomes invalid.
    void unload(SoundHandle);

    /// Fire-and-forget play. Restarts the sound from the beginning.
    void play(SoundHandle);
    /// Stops playback of the sound.
    void stop(SoundHandle);

    /// Sets the engine-wide volume (1.0 = unity gain).
    void set_master_volume(float v);
    /// The volume last passed to set_master_volume(); 1.0 unless changed.
    float master_volume() const { return m_master_volume; }

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_inited{false};
    float m_master_volume{1.0f};
};

} // namespace vaxelis
