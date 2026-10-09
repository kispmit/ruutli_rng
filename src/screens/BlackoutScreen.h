#pragma once
#include <string>
#include <vector>

#include "screens/Screen.h"

// Blackout: the tape fast-forwards. Random flash images, the timestamp races
// ahead, then PLAY and you wake up somewhere.
class BlackoutScreen : public Screen {
public:
    using Screen::Screen;
    void enter() override;
    void event(const sf::Event& e, sf::Vector2f mouse) override;
    void update(float dt, sf::Vector2f mouse) override;
    void draw(sf::RenderTarget& rt) override;

private:
    float t_ = 0;
    float total_ = 0;
    std::vector<std::string> images_;
    int from_ = 0, to_ = 0;
    bool done_ = false;
    static constexpr float kIntro = 0.7f, kPerImage = 0.75f, kOutro = 1.6f;
};
