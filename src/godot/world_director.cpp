#include "world_director.hpp"

#include "voxel_model.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cmath>

namespace godot {

namespace {

uint32_t hash_u32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

float hash01(int a, int b = 0) {
	return float(hash_u32(uint32_t(a) * 73856093u ^ uint32_t(b) * 19349663u) & 0xFFFF) / 65535.0f;
}

bool tower_window(int lx, int y) {
	return lx % 4 == 1 && y % 6 >= 2 && y % 6 < 4;
}

float smooth(float e0, float e1, float x) {
	float t = std::max(0.0f, std::min(1.0f, (x - e0) / (e1 - e0)));
	return t * t * (3.0f - 2.0f * t);
}

MeshInstance3D *mesh_node(Node *parent, const Ref<ArrayMesh> &mesh, bool wet = false) {
	MeshInstance3D *mi = memnew(MeshInstance3D);
	parent->add_child(mi);
	if (mesh.is_valid() && mesh->get_surface_count() > 0) {
		Ref<ShaderMaterial> mat = make_voxel_material();
		if (wet) {
			mat->set_shader_parameter("wet_surface", 1.0f);
		}
		mesh->surface_set_material(0, mat);
	}
	mi->set_mesh(mesh);
	return mi;
}

} // namespace

void WorldDirector::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sim_path", "p"), &WorldDirector::set_sim_path);
	ClassDB::bind_method(D_METHOD("get_sim_path"), &WorldDirector::get_sim_path);
	ClassDB::bind_method(D_METHOD("set_player_path", "p"), &WorldDirector::set_player_path);
	ClassDB::bind_method(D_METHOD("get_player_path"), &WorldDirector::get_player_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sim_path"), "set_sim_path", "get_sim_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player_path"), "set_player_path", "get_player_path");

	ClassDB::bind_method(D_METHOD("get_segment", "id"), &WorldDirector::get_segment);
	ClassDB::bind_method(D_METHOD("get_link_def", "id"), &WorldDirector::get_link_def);
	ClassDB::bind_method(D_METHOD("find_links", "seg", "x", "radius"), &WorldDirector::find_links, DEFVAL(0.8));
	ClassDB::bind_method(D_METHOD("set_door_open", "link_id", "open"), &WorldDirector::set_door_open);
	ClassDB::bind_method(D_METHOD("refresh"), &WorldDirector::refresh);
	ClassDB::bind_method(D_METHOD("set_quality", "quality"), &WorldDirector::set_quality);
	ClassDB::bind_method(D_METHOD("get_npc_anchor", "id"), &WorldDirector::get_npc_anchor);
	ClassDB::bind_method(D_METHOD("on_state_changed"), &WorldDirector::on_state_changed);
}

Ref<Texture2D> WorldDirector::load_sprite(const String &name) {
	String path = "res://assets/sprites/" + name + ".png";
	if (!ResourceLoader::get_singleton()->exists(path)) {
		return Ref<Texture2D>();
	}
	return ResourceLoader::get_singleton()->load(path);
}

Sprite3D *WorldDirector::make_character_sprite(const String &name) {
	Sprite3D *s = memnew(Sprite3D);
	Ref<Texture2D> tex = load_sprite(name);
	s->set_texture(tex);
	s->set_hframes(tex.is_valid() ? std::max(1, int(tex->get_width() / 16)) : 1);
	s->set_pixel_size(character_pixel_size());
	s->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST);
	s->set_alpha_cut_mode(SpriteBase3D::ALPHA_CUT_OPAQUE_PREPASS);
	s->set_draw_flag(SpriteBase3D::FLAG_SHADED, true);
	s->set_draw_flag(SpriteBase3D::FLAG_DOUBLE_SIDED, true);
	s->set_position(Vector3(0, 1.0f, 0));
	return s;
}

