#pragma once

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// A voxel door panel swinging on its hinge. Front doors sit in the facade
// plane and swing inwards; side doors sit in a partition wall (seen edge-on
// from the camera) and swing open towards the camera's view.
class AnimatedDoor : public Node3D {
	GDCLASS(AnimatedDoor, Node3D)

public:
	void _process(double delta) override;

	// def: {x, y, z (hinge), facing: "front"|"side", width, color, swing: 1|-1}
	void configure(const Dictionary &def);
	void set_open(bool open);
	bool is_open() const { return target_ > 0.5f; }

protected:
	static void _bind_methods();

private:
	Node3D *pivot_ = nullptr;
	float closed_deg_ = 0.0f;
	float open_deg_ = 85.0f;
	float amount_ = 0.0f;
	float target_ = 0.0f;
	float velocity_ = 0.0f;
	void apply();
};

} // namespace godot
