#include "screens/EndScreen.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "App.h"
#include "ui/Draw.h"

using draw::Align;
using draw::U;

namespace {
const char* kButtons[] = {"SKOOR", "KERI TAGASI, UUESTI", "PEAMENÜÜ"};
constexpr int kNumButtons = 3;
}

void EndScreen::enter() {
    recap_ = app_.engine.recap();
    t_ = 0;
    app_.audio.ambience(recap_.ending.id == "sunrise" || recap_.ending.id == "detained" ? "audio/amb/dawn.ogg" : "audio/amb/outside_quiet.ogg");
    sel_ = 1;
    buttons_ = {{{kW / 2 - 490, 640}, {300, 46}}, {{kW / 2 - 170, 640}, {340, 46}}, {{kW / 2 + 190, 640}, {300, 46}}};
    scoreOpen_ = false;
    scoreT_ = 0;
    saveHighScore();
}

void EndScreen::saveHighScore() {
    using nlohmann::json;
    namespace fs = std::filesystem;
    table_.clear();
    rank_ = -1;
    fs::path file = fs::path(app_.saveDir) / "ryytlirng_rekordid.json";
    try {
        std::ifstream in(file);
        if (in) {
            json j = json::parse(in, nullptr, false);
            if (j.is_array())
                for (const auto& e : j)
                    table_.push_back({e.value("score", 0), e.value("ending", ""), e.value("date", ""), e.value("seed", 0u)});
        }
    } catch (...) {}
    char date[32] = "";
    std::time_t now = std::time(nullptr);
    if (auto* tm = std::localtime(&now)) std::strftime(date, sizeof date, "%d.%m.%Y %H:%M", tm);
    HighScore me{recap_.total, recap_.ending.title, date, app_.engine.seed()};
    // a run is only recorded once (re-entering the screen must not add it twice)
    bool dup = false;
    for (const auto& h : table_) dup |= h.seed == me.seed && h.score == me.score;
    if (!dup) table_.push_back(me);
    std::stable_sort(table_.begin(), table_.end(), [](const HighScore& a, const HighScore& b) { return a.score > b.score; });
    if (table_.size() > 10) table_.resize(10);
    for (std::size_t i = 0; i < table_.size(); ++i)
        if (table_[i].seed == me.seed && table_[i].score == me.score) rank_ = int(i);
    try {
        json out = json::array();
        for (const auto& h : table_) out.push_back({{"score", h.score}, {"ending", h.ending}, {"date", h.date}, {"seed", h.seed}});
        std::ofstream o(file);
        o << out.dump(2);
    } catch (...) {
        std::cerr << "[score] could not write " << file.string() << "\n";
    }
}

void EndScreen::activate(int i) {
    if (t_ < 0.8f) return;
    app_.audio.sfx("select");
    if (i == 0) {
        scoreOpen_ = true;
        scoreT_ = 0;
    } else if (i == 1) {
        app_.go(ScreenId::Loading);
    } else {
        app_.go(ScreenId::Menu);
    }
}

void EndScreen::event(const sf::Event& e, sf::Vector2f mouse) {
    using K = sf::Keyboard::Key;
    if (scoreOpen_) {
        // the score window is on top: anything closes it
        bool close = false;
        if (const auto* k = e.getIf<sf::Event::KeyPressed>())
            close = k->code == K::Escape || k->code == K::Enter || k->code == K::Space || k->code == K::S;
        if (const auto* m = e.getIf<sf::Event::MouseButtonPressed>())
            close = m->button == sf::Mouse::Button::Left &&
                    (closeRect_.contains(mouse) || !sf::FloatRect({kW / 2 - 440, 60}, {880, 600}).contains(mouse));
        if (close) {
            scoreOpen_ = false;
            app_.audio.sfx("select");
        }
        return;
    }
    if (const auto* k = e.getIf<sf::Event::KeyPressed>()) {
        keyboard_ = true;
        if (k->code == K::Left) sel_ = (sel_ + kNumButtons - 1) % kNumButtons;
        if (k->code == K::Right || k->code == K::Tab) sel_ = (sel_ + 1) % kNumButtons;
        if (k->code == K::Enter || k->code == K::Space) activate(sel_);
        if (k->code == K::S) activate(0);
        if (k->code == K::Escape) activate(2);
    }
    if (const auto* m = e.getIf<sf::Event::MouseButtonPressed>())
        for (int i = 0; i < kNumButtons; ++i)
            if (m->button == sf::Mouse::Button::Left && buttons_[i].contains(mouse)) activate(i);
    if (e.is<sf::Event::MouseMoved>()) keyboard_ = false;
}

