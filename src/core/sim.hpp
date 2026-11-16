#pragma once

#include "json.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace od {

// Time is counted in quarters of an hour since Monday 00:00.
constexpr int kTicksPerHour = 4;
constexpr int kTicksPerDay = 24 * kTicksPerHour;
constexpr int kDayStart = 7 * kTicksPerHour;
constexpr int kDays = 7;
constexpr int kInventorySlots = 6;

std::string format_clock(int tick_of_day);
int parse_clock(const std::string &hhmm, int fallback = -1);
const char *day_name(int day);
const char *day_short(int day);

// ---------------------------------------------------------------- conditions

struct Cond {
	enum class Kind { Fact, Stat, Item, Stash, At, NpcAt, Chance, Day, Fired, Weather, Any, True };
	enum class Op { Truthy, Eq, Ne, Lt, Le, Gt, Ge };

	Kind kind = Kind::True;
	Op op = Op::Truthy;
	bool negate = false;
	std::string a; // fact / stat / item / npc / rule / weather
	std::vector<std::string> list; // locations or days
	double value = 0.0;
	std::vector<Cond> any;
};

struct Effect {
	enum class Kind { SetFact, AddFact, ClearFact, Stat, Give, Take, Stash, Msg, Phone, Time, Ending, Goto };
	Kind kind = Kind::Msg;
	std::string a, b, c;
	char op = '+'; // '+', '-', '='
	int value = 0;
};

struct ToneText {
	std::string cold, warm;
	bool empty() const { return cold.empty() && warm.empty(); }
};

struct Requirement {
	std::vector<Cond> when;
	std::string why;
};

struct Outcome {
	std::vector<Cond> when;
	std::vector<Effect> effects;
	ToneText text;
};

// ---------------------------------------------------------------- content

struct ItemDef {
	std::string id, name, desc, color;
};

struct ScheduleEntry {
	unsigned days = 0x7F; // bitmask Mon..Sun
	int from = 0, to = kTicksPerDay;
	std::string loc;
	double x = 0.0;
	bool has_x = false;
	std::string activity;
	std::vector<Cond> when;
};

struct NpcDef {
	std::string id, name, unknown_name, sprite;
	std::string known_fact; // until set, the NPC is shown with unknown_name
	std::vector<ScheduleEntry> schedule;
};

struct LocationDef {
	std::string id, name;
	int battery = 0; // battery change per tick spent here
	bool stash = false;
	bool outdoor = false;
	int open_from = 0, open_to = kTicksPerDay;
	std::string exit_to; // forced location when the place closes
	std::string spawn_seg;
	double spawn_x = 0.0;
};

struct SegmentDef {
	std::string id, loc;
	double x0 = 0, x1 = 0, y = 0, z = 0;
	bool outdoor = false;
};

struct AreaDef {
	std::string seg, loc;
	double x0 = 0, x1 = 0;
};

struct LinkEnd {
	std::string seg;
	double x = 0;
	std::string key; // input direction: "up", "down", "enter"
	std::string label;
};

struct LinkDef {
	std::string id;
	LinkEnd a, b;
	int cost = 0;
	bool oneway = false;
	std::string requires_from = "both"; // "a", "b" or "both": direction the requirements guard
	std::vector<Requirement> requires_;
	std::vector<Effect> effects;
	ToneText text;
};

struct ActionDef {
	std::string id, label, group;
	std::vector<std::string> locs; // empty = anywhere
	std::vector<Cond> when;
	std::vector<Requirement> requires_;
	int cost = 1;
	std::vector<Effect> effects;
	std::vector<Outcome> outcomes;
	ToneText text;
	std::string limit; // "", "once", "daily"
	bool sleep = false;
};

struct RuleDef {
	std::string id, label;
	std::vector<Cond> when;
	int delay_days = 0, delay_ticks = 0, delay_at = -1;
	bool delayed = false;
	std::vector<Cond> check;
	std::vector<Effect> effects, else_effects;
	ToneText text, else_text;
	std::string repeat; // "", "daily", "always"
	bool hidden = false; // bookkeeping rules: no popup, no node in the cause graph
};

