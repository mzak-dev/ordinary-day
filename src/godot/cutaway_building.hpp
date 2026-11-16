#pragma once

#include "voxel_mesher.hpp"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <vector>

namespace godot {

// A voxel building built from a definition in world.json. The front wall is a
// separate mesh that dissolves (dithered) when the building is open,
// revealing a dollhouse cross-section of every floor.
class CutawayBuilding : public Node3D {
	GDCLASS(CutawayBuilding, Node3D)

public:
	static constexpr float kVoxel = 0.125f;

	void _process(double delta) override;

	void configure(const Dictionary &def);
	void set_cutaway(bool open);
	bool is_cutaway() const { return target_alpha_ < 0.5f; }
	void set_light_level(float level);

	String get_open_loc() const { return open_loc_; }
	double get_x0() const { return x0_; }
	double get_x1() const { return x1_; }

protected:
	static void _bind_methods();

private:
	MeshInstance3D *shell_ = nullptr;
	MeshInstance3D *facade_ = nullptr;
	Ref<ShaderMaterial> facade_mat_;
	std::vector<OmniLight3D *> lights_;
	String open_loc_;
	double x0_ = 0, x1_ = 0;
	float alpha_ = 1.0f;
	float target_alpha_ = 1.0f;

	void apply_alpha();
};

} // namespace godot