void EndScreen::update(float dt, sf::Vector2f mouse) {
    t_ += dt;
    if (scoreOpen_) scoreT_ += dt;
    if (!keyboard_ && !scoreOpen_)
        for (int i = 0; i < kNumButtons; ++i)
            if (buttons_[i].contains(mouse)) sel_ = i;
}

void EndScreen::draw(sf::RenderTarget& rt) {
    const auto& th = app_.theme;
    const auto& tone = recap_.ending.tone;
    sf::Color accent = tone == "good" ? col::good : tone == "bad" ? col::bad : col::steel;

    // backdrop: the last location, heavily darkened
    {
        const sf::Texture& bg = app_.theme.texture(app_.engine.view().image);
        sf::Sprite s(bg);
        auto sz = bg.getSize();
        float cover = std::max(kW / sz.x, kH / sz.y) * 1.08f;
        s.setOrigin({sz.x / 2.f, sz.y / 2.f});
        s.setScale({cover, cover});
        s.setPosition({kW / 2, kH / 2});
        s.setColor(sf::Color(255, 255, 255, 70));
        rt.draw(s);
    }
    if (tone == "bad" && recap_.ending.id == "sunrise")
        draw::vgrad(rt, {{0, 0}, {kW, kH}}, sf::Color(255, 150, 80, 90), sf::Color(255, 230, 200, 30));
    app_.fx.vignette(rt, sf::Color(0, 0, 0, 255));
    app_.fx.grain(rt, 1.f);

    // OSD: STOP
    draw::brackets(rt, {{22, 18}, {kW - 44, kH - 36}}, 34, 2, col::osdDim);
    draw::rect(rt, {{46, 33}, {22, 22}}, col::osd);
    draw::text(rt, th.osd, "STOP", 32, {80, 26}, col::osd, Align::Left, 1.2f, true);
    draw::text(rt, th.osd, "KASSETI LÕPP", 22, {kW - 44, 32}, col::osdDim, Align::Right, 2.f, true);

    // Title
    float ta = draw::easeOut(t_ / 0.8f);
    draw::text(rt, th.title, U(recap_.ending.title), 76, {kW / 2, 62 - (1 - ta) * 20}, draw::alpha(accent, ta), Align::Center, 1.f, true);
    draw::text(rt, th.italic, U(recap_.ending.subtitle), 22, {kW / 2, 152}, draw::alpha(col::osd, draw::easeOut((t_ - 0.3f) / 0.8f)),
               Align::Center, 1.f, true);

    // Columns
    auto column = [&](float x, float w, const std::string& title, const std::vector<std::string>& lines, float delay) {
        float a = draw::easeOut((t_ - delay) / 0.6f);
        if (a <= 0) return;
        sf::FloatRect r({x, 206}, {w, 410});
        draw::rect(rt, r, draw::alpha(sf::Color(6, 8, 12, 200), a));
        draw::rect(rt, {r.position, {w, 2}}, draw::alpha(accent, a));
        draw::text(rt, th.osd, U(title), 22, {x + 18, 214}, draw::alpha(col::steel, a), Align::Left, 2.f);
        float y = 252;
        int i = 0;
        for (const auto& l : lines) {
            float la = draw::easeOut((t_ - delay - 0.25f - i * 0.12f) / 0.4f);
            auto wrapped = draw::wrap(th.bodyMed, l, 17, w - 46);
            if (y + wrapped.size() * 22 > r.position.y + r.size.y - 10) {
                draw::text(rt, th.bodyMed, "...", 17, {x + 30, y}, draw::alpha(col::osdDim, la));
                break;
            }
            draw::rect(rt, {{x + 18, y + 9}, {5, 5}}, draw::alpha(accent, la));
            for (auto& wl : wrapped) {
                draw::text(rt, th.bodyMed, wl, 17, {x + 32, y}, draw::alpha(col::osd, la));
                y += 22;
            }
            y += 8;
            ++i;
        }
        if (lines.empty()) draw::text(rt, th.body, "Midagi meeldejäävat ei juhtunud.", 17, {x + 18, y}, draw::alpha(col::osdDim, a));
    };
    std::vector<std::string> nightLines;
    for (const auto& [title, ok] : recap_.tasks) nightLines.push_back((ok ? "TEHTUD: " : "EBAÕNNESTUS: ") + title);
    for (const auto& [title, ok] : recap_.sideTasks)
        if (ok) nightLines.push_back("KÕRVALÜLESANNE: " + title);
    nightLines.insert(nightLines.end(), recap_.night.begin(), recap_.night.end());
    column(48, 500, "SINU ÖÖ", nightLines, 0.5f);
    column(566, 400, "INIMESED", recap_.friends, 0.8f);

    // Tape info
    {
        float a = draw::easeOut((t_ - 1.1f) / 0.6f);
        if (a > 0) {
            float x = 984, w = 248;
            sf::FloatRect r({x, 206}, {w, 410});
            draw::rect(rt, r, draw::alpha(sf::Color(6, 8, 12, 200), a));
            draw::rect(rt, {r.position, {w, 2}}, draw::alpha(accent, a));
            draw::text(rt, th.osd, "KASSETI INFO", 22, {x + 18, 214}, draw::alpha(col::steel, a), Align::Left, 2.f);
            float y = 254;
            for (const auto& [k, v] : recap_.stats) {
                draw::text(rt, th.osd, U(k), 20, {x + 18, y}, draw::alpha(col::osdDim, a), Align::Left, 1.4f);
                draw::text(rt, th.osd, U(v), 30, {x + w - 18, y - 6}, draw::alpha(col::osd, a), Align::Right);
                draw::rect(rt, {{x + 18, y + 32}, {w - 36, 1}}, draw::alpha(sf::Color(232, 234, 240, 30), a));
                y += 52;
            }
        }
    }

    // Buttons
    float ba = draw::easeOut((t_ - 1.3f) / 0.5f);
    for (int i = 0; i < kNumButtons; ++i) {
        const auto& r = buttons_[i];
        bool sel = i == sel_;
        if (i == 0) {
            // the score tab: yellow, shows the total, pulses once on a new record
            sf::Color c = col::amber;
            draw::rect(rt, r, draw::alpha(sel ? c : sf::Color(6, 8, 12, 210), ba));
            draw::frame(rt, r, draw::alpha(c, ba), 1.5f);
            std::string label = "SKOOR " + std::to_string(recap_.total) + (rank_ == 0 ? "  REKORD!" : "  >");
            draw::text(rt, th.osd, U(label), 26, {r.position.x + r.size.x / 2, r.position.y + 6}, draw::alpha(sel ? col::ink : c, ba),
                       Align::Center, 1.6f);
            continue;
        }
        draw::rect(rt, r, draw::alpha(sel ? sf::Color(232, 234, 240, 240) : sf::Color(6, 8, 12, 210), ba));
        if (!sel) draw::frame(rt, r, draw::alpha(sf::Color(232, 234, 240, 50), ba), 1);
        draw::text(rt, th.osd, kButtons[i], 26, {r.position.x + r.size.x / 2, r.position.y + 6}, draw::alpha(sel ? col::ink : col::osd, ba),
                   Align::Center, 1.6f);
    }
    if (scoreOpen_) drawScore(rt);
    app_.fx.scanlines(rt, 0.6f);
}

