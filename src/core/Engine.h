#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/Content.h"
#include "core/Rng.h"

// ---------------------------------------------------------------------------
// Game state: everything that changes during a run.
// Generic numeric vars hold phone/problem/money plus anything content wants
// (counters like "beers", hidden affinity like "aff_ain").
// ---------------------------------------------------------------------------
struct PhoneMessage {
    std::string from;
    std::string text;
    int minute = 0;
    json replies = json::array(); // [{label, do}] answer options shown in the phone
    int answered = -1;            // index of the reply you sent, -1 = not answered
};

struct GameState {
    int minute = 0; // minutes since 00:00
    std::map<std::string, double> vars;
    std::set<std::string> flags;
    std::set<std::string> items;
    std::set<std::string> visited;
    std::set<std::string> usedOnce;  // choice ids picked that were `once`
    std::vector<std::string> tasks;  // active task ids, in order received
    std::set<std::string> side;      // which of those are side quests (everything not drawn at the start)
    int nextSideAt = 0;              // minute when the next side quest may arrive
    std::vector<std::string> log;    // "your night" recap lines
    std::vector<PhoneMessage> messages;
    std::set<std::string> firedMessages;

    std::string location;
    std::map<std::string, std::string> friendLoc; // where each of the boys is right now
    std::map<std::string, int> friendMoveAt;      // minute when they next wander somewhere else
    std::vector<std::string> group; // friend ids in the current group talk ({a},{b},{c}, "@a", "@group")
    std::string scene;        // non-empty = a scene is on top of the location
    std::string narration;    // result text of the last action (shown instead of location text)
    std::string ending;       // non-empty = run is over
    double peakProblem = 0;
    int homeMinute = -1;

    double var(const std::string& k) const {
        auto it = vars.find(k);
        return it == vars.end() ? 0.0 : it->second;
    }
};

// ---------------------------------------------------------------------------
// What the UI needs to draw. Built fresh by Engine::view().
// ---------------------------------------------------------------------------
struct Chip {
    std::string text;
    enum class Kind { Time, Money, Problem, Phone, Risk } kind;
    bool bad = true; // red-ish vs green-ish
};

struct ViewChoice {
    std::string label;
    std::vector<Chip> chips;
    bool locked = false;
    std::string lockText;
    bool travel = false;
    int index = 0; // pass back to Engine::choose
};

struct View {
    std::string locationName;
    std::string locationSub;
    std::string image;
    std::string speaker;
    std::string portrait;   // character image to show while someone talks to you
    std::string text;
    std::string music;                // scene music override (empty = location ambience)
    std::vector<std::string> present; // friends hanging out here right now (upper-case names)
    bool inScene = false;
    std::vector<ViewChoice> choices;
};

// One-shot notifications for the UI (toasts, transitions).
struct UiEvent {
    enum class Type { StatDelta, Message, Blackout, TaskDone, TaskAdded, Ended, Travel };
    UiEvent(Type t = Type::StatDelta) : type(t) {}
    Type type;
    std::string key;     // stat name / sender / task id / location id
    double amount = 0;
    std::string text;
    std::vector<std::string> images; // blackout montage
    int fromMinute = 0, toMinute = 0;
};

struct TaskView {
    std::string title;
    std::string desc;
    bool done = false;  // for final tasks: "currently satisfied"
    bool final = false; // judged on arrival home
    bool side = false;  // side quest (white) vs main quest (yellow)
    bool failed = false;   // can no longer be done (e.g. blacked out)
    std::string progress;  // "3/6" for counted tasks
};

struct ScoreLine {
    std::string label;
    int points = 0;
};

struct Recap {
    EndingDef ending;
    std::vector<std::string> night;
    std::vector<std::string> friends;
    std::vector<std::pair<std::string, std::string>> stats;
    std::vector<std::pair<std::string, bool>> tasks;     // main quests: title, done
    std::vector<std::pair<std::string, bool>> sideTasks; // side quests: title, done
    std::vector<ScoreLine> score;                        // breakdown, see config "score"
    int total = 0;
};

