#include "player_controller.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cmath>
#include <memory>

namespace godot {

namespace {

double ease_out(double t) {
	return 1 - std::pow(1 - t, 3);
}

double ease_in(double t) {
	return t * t * t;
}

constexpr float kBusLane = 4.7f;
constexpr float kBusDoorOffset = -3.5f; // bus centre relative to its front door

} // namespace

void PlayerController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sim_path", "p"), &PlayerController::set_sim_path);
	ClassDB::bind_method(D_METHOD("get_sim_path"), &PlayerController::get_sim_path);
	ClassDB::bind_method(D_METHOD("set_world_path", "p"), &PlayerController::set_world_path);
	ClassDB::bind_method(D_METHOD("get_world_path"), &PlayerController::get_world_path);
	ClassDB::bind_method(D_METHOD("set_camera_path", "p"), &PlayerController::set_camera_path);
	ClassDB::bind_method(D_METHOD("get_camera_path"), &PlayerController::get_camera_path);
	ClassDB::bind_method(D_METHOD("set_speed", "s"), &PlayerController::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &PlayerController::get_speed);
	ClassDB::bind_method(D_METHOD("set_input_enabled", "e"), &PlayerController::set_input_enabled);
	ClassDB::bind_method(D_METHOD("get_input_enabled"), &PlayerController::get_input_enabled);
	ClassDB::bind_method(D_METHOD("set_busy", "b"), &PlayerController::set_busy);
	ClassDB::bind_method(D_METHOD("get_busy"), &PlayerController::get_busy);
	ClassDB::bind_method(D_METHOD("is_animating"), &PlayerController::is_animating);
	ClassDB::bind_method(D_METHOD("teleport", "seg", "x"), &PlayerController::teleport);
	ClassDB::bind_method(D_METHOD("on_location_forced", "seg", "x"), &PlayerController::on_location_forced);
	ClassDB::bind_method(D_METHOD("update_prompt"), &PlayerController::update_prompt);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sim_path"), "set_sim_path", "get_sim_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_path"), "set_world_path", "get_world_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_path"), "set_camera_path", "get_camera_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed"), "set_speed", "get_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "input_enabled"), "set_input_enabled", "get_input_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "busy"), "set_busy", "get_busy");

	ADD_SIGNAL(MethodInfo("prompt_changed", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("notice", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("screen_fade", PropertyInfo(Variant::FLOAT, "alpha"), PropertyInfo(Variant::FLOAT, "duration")));
}

void PlayerController::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	sim_ = Object::cast_to<GameSim>(get_node_or_null(sim_path_));
	world_ = Object::cast_to<WorldDirector>(get_node_or_null(world_path_));
	camera_ = Object::cast_to<Camera3D>(get_node_or_null(camera_path_));
	sprite_ = WorldDirector::make_character_sprite("player");
	add_child(sprite_);
	if (sim_) {
		sim_->connect("location_forced", Callable(this, "on_location_forced"));
		sim_->connect("state_changed", Callable(this, "update_prompt"));
		teleport(sim_->get_player_seg(), sim_->get_player_x());
	}
}

void PlayerController::on_location_forced(const String &seg, double x) {
	steps_.clear();
	alpha_ = 1.0f;
	teleport(seg, x);
}

void PlayerController::sync_from_sim() {
	Dictionary s = world_->get_segment(sim_->get_player_seg());
	if (s.is_empty()) {
		return;
	}
	seg_ = sim_->get_player_seg();
	x0_ = double(s.get("x0", 0.0));
	x1_ = double(s.get("x1", 0.0));
	y_ = double(s.get("y", 0.0));
	z_ = double(s.get("z", 0.0));
	x_ = std::max(x0_, std::min(x1_, sim_->get_player_x()));
}

