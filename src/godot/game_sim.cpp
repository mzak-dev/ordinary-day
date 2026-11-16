#include "game_sim.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

namespace godot {

String to_godot(const std::string &s) {
	return String::utf8(s.c_str(), int64_t(s.size()));
}

std::string to_std(const String &s) {
	CharString cs = s.utf8();
	return std::string(cs.get_data(), size_t(cs.length()));
}

Variant json_to_variant(const od::Json &j) {
	switch (j.type()) {
		case od::Json::Type::Null: return Variant();
		case od::Json::Type::Bool: return j.as_bool();
		case od::Json::Type::Number: return j.as_number();
		case od::Json::Type::String: return to_godot(j.as_string());
		case od::Json::Type::Array: {
			Array a;
			for (const od::Json &e : j.items()) {
				a.push_back(json_to_variant(e));
			}
			return a;
		}
		case od::Json::Type::Object: {
			Dictionary d;
			for (const auto &kv : j.members()) {
				d[to_godot(kv.first)] = json_to_variant(kv.second);
			}
			return d;
		}
	}
	return Variant();
}

void GameSim::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_content_dir", "dir"), &GameSim::set_content_dir);
	ClassDB::bind_method(D_METHOD("get_content_dir"), &GameSim::get_content_dir);
	ClassDB::bind_method(D_METHOD("set_autostart", "v"), &GameSim::set_autostart);
	ClassDB::bind_method(D_METHOD("get_autostart"), &GameSim::get_autostart);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "content_dir", PROPERTY_HINT_DIR), "set_content_dir", "get_content_dir");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autostart"), "set_autostart", "get_autostart");

	ClassDB::bind_method(D_METHOD("load_content"), &GameSim::load_content);
	ClassDB::bind_method(D_METHOD("get_content_errors"), &GameSim::get_content_errors);
	ClassDB::bind_method(D_METHOD("new_game", "seed"), &GameSim::new_game, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_clock"), &GameSim::get_clock);
	ClassDB::bind_method(D_METHOD("get_stats"), &GameSim::get_stats);
	ClassDB::bind_method(D_METHOD("get_warmth"), &GameSim::get_warmth);
	ClassDB::bind_method(D_METHOD("get_weather"), &GameSim::get_weather);
	ClassDB::bind_method(D_METHOD("get_fact", "name"), &GameSim::get_fact);
	ClassDB::bind_method(D_METHOD("get_location"), &GameSim::get_location);
	ClassDB::bind_method(D_METHOD("get_location_name", "id"), &GameSim::get_location_name);
	ClassDB::bind_method(D_METHOD("is_location_open", "id"), &GameSim::is_location_open);
	ClassDB::bind_method(D_METHOD("get_player_seg"), &GameSim::get_player_seg);
	ClassDB::bind_method(D_METHOD("get_player_x"), &GameSim::get_player_x);
	ClassDB::bind_method(D_METHOD("set_player_pos", "seg", "x"), &GameSim::set_player_pos);
	ClassDB::bind_method(D_METHOD("link_info", "id", "from_a"), &GameSim::link_info);
	ClassDB::bind_method(D_METHOD("use_link", "id", "from_a"), &GameSim::use_link);
	ClassDB::bind_method(D_METHOD("get_actions"), &GameSim::get_actions);
	ClassDB::bind_method(D_METHOD("perform", "id"), &GameSim::perform);
	ClassDB::bind_method(D_METHOD("get_inventory"), &GameSim::get_inventory);
	ClassDB::bind_method(D_METHOD("get_stash"), &GameSim::get_stash);
	ClassDB::bind_method(D_METHOD("can_use_stash"), &GameSim::can_use_stash);
	ClassDB::bind_method(D_METHOD("move_to_stash", "slot"), &GameSim::move_to_stash);
	ClassDB::bind_method(D_METHOD("take_from_stash", "index"), &GameSim::take_from_stash);
	ClassDB::bind_method(D_METHOD("swap_slots", "a", "b"), &GameSim::swap_slots);
	ClassDB::bind_method(D_METHOD("get_item", "id"), &GameSim::get_item);
	ClassDB::bind_method(D_METHOD("get_npcs"), &GameSim::get_npcs);
	ClassDB::bind_method(D_METHOD("get_phone_log"), &GameSim::get_phone_log);
	ClassDB::bind_method(D_METHOD("get_unread_count"), &GameSim::get_unread_count);
	ClassDB::bind_method(D_METHOD("get_recent_messages"), &GameSim::get_recent_messages);
	ClassDB::bind_method(D_METHOD("is_finished"), &GameSim::is_finished);
	ClassDB::bind_method(D_METHOD("get_ending"), &GameSim::get_ending);
	ClassDB::bind_method(D_METHOD("get_world"), &GameSim::get_world);
	ClassDB::bind_method(D_METHOD("save_game", "path"), &GameSim::save_game);
	ClassDB::bind_method(D_METHOD("load_game", "path"), &GameSim::load_game);

	ADD_SIGNAL(MethodInfo("state_changed"));
	ADD_SIGNAL(MethodInfo("message", PropertyInfo(Variant::STRING, "kind"), PropertyInfo(Variant::STRING, "from"), PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("location_forced", PropertyInfo(Variant::STRING, "seg"), PropertyInfo(Variant::FLOAT, "x")));
	ADD_SIGNAL(MethodInfo("day_started", PropertyInfo(Variant::INT, "day"), PropertyInfo(Variant::STRING, "name")));
	ADD_SIGNAL(MethodInfo("game_finished"));
	// [{kind: "stat"|"rel"|"item_gain"|"item_loss"|"time"|"event", key, label, delta}]
	ADD_SIGNAL(MethodInfo("changes", PropertyInfo(Variant::ARRAY, "list")));
}

void GameSim::ensure_input_actions() {
	InputMap *im = InputMap::get_singleton();
	struct Binding {
		const char *action;
		Key keys[2];
	};
	const Binding bindings[] = {
		{ "move_left", { KEY_A, KEY_LEFT } },
		{ "move_right", { KEY_D, KEY_RIGHT } },
		{ "move_up", { KEY_W, KEY_UP } },
		{ "move_down", { KEY_S, KEY_DOWN } },
		{ "sprint", { KEY_SHIFT, KEY_NONE } },
		{ "act", { KEY_E, KEY_ENTER } },
		{ "phone", { KEY_P, KEY_NONE } },
		{ "stash", { KEY_Q, KEY_TAB } },
		{ "quick_save", { KEY_F5, KEY_NONE } },
		{ "quick_load", { KEY_F9, KEY_NONE } },
		{ "help", { KEY_F1, KEY_H } },
	};
	for (const Binding &b : bindings) {
		if (im->has_action(b.action)) {
			continue;
		}
		im->add_action(b.action);
		for (Key k : b.keys) {
			if (k == KEY_NONE) {
				continue;
			}
			Ref<InputEventKey> ev;
			ev.instantiate();
			ev->set_physical_keycode(k);
			im->action_add_event(b.action, ev);
		}
	}
}

void GameSim::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	ensure_input_actions();
	load_content();
	if (autostart_ && loaded_) {
		call_deferred("new_game", 0);
	}
}

