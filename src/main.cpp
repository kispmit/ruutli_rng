// RyytliRNG: get home before sunrise.
//
// Usage:
//   RyytliRNG                     play
//   RyytliRNG --seed 42           play a reproducible night
//   RyytliRNG --simulate 1000     headless: play 1000 random runs, print endings (content sanity check)
//   RyytliRNG --simulate 3 --verbose
//   RyytliRNG --tour shots/       dev: scripted walkthrough that saves UI screenshots

#include <iostream>
#include <string>

#include "App.h"
#include "dev/Simulate.h"

int main(int argc, char** argv) {
    // Headless simulation must not touch SFML graphics (no window/GL context needed).
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--simulate" && i + 1 < argc) {
            bool verbose = false;
            for (int j = 1; j < argc; ++j) verbose |= std::string(argv[j]) == "--verbose";
            std::string root = Theme::findRoot(argv[0]);
            if (root.empty()) { std::cerr << "assets folder not found\n"; return 1; }
            Content content;
            try { content.load(root); } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
            return runSimulation(content, std::stoi(argv[i + 1]), verbose);
        }
    }
    App app;
    return app.run(argc, argv);
}