void PlayerController::teleport(const String &seg, double x) {
	if (!world_) {
		return;
	}
	Dictionary s = world_->get_segment(seg);
	if (s.is_empty()) {
		return;
	}
	seg_ = seg;
	x0_ = double(s.get("x0", 0.0));
	x1_ = double(s.get("x1", 0.0));
	y_ = double(s.get("y", 0.0));
	z_ = double(s.get("z", 0.0));
	x_ = std::max(x0_, std::min(x1_, x));
	vpos_ = Vector3(float(x_), float(y_), float(z_));
	set_position(vpos_);
	update_prompt();
}

// ------------------------------------------------------------------ sequencing

void PlayerController::push(double duration, std::function<void()> start, std::function<void(double)> update) {
	steps_.push_back({ duration, std::move(start), std::move(update) });
}

void PlayerController::push_walk(const Vector3 &from, const Vector3 &to, Pose pose) {
	double dist = from.distance_to(to);
	if (dist < 0.03) {
		return;
	}
	double dur = pose == Pose::Climb ? std::max(0.9, dist / 2.4) : dist / speed_;
	push(dur, [this, pose, from, to]() {
		pose_ = pose;
		if (std::fabs(to.x - from.x) > 0.01) {
			facing_left_ = to.x < from.x;
		} }, [this, from, to](double t) { vpos_ = from.lerp(to, float(t)); });
}

Vector3 PlayerController::end_position(const Dictionary &link, bool a_side) const {
	Dictionary end = link.get(a_side ? "a" : "b", Dictionary());
	Dictionary seg = world_->get_segment(end.get("seg", ""));
	return Vector3(float(double(end.get("x", 0.0))), float(double(seg.get("y", 0.0))), float(double(seg.get("z", 0.0))));
}

void PlayerController::commit_link(const String &id, bool from_a) {
	if (sim_->use_link(id, from_a)) {
		sync_from_sim();
	}
}

void PlayerController::queue_door(const Dictionary &link, bool from_a) {
	String id = link.get("id", "");
	Vector3 from = end_position(link, from_a);
	Vector3 to = end_position(link, !from_a);
	push(0.3, [this, id]() {
		pose_ = Pose::Interact;
		world_->set_door_open(id, true); }, nullptr);
	auto committed = std::make_shared<bool>(false);
	double dur = std::max(0.6, from.distance_to(to) / speed_);
	push(dur, [this, from, to]() {
		pose_ = Pose::Walk;
		if (std::fabs(to.x - from.x) > 0.01) {
			facing_left_ = to.x < from.x;
		} }, [this, from, to, id, from_a, committed](double t) {
		vpos_ = from.lerp(to, float(t));
		alpha_ = std::fabs(to.z - from.z) > 1.0f ? float(0.55 + 0.45 * std::fabs(2.0 * t - 1.0)) : 1.0f;
		if (t >= 0.5 && !*committed) {
			*committed = true;
			commit_link(id, from_a);
		} });
	push(0.25, [this, id]() {
		pose_ = Pose::Idle;
		alpha_ = 1.0f;
		world_->set_door_open(id, false); }, nullptr);
}

void PlayerController::queue_stairs(const Dictionary &link, bool from_a) {
	String id = link.get("id", "");
	Dictionary st = link.get("stairs", Dictionary());
	Vector3 from = end_position(link, from_a);
	Vector3 to = end_position(link, !from_a);
	bool up = to.y > from.y;
	float sz = float(double(st.get("z", -1.95)));
	float low_x = float(double(st.get("low_x", from.x)));
	float high_x = float(double(st.get("high_x", from.x)));
	Vector3 p1(up ? low_x : high_x, from.y, sz);
	Vector3 p2(up ? high_x : low_x, to.y, sz);
	push_walk(from, p1);
	push(0.0, [this, id, from_a]() { commit_link(id, from_a); }, nullptr);
	push_walk(p1, p2, Pose::Climb);
	push_walk(p2, to);
}

