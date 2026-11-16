#pragma once

#include "game_sim.hpp"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/cpu_particles3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/fog_material.hpp>
#include <godot_cpp/classes/fog_volume.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/world_environment.hpp>

namespace godot {

// Turns time of day and the hidden "warmth" stat into light, colour and
// weather. Low warmth: washed-out, cold, grey. High warmth: saturated, golden.
// Values glide, so a 3-hour action visibly sweeps the light across the room.
class ToneDirector : public Node {
	GDCLASS(ToneDirector, Node)

public:
	void _ready() override;
	void _process(double delta) override;

	void set_sim_path(const NodePath &p) { sim_path_ = p; }
	NodePath get_sim_path() const { return sim_path_; }
	void set_sun_path(const NodePath &p) { sun_path_ = p; }
	NodePath get_sun_path() const { return sun_path_; }
	void set_env_path(const NodePath &p) { env_path_ = p; }
	NodePath get_env_path() const { return env_path_; }
	void set_camera_path(const NodePath &p) { camera_path_ = p; }
	NodePath get_camera_path() const { return camera_path_; }

	float get_shown_warmth() const { return warmth_; }

protected:
	static void _bind_methods();

private:
	NodePath sim_path_ = NodePath("../GameSim");
	NodePath sun_path_ = NodePath("../Sun");
	NodePath env_path_ = NodePath("../WorldEnvironment");
	NodePath camera_path_ = NodePath("../Camera3D");
	GameSim *sim_ = nullptr;
	DirectionalLight3D *sun_ = nullptr;
	WorldEnvironment *env_ = nullptr;
	Camera3D *camera_ = nullptr;
	CPUParticles3D *rain_ = nullptr;
	CPUParticles3D *dust_ = nullptr;
	FogVolume *haze_ = nullptr;
	Ref<FogMaterial> haze_mat_;

	float hour_ = 7.0f;
	float warmth_ = 0.15f;
	float rain_amount_ = 0.0f;
	float wet_ = 0.0f;
	bool init_ = false;
};

} // namespace godot