bool GameSim::load_content() {
	std::vector<std::pair<std::string, std::string>> files;
	PackedStringArray names = DirAccess::get_files_at(content_dir_);
	std::vector<String> sorted;
	for (int64_t i = 0; i < names.size(); ++i) {
		if (names[i].ends_with(".json")) {
			sorted.push_back(names[i]);
		}
	}
	std::sort(sorted.begin(), sorted.end(), [](const String &a, const String &b) { return a < b; });
	for (const String &name : sorted) {
		String text = FileAccess::get_file_as_string(content_dir_.path_join(name));
		files.emplace_back(to_std(name), to_std(text));
	}
	loaded_ = sim_.load(files);
	for (const auto &e : sim_.errors()) {
		UtilityFunctions::push_error(String("[content] ") + to_godot(e));
	}
	world_ = json_to_variant(sim_.world());
	return loaded_;
}

PackedStringArray GameSim::get_content_errors() const {
	PackedStringArray out;
	for (const auto &e : sim_.errors()) {
		out.push_back(to_godot(e));
	}
	return out;
}

void GameSim::new_game(int64_t seed) {
	if (seed == 0) {
		seed = int64_t(Time::get_singleton()->get_ticks_usec() ^ 0x5EED);
	}
	sim_.new_game(uint64_t(seed));
	history_.clear();
	finished_emitted_ = false;
	last_t_ = -1;
	snap_valid_ = false;
	flush(true);
}

void GameSim::take_snapshot() {
	snap_stats_ = sim_.stats();
	snap_inv_.assign(sim_.inventory().begin(), sim_.inventory().end());
	snap_events_ = sim_.events().size();
	snap_t_ = sim_.now();
	snap_valid_ = true;
}