void PlayerController::queue_bus(const Dictionary &link, bool from_a) {
	String id = link.get("id", "");
	Node3D *bus = world_->get_bus();
	Vector3 from = end_position(link, from_a);
	Vector3 to = end_position(link, !from_a);
	Vector3 curb_from(from.x, 0.0f, 3.6f);
	Vector3 curb_to(to.x, 0.0f, 3.6f);
	float stop_a = from.x + kBusDoorOffset;
	float stop_b = to.x + kBusDoorOffset;

	push(2.0, [this, bus, stop_a]() {
		pose_ = Pose::Idle;
		facing_left_ = false;
		bus->set_visible(true);
		bus->set_position(Vector3(stop_a - 55.0f, 0.0f, kBusLane)); }, [bus, stop_a](double t) { bus->set_position(Vector3(float(stop_a - 55.0 * (1.0 - ease_out(t))), 0.0f, kBusLane)); });
	push_walk(from, curb_from);
	push(0.3, nullptr, [this](double t) { alpha_ = float(1.0 - t); });
	auto faded = std::make_shared<bool>(false);
	push(1.5, nullptr, [this, bus, stop_a, faded](double t) {
		bus->set_position(Vector3(float(stop_a + 60.0 * ease_in(t)), 0.0f, kBusLane));
		if (t > 0.35 && !*faded) {
			*faded = true;
			emit_signal("screen_fade", 1.0, 0.6);
		} });
	push(0.0, [this, id, from_a, bus, stop_b, curb_to]() {
		commit_link(id, from_a);
		vpos_ = curb_to;
		bus->set_position(Vector3(stop_b, 0.0f, kBusLane));
		emit_signal("screen_fade", 0.0, 0.8); }, nullptr);
	push(0.9, nullptr, nullptr);
	push(0.3, nullptr, [this](double t) { alpha_ = float(t); });
	push_walk(curb_to, to);
	push(1.6, nullptr, [bus, stop_b](double t) {
		bus->set_position(Vector3(float(stop_b + 60.0 * ease_in(t)), 0.0f, kBusLane));
		if (t >= 1.0) {
			bus->set_visible(false);
		} });
}

void PlayerController::try_link(const String &key) {
	if (!world_ || !sim_) {
		return;
	}
	// Pick the nearest link with this key within reach; the player walks to it.
	Array links = world_->find_links(seg_, x_, 1.6);
	Dictionary best;
	double best_d = 1e9;
	for (int64_t i = 0; i < links.size(); ++i) {
		Dictionary l = links[i];
		double d = std::fabs(double(l.get("x", 0.0)) - x_);
		if (String(l.get("key", "")) == key && d < best_d) {
			best_d = d;
			best = l;
		}
	}
	if (!best.is_empty()) {
		Dictionary l = best;
		if (!bool(l.get("ok", false))) {
			emit_signal("notice", l.get("why", ""));
			return;
		}
		String id = l.get("id", "");
		bool from_a = bool(l.get("from_a", true));
		Dictionary def = world_->get_link_def(id);
		def["id"] = id;
		String anim = def.get("anim", "");
		Vector3 start = end_position(def, from_a);
		push_walk(vpos_, start);
		if (anim == "door") {
			queue_door(def, from_a);
		} else if (anim == "stairs") {
			queue_stairs(def, from_a);
		} else if (anim == "bus") {
			queue_bus(def, from_a);
		} else {
			push(0.0, [this, id, from_a]() {
				commit_link(id, from_a);
				vpos_ = Vector3(float(x_), float(y_), float(z_)); }, nullptr);
		}
		update_prompt();
	}
}

void PlayerController::update_prompt() {
	String text;
	if (world_ && sim_ && !sim_->is_finished() && steps_.empty()) {
		Array links = world_->find_links(seg_, x_);
		for (int64_t i = 0; i < links.size(); ++i) {
			Dictionary l = links[i];
			String key = String(l.get("key", "")) == "down" ? "S" : "W";
			if (!text.is_empty()) {
				text += "    ";
			}
			text += "[" + key + "] " + String(l.get("label", ""));
		}
	}
	if (text != prompt_) {
		prompt_ = text;
		emit_signal("prompt_changed", text);
	}
}

