#include "screens/BlackoutScreen.h"

#include <cmath>
#include <cstdio>

#include "App.h"
#include "screens/PlayScreen.h"
#include "ui/Draw.h"

using draw::Align;

void BlackoutScreen::enter() {
    const UiEvent& ev = app_.pendingBlackout;
    images_ = ev.images;
    from_ = ev.fromMinute;
    to_ = ev.toMinute;
    t_ = 0;
    done_ = false;
    total_ = kIntro + kPerImage * float(images_.size()) + kOutro;
    app_.audio.ambience("");
    app_.audio.sfx("glitch");
    app_.audio.loopSfx("ff", true);
}

void BlackoutScreen::event(const sf::Event& e, sf::Vector2f) {
    // allow skipping once the montage started
    if ((e.is<sf::Event::KeyPressed>() || e.is<sf::Event::MouseButtonPressed>()) && t_ > kIntro) t_ = std::max(t_, total_ - kOutro);
}

void BlackoutScreen::update(float dt, sf::Vector2f) {
    t_ += dt;
    if (t_ >= total_ - kOutro) app_.audio.loopSfx("ff", false);
    if (t_ >= total_ && !done_) {
        done_ = true;
        app_.play().sync();
        app_.go(ScreenId::Play);
    }
}

void BlackoutScreen::draw(sf::RenderTarget& rt) {
    const auto& th = app_.theme;
    auto& rng = app_.fx.rng();
    draw::rect(rt, {{0, 0}, {kW, kH}}, sf::Color::Black);

    const float montageEnd = kIntro + kPerImage * float(images_.size());
    const sf::Texture* cur = nullptr;

    if (t_ < kIntro) {
        // tape tearing
        app_.fx.glitch(rt, 1.f - t_ / kIntro * 0.3f);
    } else if (t_ < montageEnd) {
        int idx = int((t_ - kIntro) / kPerImage);
        float local = std::fmod(t_ - kIntro, kPerImage) / kPerImage;
        if (local < 0.78f) { // image visible, then a few black frames
            cur = &th.texture(images_[std::size_t(idx)]);
            sf::Sprite s(*cur);
            auto sz = cur->getSize();
            float cover = std::max(kW / sz.x, kH / sz.y) * (1.08f + 0.06f * local);
            s.setOrigin({sz.x / 2.f, sz.y / 2.f});
            s.setScale({cover, cover});
            sf::Vector2f jitter{float(rng.real(-6, 6)), float(rng.real(-3, 3))};
            // cheap chromatic split: red and cyan copies, offset
            s.setPosition(sf::Vector2f{kW / 2, kH / 2} + jitter + sf::Vector2f{-5, 0});
            s.setColor(sf::Color(255, 60, 60, 110));
            rt.draw(s, sf::BlendAdd);
            s.setPosition(sf::Vector2f{kW / 2, kH / 2} + jitter + sf::Vector2f{5, 0});
            s.setColor(sf::Color(60, 200, 255, 110));
            rt.draw(s, sf::BlendAdd);
            s.setPosition(sf::Vector2f{kW / 2, kH / 2} + jitter);
            s.setColor(sf::Color(255, 255, 255, 200));
            rt.draw(s);
            float flash = std::max(0.f, 1.f - local * 6.f);
            if (flash > 0) draw::rect(rt, {{0, 0}, {kW, kH}}, sf::Color(255, 255, 255, std::uint8_t(200 * flash)));
        }
        app_.fx.glitch(rt, 0.35f + 0.25f * std::sin(t_ * 17.f), cur);
    } else {
        app_.fx.grain(rt, 1.f);
        float k = (t_ - montageEnd) / kOutro;
        float a = draw::clamp01(k * 3.f) * (1.f - draw::clamp01((k - 0.75f) / 0.25f));
        draw::text(rt, th.title, "...kus ma olen?", 54, {kW / 2, 310}, draw::alpha(col::osd, a), Align::Center, 1.f, true);
    }

    app_.fx.vignette(rt, sf::Color(0, 0, 0, 255));

    // OSD: fast-forward, then PLAY
    draw::brackets(rt, {{22, 18}, {kW - 44, kH - 36}}, 34, 2, col::osdDim);
    if (t_ < montageEnd) {
        draw::triangle(rt, {44, 32}, {44, 56}, {60, 44}, col::osd);
        draw::triangle(rt, {60, 32}, {60, 56}, {76, 44}, col::osd);
        draw::text(rt, th.osd, "FF  x64", 32, {88, 26}, col::osd, Align::Left, 1.2f, true);
    } else {
        draw::triangle(rt, {48, 32}, {48, 56}, {66, 44}, col::osd);
        draw::text(rt, th.osd, "PLAY", 32, {78, 26}, col::osd, Align::Left, 1.2f, true);
    }

    // racing timestamp
    float prog = draw::clamp01((t_ - kIntro * 0.5f) / std::max(0.1f, montageEnd - kIntro * 0.5f));
    int minute = from_ + int(float(to_ - from_) * prog);
    char sec[4];
    std::snprintf(sec, sizeof sec, "%02d", rng.range(0, 59));
    std::string date = app_.content.config.value("tape_date", "04/24/2024");
    draw::text(rt, th.osd, date + "  " + Engine::fmtClock(minute) + ":" + (prog < 1 ? std::string(sec) : std::string("00")), 40,
               {kW - 44, 640}, col::osd, Align::Right, 1.f, true);
    if (t_ > kIntro && t_ < montageEnd)
        draw::text(rt, th.osd, "MÄLU KUSTUS", 22, {kW / 2, 30}, draw::alpha(col::rec, 0.5f + 0.5f * std::sin(t_ * 10)), Align::Center, 3.f);
    app_.fx.scanlines(rt, 0.7f);
}
