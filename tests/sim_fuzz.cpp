// Plays thousands of random weeks to find dead branches in the butterfly graph.
// Usage: sim_fuzz <content_dir> [runs=2000] [--verbose seed]

#include "../src/core/sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace od;

static std::vector<std::pair<std::string, std::string>> load_dir(const std::string &dir) {
	std::vector<std::pair<std::string, std::string>> files;
	DIR *d = opendir(dir.c_str());
	if (!d) {
		return files;
	}
	while (dirent *e = readdir(d)) {
		std::string name = e->d_name;
		if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
			std::ifstream f(dir + "/" + name);
			std::stringstream ss;
			ss << f.rdbuf();
			files.emplace_back(name, ss.str());
		}
	}
	closedir(d);
	std::sort(files.begin(), files.end());
	return files;
}

static void play(Sim &sim, uint64_t seed, bool verbose, std::map<std::string, int> &action_counts) {
	std::mt19937_64 rng(seed * 2654435761ull + 17);
	auto chance = [&](double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; };
	sim.new_game(seed);
	for (int step = 0; step < 6000 && !sim.finished(); ++step) {
		for (const auto &m : sim.drain_messages()) {
			if (verbose) {
				std::printf("   [%s] %s%s%s\n", m.kind.c_str(), m.from.c_str(), m.from.empty() ? "" : ": ", m.text.c_str());
			}
		}
		if (sim.can_use_stash() && chance(0.3)) {
			if (!sim.stash().empty() && chance(0.5)) {
				sim.take_from_stash(int(rng() % sim.stash().size()));
			} else {
				sim.move_to_stash(int(rng() % kInventorySlots));
			}
			continue;
		}
		std::vector<ActionView> acts;
		bool allow_wait = chance(0.15);
		for (const auto &a : sim.available_actions()) {
			if (a.enabled && (allow_wait || a.group != "czekanie")) {
				acts.push_back(a);
			}
		}
		double r = std::uniform_real_distribution<double>(0, 1)(rng);
		if (r < 0.45 && !acts.empty()) {
			// Coverage-guided: rarely tried actions are more likely to be picked.
			double total = 0;
			for (const auto &c : acts) {
				total += 1.0 / (1.0 + action_counts[c.id]);
			}
			double roll = std::uniform_real_distribution<double>(0, total)(rng);
			size_t pick = 0;
			for (; pick + 1 < acts.size(); ++pick) {
				roll -= 1.0 / (1.0 + action_counts[acts[pick].id]);
				if (roll <= 0) {
					break;
				}
			}
			const ActionView &a = acts[pick];
			if (verbose) {
				std::printf("%s %s @%s > %s\n", day_short(sim.day()), format_clock(sim.tick_of_day()).c_str(), sim.location().c_str(), a.label.c_str());
			}
			action_counts[a.id]++;
			sim.perform(a.id);
			continue;
		}
		std::vector<std::pair<const LinkDef *, bool>> usable;
		for (const auto &l : sim.links()) {
			if (l.a.seg == sim.player_seg() && sim.can_use_link(l.id, true, nullptr)) {
				usable.push_back({ &l, true });
			}
			if (l.b.seg == sim.player_seg() && sim.can_use_link(l.id, false, nullptr)) {
				usable.push_back({ &l, false });
			}
		}
		if (r < 0.85 && !usable.empty()) {
			auto pick = usable[rng() % usable.size()];
			const LinkEnd &from = pick.second ? pick.first->a : pick.first->b;
			sim.set_player_pos(from.seg, from.x);
			if (verbose) {
				std::printf("%s %s @%s -> link %s\n", day_short(sim.day()), format_clock(sim.tick_of_day()).c_str(), sim.location().c_str(), pick.first->id.c_str());
			}
			sim.use_link(pick.first->id, pick.second);
			continue;
		}
		// wander along the current segment
		const Json &segs = sim.world()["segments"];
		for (const Json &s : segs.items()) {
			if (s.get_string("id") == sim.player_seg()) {
				double x0 = s.get_number("x0"), x1 = s.get_number("x1");
				sim.set_player_pos(sim.player_seg(), x0 + (x1 - x0) * std::uniform_real_distribution<double>(0, 1)(rng));
			}
		}
	}
	for (const auto &m : sim.drain_messages()) {
		if (verbose) {
			std::printf("   [%s] %s\n", m.kind.c_str(), m.text.c_str());
		}
	}
}

