#include "animated_door.hpp"

#include "voxel_mesher.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <cmath>

namespace godot {

void AnimatedDoor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "def"), &AnimatedDoor::configure);
	ClassDB::bind_method(D_METHOD("set_open", "open"), &AnimatedDoor::set_open);
	ClassDB::bind_method(D_METHOD("is_open"), &AnimatedDoor::is_open);
}

void AnimatedDoor::configure(const Dictionary &def) {
	const float vs = 0.0625f;
	const float width = float(double(def.get("width", 1.0)));
	const int W = int(std::lround(width / vs));
	const int H = int(std::lround(2.1 / vs));
	const uint32_t base = parse_hex_color(def.get("color", "#6d4c35"));
	const uint32_t inset = shade(base, 0.82f);
	const uint32_t handle = parse_hex_color("#d8c48a");
	const bool glass = bool(def.get("glass", false));

	// Panel along +X from the hinge, 1 voxel thick.
	VoxelGrid g;
	g.resize(W, H, 1);
	for (int x = 0; x < W; ++x) {
		for (int y = 0; y < H; ++y) {
			bool border = x < 2 || x >= W - 2 || y < 2 || y >= H - 2;
			bool mid = y >= H / 2 - 1 && y <= H / 2;
			uint32_t c = (border || mid) ? base : inset;
			if (glass && !border && y > H / 2 + 1 && y < H - 4) {
				c = glow_night(parse_hex_color("#bcd6e6"));
			}
			g.set(x, y, 0, c);
		}
	}
	for (int y = H / 2 - 2; y < H / 2 + 1; ++y) {
		g.set(W - 4, y, 0, handle);
	}
	MeshInstance3D *mesh = memnew(MeshInstance3D);
	Ref<ArrayMesh> m = build_voxel_mesh(g, vs, Vector3(0, 0, -vs * 0.5f), 0.04f);
	if (m->get_surface_count() > 0) {
		m->surface_set_material(0, make_voxel_material());
	}
	mesh->set_mesh(m);

	pivot_ = memnew(Node3D);
	add_child(pivot_);
	pivot_->add_child(mesh);
	set_position(Vector3(float(double(def.get("x", 0.0))), float(double(def.get("y", 0.0))), float(double(def.get("z", 0.0)))));

	String facing = def.get("facing", "front");
	float swing = float(double(def.get("swing", 1.0)));
	if (facing == "side") {
		// Closed: panel runs back into the scene (-Z). Open: swings flat towards +X/-X.
		closed_deg_ = 90.0f;
		open_deg_ = swing > 0 ? 8.0f : 172.0f;
	} else {
		// Closed: in the facade plane. Open: swings inwards.
		closed_deg_ = 0.0f;
		open_deg_ = 82.0f * swing;
	}
	apply();
}

void AnimatedDoor::set_open(bool open) {
	target_ = open ? 1.0f : 0.0f;
}

void AnimatedDoor::_process(double delta) {
	// Damped spring: a little overshoot sells the weight of the door.
	float dt = float(std::min(delta, 0.05));
	float force = (target_ - amount_) * 90.0f - velocity_ * 11.0f;
	velocity_ += force * dt;
	amount_ += velocity_ * dt;
	if (std::fabs(target_ - amount_) < 0.001f && std::fabs(velocity_) < 0.001f) {
		amount_ = target_;
		velocity_ = 0.0f;
	}
	apply();
}

void AnimatedDoor::apply() {
	if (pivot_) {
		pivot_->set_rotation_degrees(Vector3(0, closed_deg_ + (open_deg_ - closed_deg_) * amount_, 0));
	}
}

} // namespace godot
