#include "App.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "screens/BlackoutScreen.h"
#include "screens/EndScreen.h"
#include "screens/MenuScreen.h"
#include "screens/PlayScreen.h"
#include "ui/Draw.h"

App::App() = default;
App::~App() = default;

bool App::init(const std::string& exePath) {
    std::string err;
    {
        std::error_code ec;
        saveDir = std::filesystem::absolute(exePath, ec).parent_path().string();
    }
    if (!theme.load(exePath, err)) {
        std::cerr << "[RyytliRNG] " << err << "\n";
        return false;
    }
    try {
        content.load(theme.root());
    } catch (const std::exception& e) {
        std::cerr << "[RyytliRNG] content error: " << e.what() << "\n";
        return false;
    }
    return true;
}

int App::run(int argc, char** argv) {
    std::string tourDir;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--seed" && i + 1 < argc) seed_ = std::uint32_t(std::stoul(argv[++i]));
        else if (a == "--tour" && i + 1 < argc) tourDir = argv[++i];
    }

    // Everything printed to stderr (ours + SFML's) also goes to ryytlirng.log next to the exe,
    // so problems on Windows (no console) can be diagnosed.
    {
        std::error_code ec;
        auto logPath = std::filesystem::absolute(argc > 0 ? argv[0] : "", ec).parent_path() / "ryytlirng.log";
        static std::ofstream log(logPath);
        if (log) {
            std::cerr.rdbuf(log.rdbuf());
            sf::err().rdbuf(log.rdbuf());
        }
    }
    if (!init(argc > 0 ? argv[0] : "")) return 1;

    window_.create(sf::VideoMode({unsigned(kW), unsigned(kH)}), "RyytliRNG", sf::Style::Default);
    window_.setVerticalSyncEnabled(true);
    window_.setKeyRepeatEnabled(false);
    if (!scene_.resize({unsigned(kW), unsigned(kH)})) {
        std::cerr << "[RyytliRNG] could not create render texture\n";
        return 1;
    }
    scene_.setSmooth(true);
    letterbox();
    fx.init();
    audio.init(theme.root());

    menu_ = std::make_unique<MenuScreen>(*this);
    loading_ = std::make_unique<LoadingScreen>(*this);
    play_ = std::make_unique<PlayScreen>(*this);
    blackout_ = std::make_unique<BlackoutScreen>(*this);
    end_ = std::make_unique<EndScreen>(*this);
    menu_->enter();

    if (!tourDir.empty()) return runTour(tourDir);

    sf::Clock clock;
    while (window_.isOpen() && !quit_) {
        float dt = std::min(clock.restart().asSeconds(), 0.1f);
        while (const std::optional ev = window_.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window_.close();
            if (ev->is<sf::Event::Resized>()) letterbox();
            if (const auto* mm = ev->getIf<sf::Event::MouseMoved>()) mouse_ = window_.mapPixelToCoords(mm->position);
            if (const auto* k = ev->getIf<sf::Event::KeyPressed>()) {
                using K = sf::Keyboard::Key;
                if (k->code == K::M) audio.setMuted(!audio.muted());
                if (k->code == K::Hyphen || k->code == K::Subtract) audio.changeVolume(-0.1f);
                if (k->code == K::Equal || k->code == K::Add) audio.changeVolume(0.1f);
            }
            if (fade_ == Fade::None) screen(current_)->event(*ev, mouse_);
        }
        frame(dt);
        render();
    }
    return 0;
}

void App::letterbox() {
    sf::Vector2u ws = window_.getSize();
    float wr = float(ws.x) / float(ws.y), r = kW / kH;
    sf::View v({kW / 2, kH / 2}, {kW, kH});
    if (wr > r) {
        float w = r / wr;
        v.setViewport(sf::FloatRect({(1 - w) / 2, 0}, {w, 1}));
    } else {
        float h = wr / r;
        v.setViewport(sf::FloatRect({0, (1 - h) / 2}, {1, h}));
    }
    window_.setView(v);
}

Screen* App::screen(ScreenId id) {
    switch (id) {
        case ScreenId::Menu: return menu_.get();
        case ScreenId::Loading: return loading_.get();
        case ScreenId::Play: return play_.get();
        case ScreenId::Blackout: return blackout_.get();
        case ScreenId::Ending: return end_.get();
    }
    return menu_.get();
}

void App::go(ScreenId id, bool fade) {
    if (!fade) {
        current_ = id;
        screen(current_)->enter();
        return;
    }
    next_ = id;
    fade_ = Fade::Out;
}

void App::newGame() {
    std::uint32_t seed = seed_ ? *seed_ : std::random_device{}();
    seed_.reset(); // --seed applies to the first run only
    engine.newGame(seed);
}

