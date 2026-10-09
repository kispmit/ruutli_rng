#pragma once
#include <memory>
#include <optional>
#include <string>

#include <SFML/Graphics.hpp>

#include "audio/Audio.h"
#include "core/Content.h"
#include "core/Engine.h"
#include "ui/Fx.h"
#include "ui/Theme.h"

class Screen;
class MenuScreen;
class LoadingScreen;
class PlayScreen;
class BlackoutScreen;
class EndScreen;

enum class ScreenId { Menu, Loading, Play, Blackout, Ending };

class App {
public:
    App();
    ~App();

    // Returns process exit code.
    int run(int argc, char** argv);

    // --- used by screens ---
    void go(ScreenId id, bool fade = true);
    void newGame();
    void quit() { quit_ = true; }
    float time() const { return time_; }

    Theme theme;
    Content content;
    Engine engine{content};
    Fx fx;
    Audio audio;

    // Blackout data handed from Play -> Blackout screen
    UiEvent pendingBlackout;
    std::string saveDir; // next to the exe: high scores live here

    PlayScreen& play() { return *play_; }

private:
    bool init(const std::string& exePath);
    Screen* screen(ScreenId id);
    void frame(float dt);
    void render();
    void letterbox();
    int runTour(const std::string& outDir);
    void step(float seconds, sf::Vector2f mouse = {-100, -100});
    void shot(const std::string& path);

    sf::RenderWindow window_;
    sf::RenderTexture scene_;
    std::unique_ptr<MenuScreen> menu_;
    std::unique_ptr<LoadingScreen> loading_;
    std::unique_ptr<PlayScreen> play_;
    std::unique_ptr<BlackoutScreen> blackout_;
    std::unique_ptr<EndScreen> end_;

    ScreenId current_ = ScreenId::Menu;
    std::optional<ScreenId> next_;
    enum class Fade { None, Out, In } fade_ = Fade::None;
    float fadeAlpha_ = 0;

    float time_ = 0;
    bool quit_ = false;
    std::optional<std::uint32_t> seed_;
    sf::Vector2f mouse_{-100, -100};
};