void WorldDirector::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	sim_ = Object::cast_to<GameSim>(get_node_or_null(sim_path_));
	if (!sim_) {
		ERR_PRINT("WorldDirector: GameSim not found");
		return;
	}
	world_ = sim_->get_world();
	Array segs = world_.get("segments", Array());
	for (int64_t i = 0; i < segs.size(); ++i) {
		Dictionary s = segs[i];
		segments_[String(s.get("id", ""))] = s;
	}
	Array links = world_.get("links", Array());
	for (int64_t i = 0; i < links.size(); ++i) {
		Dictionary l = links[i];
		links_[String(l.get("id", ""))] = l;
	}
	build_terrain();
	build_skyline();
	build_buildings();
	build_props();
	build_doors();
	build_npcs();
	sim_->connect("state_changed", Callable(this, "on_state_changed"));
}

Ref<ArrayMesh> WorldDirector::prop_mesh(const String &name, float voxel_size) {
	// Hand-made models drawn on the 5 cm art grid set "vs" in world.json;
	// the generated placeholders stay at 1/16 m.
	String key = name + String("@") + String::num(voxel_size, 4);
	auto it = mesh_cache_.find(key);
	if (it != mesh_cache_.end()) {
		return it->second;
	}
	Ref<ArrayMesh> m = VoxelModel::build_from_file("res://assets/vox/" + name + ".vox", voxel_size);
	mesh_cache_[key] = m;
	return m;
}

void WorldDirector::build_terrain() {
	const float vs = 0.25f;
	const float zmin = -36.0f, zmax = 26.0f;
	Array streets = world_.get("streets", Array());
	Array buildings = world_.get("buildings", Array());
	Array areas = world_.get("areas", Array());
	auto in_building = [&](float x) {
		for (int64_t i = 0; i < buildings.size(); ++i) {
			Dictionary b = buildings[i];
			if (x >= float(double(b.get("x0", 0))) && x < float(double(b.get("x1", 0)))) {
				return true;
			}
		}
		return false;
	};
	auto in_park = [&](float x) {
		for (int64_t i = 0; i < areas.size(); ++i) {
			Dictionary a = areas[i];
			if (String(a.get("loc", "")) == "park" && x >= float(double(a.get("x0", 0))) && x < float(double(a.get("x1", 0)))) {
				return true;
			}
		}
		return false;
	};
	const uint32_t grass[3] = { parse_hex_color("#5d7d45"), parse_hex_color("#53723e"), parse_hex_color("#668a4b") };
	const uint32_t dirt = parse_hex_color("#7a6a55");
	const uint32_t paving = parse_hex_color("#8a857b");
	const uint32_t walk1 = parse_hex_color("#b3ada2");
	const uint32_t walk2 = parse_hex_color("#a7a196");
	const uint32_t gravel = parse_hex_color("#bca98a");
	const uint32_t curb = parse_hex_color("#8e8c88");
	const uint32_t asphalt = parse_hex_color("#45464b");
	const uint32_t line = parse_hex_color("#dcdcd2");

	for (int64_t si = 0; si < streets.size(); ++si) {
		Dictionary st = streets[si];
		float x0 = float(double(st.get("x0", 0))), x1 = float(double(st.get("x1", 0)));
		VoxelGrid g;
		int W = int((x1 - x0) / vs);
		int D = int((zmax - zmin) / vs);
		g.resize(W, 3, D);
		for (int ix = 0; ix < W; ++ix) {
			float x = x0 + (float(ix) + 0.5f) * vs;
			bool park = in_park(x);
			bool built = in_building(x);
			for (int iz = 0; iz < D; ++iz) {
				float z = zmin + (float(iz) + 0.5f) * vs;
				float n = hash01(ix + int(si) * 10007, iz);
				uint32_t c;
				bool tuft = false, raised = false;
				auto lawn = [&]() {
					return n < 0.08f ? dirt : grass[int(n * 37.0f) % 3];
				};
				if (z < -5.0f) {
					c = lawn();
				} else if (z < 0.0f) {
					c = built ? paving : lawn();
				} else if (z < 3.0f) {
					if (park) {
						c = shade(gravel, 0.93f + 0.1f * n);
					} else {
						c = (((ix / 2) + (iz / 2)) % 2) ? walk1 : walk2;
						if (ix % 2 == 0 && iz % 2 == 0) {
							c = shade(c, 0.9f);
						}
					}
				} else if (z < 3.5f) {
					c = curb;
					raised = true;
				} else if (z < 8.0f) {
					bool dash = std::fabs(z - 5.75f) < 0.13f && (ix / 6) % 2 == 0;
					c = dash ? line : shade(asphalt, 0.95f + 0.08f * n);
					if (n > 0.985f) {
						c = shade(asphalt, 0.7f); // cracks and patches
					}
				} else if (z < 8.5f) {
					c = curb;
					raised = true;
				} else if (z < 11.0f) {
					c = (((ix / 2) + (iz / 2)) % 2) ? walk1 : walk2;
				} else {
					c = lawn();
				}
				g.set(ix, 0, iz, shade(c, 0.8f));
				g.set(ix, 1, iz, c);
				if (raised) {
					g.set(ix, 2, iz, shade(c, 1.05f));
				} else if (tuft) {
					g.set(ix, 2, iz, shade(c, 1.15f));
				}
			}
		}
		mesh_node(this, build_voxel_mesh(g, vs, Vector3(x0, -2 * vs, zmin), 0.05f), true);

		// Distant fields under the far skyline layers.
		Ref<PlaneMesh> plane;
		plane.instantiate();
		plane->set_size(Vector2(x1 - x0 + 600.0f, 260.0f));
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_albedo(Color::html("#4a6438"));
		mat->set_roughness(1.0f);
		plane->set_material(mat);
		MeshInstance3D *far = memnew(MeshInstance3D);
		add_child(far);
		far->set_mesh(plane);
		far->set_position(Vector3((x0 + x1) * 0.5f, -0.3f, zmin - 128.0f));
		far->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	}
}

