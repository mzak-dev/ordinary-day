#include "cutaway_building.hpp"

#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cmath>

namespace godot {

namespace {

constexpr float kVs = CutawayBuilding::kVoxel;

int V(double meters) {
	return int(std::lround(meters / kVs));
}

uint32_t hash3(int x, int y, int z) {
	uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return h;
}

float rand01(int x, int y, int z) {
	return float(hash3(x, y, z) & 0xFFFF) / 65535.0f;
}

struct Room {
	int floor = 0;
	int x0 = 0, x1 = 0; // voxel range
	uint32_t wall = 0, low = 0, floor_col = 0;
	bool has_low = false;
	String floor_kind;
	Color light;
	bool has_light = false;
	bool window = true;
	// Back layer (rooms behind the walkable ones)
	bool open = false;          // no wall between this back room and the room in front
	bool divider = false;       // wall at x0 inside the back layer
	bool divider_door = false;  // ...with a doorway in it
	std::vector<int> doorways;  // front rooms: openings into the back layer (voxel x)
};

} // namespace

void CutawayBuilding::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "def"), &CutawayBuilding::configure);
	ClassDB::bind_method(D_METHOD("set_cutaway", "open"), &CutawayBuilding::set_cutaway);
	ClassDB::bind_method(D_METHOD("is_cutaway"), &CutawayBuilding::is_cutaway);
	ClassDB::bind_method(D_METHOD("set_light_level", "level"), &CutawayBuilding::set_light_level);
	ClassDB::bind_method(D_METHOD("get_open_loc"), &CutawayBuilding::get_open_loc);
}

