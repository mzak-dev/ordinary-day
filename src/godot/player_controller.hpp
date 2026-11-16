#pragma once

#include "game_sim.hpp"
#include "world_director.hpp"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/sprite3d.hpp>

#include <deque>
#include <functional>

namespace godot {

// Side-scrolling movement along walk segments. W/S use doors, stairs and the
// bus — each with its own little animation. Drives the camera too.
class PlayerController : public Node3D {
	GDCLASS(PlayerController, Node3D)

public:
	void _ready() override;
	void _physics_process(double delta) override;
	void _process(double delta) override;

	void set_sim_path(const NodePath &p) { sim_path_ = p; }
	NodePath get_sim_path() const { return sim_path_; }
	void set_world_path(const NodePath &p) { world_path_ = p; }
	NodePath get_world_path() const { return world_path_; }
	void set_camera_path(const NodePath &p) { camera_path_ = p; }
	NodePath get_camera_path() const { return camera_path_; }
	void set_speed(double s) { speed_ = s; }
	double get_speed() const { return speed_; }
	void set_input_enabled(bool e) { input_enabled_ = e; }
	bool get_input_enabled() const { return input_enabled_; }
	void set_busy(bool b) { busy_ = b; }
	bool get_busy() const { return busy_; }
	bool is_animating() const { return !steps_.empty(); }

	void teleport(const String &seg, double x);
	void update_prompt();

protected:
	static void _bind_methods();

private:
	enum class Pose { Idle, Walk, Climb, Interact };
	struct Step {
		double duration = 0.0;
		std::function<void()> start;
		std::function<void(double)> update; // t in [0, 1]
	};

	NodePath sim_path_ = NodePath("../GameSim");
	NodePath world_path_ = NodePath("../World");
	NodePath camera_path_ = NodePath("../Camera3D");
	GameSim *sim_ = nullptr;
	WorldDirector *world_ = nullptr;
	Camera3D *camera_ = nullptr;
	Sprite3D *sprite_ = nullptr;

	String seg_;
	double x_ = 0, y_ = 0, z_ = 0, x0_ = 0, x1_ = 0;
	Vector3 vpos_; // visual position (differs from logical during animations)
	double speed_ = 3.6;
	bool input_enabled_ = true;
	bool busy_ = false;
	bool facing_left_ = false;
	double anim_ = 0;
	Pose pose_ = Pose::Idle;
	float alpha_ = 1.0f;
	String prompt_;
	Vector3 cam_focus_;
	bool cam_init_ = false;

	std::deque<Step> steps_;
	double step_t_ = 0.0;
	bool step_started_ = false;

	void on_location_forced(const String &seg, double x);
	void try_link(const String &key);
	void push(double duration, std::function<void()> start, std::function<void(double)> update);
	void push_walk(const Vector3 &from, const Vector3 &to, Pose pose = Pose::Walk);
	void queue_door(const Dictionary &link, bool from_a);
	void queue_stairs(const Dictionary &link, bool from_a);
	void queue_bus(const Dictionary &link, bool from_a);
	void commit_link(const String &id, bool from_a);
	Vector3 end_position(const Dictionary &link, bool a_side) const;
	void sync_from_sim();
};

} // namespace godot
