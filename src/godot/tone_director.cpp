#include "tone_director.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cmath>

namespace godot {

namespace {

struct LightKey {
	float hour;
	Color sun;
	float energy;
	Color sky;
	Color ambient;
	float ambient_energy;
	float elevation; // degrees
};

// Keyframes for one day. Interpolated linearly by hour.
const LightKey kKeys[] = {
	{ 0.0f, Color(0.35f, 0.42f, 0.7f), 0.08f, Color(0.05f, 0.07f, 0.13f), Color(0.3f, 0.35f, 0.55f), 0.35f, 20 },
	{ 6.0f, Color(0.5f, 0.5f, 0.75f), 0.15f, Color(0.18f, 0.2f, 0.32f), Color(0.4f, 0.42f, 0.6f), 0.4f, 8 },
	{ 7.5f, Color(1.0f, 0.78f, 0.6f), 0.7f, Color(0.85f, 0.7f, 0.62f), Color(0.65f, 0.6f, 0.62f), 0.55f, 14 },
	{ 10.0f, Color(1.0f, 0.95f, 0.88f), 1.15f, Color(0.62f, 0.78f, 0.9f), Color(0.7f, 0.74f, 0.8f), 0.7f, 40 },
	{ 14.0f, Color(1.0f, 0.97f, 0.92f), 1.25f, Color(0.6f, 0.77f, 0.92f), Color(0.72f, 0.75f, 0.8f), 0.72f, 55 },
	{ 17.5f, Color(1.0f, 0.75f, 0.5f), 0.9f, Color(0.92f, 0.7f, 0.52f), Color(0.72f, 0.62f, 0.6f), 0.6f, 22 },
	{ 19.5f, Color(0.95f, 0.45f, 0.35f), 0.35f, Color(0.45f, 0.3f, 0.4f), Color(0.45f, 0.4f, 0.55f), 0.45f, 6 },
	{ 21.0f, Color(0.4f, 0.45f, 0.75f), 0.12f, Color(0.09f, 0.1f, 0.2f), Color(0.32f, 0.35f, 0.55f), 0.38f, 15 },
	{ 24.0f, Color(0.35f, 0.42f, 0.7f), 0.08f, Color(0.05f, 0.07f, 0.13f), Color(0.3f, 0.35f, 0.55f), 0.35f, 20 },
};

LightKey sample(float h) {
	const int n = int(sizeof(kKeys) / sizeof(kKeys[0]));
	for (int i = 0; i < n - 1; ++i) {
		const LightKey &a = kKeys[i];
		const LightKey &b = kKeys[i + 1];
		if (h >= a.hour && h <= b.hour) {
			float t = (h - a.hour) / (b.hour - a.hour);
			return { h, a.sun.lerp(b.sun, t), a.energy + (b.energy - a.energy) * t, a.sky.lerp(b.sky, t),
				a.ambient.lerp(b.ambient, t), a.ambient_energy + (b.ambient_energy - a.ambient_energy) * t,
				a.elevation + (b.elevation - a.elevation) * t };
		}
	}
	return kKeys[0];
}

} // namespace

void ToneDirector::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sim_path", "p"), &ToneDirector::set_sim_path);
	ClassDB::bind_method(D_METHOD("get_sim_path"), &ToneDirector::get_sim_path);
	ClassDB::bind_method(D_METHOD("set_sun_path", "p"), &ToneDirector::set_sun_path);
	ClassDB::bind_method(D_METHOD("get_sun_path"), &ToneDirector::get_sun_path);
	ClassDB::bind_method(D_METHOD("set_env_path", "p"), &ToneDirector::set_env_path);
	ClassDB::bind_method(D_METHOD("get_env_path"), &ToneDirector::get_env_path);
	ClassDB::bind_method(D_METHOD("set_camera_path", "p"), &ToneDirector::set_camera_path);
	ClassDB::bind_method(D_METHOD("get_camera_path"), &ToneDirector::get_camera_path);
	ClassDB::bind_method(D_METHOD("get_shown_warmth"), &ToneDirector::get_shown_warmth);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sim_path"), "set_sim_path", "get_sim_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_path"), "set_sun_path", "get_sun_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "env_path"), "set_env_path", "get_env_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_path"), "set_camera_path", "get_camera_path");
}

