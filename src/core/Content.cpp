#include "core/Content.h"

#include <filesystem>
#include <functional>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

json readJson(const fs::path& p) {
    std::ifstream in(p);
    if (!in) throw std::runtime_error("Cannot open " + p.string());
    try {
        return json::parse(in, nullptr, true, /*ignore_comments=*/true);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(p.filename().string() + ": " + e.what());
    }
}

json asArray(const json& j) {
    if (j.is_null()) return json::array();
    if (j.is_array()) return j;
    return json::array({j});
}

Choice parseChoice(const json& j, const std::string& idPrefix, int index) {
    Choice c;
    c.label = j.value("label", "???");
    c.conditions = asArray(j.value("if", json()));
    c.showIf = asArray(j.value("show_if", json()));
    c.effects = asArray(j.value("do", json()));
    c.next = j.value("next", "");
    c.lockedText = j.value("locked", "");
    c.once = j.value("once", false);
    c.id = j.value("id", idPrefix + "#" + std::to_string(index));
    return c;
}

std::vector<Choice> parseChoices(const json& arr, const std::string& idPrefix) {
    std::vector<Choice> out;
    int i = 0;
    for (const auto& j : asArray(arr)) out.push_back(parseChoice(j, idPrefix, i++));
    return out;
}

std::string joinText(const json& j) {
    // Text can be a string or an array of paragraphs (nicer to edit in JSON).
    if (j.is_string()) return j.get<std::string>();
    std::string out;
    for (const auto& p : asArray(j)) {
        if (!out.empty()) out += "\n\n";
        out += p.get<std::string>();
    }
    return out;
}

} // namespace

