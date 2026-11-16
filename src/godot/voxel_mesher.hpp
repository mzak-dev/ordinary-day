#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

namespace godot {

// Dense voxel grid. x → right, y → up, z → towards the camera.
// Colors are packed RGBA8 (0 = empty).
struct VoxelGrid {
	int sx = 0, sy = 0, sz = 0;
	std::vector<uint32_t> cells;

	void resize(int x, int y, int z) {
		sx = x;
		sy = y;
		sz = z;
		cells.assign(size_t(x) * size_t(y) * size_t(z), 0u);
	}
	bool inside(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz; }
	uint32_t get(int x, int y, int z) const { return inside(x, y, z) ? cells[(size_t(y) * sz + z) * sx + x] : 0u; }
	void set(int x, int y, int z, uint32_t c) {
		if (inside(x, y, z)) {
			cells[(size_t(y) * sz + z) * sx + x] = c;
		}
	}
	void fill(int x0, int y0, int z0, int x1, int y1, int z1, uint32_t c); // inclusive-exclusive
};

uint32_t pack_color(const Color &c);
uint32_t parse_hex_color(const String &hex, uint32_t fallback = 0xFF888888u);
uint32_t shade(uint32_t rgba, float factor);
// Marks a colour as emissive: always-on (screens) or night-only (lamps, windows).
uint32_t glow_always(uint32_t rgba);
uint32_t glow_night(uint32_t rgba);

// Builds a face-culled mesh with baked ambient occlusion and per-voxel colour
// noise. `origin` is the world-space position of voxel (0,0,0)'s min corner.
Ref<ArrayMesh> build_voxel_mesh(const VoxelGrid &grid, float voxel_size, const Vector3 &origin, float noise = 0.06f);
// New material instance using res://shaders/voxel.gdshader.
Ref<ShaderMaterial> make_voxel_material();

// Reads a MagicaVoxel .vox file. MagicaVoxel is Z-up with Y going away from
// the viewer; we map it to Y-up with the model's y=0 row facing the camera.
bool load_vox(const String &path, VoxelGrid &out);

} // namespace godot
