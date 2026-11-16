#include "sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace od {

namespace {

const char *kDayNames[kDays] = { "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota", "Niedziela" };
const char *kDayIds[kDays] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
const char *kDayShort[kDays] = { "Pon", "Wt", "Śr", "Czw", "Pt", "Sob", "Nd" };

std::string trim(const std::string &s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos) {
		return "";
	}
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

std::vector<std::string> split(const std::string &s, char sep) {
	std::vector<std::string> out;
	size_t start = 0;
	while (true) {
		size_t p = s.find(sep, start);
		out.push_back(trim(s.substr(start, p == std::string::npos ? std::string::npos : p - start)));
		if (p == std::string::npos) {
			break;
		}
		start = p + 1;
	}
	return out;
}

bool starts_with(const std::string &s, const char *prefix) {
	return s.rfind(prefix, 0) == 0;
}

uint64_t splitmix(uint64_t x) {
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

uint64_t fnv(const std::string &s) {
	uint64_t h = 1469598103934665603ull;
	for (unsigned char c : s) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

bool compare(double lhs, Cond::Op op, double rhs) {
	switch (op) {
		case Cond::Op::Truthy: return lhs != 0.0;
		case Cond::Op::Eq: return lhs == rhs;
		case Cond::Op::Ne: return lhs != rhs;
		case Cond::Op::Lt: return lhs < rhs;
		case Cond::Op::Le: return lhs <= rhs;
		case Cond::Op::Gt: return lhs > rhs;
		case Cond::Op::Ge: return lhs >= rhs;
	}
	return false;
}

int clamp_stat(const std::string &name, int v) {
	if (name == "money") {
		return v;
	}
	if (starts_with(name, "rel.")) {
		return std::max(-100, std::min(100, v));
	}
	return std::max(0, std::min(100, v));
}

} // namespace

std::string format_clock(int tick_of_day) {
	char buf[8];
	std::snprintf(buf, sizeof(buf), "%02d:%02d", (tick_of_day / kTicksPerHour) % 24, (tick_of_day % kTicksPerHour) * 15);
	return buf;
}

int parse_clock(const std::string &hhmm, int fallback) {
	int h = 0, m = 0;
	if (std::sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2) {
		return fallback;
	}
	return h * kTicksPerHour + m / 15;
}

const char *day_name(int day) {
	return day >= 0 && day < kDays ? kDayNames[day] : "Koniec";
}

const char *day_short(int day) {
	return day >= 0 && day < kDays ? kDayShort[day] : "-";
}

// =================================================================== loading

ToneText Sim::parse_text(const Json &v) {
	ToneText t;
	if (v.is_string()) {
		t.cold = t.warm = v.as_string();
	} else if (v.is_object()) {
		t.cold = v.get_string("cold");
		t.warm = v.get_string("warm", t.cold);
		if (t.cold.empty()) {
			t.cold = t.warm;
		}
	}
	return t;
}

unsigned Sim::parse_days(const Json &v) {
	std::vector<std::string> names;
	if (v.is_null()) {
		return 0x7F;
	}
	if (v.is_string()) {
		names = split(v.as_string(), ',');
	} else {
		for (const Json &e : v.items()) {
			if (e.is_number()) {
				names.push_back(kDayIds[std::max(0, std::min(6, e.as_int()))]);
			} else {
				names.push_back(e.as_string());
			}
		}
	}
	unsigned mask = 0;
	for (const std::string &n : names) {
		if (n == "*" || n == "all") {
			mask |= 0x7F;
		} else if (n == "weekdays") {
			mask |= 0x1F;
		} else if (n == "weekend") {
			mask |= 0x60;
		} else {
			for (int d = 0; d < kDays; ++d) {
				if (n == kDayIds[d]) {
					mask |= 1u << d;
				}
			}
		}
	}
	return mask;
}

Cond Sim::parse_cond(const std::string &raw, const std::string &ctx) {
	Cond c;
	std::string s = trim(raw);
	if (s.find('|') != std::string::npos) {
		c.kind = Cond::Kind::Any;
		for (const std::string &part : split(s, '|')) {
			c.any.push_back(parse_cond(part, ctx));
		}
		return c;
	}
	if (!s.empty() && s[0] == '!') {
		c.negate = true;
		s = trim(s.substr(1));
	}
	auto after = [&](size_t n) { return trim(s.substr(n)); };
	if (starts_with(s, "item:")) {
		c.kind = Cond::Kind::Item;
		c.a = after(5);
	} else if (starts_with(s, "stash:")) {
		c.kind = Cond::Kind::Stash;
		c.a = after(6);
	} else if (starts_with(s, "at:")) {
		c.kind = Cond::Kind::At;
		c.list = split(after(3), ',');
	} else if (starts_with(s, "npc:")) {
		c.kind = Cond::Kind::NpcAt;
		std::string rest = after(4);
		size_t at = rest.find('@');
		c.a = trim(rest.substr(0, at));
		c.list = split(at == std::string::npos ? "*" : rest.substr(at + 1), ',');
	} else if (starts_with(s, "chance:")) {
		c.kind = Cond::Kind::Chance;
		c.value = std::atof(after(7).c_str());
		c.a = ctx;
	} else if (starts_with(s, "day:")) {
		c.kind = Cond::Kind::Day;
		c.value = parse_days(Json(after(4)));
	} else if (starts_with(s, "fired:")) {
		c.kind = Cond::Kind::Fired;
		c.a = after(6);
	} else if (starts_with(s, "weather:")) {
		c.kind = Cond::Kind::Weather;
		c.a = after(8);
	} else {
		static const std::pair<const char *, Cond::Op> ops[] = {
			{ ">=", Cond::Op::Ge }, { "<=", Cond::Op::Le }, { "==", Cond::Op::Eq }, { "!=", Cond::Op::Ne },
			{ ">", Cond::Op::Gt }, { "<", Cond::Op::Lt }, { "=", Cond::Op::Eq },
		};
		std::string lhs = s;
		for (const auto &op : ops) {
			size_t p = s.find(op.first);
			if (p != std::string::npos) {
				lhs = trim(s.substr(0, p));
				std::string rhs = trim(s.substr(p + std::string(op.first).size()));
				c.op = op.second;
				c.value = rhs.find(':') != std::string::npos ? parse_clock(rhs, 0) : std::atof(rhs.c_str());
				break;
			}
		}
		if (!lhs.empty() && lhs[0] == '$') {
			c.kind = Cond::Kind::Stat;
			c.a = lhs.substr(1);
		} else {
			c.kind = Cond::Kind::Fact;
			c.a = lhs;
		}
		if (c.a.empty()) {
			errors_.push_back(ctx + ": empty condition '" + raw + "'");
		}
	}
	return c;
}

std::vector<Cond> Sim::parse_conds(const Json &v, const std::string &ctx) {
	std::vector<Cond> out;
	if (v.is_string()) {
		out.push_back(parse_cond(v.as_string(), ctx + "#0"));
	} else {
		for (size_t i = 0; i < v.size(); ++i) {
			out.push_back(parse_cond(v[i].as_string(), ctx + "#" + std::to_string(i)));
		}
	}
	return out;
}

Effect Sim::parse_effect(const std::string &raw, const std::string &ctx) {
	Effect e;
	std::string s = trim(raw);
	auto kv = [&](const std::string &body, int def) {
		size_t eq = body.find('=');
		e.a = trim(body.substr(0, eq));
		e.value = eq == std::string::npos ? def : std::atoi(trim(body.substr(eq + 1)).c_str());
	};
	if (starts_with(s, "set:")) {
		e.kind = Effect::Kind::SetFact;
		kv(s.substr(4), 1);
	} else if (starts_with(s, "add:")) {
		e.kind = Effect::Kind::AddFact;
		kv(s.substr(4), 1);
	} else if (starts_with(s, "clear:")) {
		e.kind = Effect::Kind::ClearFact;
		e.a = trim(s.substr(6));
	} else if (starts_with(s, "give:")) {
		e.kind = Effect::Kind::Give;
		e.a = trim(s.substr(5));
	} else if (starts_with(s, "take:")) {
		e.kind = Effect::Kind::Take;
		e.a = trim(s.substr(5));
	} else if (starts_with(s, "stash:")) {
		e.kind = Effect::Kind::Stash;
		e.a = trim(s.substr(6));
	} else if (starts_with(s, "msg:")) {
		e.kind = Effect::Kind::Msg;
		e.a = trim(s.substr(4));
	} else if (starts_with(s, "phone:")) {
		e.kind = Effect::Kind::Phone;
		std::string body = s.substr(6);
		size_t p1 = body.find('|');
		size_t p2 = p1 == std::string::npos ? std::string::npos : body.find('|', p1 + 1);
		if (p2 == std::string::npos) {
			errors_.push_back(ctx + ": phone effect needs id|from|text");
		} else {
			e.a = trim(body.substr(0, p1));
			e.b = trim(body.substr(p1 + 1, p2 - p1 - 1));
			e.c = trim(body.substr(p2 + 1));
		}
	} else if (starts_with(s, "time+=")) {
		e.kind = Effect::Kind::Time;
		e.value = std::atoi(s.substr(6).c_str());
	} else if (starts_with(s, "ending:")) {
		e.kind = Effect::Kind::Ending;
		e.a = trim(s.substr(7));
	} else if (starts_with(s, "goto:")) {
		e.kind = Effect::Kind::Goto;
		e.a = trim(s.substr(5));
	} else if (!s.empty() && s[0] == '$') {
		e.kind = Effect::Kind::Stat;
		size_t p = s.find_first_of("+-=", 1);
		if (p == std::string::npos) {
			errors_.push_back(ctx + ": bad stat effect '" + raw + "'");
			return e;
		}
		e.a = trim(s.substr(1, p - 1));
		e.op = s[p];
		size_t vpos = (e.op != '=' && p + 1 < s.size() && s[p + 1] == '=') ? p + 2 : p + 1;
		e.value = std::atoi(trim(s.substr(vpos)).c_str());
	} else {
		errors_.push_back(ctx + ": unknown effect '" + raw + "'");
	}
	return e;
}

std::vector<Effect> Sim::parse_effects(const Json &v, const std::string &ctx) {
	std::vector<Effect> out;
	if (v.is_string()) {
		out.push_back(parse_effect(v.as_string(), ctx));
	}
	for (const Json &e : v.items()) {
		out.push_back(parse_effect(e.as_string(), ctx));
	}
	return out;
}

std::vector<Requirement> Sim::parse_requires(const Json &v, const std::string &ctx) {
	std::vector<Requirement> out;
	for (const Json &r : v.items()) {
		Requirement req;
		req.when = parse_conds(r["if"], ctx);
		req.why = r.get_string("why", "Nie teraz.");
		out.push_back(std::move(req));
	}
	return out;
}

void Sim::load_file(const std::string &name, const Json &root) {
	for (const auto &kv : root["items"].members()) {
		ItemDef d;
		d.id = kv.first;
		d.name = kv.second.get_string("name", kv.first);
		d.desc = kv.second.get_string("desc");
		d.color = kv.second.get_string("color", "#cccccc");
		items_[d.id] = d;
	}
	for (const auto &kv : root["npcs"].members()) {
		const Json &j = kv.second;
		NpcDef n;
		n.id = kv.first;
		n.name = j.get_string("name", n.id);
		n.unknown_name = j.get_string("unknown_name", n.name);
		n.known_fact = j.get_string("known_fact");
		n.sprite = j.get_string("sprite", n.id);
		for (const Json &s : j["schedule"].items()) {
			ScheduleEntry e;
			e.days = parse_days(s["days"]);
			e.from = parse_clock(s.get_string("from", "00:00"), 0);
			e.to = parse_clock(s.get_string("to", "24:00"), kTicksPerDay);
			e.loc = s.get_string("loc");
			e.has_x = s.has("x");
			e.x = s.get_number("x");
			e.activity = s.get_string("activity");
			e.when = parse_conds(s["when"], name + ":npc:" + n.id);
			n.schedule.push_back(std::move(e));
		}
		npcs_.push_back(std::move(n));
	}
	for (const auto &kv : root["locations"].members()) {
		const Json &j = kv.second;
		LocationDef l;
		l.id = kv.first;
		l.name = j.get_string("name", l.id);
		l.battery = j.get_int("battery");
		l.stash = j.get_bool("stash");
		l.outdoor = j.get_bool("outdoor");
		std::string open = j.get_string("open");
		if (!open.empty()) {
			auto parts = split(open, '-');
			l.open_from = parse_clock(parts[0], 0);
			l.open_to = parts.size() > 1 ? parse_clock(parts[1], kTicksPerDay) : kTicksPerDay;
		}
		l.exit_to = j.get_string("exit_to");
		l.spawn_seg = j["spawn"].get_string("seg");
		l.spawn_x = j["spawn"].get_number("x");
		locations_[l.id] = l;
	}
	for (const Json &j : root["segments"].items()) {
		SegmentDef s;
		s.id = j.get_string("id");
		s.loc = j.get_string("loc");
		s.x0 = j.get_number("x0");
		s.x1 = j.get_number("x1");
		s.y = j.get_number("y");
		s.z = j.get_number("z");
		s.outdoor = j.get_bool("outdoor");
		segments_.push_back(s);
	}
	for (const Json &j : root["areas"].items()) {
		AreaDef a;
		a.seg = j.get_string("seg");
		a.loc = j.get_string("loc");
		a.x0 = j.get_number("x0");
		a.x1 = j.get_number("x1");
		areas_.push_back(a);
	}
	for (const Json &j : root["links"].items()) {
		LinkDef l;
		l.id = j.get_string("id");
		auto end = [](const Json &e) {
			LinkEnd le;
			le.seg = e.get_string("seg");
			le.x = e.get_number("x");
			le.key = e.get_string("key", "enter");
			le.label = e.get_string("label");
			return le;
		};
		l.a = end(j["a"]);
		l.b = end(j["b"]);
		l.cost = j.get_int("cost");
		l.oneway = j.get_bool("oneway");
		l.requires_from = j.get_string("requires_from", "both");
		l.requires_ = parse_requires(j["requires"], name + ":link:" + l.id);
		l.effects = parse_effects(j["effects"], name + ":link:" + l.id);
		l.text = parse_text(j["text"]);
		links_.push_back(std::move(l));
	}
	for (const Json &j : root["actions"].items()) {
		ActionDef a;
		a.id = j.get_string("id");
		std::string ctx = name + ":action:" + a.id;
		a.label = j.get_string("label", a.id);
		a.group = j.get_string("group");
		a.locs = j.get_strings("loc");
		a.when = parse_conds(j["when"], ctx);
		a.requires_ = parse_requires(j["requires"], ctx);
		a.cost = j.get_int("cost", 1);
		a.effects = parse_effects(j["effects"], ctx);
		a.text = parse_text(j["text"]);
		a.limit = j.get_string("limit");
		a.sleep = j.get_bool("sleep");
		int oi = 0;
		for (const Json &o : j["outcomes"].items()) {
			Outcome out;
			out.when = parse_conds(o["when"], ctx + ":o" + std::to_string(oi++));
			out.effects = parse_effects(o["effects"], ctx);
			out.text = parse_text(o["text"]);
			a.outcomes.push_back(std::move(out));
		}
		actions_.push_back(std::move(a));
	}
	for (const Json &j : root["rules"].items()) {
		RuleDef r;
		r.id = j.get_string("id");
		std::string ctx = name + ":rule:" + r.id;
		r.label = j.get_string("label", r.id);
		r.when = parse_conds(j["when"], ctx);
		r.check = parse_conds(j["check"], ctx + ":check");
		r.effects = parse_effects(j["effects"], ctx);
		r.else_effects = parse_effects(j["else_effects"], ctx);
		r.text = parse_text(j["text"]);
		r.else_text = parse_text(j["else_text"]);
		r.repeat = j.get_string("repeat");
		r.hidden = j.get_bool("hidden");
		const Json &d = j["delay"];
		if (d.is_object()) {
			r.delayed = true;
			r.delay_days = d.get_int("days");
			r.delay_ticks = d.get_int("hours") * kTicksPerHour + d.get_int("ticks");
			r.delay_at = parse_clock(d.get_string("at"), -1);
		}
		rules_.push_back(std::move(r));
	}
	for (const Json &j : root["endings"].items()) {
		EndingDef e;
		e.id = j.get_string("id");
		e.title = j.get_string("title");
		e.when = parse_conds(j["when"], name + ":ending:" + e.id);
		e.text = parse_text(j["text"]);
		endings_.push_back(std::move(e));
	}
	for (const Json &j : root["epilogue"].items()) {
		EpilogueDef e;
		e.when = parse_conds(j["when"], name + ":epilogue");
		e.text = parse_text(j["text"]);
		epilogue_.push_back(std::move(e));
	}
	for (const Json &j : root["weather"].items()) {
		WeatherDef w;
		w.days = parse_days(j["days"]);
		w.from = parse_clock(j.get_string("from", "00:00"), 0);
		w.to = parse_clock(j.get_string("to", "24:00"), kTicksPerDay);
		w.type = j.get_string("type");
		weather_.push_back(w);
	}
	if (root.has("start")) {
		start_ = root["start"];
	}
	for (const char *key : { "buildings", "props", "streets", "segments", "links", "areas" }) {
		for (const Json &e : root[key].items()) {
			Json arr = world_[key];
			arr.push_back(e);
			world_.set(key, arr);
		}
	}
	for (const auto &kv : root["locations"].members()) {
		Json obj = world_["locations"];
		obj.set(kv.first, kv.second);
		world_.set("locations", obj);
	}
	for (const auto &kv : root["npcs"].members()) {
		Json obj = world_["npcs"];
		Json n = Json::object();
		n.set("sprite", kv.second.get_string("sprite", kv.first));
		n.set("name", kv.second.get_string("name", kv.first));
		obj.set(kv.first, n);
		world_.set("npcs", obj);
	}
	for (const auto &kv : root["items"].members()) {
		Json obj = world_["items"];
		obj.set(kv.first, kv.second);
		world_.set("items", obj);
	}
}

bool Sim::load(const std::vector<std::pair<std::string, std::string>> &files) {
	*this = Sim();
	world_ = Json::object();
	for (const auto &f : files) {
		std::string err;
		Json root = Json::parse(f.second, &err);
		if (!root.is_object()) {
			errors_.push_back(f.first + ": " + (err.empty() ? "root must be an object" : err));
			continue;
		}
		load_file(f.first, root);
	}
	validate();
	return errors_.empty();
}

void Sim::validate() {
	std::set<std::string> written, read;
	std::function<void(const std::vector<Cond> &, const std::string &)> check_conds;
	check_conds = [&](const std::vector<Cond> &cs, const std::string &ctx) {
		for (const Cond &c : cs) {
			switch (c.kind) {
				case Cond::Kind::Fact: read.insert(c.a); break;
				case Cond::Kind::Item:
				case Cond::Kind::Stash:
					if (!items_.count(c.a)) {
						errors_.push_back(ctx + ": unknown item '" + c.a + "'");
					}
					break;
				case Cond::Kind::At:
					for (const auto &l : c.list) {
						if (!locations_.count(l)) {
							errors_.push_back(ctx + ": unknown location '" + l + "'");
						}
					}
					break;
				case Cond::Kind::NpcAt:
					if (!npc(c.a)) {
						errors_.push_back(ctx + ": unknown npc '" + c.a + "'");
					}
					for (const auto &l : c.list) {
						if (l != "here" && l != "*" && !locations_.count(l)) {
							errors_.push_back(ctx + ": unknown location '" + l + "'");
						}
					}
					break;
				case Cond::Kind::Any: check_conds(c.any, ctx); break;
				default: break;
			}
		}
	};
	auto check_effects = [&](const std::vector<Effect> &es, const std::string &ctx) {
		for (const Effect &e : es) {
			switch (e.kind) {
				case Effect::Kind::SetFact:
				case Effect::Kind::AddFact:
				case Effect::Kind::ClearFact: written.insert(e.a); break;
				case Effect::Kind::Phone: written.insert("read_" + e.a); break;
				case Effect::Kind::Give:
				case Effect::Kind::Take:
				case Effect::Kind::Stash:
					if (!items_.count(e.a)) {
						errors_.push_back(ctx + ": unknown item '" + e.a + "'");
					}
					written.insert("no_room_" + e.a);
					break;
				case Effect::Kind::Goto:
					if (!locations_.count(e.a)) {
						errors_.push_back(ctx + ": unknown location '" + e.a + "'");
					}
					break;
				default: break;
			}
		}
	};
	for (const auto &a : actions_) {
		std::string ctx = "action " + a.id;
		check_conds(a.when, ctx);
		for (const auto &r : a.requires_) {
			check_conds(r.when, ctx);
		}
		check_effects(a.effects, ctx);
		for (const auto &o : a.outcomes) {
			check_conds(o.when, ctx);
			check_effects(o.effects, ctx);
		}
		for (const auto &l : a.locs) {
			if (!locations_.count(l)) {
				errors_.push_back(ctx + ": unknown location '" + l + "'");
			}
		}
	}
	for (const auto &r : rules_) {
		std::string ctx = "rule " + r.id;
		check_conds(r.when, ctx);
		check_conds(r.check, ctx);
		check_effects(r.effects, ctx);
		check_effects(r.else_effects, ctx);
	}
	for (const auto &l : links_) {
		for (const auto &r : l.requires_) {
			check_conds(r.when, "link " + l.id);
		}
		check_effects(l.effects, "link " + l.id);
		if (!segment(l.a.seg) || !segment(l.b.seg)) {
			errors_.push_back("link " + l.id + ": unknown segment");
		}
	}
	for (const auto &n : npcs_) {
		for (const auto &e : n.schedule) {
			check_conds(e.when, "npc " + n.id);
			if (!locations_.count(e.loc)) {
				errors_.push_back("npc " + n.id + ": unknown location '" + e.loc + "'");
			}
		}
		if (!n.known_fact.empty()) {
			read.insert(n.known_fact);
		}
	}
	for (const auto &e : endings_) {
		check_conds(e.when, "ending " + e.id);
	}
	for (const auto &e : epilogue_) {
		check_conds(e.when, "epilogue");
	}
	for (const auto &kv : start_["facts"].members()) {
		written.insert(kv.first);
	}
	for (const auto &f : read) {
		if (!written.count(f)) {
			errors_.push_back("fact '" + f + "' is read but never set");
		}
	}
	for (const auto &s : segments_) {
		if (!locations_.count(s.loc)) {
			errors_.push_back("segment " + s.id + ": unknown location '" + s.loc + "'");
		}
	}
}

// =================================================================== state

void Sim::new_game(uint64_t seed) {
	seed_ = seed;
	t_ = kDayStart;
	stats_.clear();
	facts_.clear();
	inv_.fill("");
	stash_.clear();
	fired_.clear();
	performed_.clear();
	fact_event_.clear();
	pending_.clear();
	unread_.clear();
	phone_.clear();
	events_.clear();
	out_.clear();
	finished_ = false;
	forced_ending_.clear();
	walk_acc_ = 0;
	energy_acc_ = 0;
	pending_seq_ = 0;

	for (const char *s : { "battery", "energy", "mood", "warmth", "money" }) {
		stats_[s] = 0;
	}
	for (const auto &kv : start_["stats"].members()) {
		stats_[kv.first] = kv.second.as_int();
	}
	int ev = new_event("start", "Poniedziałek. Budzisz się w swoim pokoju.", {});
	for (const auto &kv : start_["facts"].members()) {
		facts_[kv.first] = kv.second.as_int();
		fact_event_[kv.first] = ev;
	}
	int slot = 0;
	for (const std::string &id : start_.get_strings("inventory")) {
		if (slot < kInventorySlots) {
			inv_[slot++] = id;
			fact_event_["item:" + id] = ev;
		}
	}
	stash_ = start_.get_strings("stash");
	loc_ = start_.get_string("location", "room");
	if (const LocationDef *l = location_def(loc_)) {
		seg_ = l->spawn_seg;
		x_ = l->spawn_x;
	}
	fact_event_["at:" + loc_] = ev;
	location_forced_ = true;
	evaluate_rules();
}

const LocationDef *Sim::location_def(const std::string &id) const {
	auto it = locations_.find(id);
	return it == locations_.end() ? nullptr : &it->second;
}

std::string Sim::location_name(const std::string &id) const {
	const LocationDef *l = location_def(id);
	return l ? l->name : id;
}

const SegmentDef *Sim::segment(const std::string &id) const {
	for (const auto &s : segments_) {
		if (s.id == id) {
			return &s;
		}
	}
	return nullptr;
}

const NpcDef *Sim::npc(const std::string &id) const {
	for (const auto &n : npcs_) {
		if (n.id == id) {
			return &n;
		}
	}
	return nullptr;
}

const ItemDef *Sim::item(const std::string &id) const {
	auto it = items_.find(id);
	return it == items_.end() ? nullptr : &it->second;
}

const LinkDef *Sim::link(const std::string &id) const {
	for (const auto &l : links_) {
		if (l.id == id) {
			return &l;
		}
	}
	return nullptr;
}

int Sim::stat(const std::string &name) const {
	if (name == "day") {
		return day();
	}
	if (name == "time") {
		return tick_of_day();
	}
	if (name == "hour") {
		return tick_of_day() / kTicksPerHour;
	}
	if (name == "free_slots") {
		return int(std::count(inv_.begin(), inv_.end(), std::string()));
	}
	auto it = stats_.find(name);
	return it == stats_.end() ? 0 : it->second;
}

int Sim::fact(const std::string &name) const {
	auto it = facts_.find(name);
	return it == facts_.end() ? 0 : it->second;
}

std::string Sim::weather() const {
	for (const auto &w : weather_) {
		if ((w.days >> day()) & 1u && tick_of_day() >= w.from && tick_of_day() < w.to) {
			return w.type;
		}
	}
	return "";
}

bool Sim::has_item(const std::string &id) const {
	return std::find(inv_.begin(), inv_.end(), id) != inv_.end();
}

std::string Sim::location_at(const std::string &seg, double x) const {
	for (const auto &a : areas_) {
		if (a.seg == seg && x >= a.x0 && x < a.x1) {
			return a.loc;
		}
	}
	const SegmentDef *s = segment(seg);
	return s ? s->loc : loc_;
}

bool Sim::location_open(const std::string &id) const {
	const LocationDef *l = location_def(id);
	if (!l) {
		return true;
	}
	int tod = tick_of_day();
	return tod >= l->open_from && tod < l->open_to;
}

// =================================================================== evaluation

bool Sim::eval(const Cond &c) const {
	bool r = false;
	switch (c.kind) {
		case Cond::Kind::True: r = true; break;
		case Cond::Kind::Fact: r = compare(fact(c.a), c.op, c.value); break;
		case Cond::Kind::Stat: r = compare(stat(c.a), c.op, c.value); break;
		case Cond::Kind::Item: r = has_item(c.a); break;
		case Cond::Kind::Stash: r = std::find(stash_.begin(), stash_.end(), c.a) != stash_.end(); break;
		case Cond::Kind::At: r = std::find(c.list.begin(), c.list.end(), loc_) != c.list.end(); break;
		case Cond::Kind::NpcAt: {
			std::string where = npc_location(c.a);
			if (!where.empty()) {
				for (const auto &l : c.list) {
					if (l == "*" || l == where || (l == "here" && where == loc_)) {
						r = true;
					}
				}
			}
			break;
		}
		case Cond::Kind::Chance: {
			// Deterministic per seed, time and condition, so replays match exactly.
			uint64_t h = splitmix(seed_ ^ splitmix(uint64_t(t_) * 7919u) ^ fnv(c.a));
			r = double(h >> 11) / double(1ull << 53) < c.value;
			break;
		}
		case Cond::Kind::Day: r = (unsigned(c.value) >> day()) & 1u; break;
		case Cond::Kind::Fired: r = fired_.count(c.a) > 0; break;
		case Cond::Kind::Weather: r = weather() == c.a; break;
		case Cond::Kind::Any:
			for (const Cond &sub : c.any) {
				if (eval(sub)) {
					r = true;
					break;
				}
			}
			break;
	}
	return c.negate ? !r : r;
}

bool Sim::eval_all(const std::vector<Cond> &cs) const {
	for (const Cond &c : cs) {
		if (!eval(c)) {
			return false;
		}
	}
	return true;
}

void Sim::collect_facts(const std::vector<Cond> &cs, std::vector<std::string> &out) const {
	for (const Cond &c : cs) {
		switch (c.kind) {
			case Cond::Kind::Fact: out.push_back(c.a); break;
			case Cond::Kind::Item: out.push_back("item:" + c.a); break;
			case Cond::Kind::At:
				// Only the place you actually are explains a satisfied "at:" condition.
				if (!c.negate && std::find(c.list.begin(), c.list.end(), loc_) != c.list.end()) {
					out.push_back("at:" + loc_);
				}
				break;
			case Cond::Kind::Any:
				for (const Cond &sub : c.any) {
					if (eval(sub)) {
						collect_facts({ sub }, out);
					}
				}
				break;
			default: break;
		}
	}
}

std::vector<int> Sim::parents_for(const std::vector<Cond> &cs) const {
	std::vector<std::string> keys;
	collect_facts(cs, keys);
	std::vector<int> out;
	for (const auto &k : keys) {
		auto it = fact_event_.find(k);
		if (it != fact_event_.end() && std::find(out.begin(), out.end(), it->second) == out.end()) {
			out.push_back(it->second);
		}
	}
	return out;
}

int Sim::new_event(const std::string &source, const std::string &label, std::vector<int> parents) {
	CauseEvent e;
	e.id = int(events_.size());
	e.t = t_;
	e.source = source;
	e.label = label;
	e.parents = std::move(parents);
	events_.push_back(std::move(e));
	return events_.back().id;
}

const std::string &Sim::pick(const ToneText &t) const {
	return warmth() >= 50 ? t.warm : t.cold;
}

void Sim::say(const ToneText &t, const std::string &kind, const std::string &from) {
	if (!t.empty()) {
		out_.push_back({ kind, from, pick(t) });
	}
}

void Sim::touch_fact(const std::string &key, const std::string &desc, int event) {
	fact_event_[key] = event;
	if (event >= 0 && event < int(events_.size())) {
		events_[size_t(event)].changes.push_back(desc);
	}
}

void Sim::add_stat(const std::string &name, int delta) {
	stats_[name] = clamp_stat(name, stat(name) + delta);
}

void Sim::set_stat(const std::string &name, int value) {
	stats_[name] = clamp_stat(name, value);
}

bool Sim::give_item(const std::string &id, int event) {
	for (auto &slot : inv_) {
		if (slot.empty()) {
			slot = id;
			touch_fact("item:" + id, "+" + id, event);
			if (id == "phone") {
				deliver_unread();
			}
			return true;
		}
	}
	const ItemDef *d = item(id);
	out_.push_back({ "system", "", "Brak miejsca w kieszeniach – " + (d ? d->name : id) + " zostaje." });
	facts_["no_room_" + id] = 1;
	touch_fact("no_room_" + id, "no_room_" + id, event);
	return false;
}

bool Sim::remove_item(const std::string &id, int event) {
	for (auto &slot : inv_) {
		if (slot == id) {
			slot.clear();
			touch_fact("item:" + id, "-" + id, event);
			return true;
		}
	}
	auto it = std::find(stash_.begin(), stash_.end(), id);
	if (it != stash_.end()) {
		stash_.erase(it);
		return true;
	}
	return false;
}

void Sim::apply(const std::vector<Effect> &effects, int event) {
	for (const Effect &e : effects) {
		apply_one(e, event);
	}
}

void Sim::apply_one(const Effect &e, int event) {
	switch (e.kind) {
		case Effect::Kind::SetFact:
			facts_[e.a] = e.value;
			touch_fact(e.a, e.a + "=" + std::to_string(e.value), event);
			break;
		case Effect::Kind::AddFact:
			facts_[e.a] = fact(e.a) + e.value;
			touch_fact(e.a, e.a + (e.value >= 0 ? "+" : "") + std::to_string(e.value), event);
			break;
		case Effect::Kind::ClearFact:
			// An absent fact has no cause worth tracing back to.
			facts_.erase(e.a);
			touch_fact(e.a, "!" + e.a, event);
			fact_event_.erase(e.a);
			break;
		case Effect::Kind::Stat:
			if (e.op == '=') {
				set_stat(e.a, e.value);
			} else {
				add_stat(e.a, e.op == '-' ? -e.value : e.value);
			}
			break;
		case Effect::Kind::Give: give_item(e.a, event); break;
		case Effect::Kind::Take: remove_item(e.a, event); break;
		case Effect::Kind::Stash:
			remove_item(e.a, event);
			stash_.push_back(e.a);
			break;
		case Effect::Kind::Msg: out_.push_back({ "narr", "", e.a }); break;
		case Effect::Kind::Phone:
			if (has_item("phone")) {
				phone_.push_back({ e.a, e.b, e.c, t_, t_ });
				out_.push_back({ "phone", e.b, e.c });
				facts_["read_" + e.a] = t_;
				touch_fact("read_" + e.a, "read_" + e.a, event);
			} else {
				unread_.push_back({ e.a, e.b, e.c, t_ });
			}
			break;
		case Effect::Kind::Time: advance(e.value); break;
		case Effect::Kind::Ending:
			// "ending:auto" ends the week now and lets the endings list decide.
			forced_ending_ = e.a == "auto" ? "" : e.a;
			finished_ = true;
			break;
		case Effect::Kind::Goto:
			if (const LocationDef *l = location_def(e.a)) {
				loc_ = l->id;
				seg_ = l->spawn_seg;
				x_ = l->spawn_x;
				location_forced_ = true;
				touch_fact("at:" + loc_, "→ " + l->name, event);
			}
			break;
	}
}

void Sim::deliver_unread() {
	if (unread_.empty()) {
		return;
	}
	int ev = new_event("phone", "Sprawdzasz telefon", {});
	for (const auto &u : unread_) {
		phone_.push_back({ u.id, u.from, u.text, u.sent, t_ });
		std::string late = t_ - u.sent >= kTicksPerHour ? " (wysłane " + std::string(day_short(u.sent / kTicksPerDay)) + " " + format_clock(u.sent % kTicksPerDay) + ")" : "";
		out_.push_back({ "phone", u.from, u.text + late });
		facts_["read_" + u.id] = t_;
		touch_fact("read_" + u.id, "read_" + u.id, ev);
	}
	unread_.clear();
}

// =================================================================== time

void Sim::tick_once() {
	++t_;
	const LocationDef *l = location_def(loc_);
	int battery = l ? l->battery : 0;
	if (battery < 0 && has_item("headphones")) {
		battery = battery / 2;
	}
	add_stat("battery", battery);
	if (++energy_acc_ >= 2) {
		energy_acc_ = 0;
		add_stat("energy", -1);
	}
	if (stat("battery") < 15) {
		add_stat("mood", -1);
	}
	if (stat("energy") <= 0 && tick_of_day() % kTicksPerHour == 0) {
		add_stat("mood", -2);
	}
}

void Sim::advance(int ticks) {
	for (int i = 0; i < ticks && !finished_; ++i) {
		tick_once();
		if (tick_of_day() == 0) {
			--t_; // stay on the right day while handling the forced night
			end_day(false);
			return;
		}
		process_pending();
		evaluate_rules();
	}
}

void Sim::end_day(bool slept) {
	int bedtime = tick_of_day();
	int d = day();
	if (slept) {
		int hours = (kTicksPerDay - bedtime + kDayStart) / kTicksPerHour;
		add_stat("energy", hours * 10);
		out_.push_back({ "system", "", std::string("Kładziesz się o ") + format_clock(bedtime) + "." });
	} else {
		add_stat("energy", 45);
		add_stat("mood", -8);
		out_.push_back({ "system", "", "Północ. Wracasz do domu na autopilocie i zasypiasz w ubraniu." });
		facts_["late_night"] = fact("late_night") + 1;
		int ev = new_event("sleep", "Zarwana noc", {});
		touch_fact("late_night", "late_night+1", ev);
	}
	add_stat("battery", 30);
	t_ = (d + 1) * kTicksPerDay + kDayStart;
	if (d + 1 >= kDays) {
		finished_ = true;
		return;
	}
	if (const LocationDef *room = location_def(start_.get_string("location", "room"))) {
		if (loc_ != room->id) {
			loc_ = room->id;
			seg_ = room->spawn_seg;
			x_ = room->spawn_x;
			location_forced_ = true;
		}
	}
	walk_acc_ = 0;
	out_.push_back({ "system", "", std::string(day_name(day())) + ", 07:00." });
	process_pending();
	evaluate_rules();
}

void Sim::process_pending() {
	while (!finished_) {
		auto it = std::min_element(pending_.begin(), pending_.end(), [](const PendingEvent &a, const PendingEvent &b) {
			return a.due != b.due ? a.due < b.due : a.seq < b.seq;
		});
		if (it == pending_.end() || it->due > t_) {
			return;
		}
		PendingEvent p = *it;
		pending_.erase(it);
		const RuleDef *r = nullptr;
		for (const auto &rule : rules_) {
			if (rule.id == p.rule) {
				r = &rule;
			}
		}
		if (!r) {
			continue;
		}
		bool ok = eval_all(r->check);
		std::vector<int> parents = p.parents;
		for (int id : parents_for(r->check)) {
			if (std::find(parents.begin(), parents.end(), id) == parents.end()) {
				parents.push_back(id);
			}
		}
		int ev = new_event("rule", r->label, parents);
		events_[size_t(ev)].notify = !r->hidden;
		if (ok) {
			say(r->text);
			apply(r->effects, ev);
		} else {
			say(r->else_text);
			apply(r->else_effects, ev);
		}
	}
}

void Sim::fire_rule(const RuleDef &r, std::vector<int> parents) {
	fired_[r.id] = day() + 1;
	if (r.delayed) {
		int due = t_ + r.delay_days * kTicksPerDay + r.delay_ticks;
		if (r.delay_at >= 0) {
			due = (due / kTicksPerDay) * kTicksPerDay + r.delay_at;
			if (due <= t_) {
				due += kTicksPerDay;
			}
		}
		pending_.push_back({ due, r.id, std::move(parents), pending_seq_++ });
		return;
	}
	int ev = new_event("rule", r.label, std::move(parents));
	events_[size_t(ev)].notify = !r.hidden;
	say(r.text);
	apply(r.effects, ev);
}

void Sim::evaluate_rules() {
	std::set<std::string> fired_now;
	for (int pass = 0; pass < 8 && !finished_; ++pass) {
		bool any = false;
		for (const RuleDef &r : rules_) {
			if (fired_now.count(r.id)) {
				continue;
			}
			auto it = fired_.find(r.id);
			if (it != fired_.end()) {
				if (r.repeat.empty()) {
					continue;
				}
				if (r.repeat == "daily" && it->second == day() + 1) {
					continue;
				}
			}
			if (!eval_all(r.when)) {
				continue;
			}
			fired_now.insert(r.id);
			fire_rule(r, parents_for(r.when));
			any = true;
			if (finished_) {
				return;
			}
		}
		if (!any) {
			break;
		}
	}
}

void Sim::enforce_location() {
	if (finished_ || location_open(loc_)) {
		return;
	}
	const LocationDef *l = location_def(loc_);
	const LocationDef *exit = l ? location_def(l->exit_to) : nullptr;
	if (!exit) {
		return;
	}
	out_.push_back({ "system", "", l->name + ": zamykają. Musisz wyjść." });
	loc_ = exit->id;
	seg_ = exit->spawn_seg;
	x_ = exit->spawn_x;
	location_forced_ = true;
}

void Sim::check_finished() {
	if (day() >= kDays) {
		finished_ = true;
	}
}

// =================================================================== player

void Sim::set_player_pos(const std::string &seg, double x) {
	if (finished_) {
		return;
	}
	const SegmentDef *s = segment(seg);
	if (s && s->outdoor && seg == seg_) {
		walk_acc_ += std::fabs(x - x_);
	}
	seg_ = seg;
	x_ = x;
	std::string new_loc = location_at(seg, x);
	bool moved = new_loc != loc_;
	if (moved) {
		loc_ = new_loc;
		int ev = new_event("move", "Idziesz: " + location_name(loc_), {});
		touch_fact("at:" + loc_, "→ " + location_name(loc_), ev);
	}
	int walked = 0;
	while (walk_acc_ >= 18.0) {
		walk_acc_ -= 18.0;
		++walked;
	}
	if (walked) {
		advance(walked);
	}
	if (moved || walked) {
		evaluate_rules();
		enforce_location();
		check_finished();
	}
}

bool Sim::requirements_met(const std::vector<Requirement> &reqs, std::string *why) const {
	for (const auto &r : reqs) {
		if (!eval_all(r.when)) {
			if (why) {
				*why = r.why;
			}
			return false;
		}
	}
	return true;
}

bool Sim::can_use_link(const std::string &id, bool from_a, std::string *why) const {
	const LinkDef *l = link(id);
	if (!l || finished_) {
		return false;
	}
	if (!from_a && l->oneway) {
		return false;
	}
	bool guarded = l->requires_from == "both" || (l->requires_from == "a") == from_a;
	if (guarded && !requirements_met(l->requires_, why)) {
		return false;
	}
	const LinkEnd &to = from_a ? l->b : l->a;
	const SegmentDef *s = segment(to.seg);
	std::string target = location_at(to.seg, to.x);
	if (s && !location_open(target)) {
		if (why) {
			const LocationDef *ld = location_def(target);
			*why = "Zamknięte" + (ld ? " (" + format_clock(ld->open_from) + "–" + format_clock(ld->open_to) + ")" : "");
		}
		return false;
	}
	return true;
}

bool Sim::use_link(const std::string &id, bool from_a) {
	if (!can_use_link(id, from_a, nullptr)) {
		return false;
	}
	const LinkDef *l = link(id);
	const LinkEnd &to = from_a ? l->b : l->a;
	std::vector<Cond> req_conds;
	for (const auto &r : l->requires_) {
		req_conds.insert(req_conds.end(), r.when.begin(), r.when.end());
	}
	std::string old_loc = loc_;
	seg_ = to.seg;
	x_ = to.x;
	loc_ = location_at(seg_, x_);
	int ev = new_event(loc_ == old_loc ? "link" : "move", "Idziesz: " + location_name(loc_), parents_for(req_conds));
	touch_fact("at:" + loc_, "→ " + location_name(loc_), ev);
	say(l->text);
	apply(l->effects, ev);
	advance(l->cost);
	evaluate_rules();
	enforce_location();
	check_finished();
	return true;
}

// =================================================================== actions

bool Sim::action_visible(const ActionDef &a) const {
	if (!a.locs.empty() && std::find(a.locs.begin(), a.locs.end(), loc_) == a.locs.end()) {
		return false;
	}
	auto it = performed_.find(a.id);
	if (it != performed_.end()) {
		if (a.limit == "once") {
			return false;
		}
		if (a.limit == "daily" && it->second == day() + 1) {
			return false;
		}
	}
	return eval_all(a.when);
}

std::vector<ActionView> Sim::available_actions() const {
	std::vector<ActionView> out;
	if (finished_) {
		return out;
	}
	for (const ActionDef &a : actions_) {
		if (!action_visible(a)) {
			continue;
		}
		ActionView v;
		v.id = a.id;
		v.label = a.label;
		v.group = a.group;
		v.cost = a.cost;
		v.enabled = requirements_met(a.requires_, &v.why);
		std::function<void(const std::vector<Cond> &)> scan = [&](const std::vector<Cond> &cs) {
			for (const Cond &c : cs) {
				if (c.kind == Cond::Kind::NpcAt && !c.negate && std::find(c.list.begin(), c.list.end(), "here") != c.list.end() &&
						std::find(v.npcs.begin(), v.npcs.end(), c.a) == v.npcs.end()) {
					v.npcs.push_back(c.a);
				} else if (c.kind == Cond::Kind::Any) {
					scan(c.any);
				}
			}
		};
		scan(a.when);
		out.push_back(std::move(v));
	}
	return out;
}

bool Sim::perform(const std::string &action_id) {
	const ActionDef *a = nullptr;
	for (const auto &def : actions_) {
		if (def.id == action_id) {
			a = &def;
		}
	}
	if (!a || finished_ || !action_visible(*a) || !requirements_met(a->requires_, nullptr)) {
		return false;
	}
	std::vector<Cond> conds = a->when;
	for (const auto &r : a->requires_) {
		conds.insert(conds.end(), r.when.begin(), r.when.end());
	}
	const Outcome *outcome = nullptr;
	for (const auto &o : a->outcomes) {
		if (eval_all(o.when)) {
			outcome = &o;
			conds.insert(conds.end(), o.when.begin(), o.when.end());
			break;
		}
	}
	int ev = new_event("action", a->label, parents_for(conds));
	performed_[a->id] = day() + 1;
	say(a->text);
	apply(a->effects, ev);
	if (outcome) {
		say(outcome->text);
		apply(outcome->effects, ev);
	}
	if (finished_) {
		return true;
	}
	if (a->sleep) {
		end_day(true);
	} else {
		advance(a->cost);
	}
	evaluate_rules();
	enforce_location();
	check_finished();
	return true;
}

// =================================================================== inventory

bool Sim::can_use_stash() const {
	const LocationDef *l = location_def(loc_);
	return l && l->stash && !finished_;
}

bool Sim::move_to_stash(int slot) {
	if (!can_use_stash() || slot < 0 || slot >= kInventorySlots || inv_[size_t(slot)].empty()) {
		return false;
	}
	std::string id = inv_[size_t(slot)];
	inv_[size_t(slot)].clear();
	stash_.push_back(id);
	int ev = new_event("action", "Odkładasz: " + (item(id) ? item(id)->name : id), {});
	touch_fact("item:" + id, "-" + id, ev);
	evaluate_rules();
	return true;
}

bool Sim::take_from_stash(int index) {
	if (!can_use_stash() || index < 0 || index >= int(stash_.size())) {
		return false;
	}
	auto free = std::find(inv_.begin(), inv_.end(), std::string());
	if (free == inv_.end()) {
		out_.push_back({ "system", "", "Masz tylko sześć kieszeni. Najpierw coś odłóż." });
		return false;
	}
	std::string id = stash_[size_t(index)];
	stash_.erase(stash_.begin() + index);
	*free = id;
	int ev = new_event("action", "Bierzesz: " + (item(id) ? item(id)->name : id), {});
	touch_fact("item:" + id, "+" + id, ev);
	if (id == "phone") {
		deliver_unread();
	}
	evaluate_rules();
	return true;
}

bool Sim::swap_slots(int a, int b) {
	if (a < 0 || b < 0 || a >= kInventorySlots || b >= kInventorySlots) {
		return false;
	}
	std::swap(inv_[size_t(a)], inv_[size_t(b)]);
	return true;
}

// =================================================================== npcs

const ScheduleEntry *Sim::npc_entry(const NpcDef &n) const {
	int tod = tick_of_day();
	for (const auto &e : n.schedule) {
		if ((e.days >> day()) & 1u && tod >= e.from && tod < e.to && eval_all(e.when)) {
			return &e;
		}
	}
	return nullptr;
}

std::string Sim::npc_location(const std::string &id) const {
	const NpcDef *n = npc(id);
	const ScheduleEntry *e = n ? npc_entry(*n) : nullptr;
	return e ? e->loc : "";
}

std::string Sim::npc_name(const std::string &id) const {
	const NpcDef *n = npc(id);
	if (!n) {
		return id;
	}
	return n->known_fact.empty() || fact(n->known_fact) ? n->name : n->unknown_name;
}

std::vector<NpcView> Sim::npcs() const {
	std::vector<NpcView> out;
	for (const auto &n : npcs_) {
		const ScheduleEntry *e = npc_entry(n);
		if (!e) {
			continue;
		}
		NpcView v;
		v.id = n.id;
		v.name = npc_name(n.id);
		v.sprite = n.sprite;
		v.loc = e->loc;
		v.activity = e->activity;
		v.x = e->x;
		v.has_x = e->has_x;
		out.push_back(std::move(v));
	}
	return out;
}

// =================================================================== output

std::vector<OutMessage> Sim::drain_messages() {
	std::vector<OutMessage> out;
	out.swap(out_);
	return out;
}

bool Sim::consume_location_forced() {
	bool f = location_forced_;
	location_forced_ = false;
	return f;
}

std::string Sim::ending_id() const {
	if (!forced_ending_.empty()) {
		return forced_ending_;
	}
	for (const auto &e : endings_) {
		if (eval_all(e.when)) {
			return e.id;
		}
	}
	return endings_.empty() ? "" : endings_.back().id;
}

EndingView Sim::ending() const {
	EndingView v;
	v.id = ending_id();
	std::vector<Cond> roots;
	for (const auto &e : endings_) {
		if (e.id == v.id) {
			v.title = e.title;
			v.text = pick(e.text);
			roots = e.when;
		}
	}
	for (const auto &ep : epilogue_) {
		if (eval_all(ep.when)) {
			v.epilogue.push_back(pick(ep.text));
			roots.insert(roots.end(), ep.when.begin(), ep.when.end());
		}
	}
	std::set<int> seen;
	std::function<void(int, int)> walk = [&](int id, int depth) {
		if (id < 0 || id >= int(events_.size()) || depth > 7) {
			return;
		}
		const CauseEvent &e = events_[size_t(id)];
		std::string when = std::string(day_short(e.t / kTicksPerDay)) + " " + format_clock(e.t % kTicksPerDay);
		std::string line = std::string(size_t(depth) * 2, ' ') + (depth ? "↳ " : "• ") + when + "  " + e.label;
		if (seen.count(id)) {
			v.why.push_back(line + " (…)");
			return;
		}
		seen.insert(id);
		v.why.push_back(line);
		for (int p : e.parents) {
			if (events_[size_t(p)].source != "start" && events_[size_t(p)].source != "hidden") {
				walk(p, depth + 1);
			}
		}
	};
	for (int id : parents_for(roots)) {
		if (events_[size_t(id)].source != "start") {
			walk(id, 0);
		}
	}
	return v;
}

// =================================================================== persistence

Json Sim::save() const {
	Json j = Json::object();
	j.set("version", 1);
	j.set("seed", (double)seed_);
	j.set("t", t_);
	j.set("loc", loc_);
	j.set("seg", seg_);
	j.set("x", x_);
	j.set("walk_acc", walk_acc_);
	j.set("energy_acc", energy_acc_);
	j.set("finished", finished_);
	j.set("forced_ending", forced_ending_);
	j.set("pending_seq", pending_seq_);
	auto map_json = [](const std::map<std::string, int> &m) {
		Json o = Json::object();
		for (const auto &kv : m) {
			o.set(kv.first, kv.second);
		}
		return o;
	};
	j.set("stats", map_json(stats_));
	j.set("facts", map_json(facts_));
	j.set("fired", map_json(fired_));
	j.set("performed", map_json(performed_));
	j.set("fact_event", map_json(fact_event_));
	Json inv = Json::array();
	for (const auto &s : inv_) {
		inv.push_back(s);
	}
	j.set("inventory", inv);
	Json stash = Json::array();
	for (const auto &s : stash_) {
		stash.push_back(s);
	}
	j.set("stash", stash);
	Json pend = Json::array();
	for (const auto &p : pending_) {
		Json pj = Json::object();
		pj.set("due", p.due);
		pj.set("rule", p.rule);
		pj.set("seq", p.seq);
		Json par = Json::array();
		for (int id : p.parents) {
			par.push_back(id);
		}
		pj.set("parents", par);
		pend.push_back(pj);
	}
	j.set("pending", pend);
	Json unread = Json::array();
	for (const auto &u : unread_) {
		Json uj = Json::object();
		uj.set("id", u.id);
		uj.set("from", u.from);
		uj.set("text", u.text);
		uj.set("sent", u.sent);
		unread.push_back(uj);
	}
	j.set("unread", unread);
	Json phone = Json::array();
	for (const auto &p : phone_) {
		Json pj = Json::object();
		pj.set("id", p.id);
		pj.set("from", p.from);
		pj.set("text", p.text);
		pj.set("sent", p.sent);
		pj.set("read", p.read);
		phone.push_back(pj);
	}
	j.set("phone", phone);
	Json evs = Json::array();
	for (const auto &e : events_) {
		Json ej = Json::object();
		ej.set("t", e.t);
		ej.set("source", e.source);
		ej.set("label", e.label);
		ej.set("notify", e.notify);
		Json ch = Json::array();
		for (const auto &c : e.changes) {
			ch.push_back(c);
		}
		ej.set("changes", ch);
		Json par = Json::array();
		for (int p : e.parents) {
			par.push_back(p);
		}
		ej.set("parents", par);
		evs.push_back(ej);
	}
	j.set("events", evs);
	return j;
}

bool Sim::restore(const Json &j) {
	if (j.get_int("version") != 1) {
		return false;
	}
	seed_ = uint64_t(j.get_number("seed"));
	t_ = j.get_int("t");
	loc_ = j.get_string("loc");
	seg_ = j.get_string("seg");
	x_ = j.get_number("x");
	walk_acc_ = j.get_number("walk_acc");
	energy_acc_ = j.get_int("energy_acc");
	finished_ = j.get_bool("finished");
	forced_ending_ = j.get_string("forced_ending");
	pending_seq_ = j.get_int("pending_seq");
	auto read_map = [](const Json &o) {
		std::map<std::string, int> m;
		for (const auto &kv : o.members()) {
			m[kv.first] = kv.second.as_int();
		}
		return m;
	};
	stats_ = read_map(j["stats"]);
	facts_ = read_map(j["facts"]);
	fired_ = read_map(j["fired"]);
	performed_ = read_map(j["performed"]);
	fact_event_ = read_map(j["fact_event"]);
	inv_.fill("");
	for (size_t i = 0; i < kInventorySlots; ++i) {
		inv_[i] = j["inventory"][i].as_string("");
	}
	stash_ = j.get_strings("stash");
	pending_.clear();
	for (const Json &p : j["pending"].items()) {
		PendingEvent pe;
		pe.due = p.get_int("due");
		pe.rule = p.get_string("rule");
		pe.seq = p.get_int("seq");
		for (const Json &id : p["parents"].items()) {
			pe.parents.push_back(id.as_int());
		}
		pending_.push_back(pe);
	}
	unread_.clear();
	for (const Json &u : j["unread"].items()) {
		unread_.push_back({ u.get_string("id"), u.get_string("from"), u.get_string("text"), u.get_int("sent") });
	}
	phone_.clear();
	for (const Json &p : j["phone"].items()) {
		phone_.push_back({ p.get_string("id"), p.get_string("from"), p.get_string("text"), p.get_int("sent"), p.get_int("read") });
	}
	events_.clear();
	for (const Json &e : j["events"].items()) {
		CauseEvent ce;
		ce.id = int(events_.size());
		ce.t = e.get_int("t");
		ce.source = e.get_string("source");
		ce.label = e.get_string("label");
		ce.notify = e.get_bool("notify");
		ce.changes = e.get_strings("changes");
		for (const Json &p : e["parents"].items()) {
			ce.parents.push_back(p.as_int());
		}
		events_.push_back(ce);
	}
	out_.clear();
	location_forced_ = true;
	return true;
}

} // namespace od
