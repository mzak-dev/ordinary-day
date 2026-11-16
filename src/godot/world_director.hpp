#pragma once

#include "animated_door.hpp"
#include "cutaway_building.hpp"
#include "game_sim.hpp"

#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/sprite3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <map>
#include <vector>

namespace godot {

// Builds terrain, skyline, buildings, props, doors, street lights, the bus and
// NPC sprites from world.json, keeps them in sync with the simulation and
// answers geometry questions for the player controller.
class WorldDirector : public Node3D {
	GDCLASS(WorldDirector, Node3D)

public:
	void _ready() override;
	void _process(double delta) override;

	void set_sim_path(const NodePath &p) { sim_path_ = p; }
	NodePath get_sim_path() const { return sim_path_; }
	void set_player_path(const NodePath &p) { player_path_ = p; }
	NodePath get_player_path() const { return player_path_; }

	Dictionary get_segment(const String &id) const;
	Dictionary get_link_def(const String &id) const;
	// Link ends on `seg` within `radius` of x: [{id, from_a, key, label, ok, why, x}].
	Array find_links(const String &seg, double x, double radius = 0.8) const;
	void set_door_open(const String &link_id, bool open);
	Node3D *get_bus() const { return bus_; }
	// {visible: bool, pos: Vector3 above the head} for HUD prompts.
	Dictionary get_npc_anchor(const String &id) const;
	void refresh();
	// 0 = low, 1 = medium, 2 = high
	void set_quality(int quality);

	static float character_pixel_size() { return 0.0625f; }
	static Ref<Texture2D> load_sprite(const String &name);
	static Sprite3D *make_character_sprite(const String &name);

protected:
	static void _bind_methods();

private:
	struct NpcActor {
		String id;
		Node3D *root = nullptr;
		Sprite3D *sprite = nullptr;
		Label3D *label = nullptr;
		Label3D *bubble = nullptr;
		double phase = 0;
		float alpha = 0.0f;
		bool wanted = false;
		bool has_action = false;
	};

	NodePath sim_path_ = NodePath("../GameSim");
	NodePath player_path_ = NodePath("../Player");
	GameSim *sim_ = nullptr;
	Dictionary world_;
	std::map<String, Dictionary> segments_;
	std::map<String, Dictionary> links_;
	std::map<String, Ref<ArrayMesh>> mesh_cache_;
	std::map<String, AnimatedDoor *> doors_;
	std::vector<CutawayBuilding *> buildings_;
	std::vector<SpotLight3D *> street_lights_;
	std::vector<NpcActor> npcs_;
	Node3D *bus_ = nullptr;
	double time_ = 0;

	void build_terrain();
	void build_skyline();
	void build_buildings();
	void build_props();
	void build_doors();
	void build_npcs();
	void on_state_changed();
	Ref<ArrayMesh> prop_mesh(const String &name);
};

} // namespace godot
