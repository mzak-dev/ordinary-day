extends Node3D
## Test stylu: przedpokój z dawnego kadru zbudowany z wokseli 5 cm.
##   godot --path game res://scenes/style_hall.tscn --resolution 1920x540 -- --shot=res://../build/hall.png --pitch=0.11
## Układ leży w res://style/hall.json w pikselach grafiki (1 piksel = 1 woksel).

var shot_path := ""
var pitch := 0.11
var view_x := -1.0  # środek kadru w pikselach grafiki; -1 = cały przedpokój
var frames := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="):
			shot_path = arg.substr(7)
		elif arg.begins_with("--pitch="):
			pitch = float(arg.substr(8))
		elif arg.begins_with("--x="):
			view_x = float(arg.substr(4))

	var def: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://style/hall.json"))
	var vs: float = def.voxel
	var floor_row: float = def.floor_row
	for it in def.items:
		var m := VoxelModel.new()
		m.voxel_size = vs
		m.vox_path = "res://assets/vox/%s.vox" % it.vox
		add_child(m)
		# VoxelModel jest wyśrodkowany w x i z, a y = 0 to spód modelu.
		m.position = Vector3((it.x0 + it.w * 0.5) * vs, (floor_row - it.bottom) * vs, (it.z - it.d * 0.5) * vs)
		if it.has("light"):
			var l := OmniLight3D.new()
			add_child(l)
			l.position = m.position + Vector3(0, -0.15, 0.3)
			l.light_color = Color.html(it.light)
			l.light_energy = 2.2
			l.omni_range = 9.0
			l.omni_attenuation = 1.1
			l.shadow_enabled = true
			l.light_size = 0.2

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.05, 0.05, 0.07)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.75, 0.76, 0.82)
	env.ambient_light_energy = 0.55
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.ssao_enabled = true
	env.ssao_radius = 0.5
	env.ssao_intensity = 1.6
	env.glow_enabled = true
	env.glow_intensity = 0.5
	env.glow_bloom = 0.05
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	# Kadr: wysokość 108 wokseli, jak dawne płótno 192×108 (lub 384×108 dla całości).
	var cam := Camera3D.new()
	add_child(cam)
	cam.fov = 14.0
	cam.near = 2.0
	cam.far = 200.0
	var size: Array = def.size
	var h: float = size[1] * vs
	var dist := h * 0.5 / tan(deg_to_rad(cam.fov * 0.5))
	var cx: float = (size[0] * 0.5 if view_x < 0.0 else view_x) * vs
	var focus := Vector3(cx, (floor_row - size[1] * 0.5) * vs, 0.0)
	cam.position = focus + Vector3(0.0, dist * sin(pitch), dist * cos(pitch))
	cam.rotation = Vector3(-pitch, 0.0, 0.0)
	cam.current = true


func _process(_delta: float) -> void:
	frames += 1
	if frames == 30 and not shot_path.is_empty():
		var img := get_viewport().get_texture().get_image()
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(shot_path).get_base_dir())
		img.save_png(ProjectSettings.globalize_path(shot_path))
		get_tree().quit()
