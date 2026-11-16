extends CanvasLayer
## Interfejs: zegar, statystyki, 6 slotów ekwipunku, menu akcji, szuflada,
## telefon, dziennik wydarzeń i ekran końca tygodnia z grafem przyczyn.
## Cała logika gry żyje w C++ (GameSim); tu jest tylko prezentacja.

const SAVE_PATH := "user://save.json"
const STATS := [
	["battery", "Bateria społeczna", Color("#7fb4ff")],
	["energy", "Energia", Color("#ffd166")],
	["mood", "Nastrój", Color("#ef8fb0")],
]
const LOG_SIZE := 7
const STAT_NAMES := {"battery": "Bateria", "energy": "Energia", "mood": "Nastrój", "money": "zł"}
const STAT_COLORS := {"battery": Color("#7fb4ff"), "energy": Color("#ffd166"), "mood": Color("#ef8fb0"), "money": Color("#9be89b")}

@onready var sim: GameSim = get_node("../GameSim")
@onready var player: PlayerController = get_node("../Player")

var clock_label: Label
var loc_label: Label
var money_label: Label
var weather_label: Label
var bars := {}
var slot_buttons: Array[Button] = []
var prompt_label: Label
var notice_label: Label
var notice_time := 0.0
var log_box: VBoxContainer
var action_panel: PanelContainer
var action_list: VBoxContainer
var action_buttons: Array[Button] = []
var stash_panel: PanelContainer
var stash_grid: GridContainer
var phone_panel: PanelContainer
var phone_list: VBoxContainer
var help_panel: PanelContainer
var end_panel: PanelContainer
var fade: ColorRect
var day_label: Label
var root_ctl: Control
var toast_box: VBoxContainer
var timelapse: PanelContainer
var tl_title: Label
var tl_clock: Label
var tl_bar: ProgressBar
var busy := false
var pending_changes: Array = []
var action_labels := {}
var tl_t0 := 0
var tl_ticks := 1
var tl_day := ""


func _ready() -> void:
	_build()
	sim.message.connect(_on_message)
	sim.state_changed.connect(_refresh)
	sim.game_finished.connect(_on_finished)
	sim.day_started.connect(_on_day_started)
	player.prompt_changed.connect(func(t: String) -> void: prompt_label.text = t)
	player.notice.connect(_notice)
	player.screen_fade.connect(_on_screen_fade)
	sim.changes.connect(_on_changes)
	for m in sim.get_recent_messages():
		_on_message(m.kind, m.from, m.text)
	_refresh()


# ------------------------------------------------------------------ building

func _panel_style(alpha := 0.82) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = Color(0.07, 0.07, 0.1, alpha)
	s.set_corner_radius_all(6)
	s.set_content_margin_all(12)
	s.border_color = Color(1, 1, 1, 0.08)
	s.set_border_width_all(1)
	return s


func _label(text := "", size := 18, color := Color.WHITE) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l


