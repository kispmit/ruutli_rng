#pragma once
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Everything in here is loaded from assets/content/*.json.
// Conditions and effects are kept as raw JSON and interpreted by the Engine,
// so adding a new kind of effect is one `if` in Engine::applyEffect.

struct Choice {
    std::string label;
    json showIf = json::array();     // all must pass for the choice to be visible at all
    json conditions = json::array(); // all must pass, otherwise locked/hidden
    json effects = json::array();    // applied in order
    std::string next;                // scene to show after (empty = back to location)
    std::string lockedText;          // shown when locked (if set, choice is shown greyed out instead of hidden)
    bool once = false;               // disappears after being picked
    std::string id;                  // auto-generated, used for `once`
};

struct Scene {
    std::string id;
    std::string speaker;
    std::string text;
    std::string image;               // optional: overrides location background
    std::string portrait;            // optional: character image (else config.portraits[speaker])
    std::vector<Choice> choices;
    json redirectIf = nullptr;       // {"if":[conditions], "to":"scene"}
    std::string music;               // optional: ambience override while the scene is open
    json onEnter = nullptr;          // effects applied when the scene opens (task_add, flag, message...)
};

struct Encounter {
    std::string id;
    std::string trigger = "enter";   // "enter" = on arriving, "action" = after any action here
    double chance = 1.0;
    json chanceScale = nullptr;      // {"var":"problem","from":50,"to":100,"min":0,"max":0.6}
    bool once = true;
    json conditions = json::array();
    std::vector<std::pair<double, std::string>> outcomes; // weight -> scene id
};

struct Location {
    std::string id;
    std::string name;
    std::string subtitle;
    std::string image;
    std::string ambience; // looping audio, e.g. "audio/amb/seik.ogg"
    std::string in;       // Estonian inessive for phone replies: "Seigas", "Satiiris"
    std::string text;
    std::vector<Choice> actions;
    std::vector<Choice> exits;
    std::vector<Encounter> encounters;
    bool isHome = false;
    json gate = nullptr; // {"if":[...], "to":"other_location"}: entering redirects there while the condition holds
    bool allowTaxi = true;
};

struct TaskDef {
    std::string id;
    std::string title;
    std::string desc;
    json doneWhen = json::array();
    bool final = false; // only judged when you get home (e.g. "come home with 50 EUR")
    json progress = nullptr; // {"var": "drinks", "target": 6} -> HUD shows "3/6"
    json failIf = json::array(); // once these hold the task can't be done any more (shown red)
};

struct RecapSnippet {
    std::string section;  // "night" or "friends"
    std::string npc;      // friends: only the first matching snippet per npc is used
    json conditions = json::array();
    std::string text;
};

struct EndingDef {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string tone; // "good" | "neutral" | "bad"
};

struct Content {
    std::map<std::string, Location> locations;
    std::map<std::string, Scene> scenes;
    std::map<std::string, TaskDef> tasks;
    std::map<std::string, EndingDef> endings;
    std::vector<RecapSnippet> recap;
    std::vector<Choice> globalActions; // config.global_actions: offered at every location except home
    json config; // start stats, sunrise, drains, taxi table, blackout, messages, npc names

    // Loads every file in <assetRoot>/content. Throws std::runtime_error with a readable message.
    void load(const std::string& assetRoot);
};