struct EndingDef {
	std::string id, title;
	std::vector<Cond> when;
	ToneText text;
};

struct EpilogueDef {
	std::vector<Cond> when;
	ToneText text;
};

struct WeatherDef {
	unsigned days = 0;
	int from = 0, to = 0;
	std::string type;
};

// ---------------------------------------------------------------- runtime

struct OutMessage {
	std::string kind; // "narr", "phone", "system", "dialog"
	std::string from;
	std::string text;
};

struct CauseEvent {
	int id = 0;
	int t = 0;
	std::string source; // "action", "rule", "link", "sleep", "phone", "start"
	std::string label;
	std::vector<std::string> changes;
	std::vector<int> parents;
	bool notify = false; // worth showing to the player as a consequence
};

struct PendingEvent {
	int due = 0;
	std::string rule;
	std::vector<int> parents;
	int seq = 0;
};

struct UnreadPhone {
	std::string id, from, text;
	int sent = 0;
};

struct PhoneEntry {
	std::string id, from, text;
	int sent = 0, read = 0;
};

struct ActionView {
	std::string id, label, group;
	int cost = 0;
	bool enabled = true;
	std::string why;
	std::vector<std::string> npcs; // NPCs this action is about (npc:x@here)
};

struct NpcView {
	std::string id, name, sprite, loc, activity;
	double x = 0;
	bool has_x = false;
};

struct EndingView {
	std::string id, title, text;
	std::vector<std::string> epilogue;
	std::vector<std::string> why; // indented cause tree lines
};

class Sim {
public:
	// Each file is an object whose keys are merged: items, npcs, locations,
	// segments, areas, links, actions, rules, endings, epilogue, weather, start.
	bool load(const std::vector<std::pair<std::string, std::string>> &files);
	const std::vector<std::string> &errors() const { return errors_; }
	const Json &world() const { return world_; }

	void new_game(uint64_t seed);

	// --- time
	int now() const { return t_; }
	int day() const { return t_ / kTicksPerDay; }
	int tick_of_day() const { return t_ % kTicksPerDay; }
	bool finished() const { return finished_; }

	// --- player
	const std::string &location() const { return loc_; }
	const LocationDef *location_def(const std::string &id) const;
	std::string location_name(const std::string &id) const;
	const std::string &player_seg() const { return seg_; }
	double player_x() const { return x_; }
	// Moves the player within a segment. Walking outdoors slowly costs time.
	void set_player_pos(const std::string &seg, double x);
	std::string location_at(const std::string &seg, double x) const;
	bool location_open(const std::string &id) const;

	// --- links (doors, stairs, bus)
	const std::vector<LinkDef> &links() const { return links_; }
	const LinkDef *link(const std::string &id) const;
	bool can_use_link(const std::string &id, bool from_a, std::string *why) const;
	bool use_link(const std::string &id, bool from_a);

	// --- actions
	std::vector<ActionView> available_actions() const;
	bool perform(const std::string &action_id);

	// --- stats & facts
	int stat(const std::string &name) const;
	const std::map<std::string, int> &stats() const { return stats_; }
	int fact(const std::string &name) const;
	const std::map<std::string, int> &facts() const { return facts_; }
	int warmth() const { return stat("warmth"); }
	std::string weather() const;

	// --- inventory
	const std::array<std::string, kInventorySlots> &inventory() const { return inv_; }
	const std::vector<std::string> &stash() const { return stash_; }
	const ItemDef *item(const std::string &id) const;
	bool has_item(const std::string &id) const;
	bool can_use_stash() const;
	bool move_to_stash(int slot);
	bool take_from_stash(int index);
	bool swap_slots(int a, int b);

	// --- npcs
	std::vector<NpcView> npcs() const; // all NPCs currently on stage
	std::string npc_location(const std::string &npc) const;
	std::string npc_name(const std::string &npc) const;

	// --- phone
	const std::vector<PhoneEntry> &phone_log() const { return phone_; }
	int unread_count() const { return int(unread_.size()); }