func _build() -> void:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	root_ctl = root

	# Top-left: clock, place, stats.
	var top := PanelContainer.new()
	top.add_theme_stylebox_override("panel", _panel_style())
	top.position = Vector2(16, 16)
	top.custom_minimum_size = Vector2(300, 0)
	root.add_child(top)
	var tv := VBoxContainer.new()
	tv.add_theme_constant_override("separation", 4)
	top.add_child(tv)
	clock_label = _label("", 26)
	tv.add_child(clock_label)
	loc_label = _label("", 16, Color(0.8, 0.82, 0.88))
	tv.add_child(loc_label)
	for s in STATS:
		var row := HBoxContainer.new()
		var stat_name := _label(s[1], 13, Color(0.75, 0.75, 0.8))
		stat_name.custom_minimum_size = Vector2(130, 0)
		row.add_child(stat_name)
		var bar := ProgressBar.new()
		bar.custom_minimum_size = Vector2(140, 12)
		bar.show_percentage = false
		bar.max_value = 100
		var fill := StyleBoxFlat.new()
		fill.bg_color = s[2]
		fill.set_corner_radius_all(3)
		var bg := StyleBoxFlat.new()
		bg.bg_color = Color(1, 1, 1, 0.08)
		bg.set_corner_radius_all(3)
		bar.add_theme_stylebox_override("fill", fill)
		bar.add_theme_stylebox_override("background", bg)
		bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		row.add_child(bar)
		tv.add_child(row)
		bars[s[0]] = bar
	var mrow := HBoxContainer.new()
	money_label = _label("", 15, Color(0.7, 0.9, 0.7))
	mrow.add_child(money_label)
	weather_label = _label("", 15, Color(0.7, 0.8, 1.0))
	weather_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	weather_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	mrow.add_child(weather_label)
	tv.add_child(mrow)

	# Right: event log.
	log_box = VBoxContainer.new()
	log_box.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	log_box.position = Vector2(-500, 16)
	log_box.custom_minimum_size = Vector2(480, 0)
	log_box.alignment = BoxContainer.ALIGNMENT_BEGIN
	log_box.add_theme_constant_override("separation", 6)
	log_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(log_box)

	# Right-middle: change cards.
	toast_box = VBoxContainer.new()
	toast_box.set_anchors_preset(Control.PRESET_CENTER_RIGHT)
	toast_box.position = Vector2(-340, -40)
	toast_box.custom_minimum_size = Vector2(320, 0)
	toast_box.add_theme_constant_override("separation", 6)
	toast_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(toast_box)

	# Time-lapse card shown while an action eats time.
	timelapse = PanelContainer.new()
	timelapse.add_theme_stylebox_override("panel", _panel_style(0.86))
	timelapse.set_anchors_preset(Control.PRESET_CENTER_TOP)
	timelapse.grow_horizontal = Control.GROW_DIRECTION_BOTH
	timelapse.position.y = 120
	timelapse.custom_minimum_size = Vector2(420, 0)
	timelapse.visible = false
	root.add_child(timelapse)
	var tlv := VBoxContainer.new()
	tlv.add_theme_constant_override("separation", 6)
	timelapse.add_child(tlv)
	tl_title = _label("", 18, Color(0.9, 0.9, 0.95))
	tl_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	tlv.add_child(tl_title)
	tl_clock = _label("", 40)
	tl_clock.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	tlv.add_child(tl_clock)
	tl_bar = ProgressBar.new()
	tl_bar.show_percentage = false
	tl_bar.custom_minimum_size = Vector2(380, 6)
	var tl_fill := StyleBoxFlat.new()
	tl_fill.bg_color = Color(1.0, 0.85, 0.55)
	tl_fill.set_corner_radius_all(3)
	var tl_bg := StyleBoxFlat.new()
	tl_bg.bg_color = Color(1, 1, 1, 0.1)
	tl_bg.set_corner_radius_all(3)
	tl_bar.add_theme_stylebox_override("fill", tl_fill)
	tl_bar.add_theme_stylebox_override("background", tl_bg)
	tlv.add_child(tl_bar)

	# Bottom: prompt, notice and inventory.
	var bottom := VBoxContainer.new()
	bottom.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	bottom.grow_horizontal = Control.GROW_DIRECTION_BOTH
	bottom.grow_vertical = Control.GROW_DIRECTION_BEGIN
	bottom.position.y = -16
	bottom.alignment = BoxContainer.ALIGNMENT_END
	bottom.add_theme_constant_override("separation", 6)
	root.add_child(bottom)
	notice_label = _label("", 17, Color(1.0, 0.82, 0.6))
	notice_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	notice_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	notice_label.custom_minimum_size = Vector2(700, 0)
	bottom.add_child(notice_label)
	prompt_label = _label("", 18, Color(1, 1, 1))
	prompt_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	prompt_label.add_theme_constant_override("outline_size", 6)
	prompt_label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8))
	bottom.add_child(prompt_label)
	var inv_panel := PanelContainer.new()
	inv_panel.add_theme_stylebox_override("panel", _panel_style())
	inv_panel.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	bottom.add_child(inv_panel)
	var inv_row := HBoxContainer.new()
	inv_row.add_theme_constant_override("separation", 8)
	inv_panel.add_child(inv_row)
	for i in 6:
		var b := Button.new()
		b.custom_minimum_size = Vector2(68, 68)
		b.expand_icon = true
		b.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		b.focus_mode = Control.FOCUS_NONE
		b.pressed.connect(_on_slot.bind(i))
		var num := _label(str(i + 1), 11, Color(1, 1, 1, 0.5))
		num.position = Vector2(4, 2)
		b.add_child(num)
		inv_row.add_child(b)
		slot_buttons.append(b)
	var keys := _label("[E] akcje   [Q] szuflada   [P] telefon   [F1] pomoc", 13, Color(1, 1, 1, 0.45))
	keys.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	bottom.add_child(keys)

	# Action menu.
	action_panel = _center_panel(root, Vector2(560, 0))
	var av := VBoxContainer.new()
	av.add_theme_constant_override("separation", 6)
	action_panel.add_child(av)
	av.add_child(_label("Co robisz?", 22))
	action_list = VBoxContainer.new()
	action_list.add_theme_constant_override("separation", 4)
	av.add_child(action_list)
	av.add_child(_label("[1–9] wybór   [Esc] zamknij", 12, Color(1, 1, 1, 0.45)))

	# Stash (desk drawer in your room).
	stash_panel = _center_panel(root, Vector2(520, 0))
	var sv := VBoxContainer.new()
	stash_panel.add_child(sv)
	sv.add_child(_label("Szuflada i biurko", 22))
	sv.add_child(_label("Kliknij przedmiot, by go zabrać. Klik w slot ekwipunku – odkładasz.", 13, Color(1, 1, 1, 0.6)))
	stash_grid = GridContainer.new()
	stash_grid.columns = 3
	stash_grid.add_theme_constant_override("h_separation", 8)
	stash_grid.add_theme_constant_override("v_separation", 8)
	sv.add_child(stash_grid)

	# Phone.
	phone_panel = _center_panel(root, Vector2(460, 0))
	var pv := VBoxContainer.new()
	phone_panel.add_child(pv)
	pv.add_child(_label("Telefon", 22))
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(440, 360)
	pv.add_child(scroll)
	phone_list = VBoxContainer.new()
	phone_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(phone_list)

	# Help.
	help_panel = _center_panel(root, Vector2(620, 0))
	var hv := VBoxContainer.new()
	help_panel.add_child(hv)
	hv.add_child(_label("Ordinary Day", 28))
	var help := _label("", 16, Color(0.85, 0.85, 0.9))
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	help.custom_minimum_size = Vector2(580, 0)
	help.text = """Tydzień z życia kogoś, kto woli zostać w pokoju.

A / D – chodzenie     W / S – drzwi, schody, autobus
E – co robisz w tym miejscu (akcje zjadają czas)
Q – szuflada w twoim pokoju (6 kieszeni – wybieraj mądrze)
P – telefon (wiadomości przychodzą tylko, gdy go masz)
F5 / F9 – zapis / wczytanie

Czas płynie tylko wtedy, gdy coś robisz. Każdy wybór coś zamyka.
Nie ma złych zakończeń. Są tylko twoje."""
	hv.add_child(help)
	hv.add_child(_label("[F1] zamknij", 12, Color(1, 1, 1, 0.45)))

	# Ending.
	end_panel = PanelContainer.new()
	end_panel.add_theme_stylebox_override("panel", _panel_style(0.94))
	end_panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	end_panel.visible = false
	root.add_child(end_panel)

	# Day transition.
	fade = ColorRect.new()
	fade.color = Color(0.02, 0.02, 0.04, 0.0)
	fade.set_anchors_preset(Control.PRESET_FULL_RECT)
	fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(fade)
	day_label = _label("", 48)
	day_label.set_anchors_preset(Control.PRESET_CENTER)
	day_label.grow_horizontal = Control.GROW_DIRECTION_BOTH
	day_label.grow_vertical = Control.GROW_DIRECTION_BOTH
	day_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	day_label.modulate.a = 0.0
	root.add_child(day_label)

	_set_menu(help_panel, true)


