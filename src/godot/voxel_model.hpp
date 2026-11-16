#pragma once

#include "voxel_mesher.hpp"

#include <godot_cpp/classes/mesh_instance3d.hpp>

namespace godot {

// A MagicaVoxel model placed in a scene. Pivot is bottom-centre. Works in
// the editor, so hand-placed props show up immediately.
class VoxelModel : public MeshInstance3D {
	GDCLASS(VoxelModel, MeshInstance3D)

public:
	void _ready() override;

	void set_vox_path(const String &p);
	String get_vox_path() const { return vox_path_; }
	void set_voxel_size(float s);
	float get_voxel_size() const { return voxel_size_; }

	// Loads a .vox file into a mesh with bottom-centre pivot.
	static Ref<ArrayMesh> build_from_file(const String &path, float voxel_size);

protected:
	static void _bind_methods();

private:
	String vox_path_;
	float voxel_size_ = 0.0625f;
	void rebuild();
};

} // namespace godot