void ToneDirector::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	sim_ = Object::cast_to<GameSim>(get_node_or_null(sim_path_));
	sun_ = Object::cast_to<DirectionalLight3D>(get_node_or_null(sun_path_));
	env_ = Object::cast_to<WorldEnvironment>(get_node_or_null(env_path_));
	camera_ = Object::cast_to<Camera3D>(get_node_or_null(camera_path_));

	rain_ = memnew(CPUParticles3D);
	add_child(rain_);
	Ref<QuadMesh> drop;
	drop.instantiate();
	drop->set_size(Vector2(0.025f, 0.45f));
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	mat->set_albedo(Color(0.75f, 0.82f, 0.95f, 0.45f));
	drop->set_material(mat);
	rain_->set_mesh(drop);
	rain_->set_amount(900);
	rain_->set_lifetime(0.9);
	rain_->set_emission_shape(CPUParticles3D::EMISSION_SHAPE_BOX);
	rain_->set_emission_box_extents(Vector3(18.0f, 0.5f, 7.0f));
	rain_->set_direction(Vector3(0.08f, -1.0f, 0.0f));
	rain_->set_spread(2.0f);
	rain_->set_gravity(Vector3(0, -20.0f, 0));
	rain_->set_param_min(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 14.0f);
	rain_->set_param_max(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 18.0f);
	rain_->set_use_local_coordinates(false);
	rain_->set_emitting(false);

	// Haze only around the buildings and the street, so light shafts show up
	// indoors without fogging the 50 m of air between the camera and the scene.
	haze_ = memnew(FogVolume);
	add_child(haze_);
	haze_mat_.instantiate();
	haze_mat_->set_density(0.03f);
	haze_mat_->set_albedo(Color(0.95f, 0.92f, 0.88f));
	haze_mat_->set_edge_fade(0.25f);
	haze_->set_material(haze_mat_);
	haze_->set_shape(RenderingServer::FOG_VOLUME_SHAPE_BOX);
	haze_->set_size(Vector3(90.0f, 40.0f, 10.0f));

	// Dust motes drifting in the light, Teardown style.
	dust_ = memnew(CPUParticles3D);
	add_child(dust_);
	Ref<QuadMesh> mote;
	mote.instantiate();
	mote->set_size(Vector2(0.035f, 0.035f));
	Ref<StandardMaterial3D> dmat;
	dmat.instantiate();
	dmat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	dmat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	dmat->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
	dmat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	dmat->set_albedo(Color(1.0f, 0.95f, 0.85f, 0.5f));
	mote->set_material(dmat);
	dust_->set_mesh(mote);
	dust_->set_amount(260);
	dust_->set_lifetime(9.0);
	dust_->set_pre_process_time(9.0);
	dust_->set_emission_shape(CPUParticles3D::EMISSION_SHAPE_BOX);
	dust_->set_emission_box_extents(Vector3(14.0f, 7.0f, 1.6f));
	dust_->set_direction(Vector3(1, 0.2f, 0));
	dust_->set_spread(180.0f);
	dust_->set_gravity(Vector3(0, -0.01f, 0));
	dust_->set_param_min(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 0.02f);
	dust_->set_param_max(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, 0.12f);
	dust_->set_param_min(CPUParticles3D::PARAM_SCALE, 0.5f);
	dust_->set_param_max(CPUParticles3D::PARAM_SCALE, 1.4f);
	dust_->set_use_local_coordinates(false);
	dust_->set_emitting(true);
}

