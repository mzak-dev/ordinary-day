extends Node
## Narzędzia deweloperskie sterowane argumentami po "--":
##   --do=act:breakfast,wait:20:00,go:home_door:b,walk:b2f:8,take:umbrella,
##        key:move_up,hud:breakfast,delay:1.5,shot:res://../build/a.png
##   --shot=res://../build/shot.png   (zrzut po wykonaniu komend i wyjście)

@onready var sim: GameSim = get_node("../GameSim")
@onready var player: PlayerController = get_node("../Player")
@onready var hud: CanvasLayer = get_node("../HUD")

var shot_path := ""
var commands: Array = []
var frames := 0
var wait_left := 0.0
var done := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="):
			shot_path = arg.substr(7)
		elif arg.begins_with("--do="):
			commands = Array(arg.substr(5).split(","))
	set_process(not shot_path.is_empty() or not commands.is_empty())


func _save(path: String) -> void:
	var img := get_viewport().get_texture().get_image()
	img.save_png(path)
	print("dev: shot frame=", frames, " t=", Time.get_ticks_msec(), " clock=", sim.get_clock().time, " anim=", player.is_animating(), " seg=", sim.get_player_seg())


func _run(cmd: String) -> void:
	var p := cmd.split(":")
	match p[0]:
		"act":
			if not sim.perform(p[1]):
				push_warning("dev: action failed " + p[1])
		"hud":
			hud._on_action(p[1])
		"wait":
			var target := int(p[1]) * 4 + int(p[2]) / 15
			var guard := 0
			while sim.get_clock().tick < target and guard < 200:
				sim.perform("wait")
				guard += 1
		"go":
			var link: Dictionary = {}
			for l in sim.get_world().links:
				if l.id == p[1]:
					link = l
			var end: Dictionary = link.a if p[2] == "a" else link.b
			sim.set_player_pos(end.seg, end.x)
			sim.use_link(p[1], p[2] == "a")
			player.teleport(sim.get_player_seg(), sim.get_player_x())
		"walk":
			sim.set_player_pos(p[1], float(p[2]))
			player.teleport(p[1], float(p[2]))
		"take":
			var stash := sim.get_stash()
			for i in stash.size():
				if stash[i].id == p[1]:
					sim.take_from_stash(i)
					break
		"key":
			var ev := InputEventAction.new()
			ev.action = p[1]
			ev.pressed = true
			Input.parse_input_event(ev)
			var up := InputEventAction.new()
			up.action = p[1]
			up.pressed = false
			Input.call_deferred("parse_input_event", up)
		"pause":
			hud._open_pause()
		"nohud":
			hud.visible = false
		"quality":
			Session.set_quality(int(p[1]))
		"nolimit":
			Session.set_fps_index(2)
			DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
		"delay":
			wait_left = float(p[1])
		"shot":
			_save(":".join(p.slice(1)))


func _process(delta: float) -> void:
	frames += 1
	if frames < 5:
		return
	if wait_left > 0.0:
		wait_left -= delta
		return
	while not commands.is_empty() and wait_left <= 0.0:
		_run(commands.pop_front())
	if commands.is_empty() and not done and wait_left <= 0.0:
		done = true
		if not shot_path.is_empty():
			await get_tree().create_timer(5.0).timeout
			_save(shot_path)
			get_tree().quit()
		elif frames > 0:
			pass