// Script commands, one per line:
//   act <action>        go <link> <a|b>      walk <seg> <x>
//   take <item>         stash <item>         until <day 0-6> <HH:MM>
//   expect <fact>       # comment
static int run_script(Sim &sim, const std::string &path) {
	std::ifstream f(path);
	std::string line;
	int line_no = 0, failures = 0;
	sim.new_game(1);
	auto flush = [&]() {
		for (const auto &m : sim.drain_messages()) {
			std::printf("     [%s] %s%s%s\n", m.kind.c_str(), m.from.c_str(), m.from.empty() ? "" : ": ", m.text.c_str());
		}
	};
	auto fail = [&](const std::string &msg) {
		std::printf("!! line %d: %s\n", line_no, msg.c_str());
		++failures;
	};
	flush();
	while (std::getline(f, line)) {
		++line_no;
		std::istringstream in(line);
		std::string cmd;
		in >> cmd;
		if (cmd.empty() || cmd[0] == '#') {
			continue;
		}
		std::printf("%s %s @%-10s > %s\n", day_short(sim.day()), format_clock(sim.tick_of_day()).c_str(), sim.location().c_str(), line.c_str());
		if (cmd == "act") {
			std::string id;
			in >> id;
			if (!sim.perform(id)) {
				std::string why = "not available";
				for (const auto &a : sim.available_actions()) {
					if (a.id == id) {
						why = a.why;
					}
				}
				fail("action " + id + ": " + why);
			}
		} else if (cmd == "go") {
			std::string id, side;
			in >> id >> side;
			const LinkDef *l = sim.link(id);
			if (!l) {
				fail("no link " + id);
				continue;
			}
			bool from_a = side != "b";
			const LinkEnd &from = from_a ? l->a : l->b;
			sim.set_player_pos(from.seg, from.x);
			std::string why;
			if (!sim.can_use_link(id, from_a, &why)) {
				fail("link " + id + ": " + why);
			} else {
				sim.use_link(id, from_a);
			}
		} else if (cmd == "walk") {
			std::string seg;
			double x = 0;
			in >> seg >> x;
			double cur = sim.player_x();
			int steps = int(std::fabs(x - cur) / 0.5) + 1;
			for (int i = 1; i <= steps; ++i) {
				sim.set_player_pos(seg, cur + (x - cur) * i / steps);
			}
		} else if (cmd == "take" || cmd == "stash") {
			std::string id;
			in >> id;
			bool ok = false;
			if (cmd == "take") {
				for (size_t i = 0; i < sim.stash().size(); ++i) {
					if (sim.stash()[i] == id) {
						ok = sim.take_from_stash(int(i));
						break;
					}
				}
			} else {
				for (int i = 0; i < kInventorySlots; ++i) {
					if (sim.inventory()[size_t(i)] == id) {
						ok = sim.move_to_stash(i);
						break;
					}
				}
			}
			if (!ok) {
				fail(cmd + " " + id + " failed");
			}
		} else if (cmd == "until") {
			int d = 0;
			std::string hhmm;
			in >> d >> hhmm;
			int target = d * kTicksPerDay + parse_clock(hhmm, 0);
			while (sim.now() < target && !sim.finished()) {
				if (!sim.perform("wait")) {
					fail("wait failed");
					break;
				}
			}
		} else if (cmd == "expect") {
			std::string fact;
			in >> fact;
			if (!sim.fact(fact)) {
				fail("expected fact " + fact);
			}
		} else {
			fail("unknown command " + cmd);
		}
		flush();
	}
	std::printf("\nstats:");
	for (const auto &kv : sim.stats()) {
		std::printf(" %s=%d", kv.first.c_str(), kv.second);
	}
	EndingView e = sim.ending();
	std::printf("\n\n== %s == (finished=%d)\n%s\n", e.title.c_str(), sim.finished(), e.text.c_str());
	for (const auto &l : e.epilogue) {
		std::printf("  - %s\n", l.c_str());
	}
	std::printf("\nDlaczego:\n");
	for (const auto &l : e.why) {
		std::printf("%s\n", l.c_str());
	}
	std::printf("\n%d failures\n", failures);
	return failures ? 1 : 0;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <content_dir> [runs] [--verbose seed]\n", argv[0]);
		return 2;
	}
	Sim sim;
	bool ok = sim.load(load_dir(argv[1]));
	for (const auto &e : sim.errors()) {
		std::printf("CONTENT ERROR: %s\n", e.c_str());
	}
	if (!ok) {
		return 1;
	}
	if (argc > 3 && std::strcmp(argv[2], "--script") == 0) {
		return run_script(sim, argv[3]);
	}
	int runs = argc > 2 ? std::atoi(argv[2]) : 2000;
	std::map<std::string, int> action_counts;
	if (argc > 4 && std::strcmp(argv[3], "--verbose") == 0) {
		play(sim, uint64_t(std::atoll(argv[4])), true, action_counts);
		EndingView e = sim.ending();
		std::printf("\n== %s ==\n%s\n", e.title.c_str(), e.text.c_str());
		for (const auto &l : e.epilogue) {
			std::printf("  - %s\n", l.c_str());
		}
		std::printf("\nDlaczego:\n");
		for (const auto &l : e.why) {
			std::printf("%s\n", l.c_str());
		}
		return 0;
	}

	std::map<std::string, int> endings, rule_counts, fact_counts;
	for (int i = 0; i < runs; ++i) {
		play(sim, uint64_t(i + 1), false, action_counts);
		endings[sim.ending_id()]++;
		for (const auto &kv : sim.fired()) {
			rule_counts[kv.first]++;
		}
	}
	std::printf("Runs: %d\n\nEndings:\n", runs);
	for (const auto &kv : endings) {
		std::printf("  %-24s %5.1f%%\n", kv.first.c_str(), 100.0 * kv.second / runs);
	}
	std::printf("\nRules never fired:\n");
	int dead = 0;
	for (const auto &r : sim.rules()) {
		if (!rule_counts.count(r.id)) {
			std::printf("  %s\n", r.id.c_str());
			++dead;
		}
	}
	std::printf("\nRare rules (<1%%):\n");
	for (const auto &r : sim.rules()) {
		auto it = rule_counts.find(r.id);
		if (it != rule_counts.end() && it->second * 100 < runs) {
			std::printf("  %-28s %d\n", r.id.c_str(), it->second);
		}
	}
	std::printf("\nActions never performed:\n");
	for (const auto &a : sim.actions()) {
		if (!action_counts.count(a.id)) {
			std::printf("  %s\n", a.id.c_str());
		}
	}
	std::printf("\n%d / %zu rules dead\n", dead, sim.rules().size());
	return 0;
}