Array GameSim::diff_snapshot(Cmd cmd) {
	Array out;
	if (!snap_valid_) {
		return out;
	}
	auto push = [&](const String &kind, const String &key, const String &label, int delta) {
		Dictionary d;
		d["kind"] = kind;
		d["key"] = key;
		d["label"] = label;
		d["delta"] = delta;
		out.push_back(d);
	};
	const bool loud = cmd == Cmd::Action || cmd == Cmd::Link;
	if (cmd == Cmd::Action && sim_.now() > snap_t_ && sim_.now() - snap_t_ < od::kTicksPerDay / 2) {
		push("time", "time", "", sim_.now() - snap_t_);
	}
	std::map<std::string, int> keys = snap_stats_;
	for (const auto &kv : sim_.stats()) {
		keys[kv.first] = 0;
	}
	for (const auto &kv : keys) {
		const std::string &k = kv.first;
		if (k == "warmth") {
			continue; // the hidden stat stays hidden
		}
		int before = snap_stats_.count(k) ? snap_stats_.at(k) : 0;
		int delta = sim_.stat(k) - before;
		if (delta == 0) {
			continue;
		}
		if (k.rfind("rel.", 0) == 0) {
			std::string npc = k.substr(4);
			push("rel", to_godot(npc), to_godot(sim_.npc_name(npc)), delta);
		} else if (loud || k == "money" || std::abs(delta) >= 4) {
			push("stat", to_godot(k), "", delta);
		}
	}
	if (cmd != Cmd::Inventory) {
		std::vector<std::string> now(sim_.inventory().begin(), sim_.inventory().end());
		std::vector<std::string> before = snap_inv_;
		for (const auto &id : now) {
			auto it = std::find(before.begin(), before.end(), id);
			if (it != before.end()) {
				before.erase(it);
			} else if (!id.empty()) {
				const od::ItemDef *d = sim_.item(id);
				push("item_gain", to_godot(id), to_godot(d ? d->name : id), 1);
			}
		}
		for (const auto &id : before) {
			if (!id.empty()) {
				const od::ItemDef *d = sim_.item(id);
				push("item_loss", to_godot(id), to_godot(d ? d->name : id), -1);
			}
		}
	}
	const auto &events = sim_.events();
	for (size_t i = snap_events_; i < events.size(); ++i) {
		if (events[i].notify && !events[i].label.empty()) {
			push("event", "", to_godot(events[i].label), 0);
		}
	}
	return out;
}

void GameSim::flush(bool force_state, Cmd cmd) {
	Array changes = diff_snapshot(cmd);
	take_snapshot();
	if (!changes.is_empty()) {
		emit_signal("changes", changes);
	}
	for (const auto &m : sim_.drain_messages()) {
		Dictionary d;
		d["kind"] = to_godot(m.kind);
		d["from"] = to_godot(m.from);
		d["text"] = to_godot(m.text);
		history_.push_back(d);
		if (history_.size() > 60) {
			history_.pop_front();
		}
		emit_signal("message", d["kind"], d["from"], d["text"]);
	}
	if (sim_.consume_location_forced()) {
		emit_signal("location_forced", to_godot(sim_.player_seg()), sim_.player_x());
	}
	bool changed = force_state || sim_.now() != last_t_ || sim_.location() != last_loc_;
	int prev_day = last_t_ < 0 ? -1 : last_t_ / od::kTicksPerDay;
	last_t_ = sim_.now();
	last_loc_ = sim_.location();
	if (changed) {
		emit_signal("state_changed");
	}
	if (sim_.day() != prev_day && sim_.day() < od::kDays) {
		emit_signal("day_started", sim_.day(), String::utf8(od::day_name(sim_.day())));
	}
	if (sim_.finished() && !finished_emitted_) {
		finished_emitted_ = true;
		emit_signal("game_finished");
	}
}

Dictionary GameSim::get_clock() const {
	Dictionary d;
	d["t"] = sim_.now();
	d["day"] = sim_.day();
	d["day_name"] = String::utf8(od::day_name(sim_.day()));
	d["tick"] = sim_.tick_of_day();
	d["time"] = to_godot(od::format_clock(sim_.tick_of_day()));
	d["hour"] = double(sim_.tick_of_day()) / od::kTicksPerHour;
	d["finished"] = sim_.finished();
	return d;
}

Dictionary GameSim::get_stats() const {
	Dictionary d;
	for (const auto &kv : sim_.stats()) {
		d[to_godot(kv.first)] = kv.second;
	}
	return d;
}

void GameSim::set_player_pos(const String &seg, double x) {
	sim_.set_player_pos(to_std(seg), x);
	flush(false, Cmd::Passive);
}