void EndScreen::drawScore(sf::RenderTarget& rt) {
    const auto& th = app_.theme;
    float a = draw::easeOut(scoreT_ / 0.25f);
    draw::rect(rt, {{0, 0}, {kW, kH}}, sf::Color(0, 0, 0, std::uint8_t(170 * a)));
    sf::FloatRect win({kW / 2 - 440, 60 + (1 - a) * 20}, {880, 600});
    draw::rect(rt, win, draw::alpha(sf::Color(10, 12, 18, 245), a));
    draw::frame(rt, win, draw::alpha(col::amber, a), 1.5f);
    draw::rect(rt, {win.position, {win.size.x, 40}}, draw::alpha(col::amber, a));
    draw::text(rt, th.osd, "SKOOR  /  STATISTIKA", 24, {win.position.x + 18, win.position.y + 6}, draw::alpha(col::ink, a), Align::Left, 1.8f);
    closeRect_ = {{win.position.x + win.size.x - 44, win.position.y + 4}, {36, 32}};
    draw::text(rt, th.osd, "X", 26, {closeRect_.position.x + 18, closeRect_.position.y + 2}, draw::alpha(col::ink, a), Align::Center);

    // left: how the score was built
    float x = win.position.x + 22, y = win.position.y + 58, w = 470;
    draw::text(rt, th.osd, "KUST PUNKTID TULID", 18, {x, y}, draw::alpha(col::steel, a), Align::Left, 2.f);
    y += 30;
    int shown = 0;
    for (const auto& l : recap_.score) {
        if (y > win.position.y + win.size.y - 110) {
            draw::text(rt, th.body, "... ja " + std::to_string(int(recap_.score.size()) - shown) + " rida veel", 15, {x, y}, draw::alpha(col::osdDim, a));
            break;
        }
        float la = draw::easeOut((scoreT_ - 0.1f - shown * 0.04f) / 0.3f) * a;
        sf::Color pc = l.points >= 0 ? col::good : col::bad;
        draw::text(rt, th.bodyMed, U(l.label), 16, {x, y}, draw::alpha(col::osd, la));
        draw::text(rt, th.osd, (l.points > 0 ? "+" : "") + std::to_string(l.points), 20, {x + w, y - 2}, draw::alpha(pc, la), Align::Right);
        draw::rect(rt, {{x, y + 24}, {w, 1}}, draw::alpha(sf::Color(232, 234, 240, 22), la));
        y += 29;
        ++shown;
    }
    if (recap_.score.empty()) draw::text(rt, th.body, "Mitte ühtegi punkti. Ausalt.", 16, {x, y}, draw::alpha(col::osdDim, a));
    float ty = win.position.y + win.size.y - 74;
    draw::rect(rt, {{x, ty - 8}, {w, 2}}, draw::alpha(col::amber, a));
    draw::text(rt, th.osd, "KOKKU", 26, {x, ty + 4}, draw::alpha(col::osd, a), Align::Left, 2.f);
    draw::text(rt, th.title, std::to_string(recap_.total), 48, {x + w, ty - 8}, draw::alpha(col::amber, a), Align::Right);

    // right: high score table
    float rx = win.position.x + 530, rw = 330, ry = win.position.y + 58;
    draw::rect(rt, {{rx - 18, win.position.y + 50}, {1, win.size.y - 70}}, draw::alpha(sf::Color(232, 234, 240, 40), a));
    draw::text(rt, th.osd, "REKORDID", 18, {rx, ry}, draw::alpha(col::steel, a), Align::Left, 2.f);
    if (rank_ == 0 && std::fmod(scoreT_, 1.f) < 0.65f)
        draw::text(rt, th.osd, "UUS REKORD!", 18, {rx + rw, ry}, draw::alpha(col::amber, a), Align::Right, 1.6f);
    ry += 32;
    for (std::size_t i = 0; i < table_.size(); ++i) {
        const auto& h = table_[i];
        bool me = int(i) == rank_;
        if (me) draw::rect(rt, {{rx - 8, ry - 4}, {rw + 16, 44}}, draw::alpha(sf::Color(255, 186, 72, 40), a));
        sf::Color c = me ? col::amber : col::osd;
        draw::text(rt, th.osd, std::to_string(i + 1) + ".", 20, {rx, ry}, draw::alpha(col::osdDim, a));
        draw::text(rt, th.bodyMed, U(h.ending), 15, {rx + 34, ry}, draw::alpha(c, a));
        draw::text(rt, th.body, h.date, 13, {rx + 34, ry + 19}, draw::alpha(col::osdDim, a));
        draw::text(rt, th.osd, std::to_string(h.score), 24, {rx + rw, ry + 2}, draw::alpha(c, a), Align::Right);
        ry += 47;
    }
    draw::text(rt, th.osd, "ESC / KLIKI MUJALE SULGEB", 14, {rx + rw, win.position.y + win.size.y - 30},
               draw::alpha(col::osdDim, a), Align::Right, 1.6f);
}
