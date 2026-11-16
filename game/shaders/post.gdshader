// Full-screen finish: subtle chromatic aberration, vignette and film grain.
shader_type canvas_item;

uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;
uniform float vignette = 0.38;
uniform float grain = 0.04;
uniform float aberration = 1.4;

void fragment() {
	vec2 uv = SCREEN_UV;
	vec2 d = uv - 0.5;
	float r2 = dot(d, d);
	vec2 off = d * r2 * aberration * 0.012;
	vec3 c;
	c.r = texture(screen_tex, uv + off).r;
	c.g = texture(screen_tex, uv).g;
	c.b = texture(screen_tex, uv - off).b;
	float n = fract(sin(dot(floor(uv * vec2(1600.0, 900.0)) + fract(TIME * 7.13) * 91.7, vec2(12.9898, 78.233))) * 43758.5453);
	c += (n - 0.5) * grain;
	c *= 1.0 - vignette * smoothstep(0.08, 0.6, r2 * 1.8);
	COLOR = vec4(c, 1.0);
}