void WorldDirector::build_skyline() {
	Array streets = world_.get("streets", Array());
	const uint32_t lit = glow_night(parse_hex_color("#e8c98c"));
	const uint32_t glass = parse_hex_color("#3d4a57");
	const uint32_t panels[6] = { parse_hex_color("#d8d2c4"), parse_hex_color("#c9ccc6"), parse_hex_color("#e3d7b5"),
		parse_hex_color("#cfd8de"), parse_hex_color("#d9c6bd"), parse_hex_color("#bfc5bd") };
	const uint32_t stripes[5] = { parse_hex_color("#e07a4f"), parse_hex_color("#5b8fb9"), parse_hex_color("#e7b94b"),
		parse_hex_color("#7aa66b"), parse_hex_color("#b85c6e") };

	for (int64_t si = 0; si < streets.size(); ++si) {
		Dictionary st = streets[si];
		const int sx0 = int(double(st.get("x0", 0))) - 70;
		const int sx1 = int(double(st.get("x1", 0))) + 70;

		// ---- near layer: blocks of flats (wielka płyta) with balconies -------------
		{
			const float vs = 0.25f;
			const int W = int((sx1 - sx0) / vs);
			VoxelGrid g;
			g.resize(W, 136, 28);
			int k = int(si) * 131;
			int x = int(hash01(k, 7) * 20);
			while (x < W) {
				const int floors = 4 + int(hash01(k, 1) * 7);
				const int fh = 12; // 3 m
				const int h = floors * fh + 2;
				const int w = 72 + int(hash01(k, 2) * 72);
				const int back = int(hash01(k, 3) * 6);
				const uint32_t base = panels[int(hash01(k, 4) * 6) % 6];
				const uint32_t stripe = stripes[int(hash01(k, 5) * 5) % 5];
				const int stripe_x = int(hash01(k, 6) * 4) * 12 + 6;
				for (int bx = x; bx < std::min(x + w, W); ++bx) {
					int lx = bx - x;
					for (int y = 0; y < h && y < g.sy; ++y) {
						int ly = y % fh;
						int fl = y / fh;
						for (int z = back; z < 24; ++z) {
							g.set(bx, y, z, y < 2 ? shade(base, 0.7f) : base);
						}
						uint32_t c = base;
						bool col_band = (lx / 12) % 2 == 0;
						if (lx >= stripe_x && lx < stripe_x + 8) {
							c = stripe;
						}
						if (ly == 0) {
							c = shade(base, 0.85f); // panel joint
						}
						bool win = (lx % 12) >= 3 && (lx % 12) < 9 && ly >= 4 && ly < 9 && y > 3 && y < h - 2 && lx > 2 && lx < w - 3;
						if (win) {
							c = hash01(bx / 12 + k * 7, fl) < 0.33f ? lit : glass;
						}
						g.set(bx, y, 23, c);
						// Balconies on every other window column.
						if (col_band && fl > 0 && y < h - 2 && lx > 2 && lx < w - 3) {
							if (ly == 1 && (lx % 12) >= 2 && (lx % 12) < 10) {
								g.set(bx, y, 24, shade(base, 0.8f));
								g.set(bx, y, 25, shade(base, 0.8f));
							}
							if (ly >= 2 && ly < 5 && (lx % 12) >= 2 && (lx % 12) < 10) {
								g.set(bx, y, 25, ((lx % 12) == 2 || (lx % 12) == 9 || ly == 4) ? stripe : shade(stripe, 0.75f));
							}
						}
					}
					if (h + 4 < g.sy) {
						// roof clutter: stairwell boxes and antennas
						if (lx % 40 >= 30 && lx % 40 < 38) {
							for (int y = h; y < h + 6; ++y) {
								for (int z = back + 4; z < back + 12; ++z) {
									g.set(bx, y, z, shade(base, 0.9f));
								}
							}
						}
						if (lx % 53 == 17) {
							for (int y = h; y < std::min(g.sy, h + 14); ++y) {
								g.set(bx, y, back + 8, parse_hex_color("#6b6b6b"));
							}
						}
					}
				}
				x += w + 12 + int(hash01(k, 8) * 40);
				++k;
			}
			MeshInstance3D *mi = mesh_node(this, build_voxel_mesh(g, vs, Vector3(float(sx0), -0.25f, -30.0f), 0.05f));
			mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		}

		// ---- middle layer: towers, a heat-plant chimney, a church spire ------------
		{
			const float vs = 0.5f;
			const int W = int((sx1 - sx0 + 80) / vs);
			VoxelGrid g;
			g.resize(W, 190, 10);
			int k = int(si) * 977;
			int x = 0;
			while (x < W) {
				float r = hash01(k, 1);
				int w = 16 + int(hash01(k, 2) * 30);
				int h = 30 + int(hash01(k, 3) * 70);
				uint32_t base = shade(parse_hex_color("#9aa0a8"), 0.8f + 0.25f * hash01(k, 4));
				if (r < 0.12f) {
					// chimney with red/white bands
					w = 6;
					h = 150 + int(hash01(k, 5) * 30);
					for (int bx = x; bx < std::min(W, x + w); ++bx) {
						for (int y = 0; y < h && y < g.sy; ++y) {
							uint32_t c = ((y / 14) % 2 && y > h - 70) ? parse_hex_color("#c0392b") : parse_hex_color("#e8e4dc");
							if (y >= h - 2) {
								c = glow_night(parse_hex_color("#ff3b30"));
							}
							for (int z = 2; z < 8; ++z) {
								g.set(bx, y, z, c);
							}
						}
					}
				} else if (r < 0.2f) {
					// church tower with a spire
					w = 12;
					int body = 60;
					for (int bx = x; bx < std::min(W, x + w); ++bx) {
						int dx = std::abs(bx - x - w / 2);
						for (int y = 0; y < body + 40 && y < g.sy; ++y) {
							bool spire = y >= body && dx <= (body + 40 - y) / 7;
							if (y < body || spire) {
								for (int z = 2; z < 9; ++z) {
									g.set(bx, y, z, y < body ? parse_hex_color("#a0705a") : parse_hex_color("#4f7a6a"));
								}
							}
						}
					}
				} else {
					for (int bx = x; bx < std::min(W, x + w); ++bx) {
						for (int y = 0; y < h && y < g.sy; ++y) {
							for (int z = 0; z < 10; ++z) {
								g.set(bx, y, z, base);
							}
							bool win = tower_window(bx - x, y);
							if (win && y < h - 2) {
								g.set(bx, y, 9, hash01(bx * 13 + k, y / 3) < 0.25f ? lit : shade(base, 0.7f));
							}
						}
					}
				}
				x += w + 4 + int(hash01(k, 6) * 24);
				++k;
			}
			MeshInstance3D *mi = mesh_node(this, build_voxel_mesh(g, vs, Vector3(float(sx0 - 40), -0.5f, -70.0f), 0.03f));
			mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		}

		// ---- far layer: tree line on gentle hills -------------------------------
		{
			const float vs = 1.0f;
			const int W = sx1 - sx0 + 200;
			VoxelGrid g;
			g.resize(W, 40, 6);
			for (int bx = 0; bx < W; ++bx) {
				float hill = 6.0f + 6.0f * std::sin(float(bx) * 0.021f + float(si)) + 3.0f * std::sin(float(bx) * 0.07f);
				int h = int(hill + hash01(bx, int(si)) * 3.0f);
				for (int y = 0; y < h; ++y) {
					for (int z = 0; z < 6; ++z) {
						g.set(bx, y, z, shade(parse_hex_color("#3f5a40"), 0.85f + 0.2f * hash01(bx * 5 + y, z)));
					}
				}
			}
			MeshInstance3D *mi = mesh_node(this, build_voxel_mesh(g, vs, Vector3(float(sx0 - 100), -1.0f, -150.0f), 0.04f));
			mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		}
	}
}