void App::frame(float dt) {
    time_ += dt;
    fx.update(dt);
    audio.update(dt);

    const float fadeSpeed = 700.f;
    if (fade_ == Fade::Out) {
        fadeAlpha_ += fadeSpeed * dt;
        if (fadeAlpha_ >= 255.f) {
            fadeAlpha_ = 255.f;
            if (next_) {
                current_ = *next_;
                next_.reset();
                screen(current_)->enter();
            }
            fade_ = Fade::In;
        }
    } else if (fade_ == Fade::In) {
        fadeAlpha_ -= fadeSpeed * dt;
        if (fadeAlpha_ <= 0.f) {
            fadeAlpha_ = 0.f;
            fade_ = Fade::None;
        }
    }
    screen(current_)->update(dt, mouse_);
}

void App::render() {
    scene_.clear(sf::Color::Black);
    screen(current_)->draw(scene_);
    if (fadeAlpha_ > 0) draw::rect(scene_, {{0, 0}, {kW, kH}}, sf::Color(0, 0, 0, std::uint8_t(fadeAlpha_)));
    scene_.display();

    window_.clear(sf::Color::Black);
    sf::Sprite s(scene_.getTexture());
    window_.draw(s);
    window_.display();
}

// ---------------------------------------------------------------------------
// --tour <dir>: scripted walkthrough that saves screenshots (for reviewing UI
// without playing). Not used in normal play.
// ---------------------------------------------------------------------------
void App::step(float seconds, sf::Vector2f mouse) {
    mouse_ = mouse;
    const float dt = 1.f / 60.f;
    for (float t = 0; t < seconds; t += dt) {
        while (window_.pollEvent()) {}
        frame(dt);
    }
    render();
}

void App::shot(const std::string& path) {
    sf::Image img = scene_.getTexture().copyToImage();
    if (!img.saveToFile(path)) std::cerr << "could not save " << path << "\n";
    else std::cout << "saved " << path << "\n";
}