void ToneDirector::_process(double delta) {
	if (!sim_ || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	Dictionary clock = sim_->get_clock();
	float target_hour = float(double(clock.get("hour", 7.0)));
	float target_warmth = float(sim_->get_warmth()) / 100.0f;
	if (!init_) {
		hour_ = target_hour;
		warmth_ = target_warmth;
		init_ = true;
	}
	// Glide forward through time; new day jumps from night to morning.
	float dh = target_hour - hour_;
	if (dh < -6.0f) {
		hour_ = target_hour;
	} else {
		hour_ += dh * std::min(1.0f, float(delta) * 1.8f);
	}
	warmth_ += (target_warmth - warmth_) * std::min(1.0f, float(delta) * 0.6f);
	// Rain is only drawn while you are outside; indoors you just hear about it.
	Dictionary locs = sim_->get_world().get("locations", Dictionary());
	Dictionary here = locs.get(sim_->get_location(), Dictionary());
	bool outdoors = bool(here.get("outdoor", false));
	bool raining = sim_->get_weather() == "rain" && outdoors;
	rain_amount_ += ((raining ? 1.0f : 0.0f) - rain_amount_) * std::min(1.0f, float(delta) * 1.5f);

	LightKey k = sample(hour_);
	// Cold palette pulls everything toward blue-grey; warm toward amber.
	Color cold_tint(0.82f, 0.88f, 1.0f);
	Color warm_tint(1.0f, 0.9f, 0.76f);
	Color tint = cold_tint.lerp(warm_tint, warmth_);
	float rain_dim = 1.0f - 0.45f * rain_amount_;

	if (sun_) {
		sun_->set_color(k.sun * tint);
		sun_->set_param(Light3D::PARAM_ENERGY, k.energy * rain_dim * 2.2f);
		// The sun swings around the block: mornings shine in through the back
		// windows (towards the camera), evenings rake across the street.
		float day_t = std::max(0.0f, std::min(1.0f, (hour_ - 6.0f) / 15.0f));
		float heading = Math_PI * (0.30f - 1.05f * day_t);
		float elev = float(Math::deg_to_rad(double(std::max(6.0f, k.elevation))));
		Vector3 dir(std::sin(heading) * std::cos(elev), -std::sin(elev), std::cos(heading) * std::cos(elev));
		Vector3 pos = sun_->get_global_position();
		sun_->look_at_from_position(pos, pos + dir, std::fabs(dir.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0));
	}
	float night = 1.0f - std::max(0.0f, std::min(1.0f, (k.energy - 0.12f) / 0.5f));
	// Streets soak up quickly and dry slowly.
	if (sim_->get_weather() == "rain") {
		wet_ = std::min(1.0f, wet_ + float(delta) * 0.3f);
	} else {
		wet_ = std::max(0.0f, wet_ - float(delta) * 0.03f);
	}
	RenderingServer::get_singleton()->global_shader_parameter_set("od_night", night);
	RenderingServer::get_singleton()->global_shader_parameter_set("od_wetness", wet_);
	if (env_) {
		Ref<Environment> e = env_->get_environment();
		if (e.is_valid()) {
			Color grey(0.55f, 0.57f, 0.6f);
			Color sky = k.sky.lerp(grey * k.sky.get_luminance() * 1.8f, rain_amount_ * 0.7f) * tint;
			e->set_bg_color(sky);
			e->set_fog_light_color(sky.lerp(k.sun, 0.15f));
			Ref<Sky> sky_res = e->get_sky();
			if (sky_res.is_valid()) {
				Ref<ShaderMaterial> sm = sky_res->get_material();
				if (sm.is_valid()) {
					sm->set_shader_parameter("top_color", Vector3(sky.r * 0.62f, sky.g * 0.72f, sky.b * 0.95f));
					sm->set_shader_parameter("horizon_color", Vector3(sky.r * 1.05f, sky.g * 1.02f, sky.b));
					sm->set_shader_parameter("ground_color", Vector3(0.25f, 0.28f, 0.24f) * k.ambient_energy);
					Color cloud = k.sun.lerp(Color(1, 1, 1), 0.55f).lerp(Color(0.55f, 0.57f, 0.6f), rain_amount_);
					sm->set_shader_parameter("cloud_color", Vector3(cloud.r, cloud.g, cloud.b));
					sm->set_shader_parameter("cloud_cover", 0.4f + 0.55f * rain_amount_ + 0.1f * (1.0f - warmth_));
					sm->set_shader_parameter("night", 1.0f - std::max(0.0f, std::min(1.0f, (k.energy - 0.1f) / 0.4f)));
				}
			}
			e->set_ambient_light_color(k.ambient * tint);
			e->set_ambient_light_energy(k.ambient_energy * 0.5f);
			e->set_adjustment_enabled(true);
			e->set_adjustment_saturation(0.7f + 0.5f * warmth_);
			e->set_adjustment_contrast(1.06f + 0.06f * warmth_);
			e->set_adjustment_brightness(0.98f + 0.04f * warmth_);
			e->set_glow_intensity(0.35f + 0.5f * warmth_);
			float morning_haze = 1.0f - std::max(0.0f, std::min(1.0f, (hour_ - 7.0f) / 4.0f));
			e->set_volumetric_fog_density(0.0f);
			if (haze_mat_.is_valid()) {
				haze_mat_->set_density(0.008f + 0.03f * rain_amount_ + 0.012f * morning_haze);
				haze_mat_->set_albedo(sky.lerp(Color(1, 1, 1), 0.6f));
			}
		}
	}
	if (rain_) {
		rain_->set_emitting(rain_amount_ > 0.3f);
		if (camera_) {
			Vector3 c = camera_->get_global_position();
			rain_->set_global_position(Vector3(c.x, c.y + 9.0f, 2.5f));
		}
	}
	if (haze_ && camera_) {
		Vector3 c = camera_->get_global_position();
		haze_->set_global_position(Vector3(c.x, 15.0f, -0.5f));
	}
	if (dust_ && camera_) {
		Vector3 c = camera_->get_global_position();
		dust_->set_global_position(Vector3(c.x, c.y - 5.0f, -1.6f));
	}
}

} // namespace godot