void WorldDirector::build_buildings() {
	Array defs = world_.get("buildings", Array());
	for (int64_t i = 0; i < defs.size(); ++i) {
		CutawayBuilding *b = memnew(CutawayBuilding);
		add_child(b);
		b->configure(defs[i]);
		buildings_.push_back(b);
	}
}

void WorldDirector::build_props() {
	Array props = world_.get("props", Array());
	for (int64_t i = 0; i < props.size(); ++i) {
		Dictionary p = props[i];
		String name = p.get("vox", "");
		Ref<ArrayMesh> mesh = prop_mesh(name, float(double(p.get("vs", 0.0625))));
		if (mesh.is_null()) {
			continue;
		}
		Vector3 pos(float(double(p.get("x", 0))), float(double(p.get("y", 0))), float(double(p.get("z", 0))));
		MeshInstance3D *mi = memnew(MeshInstance3D);
		add_child(mi);
		mi->set_mesh(mesh);
		mi->set_position(pos);
		if (name == "lamp") {
			SpotLight3D *l = memnew(SpotLight3D);
			add_child(l);
			l->set_position(pos + Vector3(-0.25f, 4.05f, 0.0f));
			l->set_rotation_degrees(Vector3(-90, 0, 0));
			l->set_color(Color(1.0f, 0.78f, 0.5f));
			l->set_param(Light3D::PARAM_RANGE, 7.5f);
			l->set_param(Light3D::PARAM_SPOT_ANGLE, 58.0f);
			l->set_param(Light3D::PARAM_SPOT_ATTENUATION, 0.8f);
			l->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, 3.0f);
			l->set_param(Light3D::PARAM_ENERGY, 0.0f);
			l->set_visible(false);
			street_lights_.push_back(l);
		}
	}
	bus_ = memnew(Node3D);
	add_child(bus_);
	MeshInstance3D *bus_mesh = memnew(MeshInstance3D);
	bus_->add_child(bus_mesh);
	bus_mesh->set_mesh(prop_mesh("bus"));
	bus_->set_visible(false);
}

