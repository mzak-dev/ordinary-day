#pragma once

#include "core/sim.hpp"

#include <map>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

String to_godot(const std::string &s);
std::string to_std(const String &s);
Variant json_to_variant(const od::Json &j);

// Owns the simulation and turns its output into signals. Every other game
// node talks to the week through this one.
class GameSim : public Node {
	GDCLASS(GameSim, Node)

public:
	void _ready() override;

	void set_content_dir(const String &dir) { content_dir_ = dir; }
	String get_content_dir() const { return content_dir_; }
	void set_autostart(bool v) { autostart_ = v; }
	bool get_autostart() const { return autostart_; }

	bool load_content();
	PackedStringArray get_content_errors() const;
	void new_game(int64_t seed);

	Dictionary get_clock() const;
	Dictionary get_stats() const;
	int get_warmth() const { return sim_.warmth(); }
	String get_weather() const { return to_godot(sim_.weather()); }
	int get_fact(const String &name) const { return sim_.fact(to_std(name)); }

	String get_location() const { return to_godot(sim_.location()); }
	String get_location_name(const String &id) const { return to_godot(sim_.location_name(to_std(id))); }
	bool is_location_open(const String &id) const { return sim_.location_open(to_std(id)); }
	String get_player_seg() const { return to_godot(sim_.player_seg()); }
	double get_player_x() const { return sim_.player_x(); }
	void set_player_pos(const String &seg, double x);

	Dictionary link_info(const String &id, bool from_a) const;
	bool use_link(const String &id, bool from_a);

	Array get_actions() const;
	bool perform(const String &id);

	Array get_inventory() const;
	Array get_stash() const;
	bool can_use_stash() const { return sim_.can_use_stash(); }
	bool move_to_stash(int slot);
	bool take_from_stash(int index);
	bool swap_slots(int a, int b);
	Dictionary get_item(const String &id) const;

	Array get_npcs() const;
	Array get_phone_log() const;
	int get_unread_count() const { return sim_.unread_count(); }
	Array get_recent_messages() const { return history_; }

	bool is_finished() const { return sim_.finished(); }
	Dictionary get_ending() const;

	Dictionary get_world() const { return world_; }

	bool save_game(const String &path) const;
	bool load_game(const String &path);

protected:
	static void _bind_methods();

private:
	od::Sim sim_;
	String content_dir_ = "res://content";
	bool autostart_ = true;
	bool loaded_ = false;
	bool finished_emitted_ = false;
	Dictionary world_;
	Array history_;
	int last_t_ = -1;
	std::string last_loc_;

	// Snapshot used to tell the player what an action changed.
	std::map<std::string, int> snap_stats_;
	std::vector<std::string> snap_inv_;
	size_t snap_events_ = 0;
	int snap_t_ = 0;
	bool snap_valid_ = false;

	enum class Cmd { Passive, Action, Link, Inventory };
	void flush(bool force_state = false, Cmd cmd = Cmd::Passive);
	void take_snapshot();
	Array diff_snapshot(Cmd cmd);
	void ensure_input_actions();
};

} // namespace godot