void CutawayBuilding::configure(const Dictionary &def) {
	x0_ = double(def.get("x0", 0.0));
	x1_ = double(def.get("x1", 8.0));
	const double z0 = def.get("z0", -3.5);
	const double z1 = def.get("z1", 0.0);
	const int floors = int(def.get("floors", 1));
	const double floor_h = def.get("floor_h", 3.0);
	open_loc_ = def.get("open", "always");

	const int W = V(x1_ - x0_);
	const int FH = V(floor_h);
	const int top = floors * FH;
	const int H = top + 6;
	const int D = V(z1 - z0);
	const int Ds = D - 2; // the last two rows belong to the facade

	const uint32_t outer = parse_hex_color(def.get("wall", "#b0a898"));
	const uint32_t facade_col = parse_hex_color(def.get("facade", "#c8c0b0"));
	const uint32_t accent = parse_hex_color(def.get("accent", "#8899aa"));
	const uint32_t cut = parse_hex_color("#2a2624");
	const uint32_t roof = parse_hex_color("#5c5a57");
	const uint32_t white = parse_hex_color("#eeebe4");
	const uint32_t ceiling = parse_hex_color("#d9d5cc");
	const uint32_t radiator = parse_hex_color("#e4e1da");

	auto parse_rooms = [&](const Array &room_defs, bool back) {
	std::vector<Room> rooms;
	for (int64_t i = 0; i < room_defs.size(); ++i) {
		Dictionary r = room_defs[i];
		Room room;
		room.floor = int(r.get("floor", 0));
		room.x0 = V(double(r.get("x0", x0_)) - x0_);
		room.x1 = V(double(r.get("x1", x1_)) - x0_);
		room.wall = parse_hex_color(r.get("wall", "#d0c8b8"));
		String low = r.get("low", "");
		room.has_low = !low.is_empty();
		room.low = room.has_low ? parse_hex_color(low) : room.wall;
		room.floor_col = parse_hex_color(r.get("floor_col", "#7a6a5a"));
		room.floor_kind = r.get("floor_kind", "planks");
		room.window = bool(r.get("window", !back));
		room.open = bool(r.get("open", false));
		room.divider = bool(r.get("divider", false));
		room.divider_door = bool(r.get("divider_door", false));
		Array dw = r.get("doorways", Array());
		for (int64_t k = 0; k < dw.size(); ++k) {
			room.doorways.push_back(V(double(dw[k]) - x0_));
		}
		String light = r.get("light", "#ffe0a0");
		room.has_light = !light.is_empty();
		if (room.has_light) {
			room.light = Color::html(light);
		}
		rooms.push_back(room);
	}
	return rooms;
	};
	std::vector<Room> rooms = parse_rooms(def.get("rooms", Array()), false);
	std::vector<Room> backs = parse_rooms(def.get("back_rooms", Array()), true);
	auto find_in = [](const std::vector<Room> &list, int floor, int x) -> const Room * {
		for (const Room &r : list) {
			if (r.floor == floor && x >= r.x0 && x < r.x1) {
				return &r;
			}
		}
		return nullptr;
	};
	auto room_at = [&](int floor, int x) { return find_in(rooms, floor, x); };
	auto back_at = [&](int floor, int x) { return find_in(backs, floor, x); };
	// The back layer sits between the outer back wall (z 0-1) and an inner
	// wall at Bz..Bz+1 whose front face is where the old back wall was.
	const bool has_back = def.has("back_z");
	const int Bz = has_back ? V(double(def.get("back_z", z0)) - z0) : 0;

	Dictionary stair = def.get("stair_hole", Dictionary());
	const bool has_stair = !stair.is_empty();
	const int sh_x0 = has_stair ? V(double(stair.get("x0", 0.0)) - x0_) : 0;
	const int sh_x1 = has_stair ? V(double(stair.get("x1", 0.0)) - x0_) : 0;
	const int sh_z1 = V(-2.0 - z0);

	VoxelGrid g;
	g.resize(W, H, Ds);
	auto put = [&](int x, int y, int z, uint32_t c) {
		g.set(x, y, z, z >= Ds - 1 ? cut : c);
	};

	// ------------------------------------------------------------ floors
	for (int f = 0; f < floors; ++f) {
		const int yb = f * FH;
		for (int x = 0; x < W; ++x) {
			const Room *rf = room_at(f, x);
			const Room *rb = has_back ? back_at(f, x) : nullptr;
			for (int z = 0; z < Ds; ++z) {
				const Room *r = (has_back && z < Bz + 2 && rb && !rb->open) ? rb : rf;
				uint32_t fc = r ? r->floor_col : roof;
				String kind = r ? r->floor_kind : String("planks");
				if (has_stair && f > 0 && x >= sh_x0 && x < sh_x1 && z >= (has_back ? Bz + 2 : 2) && z < sh_z1) {
					continue;
				}
				uint32_t c = fc;
				if (kind == "tiles") {
					c = ((x / 3 + z / 3) % 2) ? fc : shade(fc, 0.9f);
					if (x % 3 == 0 || z % 3 == 0) {
						c = shade(c, 0.93f);
					}
				} else if (kind == "terrazzo") {
					float n = rand01(x, f, z);
					c = n < 0.12f ? shade(fc, 1.25f) : (n < 0.24f ? shade(fc, 0.78f) : fc);
				} else if (kind == "plain") {
					c = fc;
				} else {
					int plank = (x + ((z / 3) % 2) * 5) / 10;
					c = (plank % 2) ? fc : shade(fc, 0.92f);
					if (z % 3 == 0) {
						c = shade(c, 0.88f);
					}
				}
				put(x, yb, z, f == 0 ? shade(fc, 0.7f) : ceiling);
				put(x, yb + 1, z, c);
			}
		}

		// ------------------------------------------------------------ back wall
		auto paint_wall = [&](int x, int z_out, int z_in, int dir, const Room *r) {
			uint32_t wall_c = r ? r->wall : outer;
			uint32_t low_c = r ? r->low : outer;
			int low_top = yb + 2 + V(1.2);
			for (int y = yb + 2; y < yb + FH; ++y) {
				uint32_t c = wall_c;
				if (r && r->has_low) {
					c = y < low_top ? low_c : (y == low_top ? shade(low_c, 0.75f) : wall_c);
				}
				if (z_out >= 0) {
					g.set(x, y, z_out, outer);
				}
				g.set(x, y, z_in, c);
			}
			if (r) {
				g.set(x, yb + 2, z_in + dir, shade(r->has_low ? low_c : wall_c, 0.7f)); // skirting board
			}
		};
		for (int x = 0; x < W; ++x) {
			const Room *r = room_at(f, x);
			const Room *b = has_back ? back_at(f, x) : nullptr;
			if (!has_back || (b && b->open)) {
				paint_wall(x, 0, 1, 1, r);
				continue;
			}
			if (b) {
				paint_wall(x, 0, 1, 1, b);       // outer wall of the back room
				paint_wall(x, -1, Bz, -1, b);    // inner wall, back face
				paint_wall(x, -1, Bz + 1, 1, r); // inner wall, front face
			} else {
				for (int z = 0; z <= Bz; ++z) {
					for (int y = yb + 2; y < yb + FH; ++y) {
						g.set(x, y, z, outer);
					}
				}
				paint_wall(x, -1, Bz + 1, 1, r);
			}
		}
		// Doorways from the walkable rooms into the back layer.
		for (const Room &r : rooms) {
			if (r.floor != f || !has_back) {
				continue;
			}
			for (int dx : r.doorways) {
				const Room *b = back_at(f, dx);
				if (!b || b->open) {
					continue;
				}
				int hw = V(0.5), top = yb + 2 + V(2.15);
				for (int x = dx - hw - 1; x <= dx + hw; ++x) {
					for (int y = yb + 2; y <= top; ++y) {
						bool frame = x == dx - hw - 1 || x == dx + hw || y == top;
						for (int z = Bz - 1; z <= Bz + 2; ++z) {
							g.set(x, y, z, frame && z >= Bz && z <= Bz + 1 ? white : 0u);
						}
					}
				}
			}
		}

		// ------------------------------------------------------------ windows
		auto place_window = [&](const Room &r) {
			int ww = std::min(V(1.5), r.x1 - r.x0 - V(1.0));
			int wh = V(1.3);
			int cx = (r.x0 + r.x1) / 2;
			int wx0 = cx - ww / 2, wx1 = wx0 + ww;
			int wy0 = yb + 2 + V(0.9), wy1 = wy0 + wh;
			for (int x = wx0; x < wx1; ++x) {
				for (int y = wy0; y < wy1; ++y) {
					bool frame = x == wx0 || x == wx1 - 1 || y == wy0 || y == wy1 - 1 || x == cx || y == wy0 + wh * 2 / 3;
					g.set(x, y, 0, 0);
					g.set(x, y, 1, frame ? white : 0u);
				}
				g.set(x, wy0 - 1, 1, white);
				g.set(x, wy0 - 1, 2, white);
				// Radiator under the window.
				if (x > wx0 && x < wx1 - 1) {
					for (int y = yb + 2 + V(0.15); y < yb + 2 + V(0.7); ++y) {
						g.set(x, y, 2, (x % 2) ? radiator : shade(radiator, 0.85f));
					}
				}
			}
		};
		for (const Room &r : rooms) {
			if (r.floor != f || !r.window || r.x1 - r.x0 < V(2.0)) {
				continue;
			}
			if (has_back) {
				const Room *b = back_at(f, (r.x0 + r.x1) / 2);
				if (!(b && b->open)) {
					continue; // its back wall is an inner wall now
				}
			}
			place_window(r);
		}
		for (const Room &r : backs) {
			if (r.floor == f && r.window && !r.open && r.x1 - r.x0 >= V(2.0)) {
				place_window(r);
			}
		}

		// ------------------------------------------------------------ ceiling lamps
		for (const Room &r : rooms) {
			if (r.floor != f || !r.has_light) {
				continue;
			}
			int cx = (r.x0 + r.x1) / 2;
			int cz = has_back ? Bz + (Ds - Bz) / 2 : Ds / 2;
			uint32_t lamp = glow_night(pack_color(r.light.lerp(Color(1, 1, 1), 0.4f)));
			for (int x = cx - 2; x < cx + 2; ++x) {
				for (int z = cz - 1; z < cz + 1; ++z) {
					g.set(x, yb + FH - 1, z, lamp);
				}
			}
		}

		for (const Room &r : backs) {
			if (r.floor != f || !r.has_light || r.open) {
				continue;
			}
			int cx = (r.x0 + r.x1) / 2;
			int cz = Bz / 2 + 1;
			uint32_t lamp = glow_night(pack_color(r.light.lerp(Color(1, 1, 1), 0.4f)));
			for (int x = cx - 2; x < cx + 2; ++x) {
				for (int z = cz - 1; z < cz + 1; ++z) {
					g.set(x, yb + FH - 1, z, lamp);
				}
			}
		}
		// Dividers inside the back layer.
		for (const Room &r : backs) {
			if (r.floor != f || !r.divider) {
				continue;
			}
			uint32_t c = shade(r.wall, 0.85f);
			int dz0 = Bz / 2 - V(0.5), dz1 = Bz / 2 + V(0.5), top = yb + 2 + V(2.15);
			for (int y = yb + 2; y < yb + FH; ++y) {
				for (int z = 2; z < Bz; ++z) {
					bool hole = r.divider_door && y < top && z >= dz0 && z < dz1;
					bool frame = r.divider_door && y <= top && (z == dz0 - 1 || z == dz1 || y == top);
					g.set(r.x0 - 1, y, z, hole ? 0u : (frame ? white : c));
					g.set(r.x0, y, z, hole ? 0u : (frame ? white : c));
				}
			}
		}

		// ------------------------------------------------------------ side walls
		for (int y = yb; y < yb + FH; ++y) {
			for (int z = 0; z < Ds; ++z) {
				for (int t = 0; t < 2; ++t) {
					put(t, y, z, outer);
					put(W - 1 - t, y, z, outer);
				}
			}
		}
	}

	// ------------------------------------------------------------ partitions
	Array parts = def.get("partitions", Array());
	const int door_z0 = V(-1.95 - z0), door_z1 = V(-0.75 - z0);
	for (int64_t i = 0; i < parts.size(); ++i) {
		Dictionary p = parts[i];
		int f = int(p.get("floor", 0));
		int px = V(double(p.get("x", x0_)) - x0_);
		bool door = bool(p.get("door", true));
		const int pz0 = (has_back && bool(p.get("front_only", false))) ? Bz : 0;
		const Room *r = room_at(f, px - 2);
		uint32_t c = shade(r ? r->wall : outer, 0.85f);
		int yb = f * FH;
		for (int y = yb + 2; y < yb + FH; ++y) {
			for (int z = pz0; z < Ds; ++z) {
				if (door && y < yb + 2 + V(2.15) && z >= door_z0 && z < door_z1) {
					continue;
				}
				bool frame = door && (z == door_z0 - 1 || z == door_z1 || y == yb + 2 + V(2.15)) && y <= yb + 2 + V(2.15);
				put(px - 1, y, z, frame ? white : c);
				put(px, y, z, frame ? white : c);
			}
		}
	}

	// ------------------------------------------------------------ roof
	g.fill(0, top, 0, W, top + 2, Ds, roof);
	for (int x = 0; x < W; ++x) {
		for (int y = top + 2; y < top + 5; ++y) {
			g.set(x, y, 0, roof);
			put(x, y, Ds - 1, roof);
		}
	}
	for (int z = 0; z < Ds; ++z) {
		for (int y = top + 2; y < top + 5; ++y) {
			put(0, y, z, roof);
			put(W - 1, y, z, roof);
		}
	}
	for (int x = 0; x < W; ++x) {
		put(x, top + 1, Ds - 1, cut);
	}
	if (bool(def.get("roof_details", false))) {
		g.fill(V(3), top + 2, 3, V(4.2), top + 2 + V(0.8), 9, shade(roof, 1.15f));
		g.fill(V(11), top + 2, 4, V(11.5), top + 2 + V(0.6), 8, shade(roof, 1.25f));
		for (int y = top + 2; y < top + 2 + V(2.2); ++y) {
			g.set(V(15), y, 5, parse_hex_color("#777777"));
		}
		for (int x = V(14.5); x < V(15.6); ++x) {
			g.set(x, top + 2 + V(1.8), 5, parse_hex_color("#777777"));
		}
	}

	// ------------------------------------------------------------ inner walls
	// The wall in front of the back layer goes into its own mesh per floor so
	// it can dissolve when the player walks into a back room.
	has_back_ = has_back;
	back_z_ = has_back ? z0 + Bz * kVs : 0.0;
	floor_h_ = floor_h;
	if (has_back) {
		std::vector<bool> part_col(size_t(W) * size_t(floors), false);
		for (int64_t i = 0; i < parts.size(); ++i) {
			Dictionary p = parts[i];
			int f = int(p.get("floor", 0));
			int px = V(double(p.get("x", x0_)) - x0_);
			for (int x = px - 1; x <= px; ++x) {
				if (x >= 0 && x < W && f >= 0 && f < floors) {
					part_col[size_t(f) * W + x] = true;
				}
			}
		}
		for (int f = 0; f < floors; ++f) {
			VoxelGrid ig;
			ig.resize(W, H, Ds);
			bool any = false;
			const int yb = f * FH;
			for (int x = 2; x < W - 2; ++x) {
				const Room *b = back_at(f, x);
				if (!b || b->open || part_col[size_t(f) * W + x]) {
					continue;
				}
				for (int y = yb + 2; y < yb + FH; ++y) {
					for (int z = Bz - 1; z <= Bz + 2; ++z) {
						uint32_t c = g.get(x, y, z);
						if (c) {
							ig.set(x, y, z, c);
							g.set(x, y, z, 0u);
							any = true;
						}
					}
				}
			}
			Inner in;
			if (any) {
				in.mesh = memnew(MeshInstance3D);
				add_child(in.mesh);
				in.mesh->set_mesh(build_voxel_mesh(ig, kVs, Vector3(float(x0_), 0.0f, float(z0)), 0.07f));
				in.mat = make_voxel_material();
				in.mesh->set_material_override(in.mat);
			}
			inner_.push_back(in);
		}
	}

	shell_ = memnew(MeshInstance3D);
	add_child(shell_);
	Ref<ArrayMesh> shell_mesh = build_voxel_mesh(g, kVs, Vector3(float(x0_), 0.0f, float(z0)), 0.07f);
	if (shell_mesh->get_surface_count() > 0) {
		shell_mesh->surface_set_material(0, make_voxel_material());
	}
	shell_->set_mesh(shell_mesh);

	// ------------------------------------------------------------ facade
	VoxelGrid fg;
	fg.resize(W, top + 5, 2);
	const uint32_t glass = parse_hex_color("#2c3b4a");
	const bool shopfront = def.has("sign");
	Array doors = def.get("doors", Array());
	auto door_at = [&](int x) {
		for (int64_t i = 0; i < doors.size(); ++i) {
			int dx = V(double(doors[i]) - x0_);
			if (x >= dx - V(0.6) && x < dx + V(0.6)) {
				return true;
			}
		}
		return false;
	};
	const uint32_t sign_c = parse_hex_color(def.get("sign_color", "#ffffff"));
	for (int y = 0; y < top + 5; ++y) {
		int f = std::min(floors - 1, y / FH);
		int ly = y - f * FH;
		for (int x = 0; x < W; ++x) {
			uint32_t c = facade_col;
			bool front = true;
			if (y >= top) {
				c = roof;
			} else if (ly < 2) {
				c = accent;
			} else if (f == 0 && y < V(0.45)) {
				c = shade(facade_col, 0.72f);
			} else if (f == 0 && shopfront && ly >= FH - V(0.8)) {
				c = sign_c;
			} else if (f == 0 && shopfront && ly >= V(0.5) && ly < FH - V(0.95) && x >= 2 && x < W - 2 && !door_at(x)) {
				bool mullion = x % V(1.5) == 0 || ly == V(0.5) || ly == FH - V(0.95) - 1;
				c = mullion ? accent : glow_night(parse_hex_color("#f5e6c0"));
				front = mullion;
			} else {
				int wx = x % 16;
				int wy = ly - (2 + V(0.85));
				bool in_win = wx >= 3 && wx < 13 && wy >= 0 && wy < V(1.4) && x > 2 && x < W - 3 && !(f == 0 && door_at(x));
				if (in_win) {
					bool frame = wx == 3 || wx == 12 || wy == 0 || wy == V(1.4) - 1 || wx == 8;
					if (frame) {
						c = white;
					} else {
						c = rand01(x / 16, y / FH, 7) < 0.4f ? glow_night(parse_hex_color("#ffd59a")) : glass;
						front = false;
					}
				} else if (wy == -1 && wx >= 2 && wx < 14 && x > 2 && x < W - 3 && !(f == 0 && door_at(x))) {
					c = white;
				}
			}
			fg.set(x, y, 0, c);
			if (front) {
				fg.set(x, y, 1, c);
			}
		}
	}
	for (int64_t i = 0; i < doors.size(); ++i) {
		int dx = V(double(doors[i]) - x0_);
		for (int x = dx - V(0.6); x < dx + V(0.6); ++x) {
			for (int y = 2; y < 2 + V(2.25); ++y) {
				bool frame = x == dx - V(0.6) || x == dx + V(0.6) - 1 || y == 2 + V(2.25) - 1;
				fg.set(x, y, 0, frame ? accent : 0u);
				fg.set(x, y, 1, frame ? accent : 0u);
			}
		}
	}
	facade_ = memnew(MeshInstance3D);
	add_child(facade_);
	facade_->set_mesh(build_voxel_mesh(fg, kVs, Vector3(float(x0_), 0.0f, float(z1) - 2 * kVs), 0.05f));
	facade_mat_ = make_voxel_material();
	facade_->set_material_override(facade_mat_);

	if (shopfront) {
		Label3D *sign = memnew(Label3D);
		add_child(sign);
		sign->set_text(def.get("sign", ""));
		sign->set_font_size(40);
		sign->set_pixel_size(0.01f);
		sign->set_outline_size(10);
		sign->set_modulate(Color(1, 1, 1));
		sign->set_outline_modulate(Color(0, 0, 0, 0.7f));
		sign->set_position(Vector3(float((x0_ + x1_) * 0.5), float(floor_h - 0.4), float(z1) + 0.06f));
	}

	// ------------------------------------------------------------ lights
	for (const Room &r : rooms) {
		if (!r.has_light) {
			continue;
		}
		OmniLight3D *l = memnew(OmniLight3D);
		add_child(l);
		float cx = float(x0_) + float(r.x0 + r.x1) * 0.5f * kVs;
		l->set_position(Vector3(cx, float(r.floor * floor_h + floor_h - 0.35), float(z0 + (z1 - z0) * 0.5)));
		l->set_color(r.light);
		l->set_param(Light3D::PARAM_RANGE, std::max(4.5f, float(r.x1 - r.x0) * kVs * 0.85f));
		l->set_param(Light3D::PARAM_ATTENUATION, 1.2f);
		l->set_param(Light3D::PARAM_SIZE, 0.25f);
		l->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, 0.6f);
		l->set_param(Light3D::PARAM_ENERGY, 0.0f);
		l->set_shadow(true);
		l->set_visible(false);
		lights_.push_back(l);
	}
	for (const Room &r : backs) {
		if (!r.has_light || r.open) {
			continue;
		}
		OmniLight3D *l = memnew(OmniLight3D);
		add_child(l);
		float cx = float(x0_) + float(r.x0 + r.x1) * 0.5f * kVs;
		l->set_position(Vector3(cx, float(r.floor * floor_h + floor_h - 0.35), float(z0) + float(Bz) * kVs * 0.5f));
		l->set_color(r.light);
		l->set_param(Light3D::PARAM_RANGE, std::max(3.5f, float(r.x1 - r.x0) * kVs * 0.7f));
		l->set_param(Light3D::PARAM_ATTENUATION, 1.3f);
		l->set_param(Light3D::PARAM_SIZE, 0.2f);
		l->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, 0.4f);
		l->set_param(Light3D::PARAM_ENERGY, 0.0f);
		l->set_shadow(true);
		l->set_visible(false);
		lights_.push_back(l);
	}
	apply_alpha();
}

