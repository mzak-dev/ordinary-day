#include "voxel_model.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

void VoxelModel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_vox_path", "path"), &VoxelModel::set_vox_path);
	ClassDB::bind_method(D_METHOD("get_vox_path"), &VoxelModel::get_vox_path);
	ClassDB::bind_method(D_METHOD("set_voxel_size", "size"), &VoxelModel::set_voxel_size);
	ClassDB::bind_method(D_METHOD("get_voxel_size"), &VoxelModel::get_voxel_size);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "vox_path", PROPERTY_HINT_FILE, "*.vox"), "set_vox_path", "get_vox_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voxel_size", PROPERTY_HINT_RANGE, "0.01,1.0,0.0001"), "set_voxel_size", "get_voxel_size");
}

void VoxelModel::_ready() {
	if (get_mesh().is_null()) {
		rebuild();
	}
}

void VoxelModel::set_vox_path(const String &p) {
	vox_path_ = p;
	if (is_inside_tree()) {
		rebuild();
	}
}

void VoxelModel::set_voxel_size(float s) {
	voxel_size_ = s;
	if (is_inside_tree()) {
		rebuild();
	}
}

Ref<ArrayMesh> VoxelModel::build_from_file(const String &path, float vs) {
	VoxelGrid grid;
	if (!load_vox(path, grid)) {
		UtilityFunctions::push_warning("VoxelModel: cannot load ", path);
		return Ref<ArrayMesh>();
	}
	Vector3 origin(-grid.sx * vs * 0.5f, 0.0f, -grid.sz * vs * 0.5f);
	Ref<ArrayMesh> mesh = build_voxel_mesh(grid, vs, origin);
	if (mesh->get_surface_count() > 0) {
		mesh->surface_set_material(0, make_voxel_material());
	}
	return mesh;
}

void VoxelModel::rebuild() {
	if (vox_path_.is_empty()) {
		set_mesh(Ref<Mesh>());
		return;
	}
	set_mesh(build_from_file(vox_path_, voxel_size_));
}

} // namespace godot
