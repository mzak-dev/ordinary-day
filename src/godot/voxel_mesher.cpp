#include "voxel_mesher.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>

namespace godot {

void VoxelGrid::fill(int x0, int y0, int z0, int x1, int y1, int z1, uint32_t c) {
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	z0 = std::max(0, z0);
	x1 = std::min(sx, x1);
	y1 = std::min(sy, y1);
	z1 = std::min(sz, z1);
	for (int y = y0; y < y1; ++y) {
		for (int z = z0; z < z1; ++z) {
			for (int x = x0; x < x1; ++x) {
				cells[(size_t(y) * sz + z) * sx + x] = c;
			}
		}
	}
}

uint32_t pack_color(const Color &c) {
	auto b = [](float v) { return uint32_t(std::max(0.0f, std::min(1.0f, v)) * 255.0f + 0.5f); };
	return (b(c.r) << 24) | (b(c.g) << 16) | (b(c.b) << 8) | b(c.a);
}

uint32_t parse_hex_color(const String &hex, uint32_t fallback) {
	if (hex.is_empty() || !Color::html_is_valid(hex)) {
		return fallback;
	}
	return pack_color(Color::html(hex));
}

uint32_t glow_always(uint32_t rgba) {
	return (rgba & 0xFFFFFF00u) | 0x60u;
}

uint32_t glow_night(uint32_t rgba) {
	return (rgba & 0xFFFFFF00u) | 0xC8u;
}

uint32_t shade(uint32_t rgba, float f) {
	auto ch = [&](int shift) { return uint32_t(std::min(255.0f, float((rgba >> shift) & 0xFF) * f)); };
	return (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | (rgba & 0xFF);
}

namespace {

struct FaceDir {
	int n[3];
	int u[3];
	int v[3];
};

// For each face: normal and the two in-plane axes.
const FaceDir kDirs[6] = {
	{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
	{ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
	{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
	{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
	{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
	{ { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } },
};

// Small per-direction brightness offset keeps voxel edges readable even in flat light.
const float kFaceTint[6] = { 0.94f, 0.94f, 1.0f, 0.8f, 0.97f, 0.9f };

} // namespace

Ref<ArrayMesh> build_voxel_mesh(const VoxelGrid &g, float vs, const Vector3 &origin, float noise) {
	PackedVector3Array verts, normals;
	PackedColorArray colors;
	PackedInt32Array indices;

	auto solid = [&](int x, int y, int z) { return g.get(x, y, z) != 0u; };

	for (int y = 0; y < g.sy; ++y) {
		for (int z = 0; z < g.sz; ++z) {
			for (int x = 0; x < g.sx; ++x) {
				uint32_t c = g.get(x, y, z);
				if (!c) {
					continue;
				}
				for (int d = 0; d < 6; ++d) {
					const FaceDir &f = kDirs[d];
					int nx = x + f.n[0], ny = y + f.n[1], nz = z + f.n[2];
					if (solid(nx, ny, nz)) {
						continue;
					}
					// Base corner of the face: on the positive side for + normals.
					int bx = x + std::max(0, f.n[0]);
					int by = y + std::max(0, f.n[1]);
					int bz = z + std::max(0, f.n[2]);
					Vector3 normal(float(f.n[0]), float(f.n[1]), float(f.n[2]));
					int base = int(verts.size());
					float ao[4];
					Vector3 corner[4];
					const int du[4] = { 0, 1, 1, 0 };
					const int dv[4] = { 0, 0, 1, 1 };
					for (int k = 0; k < 4; ++k) {
						int cx = bx + f.u[0] * du[k] + f.v[0] * dv[k];
						int cy = by + f.u[1] * du[k] + f.v[1] * dv[k];
						int cz = bz + f.u[2] * du[k] + f.v[2] * dv[k];
						corner[k] = origin + Vector3(float(cx), float(cy), float(cz)) * vs;
						// AO samples the three voxels around this corner in front of the face.
						int su = du[k] ? 1 : -1;
						int sv = dv[k] ? 1 : -1;
						int ux = f.u[0] * su, uy = f.u[1] * su, uz = f.u[2] * su;
						int vx = f.v[0] * sv, vy = f.v[1] * sv, vz = f.v[2] * sv;
						bool s1 = solid(nx + ux, ny + uy, nz + uz);
						bool s2 = solid(nx + vx, ny + vy, nz + vz);
						bool s3 = solid(nx + ux + vx, ny + uy + vy, nz + uz + vz);
						int occ = (s1 && s2) ? 3 : int(s1) + int(s2) + int(s3);
						ao[k] = 1.0f - 0.16f * float(occ);
					}
					// Godot treats clockwise triangles as front-facing.
					Vector3 cross = (corner[1] - corner[0]).cross(corner[2] - corner[0]);
					bool flip = cross.dot(normal) > 0.0f;
					float r = float((c >> 24) & 0xFF) / 255.0f;
					float gg = float((c >> 16) & 0xFF) / 255.0f;
					float b = float((c >> 8) & 0xFF) / 255.0f;
					float alpha = float(c & 0xFF) / 255.0f;
					// Per-voxel brightness jitter: the hand-made look of Teardown-style voxels.
					uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
					h ^= h >> 13;
					h *= 0x5bd1e995u;
					h ^= h >> 15;
					float jitter = alpha < 0.99f ? 1.0f : 1.0f + noise * (float(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f);
					for (int k = 0; k < 4; ++k) {
						verts.push_back(corner[k]);
						normals.push_back(normal);
						float s = (alpha < 0.99f ? 1.0f : ao[k] * kFaceTint[d]) * jitter;
						colors.push_back(Color(r * s, gg * s, b * s, alpha));
					}
					// Split along the diagonal with less AO contrast to avoid anisotropy.
					bool alt = ao[0] + ao[2] < ao[1] + ao[3];
					int tri[6];
					if (!alt) {
						int t[6] = { 0, 1, 2, 0, 2, 3 };
						std::copy(t, t + 6, tri);
					} else {
						int t[6] = { 1, 2, 3, 1, 3, 0 };
						std::copy(t, t + 6, tri);
					}
					for (int k = 0; k < 6; k += 3) {
						if (flip) {
							indices.push_back(base + tri[k]);
							indices.push_back(base + tri[k + 2]);
							indices.push_back(base + tri[k + 1]);
						} else {
							indices.push_back(base + tri[k]);
							indices.push_back(base + tri[k + 1]);
							indices.push_back(base + tri[k + 2]);
						}
					}
				}
			}
		}
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	if (verts.is_empty()) {
		return mesh;
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

Ref<ShaderMaterial> make_voxel_material() {
	Ref<ShaderMaterial> m;
	m.instantiate();
	Ref<Shader> shader = ResourceLoader::get_singleton()->load("res://shaders/voxel.gdshader");
	m->set_shader(shader);
	return m;
}

bool load_vox(const String &path, VoxelGrid &out) {
	PackedByteArray data = FileAccess::get_file_as_bytes(path);
	const int64_t n = data.size();
	if (n < 8) {
		return false;
	}
	const uint8_t *p = data.ptr();
	auto u32 = [&](int64_t at) -> uint32_t {
		if (at + 4 > n) {
			return 0;
		}
		return uint32_t(p[at]) | (uint32_t(p[at + 1]) << 8) | (uint32_t(p[at + 2]) << 16) | (uint32_t(p[at + 3]) << 24);
	};
	if (p[0] != 'V' || p[1] != 'O' || p[2] != 'X' || p[3] != ' ') {
		return false;
	}
	uint32_t palette[256];
	for (int i = 0; i < 256; ++i) {
		palette[i] = 0x999999FFu;
	}
	int sx = 0, sy = 0, sz = 0;
	std::vector<uint32_t> xyzi;
	bool have_size = false, have_voxels = false;

	int64_t at = 8;
	while (at + 12 <= n) {
		char id[5] = { char(p[at]), char(p[at + 1]), char(p[at + 2]), char(p[at + 3]), 0 };
		uint32_t content = u32(at + 4);
		int64_t body = at + 12;
		std::string cid(id);
		if (cid == "MAIN") {
			at = body + content; // children follow directly
			continue;
		}
		if (cid == "SIZE" && !have_size) {
			sx = int(u32(body));
			sy = int(u32(body + 4));
			sz = int(u32(body + 8));
			have_size = true;
		} else if (cid == "XYZI" && !have_voxels) {
			uint32_t count = u32(body);
			for (uint32_t i = 0; i < count && body + 4 + i * 4 + 4 <= n; ++i) {
				xyzi.push_back(u32(body + 4 + i * 4));
			}
			have_voxels = true;
		} else if (cid == "RGBA") {
			for (int i = 0; i < 255; ++i) {
				int64_t o = body + i * 4;
				if (o + 4 <= n) {
					palette[i + 1] = (uint32_t(p[o]) << 24) | (uint32_t(p[o + 1]) << 16) | (uint32_t(p[o + 2]) << 8) | uint32_t(p[o + 3]);
				}
			}
		}
		at = body + content + u32(at + 8);
	}
	if (!have_size || !have_voxels) {
		return false;
	}
	// vox (x, y=depth, z=up) → grid (x, y=up, z=towards camera)
	out.resize(sx, sz, sy);
	for (uint32_t v : xyzi) {
		int x = int(v & 0xFF);
		int y = int((v >> 8) & 0xFF);
		int z = int((v >> 16) & 0xFF);
		int ci = int((v >> 24) & 0xFF);
		out.set(x, z, sy - 1 - y, palette[ci]);
	}
	return true;
}

} // namespace godot
