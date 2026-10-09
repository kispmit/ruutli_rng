#pragma once
#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <string>

#include <SFML/Audio.hpp>

// Ambience loops (crossfaded per location) + short UI sounds.
// Fails silently if there is no audio device; the game runs fine without sound.
class Audio {
public:
    void init(const std::string& assetRoot);
    void update(float dt);

    // Crossfade to a looping ambience track ("audio/amb/seik.ogg"). Empty = fade to silence.
    void ambience(const std::string& relPath);
    // Temporarily lower ambience (phone open, pause). 1 = normal.
    void duck(float level) { duckTarget_ = level; }

    void sfx(const std::string& name, float volume = 1.f); // "move", "select", "notify", ...
    void loopSfx(const std::string& name, bool on);        // e.g. "ff" during blackout

    void setMuted(bool m);
    bool muted() const { return muted_; }
    void changeVolume(float d) { volume_ = std::clamp(volume_ + d, 0.f, 2.f); }
    float volume() const { return volume_; }

    // Human-readable status for the menu ("AUDIO OK: Speakers (Realtek)" / "NO AUDIO DEVICE").
    const std::string& status() const { return status_; }
    bool ok() const { return ok_; }

private:
    struct Track {
        std::unique_ptr<sf::Music> music;
        std::string path;
        float level = 0;  // 0..1 fade
        float target = 0;
    };
    float master() const { return muted_ ? 0.f : volume_; }

    std::string root_;
    std::string status_ = "HELI: EI KÄIVITUNUD";
    bool ok_ = false;
    Track tracks_[2];
    int current_ = 0;
    float duck_ = 1, duckTarget_ = 1;
    bool muted_ = false;
    float volume_ = 1.f; // master, 0..2 (keys - / +)

    std::map<std::string, sf::SoundBuffer> buffers_;
    std::list<sf::Sound> voices_;
    std::map<std::string, std::unique_ptr<sf::Sound>> loops_;
};