func _center_panel(root: Control, min_size: Vector2) -> PanelContainer:
	var p := PanelContainer.new()
	p.add_theme_stylebox_override("panel", _panel_style(0.92))
	p.set_anchors_preset(Control.PRESET_CENTER)
	p.grow_horizontal = Control.GROW_DIRECTION_BOTH
	p.grow_vertical = Control.GROW_DIRECTION_BOTH
	p.custom_minimum_size = min_size
	p.visible = false
	root.add_child(p)
	return p


# ------------------------------------------------------------------ state

func _icon(id: String) -> Texture2D:
	var path := "res://assets/icons/%s.png" % id
	return load(path) if ResourceLoader.exists(path) else null


func _format_cost(ticks: int) -> String:
	var minutes := ticks * 15
	if minutes == 0:
		return "chwila"
	if minutes < 60:
		return "%d min" % minutes
	if minutes % 60 == 0:
		return "%d h" % (minutes / 60)
	return "%d h %d min" % [minutes / 60, minutes % 60]


func _refresh() -> void:
	var c := sim.get_clock()
	if not busy:
		clock_label.text = "%s  %s" % [c.day_name, c.time]
	loc_label.text = sim.get_location_name(sim.get_location())
	var st := sim.get_stats()
	for k in bars:
		var bar: ProgressBar = bars[k]
		create_tween().tween_property(bar, "value", float(st.get(k, 0)), 0.4)
	money_label.text = "%d zł" % int(st.get("money", 0))
	weather_label.text = "☂ deszcz" if sim.get_weather() == "rain" else ""
	var inv := sim.get_inventory()
	for i in 6:
		var it: Dictionary = inv[i]
		var b := slot_buttons[i]
		if it.is_empty():
			b.icon = null
			b.tooltip_text = "Pusta kieszeń"
		else:
			b.icon = _icon(it.id)
			b.tooltip_text = "%s\n%s" % [it.name, it.desc]
	if stash_panel.visible:
		if sim.can_use_stash():
			_fill_stash()
		else:
			_set_menu(stash_panel, false)
	if action_panel.visible:
		_fill_actions()
	_tint_log()