void Content::load(const std::string& assetRoot) {
    const fs::path dir = fs::path(assetRoot) / "content";

    config = readJson(dir / "config.json");
    globalActions = parseChoices(config.value("global_actions", json()), "global");

    for (const auto& j : readJson(dir / "locations.json")) {
        Location l;
        l.id = j.at("id").get<std::string>();
        l.name = j.value("name", l.id);
        l.subtitle = j.value("subtitle", "");
        l.image = j.value("image", "");
        l.ambience = j.value("ambience", "");
        l.in = j.value("in", l.name);
        l.text = joinText(j.value("text", json("")));
        l.isHome = j.value("home", false);
        l.gate = j.value("gate", json());
        l.allowTaxi = j.value("taxi", !l.isHome);
        l.actions = parseChoices(j.value("actions", json()), l.id + ".a");
        l.exits = parseChoices(j.value("exits", json()), l.id + ".x");
        for (const auto& e : asArray(j.value("encounters", json()))) {
            Encounter enc;
            enc.id = e.at("id").get<std::string>();
            enc.trigger = e.value("trigger", "enter");
            enc.chance = e.value("chance", 1.0);
            enc.chanceScale = e.value("chance_scale", json());
            enc.once = e.value("once", true);
            enc.conditions = asArray(e.value("if", json()));
            if (e.contains("scene")) enc.outcomes.emplace_back(1.0, e["scene"].get<std::string>());
            for (const auto& o : asArray(e.value("outcomes", json())))
                enc.outcomes.emplace_back(o.value("weight", 1.0), o.at("scene").get<std::string>());
            l.encounters.push_back(std::move(enc));
        }
        locations[l.id] = std::move(l);
    }

    for (const auto& j : readJson(dir / "scenes.json")) {
        Scene s;
        s.id = j.at("id").get<std::string>();
        s.speaker = j.value("speaker", "");
        s.text = joinText(j.value("text", json("")));
        s.image = j.value("image", "");
        for (const auto& p : asArray(j.value("portrait", json()))) {
            if (!s.portrait.empty()) s.portrait += "|";
            s.portrait += p.get<std::string>();
        }
        s.choices = parseChoices(j.value("choices", json()), s.id);
        s.redirectIf = j.value("redirect", json());
        s.onEnter = j.value("on_enter", json());
        s.music = j.value("music", "");
        scenes[s.id] = std::move(s);
    }

    for (const auto& j : readJson(dir / "tasks.json")) {
        TaskDef t;
        t.id = j.at("id").get<std::string>();
        t.title = j.value("title", t.id);
        t.desc = j.value("desc", "");
        t.doneWhen = asArray(j.value("done_when", json()));
        t.final = j.value("final", false);
        t.progress = j.value("progress", json());
        t.failIf = asArray(j.value("fail_if", json()));
        tasks[t.id] = std::move(t);
    }

    for (const auto& j : readJson(dir / "endings.json")) {
        EndingDef e;
        e.id = j.at("id").get<std::string>();
        e.title = j.value("title", e.id);
        e.subtitle = j.value("subtitle", "");
        e.tone = j.value("tone", "neutral");
        endings[e.id] = std::move(e);
    }

    for (const auto& j : readJson(dir / "recap.json")) {
        RecapSnippet r;
        r.section = j.value("section", "night");
        r.npc = j.value("npc", "");
        r.conditions = asArray(j.value("if", json()));
        r.text = j.value("text", "");
        recap.push_back(std::move(r));
    }

    // Light validation: catch typos in scene/location references early.
    auto checkRef = [&](const std::string& what, const std::string& id, bool isScene) {
        if (id.empty()) return;
        bool ok = isScene ? scenes.count(id) : locations.count(id);
        if (!ok) throw std::runtime_error(what + " references unknown " + (isScene ? "scene '" : "location '") + id + "'");
    };
    std::function<void(const json&, const std::string&)> checkEffects = [&](const json& fx, const std::string& where) {
        for (const auto& e : asArray(fx)) {
            if (!e.is_object()) continue;
            if (e.contains("scene")) checkRef(where, e["scene"].get<std::string>(), true);
            if (e.contains("goto")) checkRef(where, e["goto"].get<std::string>(), false);
            if (e.contains("chance")) {
                checkEffects(e.value("then", json()), where);
                checkEffects(e.value("else", json()), where);
            }
            if (e.contains("random"))
                for (const auto& r : e["random"]) checkEffects(r.value("do", json()), where);
        }
    };
    auto checkChoices = [&](const std::vector<Choice>& cs, const std::string& where) {
        for (const auto& c : cs) {
            checkRef(where + " / '" + c.label + "'", c.next, true);
            checkEffects(c.effects, where + " / '" + c.label + "'");
        }
    };
    for (const auto& [id, l] : locations) {
        checkChoices(l.actions, "location " + id);
        checkChoices(l.exits, "location " + id);
        for (const auto& e : l.encounters)
            for (const auto& [w, s] : e.outcomes) checkRef("encounter " + e.id, s, true);
    }
    checkChoices(globalActions, "config.global_actions");
    for (const auto& [id, s] : scenes) {
        checkChoices(s.choices, "scene " + id);
        for (const auto& r : asArray(s.redirectIf))
            if (r.is_object()) checkRef("scene " + id + " redirect", r.value("to", ""), true);
    }
    for (const auto& r : asArray(config.value("roaming", json::object()).value("encounters", json())))
        checkRef("roaming encounter", r.value("scene", ""), true);
    const json friendsCfg = config.value("friends", json::object());
    for (const auto& [fid, f] : friendsCfg.items()) {
        if (!f.is_object()) continue;
        for (const auto& m : asArray(f.value("meet", json()))) checkRef("friends." + fid + ".meet", m.get<std::string>(), true);
        const json haunts = f.value("haunts", json::object());
        for (const auto& [loc, w] : haunts.items()) { (void)w; checkRef("friends." + fid + ".haunts", loc, false); }
    }
    const json groupsCfg = config.value("groups", json::object());
    for (const auto& t : asArray(groupsCfg.value("talks", json()))) {
        checkRef("groups.talks", t.value("scene", ""), true);
        for (const auto& n : asArray(t.value("needs", json())))
            if (!friendsCfg.contains(n.get<std::string>())) throw std::runtime_error("groups.talks: unknown friend '" + n.get<std::string>() + "'");
    }
    for (const auto& g : asArray(groupsCfg.value("generic", json()))) checkRef("groups.generic", g.get<std::string>(), true);
    checkRef("config.start_location", config.value("start_location", ""), false);
    checkRef("config.intro_scene", config.value("intro_scene", ""), true);
}
