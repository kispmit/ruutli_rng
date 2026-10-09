#include "core/Engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {

const std::set<std::string> kMainStats = {"phone", "problem", "money"};

json arr(const json& j) {
    if (j.is_null()) return json::array();
    return j.is_array() ? j : json::array({j});
}

double round2(double v) { return std::round(v * 100.0) / 100.0; }

std::string fmtMoney(double v) {
    char buf[32];
    if (std::fabs(v - std::round(v)) < 0.005) std::snprintf(buf, sizeof buf, "%.0f", v);
    else std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

// Range or number -> pair (min,max)
std::pair<double, double> span(const json& v) {
    if (v.is_array() && v.size() == 2) {
        double a = v[0].get<double>(), b = v[1].get<double>();
        return {std::min(a, b), std::max(a, b)};
    }
    if (v.is_number()) return {v.get<double>(), v.get<double>()};
    return {0, 0};
}

std::string fmtSpan(const json& v, bool money) {
    auto [a, b] = span(v);
    auto f = [&](double x) { return money ? fmtMoney(std::fabs(x)) : std::to_string(int(std::lround(std::fabs(x)))); };
    if (a == b) return f(a);
    // for negative ranges show smaller magnitude first
    if (a < 0 && b <= 0) return f(b) + "-" + f(a);
    return f(a) + "-" + f(b);
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (std::size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
}

} // namespace

Engine::Engine(const Content& content) : c_(content) {}

std::string Engine::fmtClock(int minute) {
    int m = ((minute % 1440) + 1440) % 1440;
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", m / 60, m % 60);
    return buf;
}

int Engine::parseClock(const std::string& hhmm) {
    int h = 0, m = 0;
    if (std::sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2) throw std::runtime_error("Bad time '" + hhmm + "', use HH:MM");
    return h * 60 + m;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void Engine::newGame(std::uint32_t seed) {
    rng_.reseed(seed);
    s_ = GameState{};
    events_.clear();
    doneTasks_.clear();
    lastRolled_.clear();

    const json& cfg = c_.config;
    startMinute_ = parseClock(cfg.value("start_time", "00:00"));
    sunrise_ = parseClock(cfg.value("sunrise", "06:00"));
    s_.minute = startMinute_;

    const json startVars = cfg.value("start", json::object());
    for (auto& [k, v] : startVars.items()) s_.vars[k] = roll(v, k); // numbers or [min,max] ranges
    rerollFriends(true);
    s_.peakProblem = s_.var("problem");
    {
        const json sq = cfg.value("side_quests", json::object());
        s_.nextSideAt = parseClock(sq.value("first_at", "00:15"));
    }

    // Random starting tasks. "task_groups" = draw N from each pool (e.g. 2 "do something" + 1 constraint);
    // otherwise draw start_tasks from task_pool.
    // weighted draw without repeats: "weights": {"early_home": 0.4} (default weight 1)
    auto draw = [&](std::vector<std::string> pool, int count, const json& weights) {
        count = std::min<int>(count, int(pool.size()));
        for (int i = 0; i < count; ++i) {
            std::vector<double> w;
            for (std::size_t k = 0; k < pool.size(); ++k) w.push_back(weights.value(pool[k], 1.0));
            int j = rng_.weighted(w);
            s_.tasks.push_back(pool[j]);
            pool.erase(pool.begin() + j);
        }
    };
    if (cfg.contains("task_groups")) {
        for (const auto& g : cfg["task_groups"]) {
            auto [gmin, gmax] = span(g.value("count", json(1)));
            draw(g.value("pool", std::vector<std::string>{}), rng_.range(int(gmin), int(gmax)), g.value("weights", json::object()));
        }
    } else {
        auto [tmin, tmax] = span(cfg.value("start_tasks", json::array({2, 3})));
        draw(cfg.value("task_pool", std::vector<std::string>{}), rng_.range(int(tmin), int(tmax)), json::object());
    }

    enterLocation(cfg.at("start_location").get<std::string>());
    events_.clear(); // no toasts for the initial setup
    if (cfg.contains("intro_scene")) setScene(cfg["intro_scene"].get<std::string>());
}

std::vector<UiEvent> Engine::drainEvents() {
    std::vector<UiEvent> out;
    out.swap(events_);
    return out;
}

// ---------------------------------------------------------------------------
// Conditions
// ---------------------------------------------------------------------------
bool Engine::check(const json& conds) const {
    for (const auto& c : arr(conds))
        if (!checkOne(c)) return false;
    return true;
}

bool Engine::checkOne(const json& c) const {
    if (!c.is_object()) return true;
    if (c.contains("var")) {
        double v = s_.var(c["var"].get<std::string>());
        std::string op = c.value("op", "");
        double target = c.value("value", 0.0);
        for (const char* o : {">=", "<=", ">", "<", "=="})
            if (c.contains(o)) { op = o; target = c[o].get<double>(); }
        if (op == ">") return v > target;
        if (op == ">=") return v >= target;
        if (op == "<") return v < target;
        if (op == "<=") return v <= target;
        if (op == "==") return std::fabs(v - target) < 1e-6;
        return v != 0;
    }
    if (c.contains("flag")) return s_.flags.count(c["flag"].get<std::string>()) > 0;
    if (c.contains("not_flag")) return s_.flags.count(c["not_flag"].get<std::string>()) == 0;
    if (c.contains("item")) return s_.items.count(c["item"].get<std::string>()) > 0;
    if (c.contains("not_item")) return s_.items.count(c["not_item"].get<std::string>()) == 0;
    if (c.contains("at")) {
        for (const auto& l : arr(c["at"]))
            if (l.get<std::string>() == s_.location) return true;
        return false;
    }
    if (c.contains("not_at")) {
        for (const auto& l : arr(c["not_at"]))
            if (l.get<std::string>() == s_.location) return false;
        return true;
    }
    if (c.contains("friend_here") || c.contains("not_friend_here")) {
        // is this friend hanging out at your location right now (and not at home)?
        const json& v = c.contains("friend_here") ? c["friend_here"] : c["not_friend_here"];
        bool any = false;
        for (const auto& f : arr(v)) {
            std::string id = f.get<std::string>();
            auto it = s_.friendLoc.find(id);
            if (it != s_.friendLoc.end() && it->second == s_.location && !friendAway(id)) any = true;
        }
        return c.contains("friend_here") ? any : !any;
    }
    if (c.contains("present_min")) return int(presentFriends().size()) >= c["present_min"].get<int>();
    if (c.contains("ending")) {
        for (const auto& e : arr(c["ending"])) if (s_.ending == e.get<std::string>()) return true;
        return false;
    }
    if (c.contains("visited")) return s_.visited.count(c["visited"].get<std::string>()) > 0;
    if (c.contains("not_visited")) return s_.visited.count(c["not_visited"].get<std::string>()) == 0;
    if (c.contains("task")) return std::count(s_.tasks.begin(), s_.tasks.end(), c["task"].get<std::string>()) > 0;
    if (c.contains("before")) return s_.minute < parseClock(c["before"].get<std::string>());
    if (c.contains("after")) return s_.minute >= parseClock(c["after"].get<std::string>());
    if (c.contains("any")) {
        for (const auto& sub : c["any"])
            if (checkOne(sub)) return true;
        return false;
    }
    if (c.contains("not")) return !checkOne(c["not"]);
    return true;
}

bool Engine::affordable(const Choice& ch, std::string* why) const {
    double cost = 0;
    for (const auto& e : arr(ch.effects)) {
        if (!e.is_object()) continue;
        json m = e.contains("money") ? e["money"] : (e.contains("add") && e["add"].contains("money") ? e["add"]["money"] : json());
        if (m.is_null()) continue;
        auto [a, b] = span(m);
        if (a < 0) cost += -a;
    }
    if (cost > 0 && s_.var("money") + 1e-6 < cost) {
        if (why) *why = "Raha ei jätku";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Choices / view
// ---------------------------------------------------------------------------
std::vector<Engine::Resolved> Engine::currentChoices() const {
    std::vector<Resolved> out;
    auto add = [&](const std::vector<Choice>& list, bool travel) {
        for (const auto& ch : list) {
            if (ch.once && s_.usedOnce.count(ch.id)) continue;
            if (!check(ch.showIf)) continue;
            bool ok = check(ch.conditions);
            if (!ok && ch.lockedText.empty()) continue; // hidden
            out.push_back({&ch, travel, false});
        }
    };

    if (!s_.scene.empty()) {
        if (s_.scene == "__taxi") {
            Choice& book = taxiBook_;
            Choice& cancel = taxiCancel_;
            book = Choice{};
            book.label = "Telli ära";
            book.id = "__taxi.book";
            book.effects = json::array({json{{"taxi_ride", true}}});
            book.conditions = json::array({json{{"var", "money"}, {">=", taxiPrice_}}});
            book.lockedText = "Raha ei jätku";
            cancel = Choice{};
            cancel.label = "Ei, kõnnin parem";
            cancel.id = "__taxi.cancel";
            out.push_back({&book, true, true});
            if (s_.var("money") + 1e-6 < taxiPrice_) {
                Choice& ask = taxiAsk_;
                ask = Choice{};
                ask.label = "Kirjuta poistele, et keegi tellib sulle";
                ask.id = "__taxi.ask";
                const json help = c_.config.value("taxi", json::object()).value("friend_help", json::object());
                ask.effects = json::array({json{{"time", json::array({5, 15})}},
                                           json{{"chance", help.value("base", 0.2)}, {"per_var", "aff_total"}, {"per", help.value("per_affinity", 0.07)},
                                                {"then", json::array({json{{"friend_taxi", true}}})},
                                                {"else", json::array({json{{"say", "Kirjutad kõigile. Kaks sinist linnukest, null vastust. Klassika."}}})}}});
                out.push_back({&ask, true, true});
            }
            out.push_back({&cancel, false, true});
            return out;
        }
        add(c_.scenes.at(s_.scene).choices, false);
        return out;
    }

    const Location& loc = c_.locations.at(s_.location);
    add(loc.actions, false);
    add(loc.exits, true);
    if (!loc.isHome) add(c_.globalActions, false);
    if (!loc.isHome) {
        auto here = presentFriends();
        if (!here.empty()) {
            Choice& j = joinFriends_;
            j = Choice{};
            std::string names;
            for (std::size_t i = 0; i < here.size(); ++i)
                names += (i == 0 ? "" : (i + 1 == here.size() ? " ja " : ", ")) + friendName(here[i]);
            j.label = (here.size() == 1 ? "Mine räägi: " : "Liitu: ") + names;
            j.id = "__join";
            j.effects = json::array({json{{"join_friends", true}}});
            out.push_back({&j, false, true});
        }
    }
    if (loc.allowTaxi) {
        Choice& taxi = taxiCall_;
        taxi = Choice{};
        taxi.label = "Kutsu takso";
        taxi.id = "__taxi";
        taxi.effects = json::array({json{{"phone", -2}}, json{{"taxi", true}}});
        taxi.conditions = json::array({json{{"var", "phone"}, {">", 0}}});
        taxi.lockedText = "Telefon on tühi";
        out.push_back({&taxi, true, true});
    }
    return out;
}

std::vector<Chip> Engine::chipsFor(const Choice& ch) const {
    std::vector<Chip> chips;
    bool risk = false;
    for (const auto& e : arr(ch.effects)) {
        if (!e.is_object()) continue;
        if (e.contains("time")) chips.push_back({fmtSpan(scaledTime(e["time"]), false) + " MIN", Chip::Kind::Time, true});
        auto stat = [&](const char* key, const json& v) {
            auto [a, b] = span(v);
            bool neg = b <= 0;
            std::string sign = neg ? "-" : "+";
            if (std::string(key) == "money")
                chips.push_back({sign + fmtSpan(v, true) + " \xE2\x82\xAC", Chip::Kind::Money, neg});
            else if (std::string(key) == "problem")
                chips.push_back({sign + fmtSpan(v, false) + " PROB", Chip::Kind::Problem, !neg});
            else if (std::string(key) == "phone")
                chips.push_back({sign + fmtSpan(v, false) + "% AKU", Chip::Kind::Phone, neg});
        };
        for (const char* k : {"money", "problem", "phone"})
            if (e.contains(k)) stat(k, e[k]);
        if (e.contains("taxi_ride")) {
            int ride = c_.config.value("taxi", json::object()).value("ride_minutes", 8);
            chips.push_back({std::to_string(taxiWait_ + ride) + " MIN", Chip::Kind::Time, true});
            chips.push_back({"-" + fmtMoney(taxiPrice_) + " \xE2\x82\xAC", Chip::Kind::Money, true});
        }
        // RISK only if the random outcome can actually change something (not just a different line of text)
        std::function<bool(const json&)> consequential = [&](const json& fx) {
            for (const auto& x : arr(fx)) {
                if (!x.is_object()) continue;
                for (auto& [k, v] : x.items()) {
                    if (k == "say" || k == "log") continue;
                    if (k == "random") { for (const auto& r : v) if (consequential(r.value("do", json()))) return true; continue; }
                    return true;
                }
            }
            return false;
        };
        // odds: show the actual percentage when we can (chance = "then" branch, 2-way random = first outcome)
        if (e.contains("chance")) {
            int pct = int(std::lround(chanceOf(e) * 100));
            chips.push_back({std::to_string(pct) + "% ŠANSS", Chip::Kind::Risk, pct < 50});
            continue;
        }
        if (e.contains("random")) {
            bool cons = false;
            for (const auto& r : e["random"]) if (consequential(r.value("do", json()))) cons = true;
            if (!cons) continue;
            if (e["random"].size() == 2) {
                double w0 = e["random"][0].value("weight", 1.0), w1 = e["random"][1].value("weight", 1.0);
                int pct = int(std::lround(100.0 * w0 / std::max(1e-9, w0 + w1)));
                chips.push_back({std::to_string(pct) + "% ŠANSS", Chip::Kind::Risk, pct < 50});
            } else {
                risk = true;
            }
        }
        if (e.contains("blackout")) chips.push_back({"BLACKOUT", Chip::Kind::Risk, true});
    }
    if (risk) chips.push_back({"RISK", Chip::Kind::Risk, true});
    return chips;
}

View Engine::view() const {
    View v;
    const Location& loc = c_.locations.at(s_.location);
    v.locationName = loc.name;
    v.locationSub = loc.subtitle;
    v.image = loc.image;

    if (!s_.scene.empty()) {
        v.inScene = true;
        if (s_.scene == "__taxi") {
            v.speaker = "BOLT";
            std::string t = "Juht leitud, jõuab {wait} minutiga. Sõit koju: {price} \xE2\x82\xAC.";
            if (taxiSurge_) t += "\n\nPraegu on suur nõudlus. Hinnad on tõstetud.";
            v.text = substitute(t);
        } else {
            const Scene& sc = c_.scenes.at(s_.scene);
            v.speaker = substitute(sc.speaker);
            v.music = sc.music;
            v.portrait = sc.portrait;
            if (v.portrait == "@group") {
                v.portrait.clear();
                for (std::size_t i = 0; i < s_.group.size() && i < 4; ++i) {
                    if (!v.portrait.empty()) v.portrait += "|";
                    v.portrait += friendPortrait(s_.group[i]);
                }
            }
            if (v.portrait.empty()) {
                const json portraits = c_.config.value("portraits", json::object());
                if (portraits.contains(sc.speaker)) {
                    // one image, or [a, b] for two people talking to you at once ("a|b")
                    for (const auto& p : arr(portraits[sc.speaker])) {
                        if (!v.portrait.empty()) v.portrait += "|";
                        v.portrait += p.get<std::string>();
                    }
                }
            }
            // result of the last choice (e.g. "Martin: Olen Seigas.") comes first, then the scene text
            v.text = s_.narration.empty() ? substitute(sc.text) : s_.narration + "\n\n" + substitute(sc.text);
            if (!sc.image.empty()) v.image = sc.image;
        }
    } else {
        v.text = s_.narration.empty() ? loc.text : s_.narration;
    }

    if (!loc.isHome)
        for (const auto& id : presentFriends()) v.present.push_back(upper(friendName(id)));

    auto list = currentChoices();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Choice& ch = *list[i].choice;
        ViewChoice vc;
        vc.label = substitute(ch.label);
        vc.index = int(i);
        vc.travel = list[i].travel;
        vc.chips = chipsFor(ch);
        std::string why;
        if (!check(ch.conditions)) {
            vc.locked = true;
            vc.lockText = ch.lockedText;
        } else if (!affordable(ch, &why)) {
            vc.locked = true;
            vc.lockText = why;
        }
        v.choices.push_back(std::move(vc));
    }
    return v;
}

void Engine::choose(int index) {
    if (isOver()) return;
    auto list = currentChoices();
    if (index < 0 || index >= int(list.size())) return;
    const Choice ch = *list[index].choice; // copy: the source may change underneath us
    if (!check(ch.conditions) || !affordable(ch, nullptr)) return;

    if (ch.once) s_.usedOnce.insert(ch.id);
    execute(ch, s_.scene.empty());
}

void Engine::execute(const Choice& ch, bool fromLocation) {
    pendingGoto_.clear();
    pendingScene_.clear();
    pendingMeet_.clear();
    blackedOut_ = false;
    s_.narration.clear();

    applyEffects(ch.effects);
    if (isOver() || blackedOut_) { checkTasks(); return; }

    if (!pendingGoto_.empty()) {
        s_.scene.clear();
        enterLocation(pendingGoto_);
        if (isOver()) return;
    }
    if (!pendingMeet_.empty()) pendingScene_ = meetScene(pendingMeet_); // you found them / they found you
    if (!pendingScene_.empty()) setScene(pendingScene_);
    else if (!ch.next.empty()) setScene(ch.next);
    else if (pendingGoto_.empty()) {
        s_.scene.clear();
        if (fromLocation) rollEncounters("action");
    }

    // Problem bar maxed out: forced bad event (no choice)
    if (!isOver() && s_.var("problem") >= 100 && c_.config.contains("meltdown_scene")) {
        std::string m = c_.config["meltdown_scene"].get<std::string>();
        if (s_.scene != m) setScene(m);
    }
    checkTasks();
}

void Engine::reply(std::size_t mi, std::size_t ri) {
    if (!canReply() || mi >= s_.messages.size()) return;
    PhoneMessage& m = s_.messages[mi];
    if (m.answered >= 0 || ri >= m.replies.size()) return;
    m.answered = int(ri);
    Choice ch;
    ch.label = m.replies[ri].value("label", "");
    ch.effects = m.replies[ri].contains("do") ? m.replies[ri]["do"] : json::array();
    if (!ch.effects.is_array()) ch.effects = json::array({ch.effects});
    ch.next = m.replies[ri].value("next", "");
    execute(ch, false);
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------
void Engine::applyEffects(const json& fx) {
    for (const auto& e : arr(fx)) {
        if (isOver() || blackedOut_) return;
        applyEffect(e);
    }
}

double Engine::roll(const json& v, const std::string& key) {
    double out = 0;
    if (v.is_array() && v.size() == 2) {
        if (v[0].is_number_integer() && v[1].is_number_integer())
            out = rng_.range(v[0].get<int>(), v[1].get<int>());
        else
            out = round2(rng_.real(v[0].get<double>(), v[1].get<double>()));
    } else if (v.is_number()) {
        out = v.get<double>();
    }
    lastRolled_[key] = std::fabs(out);
    return out;
}

void Engine::addVar(const std::string& k, double amount, bool notify) {
    double before = s_.var(k);
    double after = before + amount;
    if (k == "phone" || k == "problem") after = std::clamp(after, 0.0, 100.0);
    if (k == "money") after = std::max(0.0, round2(after));
    s_.vars[k] = after;
    if (k == "problem") s_.peakProblem = std::max(s_.peakProblem, after);
    double d = after - before;
    if (notify && std::fabs(d) > 1e-6 && kMainStats.count(k)) {
        UiEvent ev{UiEvent::Type::StatDelta};
        ev.key = k;
        ev.amount = d;
        events_.push_back(ev);
    }
}

void Engine::applyEffect(const json& e) {
    if (!e.is_object()) return;

    if (e.contains("time")) advanceTime(int(roll(scaledTime(e["time"]), "time")));
    if (isOver()) return;
    for (const char* k : {"phone", "problem", "money"})
        if (e.contains(k)) addVar(k, roll(e[k], k));
    if (e.contains("add"))
        for (auto& [k, v] : e["add"].items()) addVar(k, roll(v, k));
    if (e.contains("set"))
        for (auto& [k, v] : e["set"].items()) addVar(k, roll(v, k) - s_.var(k));

    if (e.contains("flag")) for (const auto& f : arr(e["flag"])) s_.flags.insert(f.get<std::string>());
    if (e.contains("unflag")) for (const auto& f : arr(e["unflag"])) s_.flags.erase(f.get<std::string>());
    if (e.contains("item")) for (const auto& f : arr(e["item"])) s_.items.insert(f.get<std::string>());
    if (e.contains("remove_item")) for (const auto& f : arr(e["remove_item"])) s_.items.erase(f.get<std::string>());

    if (e.contains("aff"))
        for (auto& [key, v] : e["aff"].items())
            for (const auto& npc : groupIds(key)) {
                addVar("aff_" + npc, v.get<double>(), false);
                s_.flags.insert("met_" + npc);
            }
    if (e.contains("aff") || e.contains("met")) recomputeAffinity();
    if (e.contains("met")) {
        for (const auto& f : arr(e["met"]))
            for (const auto& npc : groupIds(f.get<std::string>())) s_.flags.insert("met_" + npc);
        recomputeAffinity();
    }

    if (e.contains("log")) s_.log.push_back(substitute(e["log"].get<std::string>()));
    if (e.contains("say")) {
        if (!s_.narration.empty()) s_.narration += "\n\n";
        s_.narration += substitute(e["say"].get<std::string>());
    }
    if (e.contains("message")) {
        PhoneMessage m{e["message"].value("from", "?"), e["message"].value("text", ""), s_.minute,
                       e["message"].value("replies", json::array()), -1};
        s_.messages.push_back(m);
        UiEvent ev{UiEvent::Type::Message};
        ev.key = m.from;
        ev.text = m.text;
        events_.push_back(ev);
    }
    if (e.contains("task_add")) {
        std::string t = e["task_add"].get<std::string>();
        if (c_.tasks.count(t) && std::count(s_.tasks.begin(), s_.tasks.end(), t) == 0) {
            s_.tasks.push_back(t);
            s_.side.insert(t); // anything handed out during the night is a side quest
            UiEvent ev{UiEvent::Type::TaskAdded};
            ev.key = t;
            ev.text = c_.tasks.at(t).title;
            events_.push_back(ev);
        }
    }

    if (e.contains("chance")) {
        double p = 0;
        const json& c = e["chance"];
        if (c.is_number()) p = c.get<double>();
        else if (c.is_object() && c.contains("var")) {
            // {"var":"problem","from":40,"to":100,"min":0.1,"max":0.9}
            double v = s_.var(c["var"].get<std::string>());
            double from = c.value("from", 0.0), to = c.value("to", 100.0);
            double t = std::clamp((v - from) / std::max(1e-6, to - from), 0.0, 1.0);
            p = c.value("min", 0.0) + t * (c.value("max", 1.0) - c.value("min", 0.0));
        }
        p = chanceOf(e);
        applyEffects(rng_.chance(p) ? e.value("then", json()) : e.value("else", json()));
    }
    if (e.contains("random")) {
        std::vector<double> w;
        for (const auto& r : e["random"]) w.push_back(r.value("weight", 1.0));
        applyEffects(e["random"][rng_.weighted(w)].value("do", json()));
    }

    if (e.contains("scene")) pendingScene_ = e["scene"].get<std::string>();
    if (e.contains("goto")) pendingGoto_ = e["goto"].get<std::string>();
    if (e.contains("walk_to")) {
        // walk somewhere from wherever you are (travel graph decides how long)
        std::string to = e["walk_to"].get<std::string>();
        if (to != s_.location) {
            advanceTime(int(std::lround(travelTime(s_.location, to) * timeScale())));
            if (isOver()) return;
            pendingGoto_ = to;
        }
    }
    if (e.contains("taxi")) { makeTaxiQuote(); pendingScene_ = "__taxi"; }
    if (e.contains("taxi_ride")) {
        int ride = c_.config.value("taxi", json::object()).value("ride_minutes", 8);
        int called = s_.minute;
        advanceTime(taxiWait_ + ride);
        if (isOver()) return;
        addVar("money", -taxiPrice_);
        s_.flags.insert("took_taxi");
        s_.log.push_back("Kutsusid kell " + fmtClock(called) + " takso. Maksid " + fmtMoney(taxiPrice_) + " \xE2\x82\xAC" +
                         (taxiSurge_ ? " (hinnatõus, valus)." : "."));
        pendingGoto_ = c_.config.value("home_location", "home");
    }
    if (e.contains("friend_where")) {
        std::string id = e["friend_where"].get<std::string>();
        const std::string& where = s_.friendLoc[id];
        std::string line;
        if (friendAway(id)) line = friendName(id) + ": \u201eOlen kodus. T\u00e4na mind linnas ei n\u00e4e.\u201c";
        else if (where == s_.location) line = friendName(id) + ": \u201eVaata ringi, ma olen siinsamas.\u201c";
        else {
            auto it = c_.locations.find(where);
            std::string in = it != c_.locations.end() ? it->second.in : where;
            line = friendName(id) + ": \u201eOlen " + in + ". Tule ka!\u201c";
        }
        if (!s_.narration.empty()) s_.narration += "\n\n";
        s_.narration += line;
        s_.flags.insert("knows_" + id);
        s_.flags.insert("met_" + id);
        recomputeAffinity();
    }
    if (e.contains("goto_friend")) {
        std::string id = e["goto_friend"].get<std::string>();
        if (auto g = groupIds(id); !g.empty()) id = g[0];
        std::string where = s_.friendLoc[id];
        if (friendAway(id)) {
            if (!s_.narration.empty()) s_.narration += "\n\n";
            s_.narration += friendName(id) + " on kodus. Tema juurde sa praegu k\u00fcll ei l\u00e4he.";
            s_.flags.insert("met_" + id);
            recomputeAffinity();
            return;
        }
        if (where != s_.location && c_.locations.count(where)) {
            advanceTime(int(std::lround(travelTime(s_.location, where) * timeScale())));
            if (isOver()) return;
            pendingGoto_ = where;
        }
        pendingMeet_ = id;
    }
    if (e.contains("summon_friend")) {
        // {"summon_friend":"martin"} or {"summon_friend":{"who":"martin","chance":0.9}}
        const json& sf = e["summon_friend"];
        std::string id = sf.is_string() ? sf.get<std::string>() : sf.at("who").get<std::string>();
        double p = sf.is_object() ? sf.value("chance", 0.35) : 0.35;
        p += 0.08 * s_.var("aff_" + id);
        if (!friendAway(id) && rng_.chance(std::clamp(p, 0.05, 0.98))) {
            int wait = std::max(3, travelTime(s_.friendLoc[id], s_.location) + rng_.range(2, 8));
            advanceTime(wait);
            if (isOver()) return;
            s_.friendLoc[id] = s_.location;
            if (!s_.narration.empty()) s_.narration += "\n\n";
            s_.narration += friendName(id) + " j\u00f5udis " + std::to_string(wait) + " minutiga kohale.";
            pendingMeet_ = id;
        } else {
            advanceTime(3);
            if (!s_.narration.empty()) s_.narration += "\n\n";
            s_.narration += friendName(id) + (friendAway(id) ? ": \u201eMa olen kodus, mees. T\u00e4na mitte, sorry.\u201c"
                                                             : ": \u201eEi j\u00f5ua praegu, sorry. Hiljem.\u201c");
            s_.flags.insert("met_" + id);
        }
        recomputeAffinity();
    }
    if (e.contains("join_friends")) {
        auto here = presentFriends();
        if (here.size() == 1) pendingMeet_ = here[0];
        else if (here.size() > 1) {
            std::string sc = pickGroupScene(false);
            if (!sc.empty()) pendingScene_ = sc;
            else pendingMeet_ = here[rng_.range(0, int(here.size()) - 1)];
        }
    }
    if (e.contains("friend_taxi")) {
        // Broke? One of the boys orders (and pays for) your taxi. You owe them now.
        makeTaxiQuote();
        int ride = c_.config.value("taxi", json::object()).value("ride_minutes", 8);
        int called = s_.minute;
        std::string who = bestFriend();
        advanceTime(taxiWait_ + ride);
        if (isOver()) return;
        s_.flags.insert("took_taxi");
        s_.flags.insert("friend_taxi");
        s_.log.push_back(who + " tellis sulle kell " + fmtClock(called) + " takso. Oled talle " + fmtMoney(taxiPrice_) + " \xE2\x82\xAC võlgu.");
        pendingGoto_ = c_.config.value("home_location", "home");
    }
    if (e.contains("blackout")) doBlackout();
    if (e.contains("end")) finish(e["end"].get<std::string>());
}

void Engine::advanceTime(int minutes) {
    if (minutes <= 0 || isOver()) return;
    s_.minute += minutes;
    const json& cfg = c_.config;
    addVar("phone", -minutes * cfg.value("phone_drain_per_hour", 10.0) / 60.0, false);
    addVar("problem", -minutes * cfg.value("problem_decay_per_hour", 6.0) / 60.0, false);
    checkMessages();
    rollSideQuest();
    rerollFriends(false);
    if (s_.minute >= sunrise_) finish("sunrise");
}

void Engine::checkMessages() {
    for (const auto& m : c_.config.value("messages", json::array())) {
        std::string id = m.at("id").get<std::string>();
        if (s_.firedMessages.count(id)) continue;
        if (s_.minute < parseClock(m.at("at").get<std::string>())) continue;
        s_.firedMessages.insert(id);
        if (s_.var("phone") <= 0) continue;              // dead phone = you never got it
        if (!rng_.chance(m.value("chance", 1.0))) continue;
        if (m.contains("if") && !check(m["if"])) continue;
        applyEffect(json{{"message", {{"from", m.value("from", "?")}, {"text", m.value("text", "")},
                                       {"replies", m.value("replies", json::array())}}}});
        if (m.contains("add_task")) applyEffect(json{{"task_add", m["add_task"]}});
    }
}

void Engine::checkTasks() {
    {
        int sd = 0;
        for (const auto& t : s_.side) if (check(c_.tasks.at(t).doneWhen) && !c_.tasks.at(t).final) ++sd;
        s_.vars["side_done"] = sd; // for main quests like "do 4 side quests"
    }
    for (const auto& t : s_.tasks) {
        if (doneTasks_.count(t) || c_.tasks.at(t).final) continue;
        if (check(c_.tasks.at(t).doneWhen)) {
            doneTasks_.insert(t);
            UiEvent ev{UiEvent::Type::TaskDone};
            ev.key = t;
            ev.text = c_.tasks.at(t).title;
            events_.push_back(ev);
        }
    }
}

std::vector<TaskView> Engine::taskViews() const {
    std::vector<TaskView> out;
    for (const auto& t : s_.tasks) {
        const TaskDef& d = c_.tasks.at(t);
        TaskView tv{d.title, d.desc, check(d.doneWhen), d.final, s_.side.count(t) > 0};
        tv.failed = !tv.done && !d.failIf.empty() && check(d.failIf);
        if (d.final && !d.failIf.empty() && check(d.failIf)) { tv.failed = true; tv.done = false; }
        if (d.progress.is_object()) {
            int cur = int(std::floor(s_.var(d.progress.value("var", ""))));
            tv.progress = std::to_string(cur) + "/" + std::to_string(d.progress.value("target", 0));
        }
        out.push_back(tv);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Movement, scenes, encounters
// ---------------------------------------------------------------------------
void Engine::enterLocation(const std::string& idIn) {
    std::string id = idIn;
    // gates: e.g. "home" while your keys are lost sends you to the locked door instead
    for (int guard = 0; guard < 4; ++guard) {
        const Location& l = c_.locations.at(id);
        if (l.gate.is_object() && check(l.gate.value("if", json::array()))) id = l.gate.at("to").get<std::string>();
        else break;
    }
    s_.location = id;
    s_.scene.clear();
    s_.narration.clear();
    s_.visited.insert(id);
    UiEvent ev{UiEvent::Type::Travel};
    ev.key = id;
    events_.push_back(ev);
    if (c_.locations.at(id).isHome) { arriveHome(); return; }
    rollEncounters("enter");
}

void Engine::setScene(const std::string& id) {
    std::string cur = id;
    for (int guard = 0; guard < 8 && cur != "__taxi"; ++guard) {
        const Scene& sc = c_.scenes.at(cur);
        std::string to;
        for (const auto& r : arr(sc.redirectIf)) // one rule or a list; first match wins
            if (r.is_object() && check(r.value("if", json::array()))) { to = r.at("to").get<std::string>(); break; }
        if (to.empty()) break;
        cur = to;
    }
    s_.scene = cur;
    s_.flags.insert("scene_seen_" + cur);
    if (cur != "__taxi") {
        const json fx = c_.scenes.at(cur).onEnter;
        if (!fx.is_null()) {
            // only bookkeeping effects make sense here (task_add, flag, item, message, aff)
            for (const auto& e : arr(fx)) {
                if (!e.is_object()) continue;
                json safe = json::object();
                for (const char* k : {"task_add", "flag", "unflag", "item", "remove_item", "message", "aff", "met", "add", "set", "chance", "random"})
                    if (e.contains(k)) safe[k] = e[k];
                applyEffect(safe);
            }
        }
    }
}

void Engine::rollEncounters(const std::string& trigger) {
    if (isOver() || !s_.scene.empty()) return;
    const Location& loc = c_.locations.at(s_.location);
    for (const auto& enc : loc.encounters) {
        if (enc.trigger != trigger) continue;
        if (enc.once && s_.flags.count("enc_" + enc.id)) continue;
        if (!check(enc.conditions)) continue;
        double p = enc.chance;
        if (enc.chanceScale.is_object()) {
            const json& c = enc.chanceScale;
            double v = s_.var(c.at("var").get<std::string>());
            double from = c.value("from", 0.0), to = c.value("to", 100.0);
            double t = std::clamp((v - from) / std::max(1e-6, to - from), 0.0, 1.0);
            p = (v < from) ? 0.0 : c.value("min", 0.0) + t * (c.value("max", 1.0) - c.value("min", 0.0));
        }
        if (!rng_.chance(p)) continue;
        if (enc.once) s_.flags.insert("enc_" + enc.id);
        std::vector<double> w;
        for (const auto& o : enc.outcomes) w.push_back(o.first);
        setScene(enc.outcomes[rng_.weighted(w)].second);
        return; // one encounter at a time
    }
    // two or more of the boys here: they're already talking, and they pull you in
    if (trigger == "enter" && presentFriends().size() >= 2) {
        const json g = c_.config.value("groups", json::object());
        if (rng_.chance(g.value("enter_chance", 0.6))) {
            std::string sc = pickGroupScene(true);
            if (!sc.empty()) { setScene(sc); return; }
        }
    }
    rollRoaming();
}

// The boys wander: any of them can show up anywhere and drag you into something.
bool Engine::rollRoaming() {
    const json r = c_.config.value("roaming", json::object());
    if (r.empty() || isOver() || !s_.scene.empty()) return false;
    if (!check(r.value("if", json::array()))) return false;
    if (s_.var("roam_count") >= r.value("max", 8)) return false;
    if (s_.var("roam_count") > 0 && s_.minute - s_.var("roam_last") < r.value("cooldown", 25)) return false;
    if (!rng_.chance(r.value("chance", 0.25))) return false;

    std::vector<const json*> pool;
    std::vector<double> w;
    const json encounters = r.value("encounters", json::array());
    for (const auto& enc : encounters) {
        std::string id = enc.at("id").get<std::string>();
        if (enc.value("once", true) && s_.flags.count("enc_" + id)) continue;
        if (!check(enc.value("if", json::array()))) continue;
        pool.push_back(&enc);
        w.push_back(enc.value("weight", 1.0));
    }
    if (pool.empty()) return false;
    const json& pick = *pool[rng_.weighted(w)];
    s_.flags.insert("enc_" + pick.at("id").get<std::string>());
    s_.vars["roam_count"] = s_.var("roam_count") + 1;
    s_.vars["roam_last"] = s_.minute;
    setScene(pick.at("scene").get<std::string>());
    return true;
}

void Engine::recomputeAffinity() {
    double total = 0;
    int met = 0;
    const json friends = c_.config.value("friends", json::object());
    for (const auto& [k, v] : friends.items()) {
        (void)v;
        total += s_.var("aff_" + k);
        met += s_.flags.count("met_" + k) ? 1 : 0;
    }
    s_.vars["aff_total"] = total;
    s_.vars["friends_met"] = met;
}

std::string Engine::bestFriend() const {
    std::string best = "Keegi grupivestlusest";
    double bestAff = -1e9;
    const json friends = c_.config.value("friends", json::object());
    for (const auto& [k, v] : friends.items()) {
        if (!s_.flags.count("met_" + k)) continue;
        if (s_.var("aff_" + k) > bestAff) {
            bestAff = s_.var("aff_" + k);
            best = friendName(k);
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Taxi, blackout, endings
// ---------------------------------------------------------------------------
void Engine::makeTaxiQuote() {
    const json taxi = c_.config.value("taxi", json::object());
    const json table = taxi.value("prices", json::array());
    json row = table.empty() ? json{{"min", 5}, {"max", 10}} : table.back();
    for (const auto& r : table)
        if (s_.minute < parseClock(r.at("until").get<std::string>())) { row = r; break; }
    taxiPrice_ = rng_.real(row.value("min", 5.0), row.value("max", 10.0));
    taxiSurge_ = rng_.chance(taxi.value("surge_chance", 0.15));
    if (taxiSurge_) {
        auto [a, b] = span(taxi.value("surge_mult", json::array({1.3, 1.8})));
        taxiPrice_ *= rng_.real(a, b);
    }
    taxiPrice_ = round2(taxiPrice_);
    auto [wa, wb] = span(taxi.value("wait", json::array({5, 20})));
    taxiWait_ = rng_.range(int(wa), int(wb));
}

void Engine::doBlackout() {
    const json cfg = c_.config.at("blackout");
    const int start = s_.minute;

    // Montage images
    std::vector<std::string> pool = cfg.value("montage", std::vector<std::string>{});
    auto [ca, cb] = span(cfg.value("montage_count", json::array({3, 5})));
    int count = std::min<int>(rng_.range(int(ca), int(cb)), int(pool.size()));
    std::vector<std::string> images;
    for (int i = 0; i < count; ++i) {
        int j = rng_.range(i, int(pool.size()) - 1);
        std::swap(pool[i], pool[j]);
        images.push_back(pool[i]);
    }

    int wake = start + int(roll(cfg.value("time_jump", json::array({90, 180})), "time"));
    const json late = cfg.value("late_rule", json::object());
    bool police = false;
    std::string spot;
    if (!late.empty() &&
        (start >= parseClock(late.value("start_after", "04:30")) || wake >= parseClock(late.value("wake_after", "05:00")))) {
        auto w = late.value("wake_at", std::vector<std::string>{"05:15", "05:40"});
        wake = std::max(start + 10, rng_.range(parseClock(w[0]), parseClock(w[1])));
        spot = late.at("spot").get<std::string>();
        police = true;
    } else {
        std::vector<double> weights;
        std::vector<std::string> ids;
        for (const auto& sp : cfg.at("spots")) {
            ids.push_back(sp.at("id").get<std::string>());
            weights.push_back(sp.value("weight", 1.0));
        }
        spot = ids[rng_.weighted(weights)];
    }

    blackedOut_ = true;
    s_.scene.clear();
    s_.flags.insert("blackout");
    addVar("blackouts", 1, false);

    UiEvent ev{UiEvent::Type::Blackout};
    ev.images = images;
    ev.fromMinute = start;
    ev.toMinute = wake;
    ev.key = spot;
    events_.push_back(ev);

    advanceTime(wake - start);
    if (isOver()) return;
    addVar("phone", roll(cfg.value("phone", json(-30)), "phone"));
    if (cfg.contains("problem_after")) addVar("problem", roll(cfg["problem_after"], "problem") - s_.var("problem"));
    addVar("money", roll(cfg.value("money", json::array({-30, 0})), "money"));

    const Location& loc = c_.locations.at(spot);
    s_.log.push_back("Mälu kustus kell " + fmtClock(start) + ". Ärkasid kell " + fmtClock(wake) + ": " + loc.name + ".");
    s_.location = spot;
    s_.visited.insert(spot);
    s_.narration.clear();
    if (police && late.contains("scene")) setScene(late["scene"].get<std::string>());
}

void Engine::finish(const std::string& ending) {
    if (!s_.ending.empty()) return;
    s_.ending = ending;
    if (s_.homeMinute < 0) s_.homeMinute = s_.minute;
    UiEvent ev{UiEvent::Type::Ended};
    ev.key = ending;
    events_.push_back(ev);
}

void Engine::arriveHome() {
    s_.homeMinute = s_.minute;
    s_.flags.insert("home");
    bool all = true; // only the main quests decide whether the night was a success
    for (const auto& t : s_.tasks)
        if (!s_.side.count(t) && !check(c_.tasks.at(t).doneWhen)) all = false;
    finish(all ? "success" : "unsuccessful");
}

std::string Engine::substitute(std::string t) const {
    auto get = [&](const char* k) {
        auto it = lastRolled_.find(k);
        return it == lastRolled_.end() ? 0.0 : it->second;
    };
    replaceAll(t, "{time}", std::to_string(int(get("time"))));
    replaceAll(t, "{money}", fmtMoney(get("money")));
    replaceAll(t, "{price}", fmtMoney(taxiPrice_));
    replaceAll(t, "{wait}", std::to_string(taxiWait_));
    replaceAll(t, "{clock}", fmtClock(s_.minute));
    replaceAll(t, "{cash}", fmtMoney(s_.var("money")));
    if (t.find("{friend}") != std::string::npos) replaceAll(t, "{friend}", bestFriend());
    if (t.find('{') != std::string::npos && !s_.group.empty()) {
        const json friends = c_.config.value("friends", json::object());
        const char* keys[] = {"a", "b", "c", "d"};
        for (std::size_t i = 0; i < 4; ++i) {
            std::string id = s_.group.empty() ? "" : s_.group[std::min(i, s_.group.size() - 1)];
            std::string name = friendName(id);
            std::string gen = friends.contains(id) && friends[id].is_object() ? friends[id].value("gen", name) : name;
            replaceAll(t, std::string("{") + keys[i] + "_gen}", gen);
            replaceAll(t, std::string("{") + keys[i] + "}", name);
            replaceAll(t, std::string("{") + char(std::toupper(keys[i][0])) + "}", upper(name));
        }
    }
    return t;
}

Recap Engine::recap() const {
    Recap r;
    auto it = c_.endings.find(s_.ending);
    r.ending = it != c_.endings.end() ? it->second : EndingDef{s_.ending, s_.ending, "", "neutral"};

    r.night = s_.log;
    std::set<std::string> npcsDone;
    for (const auto& sn : c_.recap) {
        if (sn.section == "night") {
            if (check(sn.conditions)) r.night.push_back(substitute(sn.text));
        } else if (sn.section == "friends") {
            if (npcsDone.count(sn.npc)) continue;
            if (!sn.npc.empty() && !s_.flags.count("met_" + sn.npc)) continue;
            if (!check(sn.conditions)) continue;
            npcsDone.insert(sn.npc);
            r.friends.push_back(substitute(sn.text));
        }
    }

    int done = 0, sideDone = 0, mainCount = 0;
    for (const auto& t : s_.tasks) {
        bool ok = check(c_.tasks.at(t).doneWhen) && (!c_.tasks.at(t).final || s_.flags.count("home"));
        if (s_.side.count(t)) {
            if (ok) ++sideDone;
            r.sideTasks.push_back({c_.tasks.at(t).title, ok});
        } else {
            ++mainCount;
            if (ok) ++done;
            r.tasks.push_back({c_.tasks.at(t).title, ok});
        }
    }
    // score: config "score" = [{label, points, if}] or [{label, per, count}]
    {
        int friendsLike = 0;
        const json friends = c_.config.value("friends", json::object());
        for (const auto& [k, v] : friends.items()) { (void)v; if (s_.var("aff_" + k) >= 2) ++friendsLike; }
        std::map<std::string, double> counts{{"main_done", done}, {"side_done", sideDone}, {"friends_like", friendsLike},
                                             {"all_main", mainCount > 0 && done == mainCount ? 1 : 0}};
        for (const auto& sc : arr(c_.config.value("score", json::array()))) {
            if (sc.contains("if") && !check(sc["if"])) continue;
            int pts = 0;
            std::string label = sc.value("label", "");
            if (sc.contains("count")) {
                std::string k = sc["count"].get<std::string>();
                double n = counts.count(k) ? counts[k] : s_.var(k);
                if (sc.contains("max")) n = std::min(n, sc["max"].get<double>());
                n = std::floor(std::max(0.0, n));
                if (n <= 0) continue;
                pts = int(std::lround(n * sc.value("per", 1.0)));
                if (sc.value("show_count", true)) label += " ×" + std::to_string(int(n));
            } else {
                pts = sc.value("points", 0);
            }
            if (pts == 0) continue;
            r.score.push_back({label, pts});
            r.total += pts;
        }
    }
    r.stats.push_back({"KODUS", s_.flags.count("home") ? fmtClock(s_.homeMinute) : "--:--"});
    r.stats.push_back({"RAHA ALLES", fmtMoney(s_.var("money")) + " \xE2\x82\xAC"});
    r.stats.push_back({"TELEFON", std::to_string(int(std::lround(s_.var("phone")))) + "%"});
    r.stats.push_back({"MAX PROBLEEM", std::to_string(int(std::lround(s_.peakProblem)))});
    r.stats.push_back({"ÜLESANDED", std::to_string(done) + "/" + std::to_string(mainCount) + " + " + std::to_string(sideDone)});
    r.stats.push_back({"SKOOR", std::to_string(r.total)});
    r.stats.push_back({"KASSETT #", std::to_string(rng_.seed() % 100000)});
    return r;
}

// ---------------------------------------------------------------------------
// The boys: where they are, phone contacts, travel times
// ---------------------------------------------------------------------------
std::string Engine::friendName(const std::string& id) const {
    const json friends = c_.config.value("friends", json::object());
    if (!friends.contains(id)) return id;
    const json& f = friends[id];
    return f.is_string() ? f.get<std::string>() : f.value("name", id);
}

std::vector<Engine::Contact> Engine::contacts() const {
    std::vector<Contact> out;
    const json friends = c_.config.value("friends", json::object());
    for (const auto& [k, v] : friends.items()) out.push_back({k, friendName(k)});
    return out;
}

bool Engine::canCall() const {
    return !isOver() && s_.scene.empty() && s_.var("phone") > 0 && !c_.locations.at(s_.location).isHome;
}

void Engine::call(const std::string& id) {
    if (!canCall() || !c_.scenes.count("call_" + id)) return;
    addVar("phone", -1);
    s_.narration.clear();
    setScene("call_" + id);
}

void Engine::rerollFriends(bool force) {
    const json friends = c_.config.value("friends", json::object());
    for (const auto& [k, v] : friends.items()) {
        if (force && v.is_object() && v.contains("presence") && !rng_.chance(v["presence"].get<double>()))
            s_.flags.insert("away_" + k); // stays home tonight
        if (!force && s_.minute < s_.friendMoveAt[k]) continue;
        const json haunts = v.is_object() ? v.value("haunts", json::object()) : json::object();
        std::vector<std::string> ids;
        std::vector<double> w;
        for (const auto& [loc, weight] : haunts.items()) {
            ids.push_back(loc);
            w.push_back(weight.get<double>());
        }
        if (!ids.empty()) s_.friendLoc[k] = ids[rng_.weighted(w)];
        else s_.friendLoc[k] = "ruutel";
        s_.friendMoveAt[k] = s_.minute + rng_.range(30, 60);
    }
}

std::string Engine::meetScene(const std::string& id) const {
    const json friends = c_.config.value("friends", json::object());
    std::vector<std::string> list;
    if (friends.contains(id) && friends[id].is_object()) list = friends[id].value("meet", std::vector<std::string>{});
    if (list.empty()) return "meet_" + id;
    // first scene with this friend you haven't seen yet; the last entry is the repeatable fallback
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
        if (!s_.flags.count("scene_seen_" + list[i]) && c_.scenes.count(list[i])) return list[i];
    return list.back();
}

int Engine::travelTime(const std::string& from, const std::string& to) const {
    if (from == to) return 0;
    // shortest path over config.travel edges ("a-b": minutes, both directions)
    const json edges = c_.config.value("travel", json::object());
    std::map<std::string, std::vector<std::pair<std::string, int>>> g;
    for (const auto& [k, v] : edges.items()) {
        auto dash = k.find('-');
        if (dash == std::string::npos) continue;
        std::string a = k.substr(0, dash), b = k.substr(dash + 1);
        g[a].push_back({b, v.get<int>()});
        g[b].push_back({a, v.get<int>()});
    }
    std::map<std::string, int> dist{{from, 0}};
    std::set<std::pair<int, std::string>> q{{0, from}};
    while (!q.empty()) {
        auto [d, u] = *q.begin();
        q.erase(q.begin());
        if (u == to) return d;
        for (const auto& [v, w] : g[u]) {
            if (!dist.count(v) || d + w < dist[v]) {
                q.erase({dist.count(v) ? dist[v] : 0, v});
                dist[v] = d + w;
                q.insert({dist[v], v});
            }
        }
    }
    return 15; // unknown: assume a short walk
}

// ---------------------------------------------------------------------------
// Friend groups
// ---------------------------------------------------------------------------
std::string Engine::upper(std::string s) {
    for (auto& ch : s)
        if (static_cast<unsigned char>(ch) < 128) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

bool Engine::friendAway(const std::string& id) const {
    if (s_.flags.count("away_" + id)) return true;
    const json friends = c_.config.value("friends", json::object());
    if (!friends.contains(id) || !friends[id].is_object()) return false;
    const json& f = friends[id];
    if (f.contains("until") && s_.minute >= parseClock(f["until"].get<std::string>())) return true;
    return false;
}

std::vector<std::string> Engine::presentFriends() const {
    std::vector<std::string> out;
    const json friends = c_.config.value("friends", json::object());
    for (const auto& [k, v] : friends.items()) {
        (void)v;
        auto it = s_.friendLoc.find(k);
        if (it != s_.friendLoc.end() && it->second == s_.location && !friendAway(k)) out.push_back(k);
    }
    return out;
}

std::string Engine::friendPortrait(const std::string& id) const {
    const json friends = c_.config.value("friends", json::object());
    if (friends.contains(id) && friends[id].is_object() && friends[id].contains("portrait"))
        return friends[id]["portrait"].get<std::string>();
    return "img/char/" + id + ".png";
}

std::vector<std::string> Engine::groupIds(const std::string& key) const {
    if (key.empty() || key[0] != '@') return {key};
    if (key == "@group") return s_.group;
    std::size_t i = std::size_t(key.size() > 1 ? key[1] - 'a' : 0);
    if (i < s_.group.size()) return {s_.group[i]};
    return {};
}

std::string Engine::pickGroupScene(bool freshOnly) {
    auto here = presentFriends();
    if (here.size() < 2) return "";
    const json g = c_.config.value("groups", json::object());
    auto seen = [&](const std::string& sc) { return s_.flags.count("scene_seen_" + sc) > 0; };

    // 1) a talk written for exactly these people
    std::vector<std::pair<std::string, std::vector<std::string>>> pool;
    for (const auto& t : arr(g.value("talks", json::array()))) {
        std::string sc = t.at("scene").get<std::string>();
        std::vector<std::string> needs = t.value("needs", std::vector<std::string>{});
        bool ok = !seen(sc) && c_.scenes.count(sc);
        for (const auto& n : needs) ok = ok && std::count(here.begin(), here.end(), n) > 0;
        if (t.contains("if") && !check(t["if"])) ok = false;
        if (ok) pool.push_back({sc, needs});
    }
    auto withRest = [&](std::vector<std::string> front) {
        std::vector<std::string> rest;
        for (const auto& h : here)
            if (std::count(front.begin(), front.end(), h) == 0) rest.push_back(h);
        for (std::size_t i = rest.size(); i > 1; --i) std::swap(rest[i - 1], rest[rng_.range(0, int(i) - 1)]);
        front.insert(front.end(), rest.begin(), rest.end());
        return front;
    };
    if (!pool.empty()) {
        auto& pick = pool[rng_.range(0, int(pool.size()) - 1)];
        s_.group = withRest(pick.second);
        s_.vars["group_size"] = double(s_.group.size());
        return pick.first;
    }

    // 2) a generic talk with whoever is here ({a} and {b}); the last one repeats
    std::vector<std::string> generic = g.value("generic", std::vector<std::string>{});
    std::vector<std::string> fresh;
    for (std::size_t i = 0; i + 1 < generic.size(); ++i)
        if (!seen(generic[i]) && c_.scenes.count(generic[i])) fresh.push_back(generic[i]);
    std::string sc;
    if (!fresh.empty()) sc = fresh[rng_.range(0, int(fresh.size()) - 1)];
    else if (!freshOnly && !generic.empty()) sc = generic.back();
    if (sc.empty()) return "";
    s_.group = withRest({});
    s_.vars["group_size"] = double(s_.group.size());
    return sc;
}

// ---------------------------------------------------------------------------
// Side quests: the night keeps handing you small jobs (via the phone)
// ---------------------------------------------------------------------------
void Engine::rollSideQuest() {
    const json sq = c_.config.value("side_quests", json::object());
    if (sq.empty() || isOver() || s_.minute < s_.nextSideAt) return;
    auto [ea, eb] = span(sq.value("every", json::array({25, 45})));
    s_.nextSideAt = s_.minute + rng_.range(int(ea), int(eb));
    if (s_.var("phone") <= 0) return; // dead phone: nobody can reach you
    int active = 0;
    for (const auto& t : s_.side)
        if (!check(c_.tasks.at(t).doneWhen)) ++active;
    if (active >= sq.value("max_active", 3)) return;

    std::vector<const json*> pool;
    std::vector<double> w;
    const json list = sq.value("pool", json::array());
    for (const auto& q : list) {
        std::string t = q.at("task").get<std::string>();
        if (!c_.tasks.count(t) || std::count(s_.tasks.begin(), s_.tasks.end(), t)) continue;
        if (check(c_.tasks.at(t).doneWhen) && !c_.tasks.at(t).final) continue; // would be done instantly
        if (q.contains("if") && !check(q["if"])) continue;
        pool.push_back(&q);
        w.push_back(q.value("weight", 1.0));
    }
    if (pool.empty()) return;
    const json q = *pool[rng_.weighted(w)];
    if (q.contains("text"))
        applyEffect(json{{"message", {{"from", q.value("from", "?")}, {"text", q["text"]}, {"replies", q.value("replies", json::array())}}}});
    applyEffect(json{{"task_add", q["task"]}});
    if (q.contains("do")) applyEffects(q["do"]);
}

// ---------------------------------------------------------------------------
// Time scale and odds
// ---------------------------------------------------------------------------
double Engine::timeScale() const { return c_.config.value("time_scale", 1.0); }

json Engine::scaledTime(const json& v) const {
    const double k = timeScale();
    auto sc = [&](const json& x) { return json(std::max(1, int(std::lround(x.get<double>() * k)))); };
    if (k == 1.0) return v;
    if (v.is_array() && v.size() == 2) return json::array({sc(v[0]), sc(v[1])});
    if (v.is_number()) return v.get<double>() <= 0 ? v : sc(v);
    return v;
}

double Engine::chanceOf(const json& e) const {
    double p = 0;
    const json& c = e["chance"];
    if (c.is_number()) p = c.get<double>();
    else if (c.is_object() && c.contains("var")) {
        double v = s_.var(c["var"].get<std::string>());
        double from = c.value("from", 0.0), to = c.value("to", 100.0);
        double t = std::clamp((v - from) / std::max(1e-6, to - from), 0.0, 1.0);
        p = c.value("min", 0.0) + t * (c.value("max", 1.0) - c.value("min", 0.0));
    }
    if (e.contains("per_var")) p += e.value("per", 0.0) * s_.var(e["per_var"].get<std::string>());
    return std::clamp(p, 0.0, 1.0);
}
