extends Node
## Autoload "Session": graphics settings, save-file info and switching
## between the main menu and the game.

const SETTINGS_PATH := "user://settings.cfg"
const SAVE_PATH := "user://save.json"
const MENU_SCENE := "res://scenes/menu.tscn"
const GAME_SCENE := "res://scenes/main.tscn"
const QUALITY_NAMES := ["Niskie", "Średnie", "Wysokie"]
const FPS_OPTIONS := [30, 60, 0]
const DAY_NAMES := ["Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota", "Niedziela"]

signal settings_changed

var quality := 1
var fps_index := 1
var fullscreen := false
## Set by the menu before loading the game scene.
var continue_game := false
## The running game scene (set by the HUD), so settings can be applied live.
var game_root: Node = null


func _ready() -> void:
	load_settings()
	apply_window()


# ------------------------------------------------------------------ settings

func load_settings() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS_PATH) != OK:
		return
	quality = clampi(int(cfg.get_value("graphics", "quality", quality)), 0, 2)
	fps_index = clampi(int(cfg.get_value("graphics", "fps_index", fps_index)), 0, FPS_OPTIONS.size() - 1)
	fullscreen = bool(cfg.get_value("display", "fullscreen", fullscreen))


func save_settings() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("graphics", "quality", quality)
	cfg.set_value("graphics", "fps_index", fps_index)
	cfg.set_value("display", "fullscreen", fullscreen)
	cfg.save(SETTINGS_PATH)


func set_quality(q: int) -> void:
	quality = clampi(q, 0, 2)
	_changed()


func set_fps_index(i: int) -> void:
	fps_index = clampi(i, 0, FPS_OPTIONS.size() - 1)
	_changed()


func set_fullscreen(on: bool) -> void:
	fullscreen = on
	_changed()


func _changed() -> void:
	save_settings()
	apply_window()
	if is_instance_valid(game_root):
		apply_to_game(game_root)
	settings_changed.emit()


func apply_window() -> void:
	Engine.max_fps = FPS_OPTIONS[fps_index]
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED)
	var mode := DisplayServer.WINDOW_MODE_FULLSCREEN if fullscreen else DisplayServer.WINDOW_MODE_WINDOWED
	if DisplayServer.window_get_mode() != mode:
		DisplayServer.window_set_mode(mode)
	# The 3D resolution scale also applies to the menu viewport (cheap there).
	_apply_viewport(get_viewport())


func _apply_viewport(vp: Viewport) -> void:
	var q := quality
	# Retina screens render at 2x: scale the 3D image down and upscale with FSR.
	vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR if q < 2 else Viewport.SCALING_3D_MODE_BILINEAR
	vp.scaling_3d_scale = [0.55, 0.77, 1.0][q]
	vp.msaa_3d = Viewport.MSAA_2X if q == 2 else Viewport.MSAA_DISABLED
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_DISABLED if q == 2 else Viewport.SCREEN_SPACE_AA_FXAA


## Applies the current preset to a running game scene (main.tscn).
func apply_to_game(root: Node) -> void:
	var q := quality
	_apply_viewport(root.get_viewport())

	var env_node := root.get_node_or_null("WorldEnvironment") as WorldEnvironment
	if env_node and env_node.environment:
		var env := env_node.environment
		env.sdfgi_enabled = q >= 2
		env.ssil_enabled = q >= 2
		env.ssr_enabled = q >= 2
		env.ssao_enabled = q >= 1
		env.volumetric_fog_enabled = q >= 1
		env.glow_enabled = true
		if q == 1:
			RenderingServer.environment_set_ssao_quality(RenderingServer.ENV_SSAO_QUALITY_LOW, true, 0.5, 2, 50.0, 300.0)
			RenderingServer.environment_set_volumetric_fog_volume_size(48, 48)
		else:
			RenderingServer.environment_set_ssao_quality(RenderingServer.ENV_SSAO_QUALITY_MEDIUM, false, 0.5, 2, 50.0, 300.0)
			RenderingServer.environment_set_volumetric_fog_volume_size(64, 64)
		RenderingServer.environment_set_ssil_quality(RenderingServer.ENV_SSIL_QUALITY_MEDIUM, true, 0.5, 4, 50.0, 300.0)

	var cam := root.get_node_or_null("Camera3D") as Camera3D
	if cam and cam.attributes is CameraAttributesPractical:
		(cam.attributes as CameraAttributesPractical).dof_blur_far_enabled = q >= 1

	var sun := root.get_node_or_null("Sun") as DirectionalLight3D
	if sun:
		sun.directional_shadow_mode = [DirectionalLight3D.SHADOW_ORTHOGONAL, DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS, DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS][q]
		sun.directional_shadow_max_distance = [35.0, 50.0, 60.0][q]
	RenderingServer.directional_shadow_atlas_set_size([2048, 4096, 4096][q], true)
	RenderingServer.directional_soft_shadow_filter_set_quality([RenderingServer.SHADOW_QUALITY_HARD, RenderingServer.SHADOW_QUALITY_SOFT_LOW, RenderingServer.SHADOW_QUALITY_SOFT_HIGH][q])
	RenderingServer.positional_soft_shadow_filter_set_quality([RenderingServer.SHADOW_QUALITY_HARD, RenderingServer.SHADOW_QUALITY_SOFT_LOW, RenderingServer.SHADOW_QUALITY_SOFT_MEDIUM][q])

	var world := root.get_node_or_null("World")
	if world:
		world.set_quality(q)
	var tone := root.get_node_or_null("ToneDirector")
	if tone:
		tone.set_quality(q)
	var post := root.get_node_or_null("PostFX/Screen") as ColorRect
	if post and post.material is ShaderMaterial:
		var m := post.material as ShaderMaterial
		m.set_shader_parameter("aberration", 0.0 if q == 0 else 1.4)


# ------------------------------------------------------------------ saves & scenes

func has_save() -> bool:
	return FileAccess.file_exists(SAVE_PATH)


## "Środa, 14:15" for the menu, or "" when there is no readable save.
func save_summary() -> String:
	if not has_save():
		return ""
	var data = JSON.parse_string(FileAccess.get_file_as_string(SAVE_PATH))
	if typeof(data) != TYPE_DICTIONARY or not data.has("t"):
		return ""
	var t := int(data.t)
	var day := clampi(t / 96, 0, 6)
	var tod := t % 96
	if bool(data.get("finished", false)):
		return "Tydzień zakończony"
	return "%s, %02d:%02d" % [DAY_NAMES[day], tod / 4, (tod % 4) * 15]


func delete_save() -> void:
	if has_save():
		DirAccess.remove_absolute(ProjectSettings.globalize_path(SAVE_PATH))


func start_new() -> void:
	continue_game = false
	get_tree().change_scene_to_file(GAME_SCENE)


func start_continue() -> void:
	continue_game = true
	get_tree().change_scene_to_file(GAME_SCENE)


func to_menu() -> void:
	game_root = null
	get_tree().change_scene_to_file(MENU_SCENE)