void WorldDirector::build_doors() {
	for (auto &kv : links_) {
		Dictionary l = kv.second;
		if (String(l.get("anim", "")) != "door" || !l.has("door")) {
			continue;
		}
		AnimatedDoor *d = memnew(AnimatedDoor);
		add_child(d);
		d->configure(l["door"]);
		doors_[kv.first] = d;
	}
}

void WorldDirector::set_door_open(const String &link_id, bool open) {
	auto it = doors_.find(link_id);
	if (it != doors_.end()) {
		it->second->set_open(open);
	}
}

void WorldDirector::build_npcs() {
	Dictionary npcs = world_.get("npcs", Dictionary());
	Array keys = npcs.keys();
	for (int64_t i = 0; i < keys.size(); ++i) {
		Dictionary n = npcs[keys[i]];
		NpcActor a;
		a.id = keys[i];
		a.root = memnew(Node3D);
		add_child(a.root);
		a.sprite = make_character_sprite(n.get("sprite", a.id));
		a.root->add_child(a.sprite);
		a.label = memnew(Label3D);
		a.root->add_child(a.label);
		a.label->set_font_size(28);
		a.label->set_pixel_size(0.008f);
		a.label->set_outline_size(8);
		a.label->set_position(Vector3(0, 2.3f, 0.05f));
		a.label->set_modulate(Color(1, 1, 1, 0.92f));
		a.bubble = memnew(Label3D);
		a.root->add_child(a.bubble);
		a.bubble->set_text("!");
		a.bubble->set_font_size(64);
		a.bubble->set_pixel_size(0.01f);
		a.bubble->set_outline_size(14);
		a.bubble->set_modulate(Color(1.0f, 0.85f, 0.3f));
		a.bubble->set_outline_modulate(Color(0.1f, 0.08f, 0.05f));
		a.bubble->set_position(Vector3(0, 3.2f, 0.05f));
		a.bubble->set_visible(false);
		a.phase = hash01(int(i), 9) * 3.0;
		a.root->set_visible(false);
		npcs_.push_back(a);
	}
}