class Engine {
public:
    explicit Engine(const Content& content);

    void newGame(std::uint32_t seed);
    View view() const;
    void choose(int index);

    const GameState& state() const { return s_; }
    std::vector<UiEvent> drainEvents();
    std::vector<TaskView> taskViews() const;
    bool isOver() const { return !s_.ending.empty(); }
    Recap recap() const;

    int sunrise() const { return sunrise_; }
    int startMinute() const { return startMinute_; }
    std::uint32_t seed() const { return rng_.seed(); }

    // Debug helpers (used by --tour / --simulate)
    GameState& mutableState() { return s_; }
    void debugEnter(const std::string& loc) { enterLocation(loc); }
    void debugScene(const std::string& sc) { setScene(sc); }

    // Phone contacts
    struct Contact { std::string id, name; };
    std::vector<Contact> contacts() const;
    bool canCall() const;                 // phone alive, not mid-conversation, not home
    void call(const std::string& friendId); // opens scene "call_<id>"
    bool canReply() const { return canCall(); }
    void reply(std::size_t message, std::size_t option); // answer a phone message
    std::string friendName(const std::string& id) const;
    int travelTime(const std::string& from, const std::string& to) const;

    static std::string fmtClock(int minute);
    static int parseClock(const std::string& hhmm);

private:
    // choice list with index mapping back to source
    struct Resolved {
        const Choice* choice = nullptr;
        bool travel = false;
        bool taxi = false;
    };
    std::vector<Resolved> currentChoices() const;
    void execute(const Choice& ch, bool fromLocation);

    bool check(const json& conds) const;
    bool checkOne(const json& c) const;
    bool affordable(const Choice& c, std::string* why) const;
    std::vector<Chip> chipsFor(const Choice& c) const;
    double chanceOf(const json& e) const;      // probability of a {"chance": ...} effect's "then"
    double timeScale() const;                  // config "time_scale": < 1 = the night moves slower
    json scaledTime(const json& v) const;      // time value / range after time_scale

    void applyEffects(const json& fx);
    void applyEffect(const json& e);
    double roll(const json& v, const std::string& key);
    void addVar(const std::string& k, double amount, bool notify = true);
    void advanceTime(int minutes);
    void checkMessages();
    void checkTasks();
    void rollSideQuest();
    void enterLocation(const std::string& id);
    void setScene(const std::string& id);
    void rollEncounters(const std::string& trigger);
    bool rollRoaming();
    void rerollFriends(bool force);
    std::string meetScene(const std::string& friendId) const;
    bool friendAway(const std::string& friendId) const;     // at home tonight / asleep
    std::vector<std::string> presentFriends() const;        // ids at your location
    std::string pickGroupScene(bool freshOnly);             // sets s_.group; "" if nothing fits
    std::vector<std::string> groupIds(const std::string& key) const; // "@a" / "@group" / plain id
    std::string friendPortrait(const std::string& friendId) const;
    std::string pendingMeet_;
    std::string bestFriend() const; // display name of the friend who likes you most
    void recomputeAffinity();
    void doBlackout();
    void makeTaxiQuote();
    void finish(const std::string& ending);
    void arriveHome();
    std::string substitute(std::string text) const;

    const Content& c_;
    GameState s_;
    mutable Rng rng_;
    std::vector<UiEvent> events_;
    std::set<std::string> doneTasks_; // to fire TaskDone once

    int sunrise_ = 360;
    int startMinute_ = 0;

    // pending transitions while applying a choice's effects
    std::string pendingGoto_;
    std::string pendingScene_;
    bool blackedOut_ = false;

    // taxi quote shown in the built-in "__taxi" scene
    double taxiPrice_ = 0;
    int taxiWait_ = 0;
    bool taxiSurge_ = false;

    std::map<std::string, double> lastRolled_; // for {time}/{money} in log text
    mutable Choice taxiBook_, taxiCancel_, taxiCall_, taxiAsk_; // built-in taxi choices
    mutable Choice joinFriends_;                                // built-in "go talk to them"
    public: static std::string upper(std::string s);
};
