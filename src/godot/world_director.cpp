#include "world_director.hpp"

#include "voxel_model.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
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

Ref<ArrayMesh> WorldDirector::prop_mesh(const String &name) {
	auto it = mesh_cache_.find(name);
	if (it != mesh_cache_.end()) {
		return it->second;
	}
	Ref<ArrayMesh> m = VoxelModel::build_from_file("res://assets/vox/" + name + ".vox", 0.0625f);
	mesh_cache_[name] = m;
	return m;
}

void WorldDirector::build_terrain() {
	const float vs = 0.25f;
	const float zmin = -9.0f, zmax = 26.0f;
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
				if (z < -3.5f) {
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
	}
}

void WorldDirector::build_skyline() {
	const float vs = 0.5f;
	Array streets = world_.get("streets", Array());
	const uint32_t lit = glow_night(parse_hex_color("#b9a47c"));
	const uint32_t dark = parse_hex_color("#4c5664");
	for (int64_t si = 0; si < streets.size(); ++si) {
		Dictionary st = streets[si];
		int x0 = int(double(st.get("x0", 0))) - 40;
		int x1 = int(double(st.get("x1", 0))) + 40;
		const int W = int((x1 - x0) / vs);
		VoxelGrid g;
		g.resize(W, 70, 8);
		int x = 0;
		int k = int(si) * 97;
		while (x < W) {
			int w = 12 + int(hash01(k, 1) * 22);
			int h = 18 + int(hash01(k, 2) * 46);
			int depth_off = int(hash01(k, 3) * 4);
			float tone = 0.75f + 0.25f * hash01(k, 4);
			uint32_t base = shade(parse_hex_color("#8d939b"), tone);
			for (int bx = x; bx < std::min(x + w, W); ++bx) {
				for (int y = 0; y < h; ++y) {
					for (int z = depth_off; z < 8; ++z) {
						uint32_t c = base;
						bool window = z == 7 && (bx - x) % 3 == 1 && y % 4 == 2 && y < h - 2 && bx < x + w - 1;
						if (window) {
							c = hash01(bx * 31 + k, y) < 0.3f ? lit : dark;
						}
						g.set(bx, y, z, c);
					}
				}
			}
			x += w + 2 + int(hash01(k, 5) * 8);
			++k;
		}
		MeshInstance3D *mi = mesh_node(this, build_voxel_mesh(g, vs, Vector3(float(x0), -0.5f, -22.0f), 0.03f));
		mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
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
		Ref<ArrayMesh> mesh = prop_mesh(name);
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
		a.bubble->set_visible(a.has_action && same_place && a.alpha > 0.5f);
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