// ------------------------------------------------------------------ frame

void PlayerController::_physics_process(double delta) {
	if (Engine::get_singleton()->is_editor_hint() || !sim_ || seg_.is_empty()) {
		return;
	}
	if (!steps_.empty() || !input_enabled_ || busy_ || sim_->is_finished()) {
		return;
	}
	Input *in = Input::get_singleton();
	pose_ = Pose::Idle;
	double dir = in->get_axis("move_left", "move_right");
	if (std::fabs(dir) > 0.1) {
		double nx = std::max(x0_, std::min(x1_, x_ + dir * speed_ * delta));
		facing_left_ = dir < 0;
		if (std::fabs(nx - x_) > 1e-4) {
			pose_ = Pose::Walk;
			x_ = nx;
			vpos_ = Vector3(float(x_), float(y_), float(z_));
			sim_->set_player_pos(seg_, x_);
			update_prompt();
		}
	}
	if (in->is_action_just_pressed("move_up")) {
		try_link("up");
	} else if (in->is_action_just_pressed("move_down")) {
		try_link("down");
	}
}

void PlayerController::_process(double delta) {
	if (Engine::get_singleton()->is_editor_hint() || !sprite_) {
		return;
	}
	anim_ += delta;

	// Run the animation queue.
	if (!steps_.empty()) {
		Step &s = steps_.front();
		if (!step_started_) {
			step_started_ = true;
			step_t_ = 0.0;
			if (s.start) {
				s.start();
			}
		}
		if (!steps_.empty()) {
			Step &cur = steps_.front();
			step_t_ += delta;
			double t = cur.duration <= 0.0 ? 1.0 : std::min(1.0, step_t_ / cur.duration);
			if (cur.update) {
				cur.update(t);
			}
			if (t >= 1.0) {
				steps_.pop_front();
				step_started_ = false;
				if (steps_.empty()) {
					pose_ = Pose::Idle;
					alpha_ = 1.0f;
					vpos_ = Vector3(float(x_), float(y_), float(z_));
					update_prompt();
				}
			}
		}
	}
	set_position(vpos_);

	switch (busy_ ? Pose::Interact : pose_) {
		case Pose::Walk: {
			static const int cycle[4] = { 1, 2, 3, 2 };
			sprite_->set_frame(cycle[int(anim_ * 8.0) % 4]);
			break;
		}
		case Pose::Climb: {
			static const int cycle[4] = { 5, 1, 5, 3 };
			sprite_->set_frame(cycle[int(anim_ * 7.0) % 4]);
			break;
		}
		case Pose::Interact:
			sprite_->set_frame(busy_ ? (std::fmod(anim_, 0.7) < 0.35 ? 4 : 0) : 4);
			break;
		case Pose::Idle:
			sprite_->set_frame(std::fmod(anim_, 1.6) < 1.2 ? 0 : 2);
			break;
	}
	sprite_->set_flip_h(facing_left_);
	sprite_->set_modulate(Color(1, 1, 1, alpha_));

	if (camera_) {
		Vector3 focus(vpos_.x, vpos_.y + 1.6f, -1.0f);
		if (!cam_init_ || focus.distance_to(cam_focus_) > 30.0f) {
			cam_focus_ = focus;
			cam_init_ = true;
		}
		float k = 1.0f - std::exp(-float(delta) * 4.0f);
		cam_focus_ = cam_focus_.lerp(focus, k);
		// Close perspective camera looking slightly down: rooms read as real
		// boxes with converging side walls, floors and ceilings.
		const float dist = 19.5f;
		const float pitch = 0.17f;
		camera_->set_position(cam_focus_ + Vector3(0.0f, dist * std::sin(pitch), dist * std::cos(pitch)));
		camera_->set_rotation(Vector3(-pitch, 0.0f, 0.0f));
	}
}

} // namespace godot