Dictionary WorldDirector::get_segment(const String &id) const {
	auto it = segments_.find(id);
	return it == segments_.end() ? Dictionary() : it->second;
}

Dictionary WorldDirector::get_link_def(const String &id) const {
	auto it = links_.find(id);
	return it == links_.end() ? Dictionary() : it->second;
}

Array WorldDirector::find_links(const String &seg, double x, double radius) const {
	Array out;
	if (!sim_) {
		return out;
	}
	for (const auto &kv : links_) {
		const Dictionary &l = kv.second;
		for (int side = 0; side < 2; ++side) {
			Dictionary end = l.get(side == 0 ? "a" : "b", Dictionary());
			if (String(end.get("seg", "")) != seg || std::fabs(double(end.get("x", 0.0)) - x) > radius) {
				continue;
			}
			bool from_a = side == 0;
			Dictionary info = sim_->link_info(kv.first, from_a);
			if (info.is_empty()) {
				continue;
			}
			info["id"] = kv.first;
			info["from_a"] = from_a;
			info["x"] = end.get("x", 0.0);
			out.push_back(info);
		}
	}
	return out;
}

Dictionary WorldDirector::get_npc_anchor(const String &id) const {
	Dictionary d;
	d["visible"] = false;
	for (const NpcActor &a : npcs_) {
		if (a.id == id) {
			d["visible"] = a.root->is_visible() && a.wanted && a.alpha > 0.5f;
			d["pos"] = a.root->get_global_position() + Vector3(0, 2.2f, 0);
		}
	}
	return d;
}

void WorldDirector::set_quality(int quality) {
	for (CutawayBuilding *b : buildings_) {
		b->set_light_shadows(quality >= 2);
	}
	for (SpotLight3D *l : street_lights_) {
		l->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, quality >= 1 ? 3.0f : 0.0f);
	}
}

void WorldDirector::refresh() {
	on_state_changed();
}

