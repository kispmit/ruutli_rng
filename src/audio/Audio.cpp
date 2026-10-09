#include "audio/Audio.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {
constexpr float kAmbienceVolume = 18.f; // SFML volume 0..100 (master volume scales this)
constexpr float kSfxVolume = 28.f;
constexpr float kFadeSpeed = 1.2f;      // per second
} // namespace

void Audio::init(const std::string& assetRoot) {
    root_ = assetRoot;

    // Diagnostics go to ryytlirng.log (see App): which device we got, what loaded.
    const auto devices = sf::PlaybackDevice::getAvailableDevices();
    std::cerr << "[audio] playback devices: " << devices.size() << "\n";
    for (const auto& d : devices) std::cerr << "[audio]   - " << d << "\n";
    const auto def = sf::PlaybackDevice::getDefaultDevice();
    std::cerr << "[audio] default device: " << (def ? *def : std::string("(none)")) << "\n";
    if (devices.empty()) {
        status_ = "HELI: VÄLJUNDSEADET EI LEITUD";
        ok_ = false;
    } else {
        status_ = "HELI: " + (def ? *def : devices.front());
        ok_ = true;
    }

    const fs::path dir = fs::path(root_) / "audio" / "sfx";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        std::cerr << "[audio] missing folder " << dir.string() << "\n";
        status_ = "HELI: assets/audio PUUDUB";
        ok_ = false;
        return;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".wav" && entry.path().extension() != ".ogg") continue;
        sf::SoundBuffer buf;
        if (buf.loadFromFile(entry.path().string())) buffers_[entry.path().stem().string()] = std::move(buf);
        else std::cerr << "[audio] could not load " << entry.path().string() << "\n";
    }
    std::cerr << "[audio] sfx loaded: " << buffers_.size() << "\n";
}

void Audio::ambience(const std::string& relPath) {
    Track& cur = tracks_[current_];
    if (cur.path == relPath) {
        cur.target = 1;
        return;
    }
    // fade out whatever is playing, fade the new one in on the other slot
    cur.target = 0;
    current_ = 1 - current_;
    Track& next = tracks_[current_];
    next.path = relPath;
    next.level = 0;
    next.target = relPath.empty() ? 0.f : 1.f;
    next.music.reset();
    if (relPath.empty()) return;
    auto m = std::make_unique<sf::Music>();
    if (!m->openFromFile((fs::path(root_) / relPath).string())) {
        std::cerr << "[audio] could not open ambience " << relPath << "\n";
        return;
    }
    std::cerr << "[audio] ambience -> " << relPath << "\n";
    m->setLooping(true);
    m->setVolume(0);
    m->play();
    next.music = std::move(m);
}

void Audio::update(float dt) {
    duck_ += (duckTarget_ - duck_) * std::min(1.f, dt * 6.f);
    for (auto& t : tracks_) {
        if (t.level < t.target) t.level = std::min(t.target, t.level + dt * kFadeSpeed);
        if (t.level > t.target) t.level = std::max(t.target, t.level - dt * kFadeSpeed);
        if (!t.music) continue;
        t.music->setVolume(kAmbienceVolume * t.level * duck_ * master());
        if (t.level <= 0 && t.target <= 0) t.music.reset(); // fully faded out: free it
    }
    voices_.remove_if([](const sf::Sound& s) { return s.getStatus() == sf::Sound::Status::Stopped; });
}

void Audio::sfx(const std::string& name, float volume) {
    if (muted_) return;
    auto it = buffers_.find(name);
    if (it == buffers_.end() || voices_.size() > 16) return;
    voices_.emplace_back(it->second);
    voices_.back().setVolume(kSfxVolume * volume * volume_);
    voices_.back().play();
}

void Audio::loopSfx(const std::string& name, bool on) {
    auto it = loops_.find(name);
    if (!on) {
        if (it != loops_.end()) loops_.erase(it);
        return;
    }
    if (it != loops_.end() || muted_) return;
    auto b = buffers_.find(name);
    if (b == buffers_.end()) return;
    auto s = std::make_unique<sf::Sound>(b->second);
    s->setLooping(true);
    s->setVolume(kSfxVolume * volume_);
    s->play();
    loops_[name] = std::move(s);
}

void Audio::setMuted(bool m) {
    muted_ = m;
    if (m) {
        voices_.clear();
        loops_.clear();
    }
}