func _tint_log() -> void:
	# Narration colour follows the hidden warmth: blue-grey → cream.
	var w := clampf(sim.get_warmth() / 100.0, 0.0, 1.0)
	var col := Color(0.72, 0.78, 0.9).lerp(Color(1.0, 0.93, 0.8), w)
	var n := log_box.get_child_count()
	for i in n:
		var l: Label = log_box.get_child(i)
		var kind: String = l.get_meta("kind", "narr")
		if kind == "narr":
			l.add_theme_color_override("font_color", col)
		l.modulate.a = lerpf(0.35, 1.0, float(i + 1) / n)


# ------------------------------------------------------------------ log & notices

func _on_message(kind: String, from: String, text: String) -> void:
	var l := Label.new()
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(480, 0)
	l.add_theme_font_size_override("font_size", 16)
	l.add_theme_constant_override("outline_size", 5)
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.75))
	l.set_meta("kind", kind)
	match kind:
		"phone":
			l.text = "✉ %s: %s" % [from, text]
			l.add_theme_color_override("font_color", Color("#ffb3d1"))
		"system":
			l.text = text
			l.add_theme_color_override("font_color", Color(0.7, 0.7, 0.72))
			l.add_theme_font_size_override("font_size", 14)
		_:
			l.text = text
	log_box.add_child(l)
	while log_box.get_child_count() > LOG_SIZE:
		var old := log_box.get_child(0)
		log_box.remove_child(old)
		old.queue_free()
	_tint_log()