void WorldDirector::on_state_changed() {
	if (!sim_) {
		return;
	}
	Dictionary clock = sim_->get_clock();
	float hour = float(double(clock.get("hour", 12.0)));
	float evening = smooth(16.5f, 19.5f, hour);
	float morning = 1.0f - smooth(6.5f, 8.0f, hour);
	float light = std::max(evening, morning);
	for (CutawayBuilding *b : buildings_) {
		String open = b->get_open_loc();
		bool is_open = open == "always" || sim_->is_location_open(open);
		b->set_cutaway(is_open);
		b->set_light_level(is_open ? std::max(light, 0.25f) : 0.0f);
	}
	float street = smooth(18.5f, 20.0f, hour) + (1.0f - smooth(6.0f, 7.0f, hour));
	street = std::min(1.0f, street);
	for (SpotLight3D *l : street_lights_) {
		l->set_param(Light3D::PARAM_ENERGY, 6.0f * street);
		l->set_visible(street > 0.02f);
	}

	// NPCs with something to do get a "!" while you're in the same place.
	std::map<String, bool> has_action;
	Array actions = sim_->get_actions();
	for (int64_t i = 0; i < actions.size(); ++i) {
		Dictionary a = actions[i];
		if (!bool(a.get("enabled", false))) {
			continue;
		}
		PackedStringArray ids = a.get("npcs", PackedStringArray());
		for (int64_t k = 0; k < ids.size(); ++k) {
			has_action[ids[k]] = true;
		}
	}

	Dictionary locations = world_.get("locations", Dictionary());
	Array on_stage = sim_->get_npcs();
	for (NpcActor &a : npcs_) {
		a.wanted = false;
	}
	for (int64_t i = 0; i < on_stage.size(); ++i) {
		Dictionary n = on_stage[i];
		String id = n.get("id", "");
		for (NpcActor &a : npcs_) {
			if (a.id != id) {
				continue;
			}
			Dictionary loc = locations.get(n.get("loc", ""), Dictionary());
			Dictionary spawn = loc.get("spawn", Dictionary());
			Dictionary seg = get_segment(spawn.get("seg", ""));
			if (seg.is_empty()) {
				continue;
			}
			double x = bool(n.get("has_x", false)) ? double(n.get("x", 0.0)) : double(spawn.get("x", 0.0));
			Vector3 target(float(x), float(double(seg.get("y", 0.0))), float(double(seg.get("z", 0.0))) - 0.2f);
			if (!a.root->is_visible() || a.root->get_position().distance_to(target) > 0.01f) {
				if (a.alpha > 0.05f && a.root->get_position().distance_to(target) > 0.01f) {
					a.alpha = 0.0f; // moved somewhere else: pop out and fade in at the new place
				}
				a.root->set_position(target);
			}
			String name = n.get("name", "");
			String activity = n.get("activity", "");
			a.label->set_text(activity.is_empty() ? name : name + "\n" + activity);
			a.wanted = true;
			a.has_action = has_action.count(id) > 0;
			a.root->set_visible(true);
		}
	}
}

void WorldDirector::_process(double delta) {
	if (!sim_) {
		return;
	}
	time_ += delta;
	Node3D *player = Object::cast_to<Node3D>(get_node_or_null(player_path_));
	Vector3 pp = player ? player->get_global_position() : Vector3();
	for (NpcActor &a : npcs_) {
		float target = a.wanted ? 1.0f : 0.0f;
		a.alpha += (target - a.alpha) * std::min(1.0f, float(delta) * 4.0f);
		if (!a.wanted && a.alpha < 0.02f) {
			a.root->set_visible(false);
			continue;
		}
		if (!a.root->is_visible()) {
			continue;
		}
		a.sprite->set_modulate(Color(1, 1, 1, a.alpha));
		Vector3 p = a.root->get_global_position();
		float dx = pp.x - p.x;
		bool near = std::fabs(dx) < 4.0f && std::fabs(pp.y - p.y) < 1.5f;
		a.label->set_visible(near && a.alpha > 0.5f);
		if (near) {
			a.sprite->set_flip_h(dx < 0);
		}
		a.sprite->set_frame(std::fmod(time_ + a.phase, 1.4) < 0.7 ? 0 : 2);
		bool same_place = std::fabs(pp.y - p.y) < 1.5f && std::fabs(dx) < 14.0f;
		a.bubble->set_visible(a.has_action && same_place && !near && a.alpha > 0.5f);
		a.bubble->set_position(Vector3(0, 3.2f + 0.08f * float(std::sin(time_ * 4.0 + a.phase)), 0.05f));
	}
	if (player) {
		for (CutawayBuilding *b : buildings_) {
			bool inside = pp.z < 0.5f && pp.x >= b->get_x0() && pp.x <= b->get_x1();
			if (inside && !b->is_cutaway()) {
				b->set_cutaway(true);
			}
		}
	}
}

} // namespace godot
