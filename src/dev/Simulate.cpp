#include "dev/Simulate.h"

#include <iostream>
#include <map>

#include "core/Engine.h"

// Plays N games with random choices and prints the ending distribution.
// Catches content bugs (crashes, dead ends with no choices, endless loops).
int runSimulation(const Content& content, int runs, bool verbose) {
    std::map<std::string, int> endings;
    int deadEnds = 0, loops = 0, blackouts = 0, police = 0, roams = 0, calls = 0, replies = 0;
    std::map<std::string, int> taskSeen, taskDone, sceneRuns, sideSeen, sideDone;
    long sideGot = 0, sideOk = 0, scoreSum = 0, scored = 0;
    int scoreMax = -99999, scoreMin = 99999;
    int groupRuns = 0, jaanHere = 0, badText = 0, jaanHome = 0;
    Rng pick(12345);
    for (int r = 0; r < runs; ++r) {
        Engine e(content);
        e.newGame(1000u + std::uint32_t(r));
        int steps = 0;
        while (!e.isOver() && steps < 400) {
            // sometimes use the phone contacts like a player would
            if (e.canCall() && pick.chance(0.12)) {
                auto cs = e.contacts();
                e.call(cs[std::size_t(pick.range(0, int(cs.size()) - 1))].id);
                ++calls;
            }
            // and answer phone messages
            if (e.canReply())
                for (std::size_t mi = 0; mi < e.state().messages.size(); ++mi) {
                    const auto& m = e.state().messages[mi];
                    if (m.answered < 0 && !m.replies.empty() && pick.chance(0.5)) {
                        e.reply(mi, std::size_t(pick.range(0, int(m.replies.size()) - 1)));
                        ++replies;
                        break;
                    }
                }
            if (e.isOver()) break;
            View v = e.view();
            auto bad = [](const std::string& t) { return t.find("{a") != std::string::npos || t.find("{b") != std::string::npos || t.find("{A}") != std::string::npos || t.find("{B}") != std::string::npos; };
            bool b = bad(v.text) || bad(v.speaker);
            for (const auto& c : v.choices) b = b || bad(c.label);
            if (b && badText++ < 5) std::cout << "UNSUBSTITUTED in " << e.state().scene << ": " << v.speaker << " / " << v.text.substr(0, 60) << "\n";
            for (const auto& p : v.present) if (p == "JAAN") { ++jaanHere; break; }
            std::vector<int> open;
            // Odd runs play "explorer": no going home before 04:30, so deep content gets exercised.
            const bool explorer = r % 2 == 1 && e.state().minute < 270;
            for (const auto& c : v.choices) {
                if (c.locked) continue;
                if (explorer && (c.label == "Kutsu takso" || c.label.rfind("Kõnni koju", 0) == 0 || c.label == "Telli ära")) continue;
                open.push_back(c.index);
            }
            if (open.empty()) {
                ++deadEnds;
                std::cout << "DEAD END at " << e.state().location << " scene=" << e.state().scene << "\n";
                break;
            }
            int c = open[pick.range(0, int(open.size()) - 1)];
            if (verbose) std::cout << Engine::fmtClock(e.state().minute) << " [" << e.state().location << "/" << e.state().scene
                                   << "] -> " << v.choices[c].label << "\n";
            e.choose(c);
            e.drainEvents();
            ++steps;
        }
        if (!e.isOver()) { ++loops; continue; }
        bool grp = false;
        for (const auto& f : e.state().flags)
            if (f.rfind("scene_seen_", 0) == 0) {
                std::string sc = f.substr(11);
                static const std::vector<std::string> watch = {"g_kardo", "kardo", "meet_kardo", "call_kardo", "in_kardo", "truuse_torta", "call_truuse_jail",
                                                                "s_charger", "s_cone_cop", "m_payback", "s_dog", "s_wallet_return", "janno", "martin"};
                bool w = false;
                for (const auto& x : watch) w = w || sc.rfind(x, 0) == 0;
                if (w || sc.rfind("g_", 0) == 0) {
                    sceneRuns[sc]++;
                    if (sc.rfind("g_", 0) == 0) grp = true;
                }
            }
        groupRuns += grp;
        if (!e.state().flags.count("away_jaan")) ++jaanHome;
        endings[e.state().ending]++;
        {
            Recap rc = e.recap();
            for (const auto& [title, ok] : rc.tasks) { taskSeen[title]++; if (ok) taskDone[title]++; }
            for (const auto& [title, ok] : rc.sideTasks) { sideSeen[title]++; sideGot++; if (ok) { sideDone[title]++; sideOk++; } }
            scoreSum += rc.total;
            scoreMax = std::max(scoreMax, rc.total);
            scoreMin = std::min(scoreMin, rc.total);
            ++scored;
        }
        if (e.state().vars.count("roam_count")) roams += int(e.state().var("roam_count"));
        if (e.state().flags.count("blackout")) ++blackouts;
        if (e.state().visited.count("police_bench")) ++police;
        if (verbose) {
            Recap rc = e.recap();
            std::cout << "== " << rc.ending.title << "\n";
            for (auto& l : rc.night) std::cout << "  * " << l << "\n";
            for (auto& l : rc.friends) std::cout << "  ~ " << l << "\n";
        }
    }
    std::cout << "Simulated " << runs << " runs\n";
    for (auto& [k, n] : endings) std::cout << "  " << k << ": " << n << "\n";
    std::cout << "  blackouts: " << blackouts << ", police bench: " << police << "\n";
    std::cout << "  message replies per run: " << double(replies) / std::max(1, runs) << "\n";
    std::cout << "  phone calls per run: " << double(calls) / std::max(1, runs) << "\n";
    std::cout << "  roaming encounters per run: " << double(roams) / std::max(1, runs) << "\n";
    std::cout << "  task completion (random play):\n";
    for (auto& [t, n] : taskSeen) std::cout << "    " << taskDone[t] * 100 / std::max(1, n) << "%  " << t << "\n";
    std::cout << "  side quests per run: " << double(sideGot) / std::max(1L, scored) << ", done per run: " << double(sideOk) / std::max(1L, scored) << "\n";
    for (auto& [t, n] : sideSeen) std::cout << "    side " << sideDone[t] * 100 / std::max(1, n) << "% of " << n << "  " << t << "\n";
    std::cout << "  score: avg " << scoreSum / std::max(1L, scored) << ", min " << scoreMin << ", max " << scoreMax << "\n";
    std::cout << "  runs with a group talk: " << groupRuns << ", Jaan in town: " << jaanHome << " runs, unsubstituted texts: " << badText << "\n";
    for (auto& [sc, n] : sceneRuns) if (verbose || sc.rfind("g_", 0) != 0 || sc.rfind("g_kardo", 0) == 0) std::cout << "    " << n << "  " << sc << "\n";
    std::cout << "  dead ends: " << deadEnds << ", unfinished (400 steps): " << loops << "\n";
    return deadEnds + loops == 0 ? 0 : 1;
}