func _notice(text: String) -> void:
	notice_label.text = text
	notice_time = 4.0


func _process(delta: float) -> void:
	if not pending_changes.is_empty() and not busy and not player.is_animating():
		_flush_changes()
	if notice_time > 0.0:
		notice_time -= delta
		notice_label.modulate.a = clampf(notice_time, 0.0, 1.0)


func _on_day_started(_day: int, day_name: String) -> void:
	day_label.text = day_name
	var t := create_tween()
	t.tween_property(fade, "color:a", 0.85, 0.25)
	t.parallel().tween_property(day_label, "modulate:a", 1.0, 0.25)
	t.tween_interval(1.2)
	t.tween_property(fade, "color:a", 0.0, 0.8)
	t.parallel().tween_property(day_label, "modulate:a", 0.0, 0.8)


# ------------------------------------------------------------------ menus

func _any_menu() -> bool:
	return action_panel.visible or stash_panel.visible or phone_panel.visible or help_panel.visible or end_panel.visible


func _set_menu(panel: Control, open: bool) -> void:
	for p in [action_panel, stash_panel, phone_panel, help_panel]:
		if p != panel:
			p.visible = false
	panel.visible = open
	player.input_enabled = not _any_menu()


func _fill_actions() -> void:
	for ch in action_list.get_children():
		ch.queue_free()
	action_buttons.clear()
	var acts := sim.get_actions()
	if acts.is_empty():
		action_list.add_child(_label("Nic tu nie przychodzi ci do głowy.", 16, Color(1, 1, 1, 0.6)))
		return
	var i := 0
	for a in acts:
		i += 1
		var b := Button.new()
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.custom_minimum_size = Vector2(520, 36)
		b.add_theme_font_size_override("font_size", 16)
		var text := "%d.  %s   · %s" % [i, a.label, _format_cost(a.cost)]
		if not a.enabled:
			text += "\n      " + a.why
			b.custom_minimum_size.y = 56
			b.disabled = true
		b.text = text
		b.pressed.connect(_on_action.bind(a.id))
		action_labels[a.id] = a.label
		action_list.add_child(b)
		action_buttons.append(b)
	for b in action_buttons:
		if not b.disabled:
			b.grab_focus()
			break


func _on_action(id: String) -> void:
	_set_menu(action_panel, false)
	if busy:
		return
	var before := sim.get_clock()
	if not action_labels.has(id):
		for a in sim.get_actions():
			action_labels[a.id] = a.label
	busy = true
	player.busy = true
	player.input_enabled = false
	sim.perform(id)
	var after := sim.get_clock()
	await _play_timelapse(String(action_labels.get(id, "")), before, after)
	player.busy = false
	busy = false
	player.input_enabled = not _any_menu() and not end_panel.visible
	_refresh()
	_flush_changes()


func _timelapse_step(v: float) -> void:
	var t := tl_t0 + int(round(v * tl_ticks))
	tl_clock.text = _clock_text(t)
	clock_label.text = "%s  %s" % [tl_day, _clock_text(t)]
	tl_bar.value = v * 100.0


func _clock_text(t: int) -> String:
	var tod := t % 96
	return "%02d:%02d" % [tod / 4, (tod % 4) * 15]


