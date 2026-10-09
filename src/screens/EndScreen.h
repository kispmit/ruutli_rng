#pragma once
#include <vector>

#include "core/Engine.h"
#include "screens/Screen.h"

// End of tape: outcome title + "your night" log + how your friends feel + tape info.
class EndScreen : public Screen {
public:
    using Screen::Screen;
    void enter() override;
    void event(const sf::Event& e, sf::Vector2f mouse) override;
    void update(float dt, sf::Vector2f mouse) override;
    void draw(sf::RenderTarget& rt) override;
    void openScore() { scoreOpen_ = true; scoreT_ = 0; } // used by --tour

private:
    void activate(int i);
    Recap recap_;
    float t_ = 0;
    int sel_ = 0;
    bool keyboard_ = false;
    std::vector<sf::FloatRect> buttons_;

    // score window ("SKOOR" tab): breakdown + high score table, saved next to the exe
    struct HighScore { int score = 0; std::string ending, date; unsigned seed = 0; };
    void saveHighScore();
    void drawScore(sf::RenderTarget& rt);
    std::vector<HighScore> table_;
    int rank_ = -1;          // where this run landed in the table (-1 = not in top 10)
    bool scoreOpen_ = false;
    float scoreT_ = 0;
    sf::FloatRect closeRect_;
};
