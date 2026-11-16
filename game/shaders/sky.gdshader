// Sky: time-of-day gradient driven by ToneDirector, sun disk with glow,
// drifting procedural clouds and stars at night.
shader_type sky;

uniform vec3 top_color = vec3(0.35, 0.55, 0.85);
uniform vec3 horizon_color = vec3(0.75, 0.82, 0.9);
uniform vec3 ground_color = vec3(0.3, 0.3, 0.32);
uniform vec3 cloud_color = vec3(1.0, 0.97, 0.94);
uniform float cloud_cover : hint_range(0.0, 1.0) = 0.45;
uniform float night : hint_range(0.0, 1.0) = 0.0;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p) {
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 5; i++) {
		v += a * noise(p);
		p = p * 2.03 + vec2(17.1, 9.2);
		a *= 0.5;
	}
	return v;
}

void sky() {
	vec3 d = normalize(EYEDIR);
	float h = d.y;
	vec3 col = mix(horizon_color, top_color, smoothstep(-0.02, 0.45, h));
	if (h < 0.0) {
		col = mix(horizon_color, ground_color, smoothstep(0.0, 0.25, -h));
	}

	if (LIGHT0_ENABLED) {
		float s = max(dot(d, LIGHT0_DIRECTION), 0.0);
		col += LIGHT0_COLOR * (smoothstep(0.9993, 0.9997, s) * 6.0 + pow(s, 48.0) * 0.35 + pow(s, 6.0) * 0.08) * (1.0 - night);
	}

	if (h > 0.0) {
		// Clouds on a virtual plane above the city.
		vec2 uv = d.xz / (h + 0.12) * 0.9 + vec2(TIME * 0.004, TIME * 0.0015);
		float c = fbm(uv * 1.6);
		c = smoothstep(1.0 - cloud_cover, 1.0 - cloud_cover * 0.35, c);
		float fade = smoothstep(0.0, 0.18, h);
		vec3 lit = mix(cloud_color, horizon_color, 0.25);
		col = mix(col, lit * mix(1.0, 0.25, night), c * fade * 0.9);

		// Stars where the clouds are thin.
		vec2 g = floor(d.xz / (h + 0.25) * 140.0);
		float star = step(0.9965, hash(g)) * (0.6 + 0.4 * sin(TIME * 2.0 + hash(g + 3.1) * 40.0));
		col += vec3(star) * night * (1.0 - c) * fade;
	}
	COLOR = col;
}