func _play_timelapse(title: String, before: Dictionary, after: Dictionary) -> void:
	var t0: int = before.t
	var t1: int = after.t
	if t1 <= t0 or int(after.day) != int(before.day) or bool(after.finished):
		return
	var ticks := t1 - t0
	var duration := clampf(0.22 * ticks, 0.6, 2.6)
	tl_title.text = title
	tl_bar.value = 0.0
	timelapse.visible = true
	timelapse.modulate.a = 0.0
	var tw := create_tween()
	tw.tween_property(timelapse, "modulate:a", 1.0, 0.15)
	tl_t0 = t0
	tl_ticks = ticks
	tl_day = after.day_name
	tw.parallel().tween_method(_timelapse_step, 0.0, 1.0, duration).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	tw.tween_interval(0.25)
	tw.tween_property(timelapse, "modulate:a", 0.0, 0.2)
	await tw.finished
	timelapse.visible = false


# ------------------------------------------------------------------ change popups

func _on_changes(list: Array) -> void:
	pending_changes.append_array(list)
	if not busy and not player.is_animating():
		_flush_changes()


func _flush_changes() -> void:
	if pending_changes.is_empty():
		return
	var list := pending_changes.duplicate()
	pending_changes.clear()
	# Merge repeated keys (e.g. a walk followed by an action).
	var merged := {}
	var order: Array = []
	for ch in list:
		var key: String = "%s:%s:%s" % [ch.kind, ch.key, ch.label]
		if ch.kind.begins_with("item"):
			key += ":%d" % order.size()
		if merged.has(key):
			merged[key].delta += ch.delta
		else:
			merged[key] = ch.duplicate()
			order.append(key)
	var floats := 0
	for key in order:
		var ch: Dictionary = merged[key]
		match ch.kind:
			"time":
				_float_text("⏱ +%s" % _format_cost(ch.delta), Color(0.85, 0.85, 0.9), floats)
				floats += 1
			"stat":
				if ch.delta == 0:
					continue
				var sign := "+" if ch.delta > 0 else "−"
				var col: Color = STAT_COLORS.get(ch.key, Color.WHITE)
				if ch.key == "money":
					_float_text("%s%d zł" % [sign, absi(ch.delta)], col if ch.delta > 0 else Color("#ff8a80"), floats)
				else:
					_float_text("%s%d %s" % [sign, absi(ch.delta), STAT_NAMES.get(ch.key, ch.key)], col, floats)
					_pulse_bar(ch.key, ch.delta > 0)
				floats += 1
			"rel":
				if ch.delta == 0:
					continue
				var up: bool = ch.delta > 0
				_toast(("♥  %s  +%d" if up else "♡  %s  −%d") % [ch.label, absi(ch.delta)], Color("#ffb3d1") if up else Color("#b0a8b8"))
			"item_gain":
				_toast("+  " + ch.label, Color("#c8f7c5"), _icon(ch.key))
			"item_loss":
				_toast("−  " + ch.label, Color("#d0c4c4"), _icon(ch.key))
			"event":
				_toast("✦  " + ch.label, Color("#ffd98a"), null, true)


func _player_screen_pos() -> Vector2:
	var cam := get_viewport().get_camera_3d()
	if cam == null:
		return Vector2(800, 400)
	return cam.unproject_position(player.global_position + Vector3(0, 2.4, 0))


func _float_text(text: String, color: Color, index: int) -> void:
	var l := _label(text, 22, color)
	l.add_theme_constant_override("outline_size", 8)
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85))
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.custom_minimum_size = Vector2(240, 0)
	root_ctl.add_child(l)
	var p := _player_screen_pos() + Vector2(-120, -index * 30)
	l.position = p + Vector2(0, 16)
	l.modulate.a = 0.0
	l.scale = Vector2(0.7, 0.7)
	l.pivot_offset = Vector2(120, 14)
	var tw := create_tween()
	tw.tween_interval(index * 0.14)
	tw.tween_property(l, "modulate:a", 1.0, 0.15)
	tw.parallel().tween_property(l, "scale", Vector2.ONE, 0.25).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	tw.parallel().tween_property(l, "position:y", p.y - 50, 1.9).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tw.parallel().tween_property(l, "modulate:a", 0.0, 0.6).set_delay(1.3)
	tw.tween_callback(l.queue_free)