	// --- output
	std::vector<OutMessage> drain_messages();
	bool consume_location_forced();
	const std::vector<CauseEvent> &events() const { return events_; }
	EndingView ending() const;

	// --- persistence
	Json save() const;
	bool restore(const Json &state);

	// --- analysis (used by the fuzzer)
	const std::vector<RuleDef> &rules() const { return rules_; }
	const std::vector<ActionDef> &actions() const { return actions_; }
	const std::map<std::string, int> &fired() const { return fired_; }
	std::string ending_id() const;

private:
	// content
	std::vector<std::string> errors_;
	Json world_;
	std::map<std::string, ItemDef> items_;
	std::vector<NpcDef> npcs_;
	std::map<std::string, LocationDef> locations_;
	std::vector<SegmentDef> segments_;
	std::vector<AreaDef> areas_;
	std::vector<LinkDef> links_;
	std::vector<ActionDef> actions_;
	std::vector<RuleDef> rules_;
	std::vector<EndingDef> endings_;
	std::vector<EpilogueDef> epilogue_;
	std::vector<WeatherDef> weather_;
	Json start_;

	// state
	uint64_t seed_ = 0;
	int t_ = 0;
	std::string loc_, seg_;
	double x_ = 0.0;
	double walk_acc_ = 0.0;
	int energy_acc_ = 0;
	std::map<std::string, int> stats_;
	std::map<std::string, int> facts_;
	std::array<std::string, kInventorySlots> inv_;
	std::vector<std::string> stash_;
	std::map<std::string, int> fired_; // rule id -> day fired (+1)
	std::map<std::string, int> performed_; // action id -> last day performed (+1)
	std::map<std::string, int> fact_event_; // fact (or "item:x") -> last cause event
	std::vector<PendingEvent> pending_;
	std::vector<UnreadPhone> unread_;
	std::vector<PhoneEntry> phone_;
	std::vector<CauseEvent> events_;
	std::vector<OutMessage> out_;
	bool finished_ = false;
	bool location_forced_ = false;
	std::string forced_ending_;
	int pending_seq_ = 0;

	// loading helpers
	void load_file(const std::string &name, const Json &root);
	Cond parse_cond(const std::string &src, const std::string &ctx);
	std::vector<Cond> parse_conds(const Json &v, const std::string &ctx);
	Effect parse_effect(const std::string &src, const std::string &ctx);
	std::vector<Effect> parse_effects(const Json &v, const std::string &ctx);
	std::vector<Requirement> parse_requires(const Json &v, const std::string &ctx);
	static ToneText parse_text(const Json &v);
	static unsigned parse_days(const Json &v);
	void validate();

	// runtime helpers
	bool eval(const Cond &c) const;
	bool eval_all(const std::vector<Cond> &cs) const;
	void collect_facts(const std::vector<Cond> &cs, std::vector<std::string> &out) const;
	std::vector<int> parents_for(const std::vector<Cond> &cs) const;
	int new_event(const std::string &source, const std::string &label, std::vector<int> parents);
	void apply(const std::vector<Effect> &effects, int event);
	void apply_one(const Effect &e, int event);
	void touch_fact(const std::string &key, const std::string &desc, int event);
	void add_stat(const std::string &name, int delta);
	void set_stat(const std::string &name, int value);
	const std::string &pick(const ToneText &t) const;
	void say(const ToneText &t, const std::string &kind = "narr", const std::string &from = "");
	void advance(int ticks);
	void tick_once();
	void end_day(bool slept);
	void process_pending();
	void evaluate_rules();
	void fire_rule(const RuleDef &r, std::vector<int> parents);
	void deliver_unread();
	void enforce_location();
	bool give_item(const std::string &id, int event);
	bool remove_item(const std::string &id, int event);
	bool action_visible(const ActionDef &a) const;
	bool requirements_met(const std::vector<Requirement> &reqs, std::string *why) const;
	const SegmentDef *segment(const std::string &id) const;
	const NpcDef *npc(const std::string &id) const;
	const ScheduleEntry *npc_entry(const NpcDef &n) const;
	void check_finished();
};

} // namespace od