void CutawayBuilding::set_cutaway(bool open) {
	target_alpha_ = open ? 0.0f : 1.0f;
}

void CutawayBuilding::set_light_level(float level) {
	for (OmniLight3D *l : lights_) {
		l->set_param(Light3D::PARAM_ENERGY, 1.6f * level);
		l->set_visible(level > 0.02f);
	}
}

void CutawayBuilding::set_light_shadows(bool enabled) {
	for (OmniLight3D *l : lights_) {
		l->set_shadow(enabled);
	}
}

void CutawayBuilding::set_inner_open(int floor) {
	for (size_t f = 0; f < inner_.size(); ++f) {
		inner_[f].target = int(f) == floor ? 0.0f : 1.0f;
	}
}

void CutawayBuilding::_process(double delta) {
	for (Inner &in : inner_) {
		if (!in.mesh || std::fabs(in.alpha - in.target) < 0.001f) {
			continue;
		}
		float step = float(delta) * 3.0f;
		in.alpha = in.alpha < in.target ? std::min(in.target, in.alpha + step) : std::max(in.target, in.alpha - step);
		in.mat->set_shader_parameter("fade", std::max(0.12f, in.alpha));
		in.mesh->set_cast_shadows_setting(in.alpha > 0.5f ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	}
	if (std::fabs(alpha_ - target_alpha_) < 0.001f) {
		return;
	}
	float step = float(delta) * 2.5f;
	alpha_ = alpha_ < target_alpha_ ? std::min(target_alpha_, alpha_ + step) : std::max(target_alpha_, alpha_ - step);
	apply_alpha();
}

void CutawayBuilding::apply_alpha() {
	if (!facade_ || facade_mat_.is_null()) {
		return;
	}
	facade_->set_visible(alpha_ > 0.01f);
	facade_mat_->set_shader_parameter("fade", alpha_);
	facade_->set_cast_shadows_setting(alpha_ > 0.5f ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
}

} // namespace godot