var toast_queue: Array = []
const MAX_TOASTS := 3


func _toast(text: String, color: Color, icon: Texture2D = null, important := false) -> void:
	if toast_box.get_child_count() >= MAX_TOASTS:
		if toast_queue.size() < 6:
			toast_queue.append([text, color, icon, important])
		return
	var p := PanelContainer.new()
	var style := _panel_style(0.88)
	style.border_color = color * Color(1, 1, 1, 0.6)
	style.border_width_left = 4
	p.add_theme_stylebox_override("panel", style)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	p.add_child(row)
	if icon:
		var tr := TextureRect.new()
		tr.texture = icon
		tr.custom_minimum_size = Vector2(32, 32)
		tr.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		tr.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		row.add_child(tr)
	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", 0)
	row.add_child(col)
	if important:
		col.add_child(_label("Konsekwencja", 12, Color(1, 1, 1, 0.5)))
	var l := _label(text, 17 if important else 16, color)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(250, 0)
	col.add_child(l)
	toast_box.add_child(p)
	p.modulate.a = 0.0
	var tw := create_tween()
	tw.tween_property(p, "modulate:a", 1.0, 0.25)
	tw.tween_interval(4.5 if important else 3.2)
	tw.tween_property(p, "modulate:a", 0.0, 0.5)
	tw.tween_callback(_drop_toast.bind(p))


func _drop_toast(p: Control) -> void:
	toast_box.remove_child(p)
	p.queue_free()
	if not toast_queue.is_empty():
		var t: Array = toast_queue.pop_front()
		_toast(t[0], t[1], t[2], t[3])


func _pulse_bar(key: String, up: bool) -> void:
	if not bars.has(key):
		return
	var bar: ProgressBar = bars[key]
	bar.modulate = Color(1.6, 1.6, 1.6) if up else Color(1.5, 0.6, 0.6)
	create_tween().tween_property(bar, "modulate", Color.WHITE, 0.8)


func _on_screen_fade(alpha: float, duration: float) -> void:
	create_tween().tween_property(fade, "color:a", alpha, duration)


func _fill_stash() -> void:
	for ch in stash_grid.get_children():
		ch.queue_free()
	var items := sim.get_stash()
	for i in items.size():
		var it: Dictionary = items[i]
		var b := Button.new()
		b.text = it.name
		b.icon = _icon(it.id)
		b.expand_icon = true
		b.custom_minimum_size = Vector2(156, 52)
		b.tooltip_text = it.desc
		b.pressed.connect(func() -> void: sim.take_from_stash(i))
		stash_grid.add_child(b)
	if items.is_empty():
		stash_grid.add_child(_label("Pusto.", 16, Color(1, 1, 1, 0.6)))


func _fill_phone() -> void:
	for ch in phone_list.get_children():
		ch.queue_free()
	var has_phone := false
	for it in sim.get_inventory():
		if not it.is_empty() and it.id == "phone":
			has_phone = true
	if not has_phone:
		phone_list.add_child(_label("Telefon leży w twoim pokoju.\nWiadomości poczekają. Ludzie – niekoniecznie.", 16, Color(1, 1, 1, 0.7)))
		return
	var msgs := sim.get_phone_log()
	if msgs.is_empty():
		phone_list.add_child(_label("Brak wiadomości. Jak zwykle.", 16, Color(1, 1, 1, 0.6)))
	for i in range(msgs.size() - 1, -1, -1):
		var m: Dictionary = msgs[i]
		var l := _label("%s · %s\n%s" % [m.from, m.sent, m.text], 15, Color("#ffd6e7"))
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		l.custom_minimum_size = Vector2(420, 0)
		phone_list.add_child(l)
		phone_list.add_child(HSeparator.new())