Dictionary GameSim::link_info(const String &id, bool from_a) const {
	Dictionary d;
	const od::LinkDef *l = sim_.link(to_std(id));
	if (!l) {
		return d;
	}
	std::string why;
	d["ok"] = sim_.can_use_link(l->id, from_a, &why);
	d["why"] = to_godot(why);
	const od::LinkEnd &from = from_a ? l->a : l->b;
	d["label"] = to_godot(from.label);
	d["key"] = to_godot(from.key);
	d["cost"] = l->cost;
	return d;
}

bool GameSim::use_link(const String &id, bool from_a) {
	bool ok = sim_.use_link(to_std(id), from_a);
	flush(false, Cmd::Link);
	return ok;
}

Array GameSim::get_actions() const {
	Array out;
	for (const auto &a : sim_.available_actions()) {
		Dictionary d;
		d["id"] = to_godot(a.id);
		d["label"] = to_godot(a.label);
		d["group"] = to_godot(a.group);
		d["cost"] = a.cost;
		d["enabled"] = a.enabled;
		d["why"] = to_godot(a.why);
		PackedStringArray npcs;
		for (const auto &n : a.npcs) {
			npcs.push_back(to_godot(n));
		}
		d["npcs"] = npcs;
		out.push_back(d);
	}
	return out;
}

bool GameSim::perform(const String &id) {
	bool ok = sim_.perform(to_std(id));
	flush(false, Cmd::Action);
	return ok;
}

Dictionary GameSim::get_item(const String &id) const {
	Dictionary d;
	const od::ItemDef *it = sim_.item(to_std(id));
	if (!it) {
		return d;
	}
	d["id"] = to_godot(it->id);
	d["name"] = to_godot(it->name);
	d["desc"] = to_godot(it->desc);
	d["color"] = to_godot(it->color);
	return d;
}

Array GameSim::get_inventory() const {
	Array out;
	for (const auto &id : sim_.inventory()) {
		out.push_back(get_item(to_godot(id)));
	}
	return out;
}

Array GameSim::get_stash() const {
	Array out;
	for (const auto &id : sim_.stash()) {
		out.push_back(get_item(to_godot(id)));
	}
	return out;
}

bool GameSim::move_to_stash(int slot) {
	bool ok = sim_.move_to_stash(slot);
	flush(true, Cmd::Inventory);
	return ok;
}

bool GameSim::take_from_stash(int index) {
	bool ok = sim_.take_from_stash(index);
	flush(true, Cmd::Inventory);
	return ok;
}

bool GameSim::swap_slots(int a, int b) {
	bool ok = sim_.swap_slots(a, b);
	flush(true, Cmd::Inventory);
	return ok;
}

Array GameSim::get_npcs() const {
	Array out;
	for (const auto &n : sim_.npcs()) {
		Dictionary d;
		d["id"] = to_godot(n.id);
		d["name"] = to_godot(n.name);
		d["sprite"] = to_godot(n.sprite);
		d["loc"] = to_godot(n.loc);
		d["activity"] = to_godot(n.activity);
		d["x"] = n.x;
		d["has_x"] = n.has_x;
		out.push_back(d);
	}
	return out;
}

Array GameSim::get_phone_log() const {
	Array out;
	for (const auto &p : sim_.phone_log()) {
		Dictionary d;
		d["from"] = to_godot(p.from);
		d["text"] = to_godot(p.text);
		d["sent"] = String::utf8(od::day_short(p.sent / od::kTicksPerDay)) + " " + to_godot(od::format_clock(p.sent % od::kTicksPerDay));
		out.push_back(d);
	}
	return out;
}

Dictionary GameSim::get_ending() const {
	od::EndingView e = sim_.ending();
	Dictionary d;
	d["id"] = to_godot(e.id);
	d["title"] = to_godot(e.title);
	d["text"] = to_godot(e.text);
	PackedStringArray epi, why;
	for (const auto &l : e.epilogue) {
		epi.push_back(to_godot(l));
	}
	for (const auto &l : e.why) {
		why.push_back(to_godot(l));
	}
	d["epilogue"] = epi;
	d["why"] = why;
	return d;
}

bool GameSim::save_game(const String &path) const {
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}
	f->store_string(to_godot(sim_.save().dump(1)));
	return true;
}

bool GameSim::load_game(const String &path) {
	if (!FileAccess::file_exists(path)) {
		return false;
	}
	std::string err;
	od::Json j = od::Json::parse(to_std(FileAccess::get_file_as_string(path)), &err);
	if (!sim_.restore(j)) {
		return false;
	}
	history_.clear();
	finished_emitted_ = false;
	last_t_ = -1;
	snap_valid_ = false;
	flush(true);
	return true;
}

} // namespace godot
