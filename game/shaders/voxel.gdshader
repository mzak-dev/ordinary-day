// Voxel surface: vertex colours (sRGB) with baked AO, emissive voxels encoded
// in vertex alpha, wet puddles with reflections and a dithered dissolve used
// when a building's front wall fades away.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

global uniform float od_night;
global uniform float od_wetness;

// 1 = fully visible; lower values dissolve the mesh with a Bayer dither.
uniform float fade : hint_range(0.0, 1.0) = 1.0;
// Outdoor ground gets wet when it rains.
uniform float wet_surface : hint_range(0.0, 1.0) = 0.0;
uniform float emission_strength = 3.2;
// 0..1: the object the player can use right now glows softly with a warm rim.
uniform float highlight : hint_range(0.0, 1.0) = 0.0;

varying vec3 world_pos;
varying vec3 world_normal;

vec3 srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float value_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	if (fade < 0.999) {
		const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
		ivec2 p = ivec2(FRAGCOORD.xy) % 4;
		if (fade <= (bayer[p.y * 4 + p.x] + 0.5) / 16.0) {
			discard;
		}
	}
	vec3 col = srgb_to_linear(COLOR.rgb);
	ALBEDO = col;
	ROUGHNESS = 0.86;
	SPECULAR = 0.3;

	// Alpha < 0.6: always glowing (screens). 0.6–0.99: glows at night (lamps, windows).
	float a = COLOR.a;
	if (a < 0.99) {
		float glow = a < 0.6 ? 1.0 : od_night;
		EMISSION = col * glow * emission_strength;
	}

	if (highlight > 0.001) {
		float pulse = 0.8 + 0.2 * sin(TIME * 4.0);
		float rim = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 2.0);
		EMISSION += (col * 0.45 + vec3(1.0, 0.78, 0.42) * (0.06 + rim * 1.4)) * highlight * pulse;
	}

	if (wet_surface > 0.0 && od_wetness > 0.001) {
		float up = smoothstep(0.6, 0.95, world_normal.y);
		float puddle = smoothstep(0.42, 0.68, value_noise(world_pos.xz * 0.45) * 0.7 + value_noise(world_pos.xz * 1.7) * 0.3);
		float wet = od_wetness * wet_surface * up;
		ALBEDO *= mix(1.0, mix(0.7, 0.45, puddle), wet);
		ROUGHNESS = mix(ROUGHNESS, mix(0.32, 0.02, puddle), wet);
		SPECULAR = mix(SPECULAR, 0.7, wet);
	}
}