func _on_slot(i: int) -> void:
	var it: Dictionary = sim.get_inventory()[i]
	if sim.can_use_stash():
		if not it.is_empty():
			sim.move_to_stash(i)
			if not stash_panel.visible:
				_set_menu(stash_panel, true)
				_fill_stash()
	elif not it.is_empty():
		_notice("%s – %s" % [it.name, it.desc])


func _unhandled_input(event: InputEvent) -> void:
	if end_panel.visible:
		return
	if event.is_action_pressed("help"):
		_set_menu(help_panel, not help_panel.visible)
	elif event.is_action_pressed("ui_cancel"):
		_set_menu(action_panel, false)
	elif event.is_action_pressed("act"):
		var open := not action_panel.visible
		_set_menu(action_panel, open)
		if open:
			_fill_actions()
	elif event.is_action_pressed("stash"):
		if sim.can_use_stash():
			var open := not stash_panel.visible
			_set_menu(stash_panel, open)
			if open:
				_fill_stash()
		else:
			_notice("Szuflada jest w twoim pokoju.")
	elif event.is_action_pressed("phone"):
		var open := not phone_panel.visible
		_set_menu(phone_panel, open)
		if open:
			_fill_phone()
	elif event.is_action_pressed("quick_save"):
		_notice("Zapisano." if sim.save_game(SAVE_PATH) else "Nie udało się zapisać.")
	elif event.is_action_pressed("quick_load"):
		_notice("Wczytano." if sim.load_game(SAVE_PATH) else "Brak zapisu.")
	elif event is InputEventKey and event.pressed and not event.echo:
		var k: int = event.physical_keycode
		if k >= KEY_1 and k <= KEY_9:
			var n := k - KEY_1
			if action_panel.visible:
				if n < action_buttons.size() and not action_buttons[n].disabled:
					action_buttons[n].pressed.emit()
			elif n < 6:
				_on_slot(n)
		else:
			return
	else:
		return
	get_viewport().set_input_as_handled()


# ------------------------------------------------------------------ ending

func _on_finished() -> void:
	_set_menu(action_panel, false)
	player.input_enabled = false
	var e := sim.get_ending()
	for ch in end_panel.get_children():
		ch.queue_free()
	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 80)
	end_panel.add_child(margin)
	var cols := HBoxContainer.new()
	cols.add_theme_constant_override("separation", 48)
	margin.add_child(cols)

	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_theme_constant_override("separation", 14)
	cols.add_child(left)
	left.add_child(_label("Koniec tygodnia", 16, Color(1, 1, 1, 0.5)))
	left.add_child(_label(e.title, 40))
	var body := _label(e.text, 18, Color(0.92, 0.9, 0.86))
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	left.add_child(body)
	for line in e.epilogue:
		var l := _label("·  " + line, 16, Color(0.8, 0.8, 0.85))
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		left.add_child(l)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 12)
	left.add_child(buttons)
	var again := Button.new()
	again.text = "Jeszcze raz ten tydzień"
	again.pressed.connect(func() -> void: get_tree().reload_current_scene())
	buttons.add_child(again)
	var quit := Button.new()
	quit.text = "Wyjdź"
	quit.pressed.connect(func() -> void: get_tree().quit())
	buttons.add_child(quit)

	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cols.add_child(right)
	right.add_child(_label("Dlaczego tak wyszło", 22))
	var why := RichTextLabel.new()
	why.bbcode_enabled = false
	why.fit_content = false
	why.size_flags_vertical = Control.SIZE_EXPAND_FILL
	why.custom_minimum_size = Vector2(560, 500)
	why.add_theme_font_size_override("normal_font_size", 14)
	why.add_theme_color_override("default_color", Color(0.78, 0.82, 0.9))
	why.text = "\n".join(e.why) if not e.why.is_empty() else "Nic szczególnego. Tydzień po prostu się wydarzył."
	right.add_child(why)

	end_panel.modulate.a = 0.0
	end_panel.visible = true
	create_tween().tween_property(end_panel, "modulate:a", 1.0, 1.5)
	again.grab_focus()