int App::runTour(const std::string& outDir) {
    std::filesystem::create_directories(outDir);
    auto P = [&](const std::string& n) { return (std::filesystem::path(outDir) / n).string(); };
    auto& st = engine.mutableState();

    step(1.0f, {640, 360});
    shot(P("01_menu.png"));

    go(ScreenId::Loading, false);
    step(1.2f);
    shot(P("02_loading.png"));

    seed_ = 7;
    newGame();
    go(ScreenId::Play, false);
    step(3.0f, {-100, -100});
    shot(P("03_intro.png"));

    engine.choose(0);              // Let's go
    engine.choose(0);              // walk to Ruutli
    play().sync();
    step(0.4f);
    play().debugSelect(2);
    step(3.0f);
    shot(P("04_ruutli.png"));

    engine.choose(1);              // cigarettes -> Martin
    play().sync();
    step(0.25f);
    shot(P("05_martin_toasts.png"));
    step(2.6f);
    shot(P("05b_martin.png"));

    engine.choose(0);              // smoke with Martin
    st.vars["money"] = 54.6;
    engine.debugEnter("ruutel");
    st.minute = 2 * 60 + 47;
    play().sync();
    step(3.0f);
    play().debugSelect(1);
    step(0.5f);
    shot(P("06_ruutel_hub.png"));
    {
        // menu mockup material: the same frame without the choice list, plus the labels/chips as text
        play().debugHideChoices(true);
        step(0.2f);
        shot(P("mock_bg.png"));
        play().debugHideChoices(false);
        std::ofstream f(std::filesystem::path(outDir) / "mock_choices.txt");
        for (const auto& c : engine.view().choices) {
            f << (c.travel ? "T" : "A") << (c.locked ? "L" : "-") << "\t" << c.label << "\t";
            for (const auto& ch : c.chips) f << ch.text << "|" << int(ch.kind) << "|" << ch.bad << ";";
            f << "\t" << c.lockText << "\n";
        }
    }

    // a few messages with replies so the phone has something to show
    engine.mutableState().minute = 2 * 60 + 47;
    {
        PhoneMessage m1{"Ain", "KUS SA OLED. ostsin liiga palju jooke ja neil on vaja kodu", 110,
                        json::array({json{{"label", "Tulen!"}}, json{{"label", "Ei jõua praegu"}}}), -1};
        PhoneMessage m2{"Toakaaslane", "kui oled HASi juures, too mulle kebab pls. maksan tagasi (ei maksa)", 80,
                        json::array({json{{"label", "Toon ära"}}, json{{"label", "Too ise"}}}), 0};
        st.messages = {m2, m1};
    }
    play().debugPhone(true);
    step(1.0f);
    shot(P("07_phone.png"));
    play().debugPhone(false);
    step(0.6f);

    // phone call: ask Martin where he is
    st.scene.clear();
    engine.call("martin");
    play().sync();
    step(2.5f);
    shot(P("07b_call.png"));
    engine.choose(0);
    play().sync();
    step(3.0f);
    shot(P("07c_where.png"));
    st.scene.clear();

    st.scene.clear();
    engine.debugEnter("ruutel");
    engine.debugScene("tk_pigeons");
    play().sync();
    step(3.0f);
    shot(P("07d_truuse_kaarna.png"));
    engine.debugScene("ets");
    engine.choose(1);
    play().sync();
    step(3.0f);
    shot(P("07e_ets.png"));

    // friend group in Satiir: three of the boys at once, then the hub with the "KOHAL" list
    st.scene.clear();
    st.narration.clear();
    st.location = "satiir";
    st.friendLoc["ain"] = "satiir";
    st.friendLoc["jasper"] = "satiir";
    st.friendLoc["ets"] = "satiir";
    st.friendMoveAt["ain"] = st.friendMoveAt["jasper"] = st.friendMoveAt["ets"] = 9999;
    st.group = {"ain", "jasper", "ets"};
    engine.debugScene("g_ain_jasper");
    play().sync();
    step(3.0f);
    shot(P("07f_group.png"));
    st.group = {"ets", "jasper"};
    engine.debugScene("g_bet");
    play().sync();
    step(3.0f);
    shot(P("07g_group_generic.png"));
    st.scene.clear();
    play().sync();
    step(2.0f);
    shot(P("07h_kohal.png"));

    st.group = {"kardo", "tristan"};
    engine.debugScene("g_kardo_tristan");
    play().sync();
    step(3.0f);
    shot(P("07i_kardo.png"));
    st.location = "ruutel";
    st.items.insert("torta");
    engine.debugScene("truuse_torta");
    play().sync();
    step(3.0f);
    shot(P("07j_torta.png"));
    engine.debugScene("kermo");
    play().sync();
    step(3.0f);
    shot(P("07k_kermo.png"));
    st.vars["problem"] = 72;
    engine.debugScene("altercation");
    play().sync();
    step(3.0f);
    shot(P("07l_odds.png"));
    st.vars["problem"] = 36;
    st.items.erase("torta");
    st.scene.clear();

    engine.debugEnter("seik");
    st.vars["problem"] = 86;
    engine.debugScene("girl");
    play().sync();
    step(3.0f);
    shot(P("08_seik_girl_high_problem.png"));

    engine.debugEnter("genn");
    st.vars["problem"] = 78;
    engine.debugScene("tristan_offer");
    play().sync();
    play().debugSelect(1);
    step(3.0f);
    shot(P("09_tristan_locked.png"));

    engine.choose(0); // drink -> blackout
    play().sync();
    step(0.1f);
    step(1.6f);
    shot(P("10_blackout_montage.png"));
    step(5.0f);
    shot(P("11_blackout_wake.png"));

    // Late blackout -> police bench at dawn
    newGame();
    st.minute = 5 * 60 + 31;
    st.vars["phone"] = 9;
    st.vars["problem"] = 64;
    st.scene.clear();
    engine.debugEnter("police_bench");
    engine.debugScene("police");
    go(ScreenId::Play, false);
    step(3.0f);
    shot(P("12_police_dawn_lowbatt.png"));

    // taxi quote
    st.scene.clear();
    engine.debugEnter("ruutel");
    st.minute = 3 * 60 + 40;
    st.vars["phone"] = 40;
    st.vars["problem"] = 30;
    {
        View v = engine.view();
        engine.choose(int(v.choices.size()) - 1); // taxi
    }
    play().sync();
    step(3.0f);
    shot(P("13_taxi.png"));

    play().debugPause(true);
    step(0.6f);
    shot(P("14_pause.png"));
    play().debugPause(false);

    // Endings
    newGame();
    st.scene.clear();
    st.log = {"Ootasid HASi järjekorras kebabi 31 minutit.", "Tõmbasid Martiniga suitsu.",
              "Elasid Satiiris Jasperi ja Ainiga terve kava üle.", "Kutsusid kell 03:41 takso. Maksid 12.80 \xE2\x82\xAC."};
    st.flags.insert("met_martin");
    st.flags.insert("met_ain");
    st.flags.insert("met_jasper");
    st.vars["aff_martin"] = 1;
    st.vars["aff_ain"] = 3;
    st.vars["beers"] = 5;
    st.items.insert("kebab_takeaway");
    st.flags.insert("smoked_with_martin");
    st.flags.insert("sang_with_ain");
    st.flags.insert("visited_seik");
    st.visited.insert("seik");
    st.vars["money"] = 41.2;
    st.vars["phone"] = 33;
    st.minute = 3 * 60 + 52;
    engine.debugEnter("home");
    go(ScreenId::Ending, false);
    step(4.0f);
    shot(P("15_ending_home.png"));
    end_->openScore();
    step(1.5f);
    shot(P("15b_score.png"));

    newGame();
    st.scene.clear();
    st.log = {"Tegid Seigas shotte.", "Tristan tuli viie Westoniga. Jõid need ära.",
              "Mälu kustus kell 03:12. Ärkasid kell 05:48: Annelinna bussipeatus."};
    st.flags.insert("met_tristan");
    st.vars["aff_tristan"] = 1;
    st.flags.insert("met_boys");
    st.vars["aff_boys"] = 2;
    engine.debugEnter("annelinn");
    engine.mutableState().minute = 5 * 60 + 58;
    engine.choose(0); // long walk -> sunrise
    go(ScreenId::Ending, false);
    step(4.0f);
    shot(P("16_ending_sunrise.png"));
    return 0;
}
