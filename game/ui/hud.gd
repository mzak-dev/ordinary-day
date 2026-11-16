extends CanvasLayer
## Interfejs: menu główne i pauza, zegar, statystyki, 6 slotów ekwipunku,
## menu akcji, szuflada, telefon, podpowiedzi interakcji w świecie, dziennik,
## upływ czasu, karty zmian i ekran końca tygodnia z grafem przyczyn.
## Cała logika gry żyje w C++ (GameSim); tu jest tylko prezentacja.

const SAVE_PATH := "user://save.json"
const STATS := [
	["battery", "Bateria społeczna", Color("#7fb4ff")],
	["energy", "Energia", Color("#ffd166")],
	["mood", "Nastrój", Color("#ef8fb0")],
]
const STAT_NAMES := {"battery": "Bateria", "energy": "Energia", "mood": "Nastrój", "money": "zł"}
const STAT_COLORS := {"battery": Color("#7fb4ff"), "energy": Color("#ffd166"), "mood": Color("#ef8fb0"), "money": Color("#9be89b")}
const LOG_SIZE := 7
const MAX_TOASTS := 3
const ACCENT := Color("#ffcf7a")
const MUTED := Color(0.72, 0.72, 0.78)

# Action groups: display name, glyph, sort order.
const GROUPS := {
	"rodzina": ["Rodzina", "⌂", 0],
	"ludzie": ["Ludzie", "☺", 1],
	"impreza": ["Impreza", "♪", 2],
	"przeprowadzka": ["Przeprowadzka", "➜", 3],
	"zajęcia": ["Zajęcia", "✎", 4],
	"projekt": ["Projekt", "▣", 5],
	"nauka": ["Nauka", "✎", 6],
	"praca": ["Praca", "☕", 7],
	"siłownia": ["Siłownia", "⚑", 8],
	"zakupy": ["Zakupy", "¤", 9],
	"spacer": ["Spacer", "❀", 10],
	"komputer": ["Komputer", "▢", 11],
	"pokój": ["Pokój", "☾", 12],
	"telefon": ["Telefon", "✆", 13],
	"czekanie": ["Czekanie", "…", 14],
}
const QUIET_GROUPS := ["czekanie", "telefon"]

@onready var sim: GameSim = get_node("../GameSim")
@onready var player: PlayerController = get_node("../Player")
@onready var world: WorldDirector = get_node("../World")
@onready var post: ShaderMaterial = get_node("../PostFX/Screen").material

var font_regular: SystemFont
var font_bold: SystemFont
var root_ctl: Control
var hud_group: Control  # everything that belongs to gameplay, hidden under the main menu

# HUD
var clock_label: Label
var loc_label: Label
var money_label: Label
var weather_label: Label
var bars := {}
var slot_buttons: Array[Button] = []
var notice_label: Label
var notice_time := 0.0
var log_box: VBoxContainer
var toast_box: VBoxContainer
var toast_queue: Array = []
var hint_bar: HBoxContainer
var hint_actions: Label
var hint_stash: Control
var hint_phone_badge: Label
var banner: PanelContainer
var banner_title: Label
var banner_sub: Label
var prompt_layer: Control
var prompt_chips := {}

# Panels
var action_panel: PanelContainer
var action_title: Label
var action_sub: Label
var action_list: VBoxContainer
var action_buttons: Array[Button] = []
var action_labels := {}
var stash_panel: PanelContainer
var stash_grid: GridContainer
var phone_panel: PanelContainer
var phone_list: VBoxContainer
var help_panel: PanelContainer
var end_panel: PanelContainer
var settings_panel: SettingsPanel
var pause_menu: Control
var pause_buttons: VBoxContainer

# Time-lapse and transitions
var timelapse: PanelContainer
var tl_title: Label
var tl_clock: Label
var tl_bar: ProgressBar
var tl_t0 := 0
var tl_ticks := 1
var tl_day := ""
var fade: ColorRect
var day_label: Label

# State
var busy := false
var pending_changes: Array = []
var last_loc := ""
var last_action_ids: Array = []
var first_refresh := true
var npc_actions := {}  # npc id -> first action label
# Interaction with things and people right next to the player (replaces the E menu
# for everything tied to an object or a person).
var focus := {}             # {key, name, pos, acts: Array[Dictionary]}
var focus_sel := 0
var focus_card: PanelContainer
var focus_sig := ""
var attached := {}          # action ids that live on a prop
var interactables: Array = []
var phone_seen := 0


func _ready() -> void:
	_build_fonts()
	_build()
	sim.message.connect(_on_message)
	sim.state_changed.connect(_refresh)
	sim.game_finished.connect(_on_finished)
	sim.day_started.connect(_on_day_started)
	sim.changes.connect(_on_changes)
	player.notice.connect(_notice)
	player.screen_fade.connect(_on_screen_fade)
	# The menu scene decides whether we start fresh or continue a save.
	Session.game_root = get_parent()
	Session.apply_to_game(get_parent())
	var loaded := Session.continue_game and sim.load_game(SAVE_PATH)
	Session.continue_game = false
	if not loaded:
		sim.new_game(0)
	_refresh()
	fade.color.a = 1.0
	create_tween().tween_property(fade, "color:a", 0.0, 0.8)


# ================================================================== building

func _build_fonts() -> void:
	font_regular = SystemFont.new()
	font_regular.font_names = PackedStringArray(["Avenir Next", "Segoe UI", "Noto Sans", "Helvetica Neue", "Sans-Serif"])
	font_regular.font_weight = 500
	font_bold = SystemFont.new()
	font_bold.font_names = font_regular.font_names
	font_bold.font_weight = 700


func _style(alpha := 0.82, radius := 8, bg := Color(0.06, 0.06, 0.09)) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = Color(bg.r, bg.g, bg.b, alpha)
	s.set_corner_radius_all(radius)
	s.set_content_margin_all(14)
	s.border_color = Color(1, 1, 1, 0.07)
	s.set_border_width_all(1)
	return s


func _label(text := "", size := 18, color := Color.WHITE, bold := false) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_override("font", font_bold if bold else font_regular)
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l


func _keycap(key: String, size := 14) -> PanelContainer:
	var p := PanelContainer.new()
	var s := StyleBoxFlat.new()
	s.bg_color = Color(0.93, 0.92, 0.88)
	s.set_corner_radius_all(4)
	s.border_color = Color(0.55, 0.53, 0.5)
	s.border_width_bottom = 3
	s.content_margin_left = 7
	s.content_margin_right = 7
	s.content_margin_top = 1
	s.content_margin_bottom = 1
	p.add_theme_stylebox_override("panel", s)
	p.add_child(_label(key, size, Color(0.12, 0.12, 0.14), true))
	p.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return p


func _menu_button(text: String, size := 26) -> Button:
	var b := Button.new()
	b.text = text
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.flat = true
	b.add_theme_font_override("font", font_bold)
	b.add_theme_font_size_override("font_size", size)
	b.add_theme_color_override("font_color", Color(0.86, 0.86, 0.9))
	b.add_theme_color_override("font_hover_color", ACCENT)
	b.add_theme_color_override("font_focus_color", ACCENT)
	b.add_theme_color_override("font_disabled_color", Color(1, 1, 1, 0.25))
	var empty := StyleBoxEmpty.new()
	for st in ["normal", "hover", "pressed", "focus", "disabled"]:
		b.add_theme_stylebox_override(st, empty)
	b.focus_entered.connect(func() -> void: create_tween().tween_property(b, "position:x", 14.0, 0.12))
	b.focus_exited.connect(func() -> void: create_tween().tween_property(b, "position:x", 0.0, 0.12))
	b.mouse_entered.connect(b.grab_focus)
	return b


func _build() -> void:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	root_ctl = root
	var theme := Theme.new()
	theme.default_font = font_regular
	root.theme = theme

	hud_group = Control.new()
	hud_group.set_anchors_preset(Control.PRESET_FULL_RECT)
	hud_group.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(hud_group)
	prompt_layer = Control.new()
	prompt_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	prompt_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hud_group.add_child(prompt_layer)

	_build_status(hud_group)
	_build_log_and_toasts(hud_group)
	_build_banner(hud_group)
	_build_bottom(hud_group)
	_build_timelapse(hud_group)
	_build_action_menu(root)
	_build_stash(root)
	_build_phone(root)
	_build_help(root)

	end_panel = PanelContainer.new()
	end_panel.add_theme_stylebox_override("panel", _style(0.94, 0))
	end_panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	end_panel.visible = false
	root.add_child(end_panel)

	fade = ColorRect.new()
	fade.color = Color(0.02, 0.02, 0.04, 0.0)
	fade.set_anchors_preset(Control.PRESET_FULL_RECT)
	fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(fade)
	day_label = _label("", 52, Color.WHITE, true)
	day_label.set_anchors_preset(Control.PRESET_CENTER)
	day_label.grow_horizontal = Control.GROW_DIRECTION_BOTH
	day_label.grow_vertical = Control.GROW_DIRECTION_BOTH
	day_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	day_label.modulate.a = 0.0
	root.add_child(day_label)

	_build_pause(root)
	settings_panel = SettingsPanel.new()
	settings_panel.visible = false
	settings_panel.closed.connect(func() -> void: pause_buttons.get_child(3).grab_focus())
	root.add_child(settings_panel)


func _build_status(root: Control) -> void:
	var top := PanelContainer.new()
	top.add_theme_stylebox_override("panel", _style())
	top.position = Vector2(16, 16)
	top.custom_minimum_size = Vector2(310, 0)
	root.add_child(top)
	var tv := VBoxContainer.new()
	tv.add_theme_constant_override("separation", 4)
	top.add_child(tv)
	clock_label = _label("", 28, Color.WHITE, true)
	tv.add_child(clock_label)
	loc_label = _label("", 16, MUTED)
	tv.add_child(loc_label)
	for s in STATS:
		var row := HBoxContainer.new()
		var stat_name := _label(s[1], 13, Color(0.75, 0.75, 0.8))
		stat_name.custom_minimum_size = Vector2(135, 0)
		row.add_child(stat_name)
		var bar := ProgressBar.new()
		bar.custom_minimum_size = Vector2(140, 10)
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
	money_label = _label("", 15, Color(0.7, 0.9, 0.7), true)
	mrow.add_child(money_label)
	weather_label = _label("", 15, Color(0.7, 0.8, 1.0))
	weather_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	weather_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	mrow.add_child(weather_label)
	tv.add_child(mrow)


func _build_log_and_toasts(root: Control) -> void:
	log_box = VBoxContainer.new()
	log_box.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	log_box.position = Vector2(-500, 16)
	log_box.custom_minimum_size = Vector2(480, 0)
	log_box.add_theme_constant_override("separation", 6)
	log_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(log_box)

	toast_box = VBoxContainer.new()
	toast_box.set_anchors_preset(Control.PRESET_CENTER_RIGHT)
	toast_box.position = Vector2(-340, -40)
	toast_box.custom_minimum_size = Vector2(320, 0)
	toast_box.add_theme_constant_override("separation", 6)
	toast_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(toast_box)


func _build_banner(root: Control) -> void:
	banner = PanelContainer.new()
	var s := _style(0.0, 0)
	s.set_border_width_all(0)
	banner.add_theme_stylebox_override("panel", s)
	banner.set_anchors_preset(Control.PRESET_CENTER_TOP)
	banner.grow_horizontal = Control.GROW_DIRECTION_BOTH
	banner.position.y = 36
	banner.mouse_filter = Control.MOUSE_FILTER_IGNORE
	banner.modulate.a = 0.0
	root.add_child(banner)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 0)
	banner.add_child(v)
	banner_title = _label("", 30, Color.WHITE, true)
	banner_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	banner_title.add_theme_constant_override("outline_size", 10)
	banner_title.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.7))
	v.add_child(banner_title)
	banner_sub = _label("", 15, ACCENT)
	banner_sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	banner_sub.add_theme_constant_override("outline_size", 6)
	banner_sub.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.7))
	v.add_child(banner_sub)


func _build_bottom(root: Control) -> void:
	var bottom := VBoxContainer.new()
	bottom.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	bottom.grow_horizontal = Control.GROW_DIRECTION_BOTH
	bottom.grow_vertical = Control.GROW_DIRECTION_BEGIN
	bottom.position.y = -14
	bottom.alignment = BoxContainer.ALIGNMENT_END
	bottom.add_theme_constant_override("separation", 8)
	bottom.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(bottom)

	notice_label = _label("", 17, Color(1.0, 0.82, 0.6), true)
	notice_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	notice_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	notice_label.custom_minimum_size = Vector2(700, 0)
	notice_label.add_theme_constant_override("outline_size", 6)
	notice_label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8))
	bottom.add_child(notice_label)

	var inv_panel := PanelContainer.new()
	inv_panel.add_theme_stylebox_override("panel", _style())
	inv_panel.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	bottom.add_child(inv_panel)
	var inv_row := HBoxContainer.new()
	inv_row.add_theme_constant_override("separation", 8)
	inv_panel.add_child(inv_row)
	for i in 6:
		var b := Button.new()
		b.custom_minimum_size = Vector2(64, 64)
		b.expand_icon = true
		b.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		b.focus_mode = Control.FOCUS_NONE
		b.pressed.connect(_on_slot.bind(i))
		var num := _label(str(i + 1), 11, Color(1, 1, 1, 0.45))
		num.position = Vector2(5, 2)
		b.add_child(num)
		inv_row.add_child(b)
		slot_buttons.append(b)

	hint_bar = HBoxContainer.new()
	hint_bar.alignment = BoxContainer.ALIGNMENT_CENTER
	hint_bar.add_theme_constant_override("separation", 18)
	hint_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	bottom.add_child(hint_bar)
	var act := _hint("E", "Akcje")
	hint_actions = act.get_child(1)
	hint_bar.add_child(act)
	hint_stash = _hint("Q", "Szuflada")
	hint_bar.add_child(hint_stash)
	var phone := _hint("P", "Telefon")
	hint_phone_badge = _label("", 12, Color.WHITE, true)
	var badge := PanelContainer.new()
	var bs := StyleBoxFlat.new()
	bs.bg_color = Color("#e5484d")
	bs.set_corner_radius_all(9)
	bs.content_margin_left = 6
	bs.content_margin_right = 6
	badge.add_theme_stylebox_override("panel", bs)
	badge.add_child(hint_phone_badge)
	badge.visible = false
	phone.add_child(badge)
	hint_bar.add_child(phone)
	hint_bar.add_child(_hint("Esc", "Menu"))


func _hint(key: String, text: String) -> HBoxContainer:
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", 6)
	h.mouse_filter = Control.MOUSE_FILTER_IGNORE
	h.add_child(_keycap(key, 12))
	var l := _label(text, 14, Color(1, 1, 1, 0.75))
	l.add_theme_constant_override("outline_size", 4)
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.6))
	h.add_child(l)
	return h


func _build_timelapse(root: Control) -> void:
	timelapse = PanelContainer.new()
	timelapse.add_theme_stylebox_override("panel", _style(0.86))
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
	tl_clock = _label("", 42, Color.WHITE, true)
	tl_clock.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	tlv.add_child(tl_clock)
	tl_bar = ProgressBar.new()
	tl_bar.show_percentage = false
	tl_bar.custom_minimum_size = Vector2(380, 6)
	var tl_fill := StyleBoxFlat.new()
	tl_fill.bg_color = ACCENT
	tl_fill.set_corner_radius_all(3)
	var tl_bg := StyleBoxFlat.new()
	tl_bg.bg_color = Color(1, 1, 1, 0.1)
	tl_bg.set_corner_radius_all(3)
	tl_bar.add_theme_stylebox_override("fill", tl_fill)
	tl_bar.add_theme_stylebox_override("background", tl_bg)
	tlv.add_child(tl_bar)


func _build_action_menu(root: Control) -> void:
	action_panel = PanelContainer.new()
	var s := _style(0.93, 12)
	s.set_content_margin_all(0)
	action_panel.add_theme_stylebox_override("panel", s)
	action_panel.set_anchors_preset(Control.PRESET_CENTER)
	action_panel.grow_horizontal = Control.GROW_DIRECTION_BOTH
	action_panel.grow_vertical = Control.GROW_DIRECTION_BOTH
	action_panel.custom_minimum_size = Vector2(620, 0)
	action_panel.visible = false
	root.add_child(action_panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 0)
	action_panel.add_child(v)

	var head := PanelContainer.new()
	var hs := StyleBoxFlat.new()
	hs.bg_color = Color(1, 1, 1, 0.04)
	hs.corner_radius_top_left = 12
	hs.corner_radius_top_right = 12
	hs.content_margin_left = 22
	hs.content_margin_right = 22
	hs.content_margin_top = 16
	hs.content_margin_bottom = 14
	head.add_theme_stylebox_override("panel", hs)
	v.add_child(head)
	var hv := VBoxContainer.new()
	hv.add_theme_constant_override("separation", 2)
	head.add_child(hv)
	action_title = _label("", 26, Color.WHITE, true)
	hv.add_child(action_title)
	action_sub = _label("", 14, MUTED)
	hv.add_child(action_sub)

	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(620, 0)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.name = "Scroll"
	v.add_child(scroll)
	var margin := MarginContainer.new()
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for side in ["left", "right"]:
		margin.add_theme_constant_override("margin_" + side, 12)
	margin.add_theme_constant_override("margin_top", 8)
	margin.add_theme_constant_override("margin_bottom", 8)
	scroll.add_child(margin)
	action_list = VBoxContainer.new()
	action_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	action_list.add_theme_constant_override("separation", 2)
	margin.add_child(action_list)

	var foot := HBoxContainer.new()
	foot.add_theme_constant_override("separation", 14)
	foot.alignment = BoxContainer.ALIGNMENT_CENTER
	var fm := MarginContainer.new()
	fm.add_theme_constant_override("margin_bottom", 12)
	fm.add_theme_constant_override("margin_top", 4)
	fm.add_child(foot)
	v.add_child(fm)
	for pair in [["↑↓", "wybór"], ["E", "zrób"], ["1–9", "szybko"], ["Esc", "zamknij"]]:
		foot.add_child(_hint(pair[0], pair[1]))


func _center_panel(root: Control, min_size: Vector2) -> PanelContainer:
	var p := PanelContainer.new()
	p.add_theme_stylebox_override("panel", _style(0.93, 12))
	p.set_anchors_preset(Control.PRESET_CENTER)
	p.grow_horizontal = Control.GROW_DIRECTION_BOTH
	p.grow_vertical = Control.GROW_DIRECTION_BOTH
	p.custom_minimum_size = min_size
	p.visible = false
	root.add_child(p)
	return p


func _build_stash(root: Control) -> void:
	stash_panel = _center_panel(root, Vector2(540, 0))
	var sv := VBoxContainer.new()
	sv.add_theme_constant_override("separation", 10)
	stash_panel.add_child(sv)
	sv.add_child(_label("Szuflada i biurko", 24, Color.WHITE, true))
	sv.add_child(_label("Kliknij przedmiot, by go zabrać. Klik w slot ekwipunku – odkładasz.", 13, MUTED))
	stash_grid = GridContainer.new()
	stash_grid.columns = 3
	stash_grid.add_theme_constant_override("h_separation", 8)
	stash_grid.add_theme_constant_override("v_separation", 8)
	sv.add_child(stash_grid)


func _build_phone(root: Control) -> void:
	phone_panel = _center_panel(root, Vector2(460, 0))
	var pv := VBoxContainer.new()
	pv.add_theme_constant_override("separation", 10)
	phone_panel.add_child(pv)
	pv.add_child(_label("Telefon", 24, Color.WHITE, true))
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(440, 380)
	pv.add_child(scroll)
	phone_list = VBoxContainer.new()
	phone_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	phone_list.add_theme_constant_override("separation", 8)
	scroll.add_child(phone_list)


func _build_help(root: Control) -> void:
	help_panel = _center_panel(root, Vector2(640, 0))
	var hv := VBoxContainer.new()
	hv.add_theme_constant_override("separation", 12)
	help_panel.add_child(hv)
	hv.add_child(_label("Jak grać", 28, Color.WHITE, true))
	hv.add_child(_label("Tydzień z życia kogoś, kto woli zostać w pokoju.", 16, MUTED))
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 16)
	grid.add_theme_constant_override("v_separation", 8)
	hv.add_child(grid)
	for row in [["A  D", "chodzenie w bok"], ["W  S", "w głąb i do przodu; przy drzwiach i schodach – przejście"], ["Shift", "sprint"],
			["E", "użyj tego, przy czym stoisz (akcje zjadają czas); gdzie indziej – inne akcje"], ["F", "następna opcja na karcie"],
			["Q", "szuflada w twoim pokoju – 6 kieszeni, wybieraj mądrze"], ["P", "telefon – wiadomości przychodzą, gdy go masz"],
			["1–6", "slot ekwipunku"], ["F5  F9", "zapis / wczytanie"], ["Esc", "menu"]]:
		var keys := HBoxContainer.new()
		keys.add_theme_constant_override("separation", 4)
		for k in row[0].split("  "):
			keys.add_child(_keycap(k, 13))
		grid.add_child(keys)
		grid.add_child(_label(row[1], 15, Color(0.88, 0.88, 0.92)))
	var tip := _label("Czas płynie tylko wtedy, gdy coś robisz. Każdy wybór coś zamyka.\nŻółty „!” nad kimś znaczy, że możesz coś z nim zrobić. Nie ma złych zakończeń – są tylko twoje.", 14, ACCENT)
	tip.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	tip.custom_minimum_size = Vector2(600, 0)
	hv.add_child(tip)
	var back := _menu_button("Wróć", 18)
	back.pressed.connect(_hide_help)
	hv.add_child(back)


func _hide_help() -> void:
	help_panel.visible = false
	_update_input()


func _build_pause(root: Control) -> void:
	pause_menu = Control.new()
	pause_menu.set_anchors_preset(Control.PRESET_FULL_RECT)
	pause_menu.visible = false
	root.add_child(pause_menu)
	var card := PanelContainer.new()
	card.add_theme_stylebox_override("panel", _style(0.9, 14))
	card.set_anchors_preset(Control.PRESET_CENTER)
	card.grow_horizontal = Control.GROW_DIRECTION_BOTH
	card.grow_vertical = Control.GROW_DIRECTION_BOTH
	card.custom_minimum_size = Vector2(380, 0)
	pause_menu.add_child(card)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 6)
	card.add_child(v)
	v.add_child(_label("Pauza", 34, Color.WHITE, true))
	v.add_child(_label("Świat czeka. Jak zawsze.", 14, MUTED))
	v.add_child(HSeparator.new())
	pause_buttons = VBoxContainer.new()
	pause_buttons.add_theme_constant_override("separation", 2)
	v.add_child(pause_buttons)
	var items := [
		["Wznów", _close_pause],
		["Zapisz", _pause_save],
		["Wczytaj", _pause_load],
		["Ustawienia", _show_settings],
		["Jak grać", _show_help],
		["Menu główne", _pause_to_main],
		["Wyjdź z gry", _save_and_quit],
	]
	for it in items:
		var b := _menu_button(it[0], 22)
		b.pressed.connect(it[1])
		pause_buttons.add_child(b)


func _pause_save() -> void:
	_notice("Zapisano." if sim.save_game(SAVE_PATH) else "Nie udało się zapisać.")


func _pause_load() -> void:
	if sim.load_game(SAVE_PATH):
		_close_pause()
		_notice("Wczytano.")
	else:
		_notice("Brak zapisu.")


func _show_settings() -> void:
	settings_panel.open()


func _show_help() -> void:
	help_panel.visible = true


func _pause_to_main() -> void:
	if not sim.is_finished():
		sim.save_game(SAVE_PATH)
	_to_menu()


func _to_menu() -> void:
	var tw := create_tween()
	tw.tween_property(fade, "color:a", 1.0, 0.4)
	tw.tween_callback(Session.to_menu)


func _save_and_quit() -> void:
	if not sim.is_finished():
		sim.save_game(SAVE_PATH)
	get_tree().quit()


var menu_blur_amount := 0.0


func _menu_blur(on: bool) -> void:
	create_tween().tween_method(_set_blur, menu_blur_amount, 1.0 if on else 0.0, 0.35)


func _set_blur(v: float) -> void:
	menu_blur_amount = v
	post.set_shader_parameter("blur", 0.6 * v)
	post.set_shader_parameter("dim", 0.35 * v)


func _open_pause() -> void:
	_close_panels()
	pause_menu.visible = true
	_menu_blur(true)
	_update_input()
	pause_buttons.get_child(0).grab_focus()


func _close_pause() -> void:
	pause_menu.visible = false
	help_panel.visible = false
	_menu_blur(false)
	_update_input()


func _overlay_open() -> bool:
	return pause_menu.visible or end_panel.visible


func _any_menu() -> bool:
	return action_panel.visible or stash_panel.visible or phone_panel.visible or help_panel.visible or _overlay_open()


func _update_input() -> void:
	player.input_enabled = not _any_menu() and not busy


func _close_panels() -> void:
	for p in [action_panel, stash_panel, phone_panel, help_panel]:
		p.visible = false
	_update_input()


func _set_menu(panel: Control, open: bool) -> void:
	for p in [action_panel, stash_panel, phone_panel, help_panel]:
		if p != panel:
			p.visible = false
	panel.visible = open
	if open:
		panel.modulate.a = 0.0
		panel.scale = Vector2(0.97, 0.97)
		panel.pivot_offset = panel.size / 2.0
		var tw := create_tween().set_parallel(true)
		tw.tween_property(panel, "modulate:a", 1.0, 0.14)
		tw.tween_property(panel, "scale", Vector2.ONE, 0.18).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	_update_input()


# ================================================================== state

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


func _npc_names() -> Dictionary:
	var names := {}
	for n in sim.get_npcs():
		names[n.id] = n.name
	return names


func _has_item(id: String) -> bool:
	for it in sim.get_inventory():
		if not it.is_empty() and it.id == id:
			return true
	return false


func _refresh() -> void:
	var c := sim.get_clock()
	if not busy:
		clock_label.text = "%s  %s" % [c.day_name, c.time]
	var loc := sim.get_location()
	loc_label.text = sim.get_location_name(loc)
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
	if action_panel.visible and not busy:
		_fill_actions()
	_tint_log()
	_update_hints(loc)


func _update_hints(loc: String) -> void:
	# Count meaningful actions here, remember which NPCs have one, and announce
	# new possibilities that appear while you're standing in the same place.
	var ids: Array = []
	var meaningful := 0
	var general := 0  # what's left for the E menu (not on a thing or a person)
	_ensure_interactables()
	npc_actions.clear()
	var npc_cost := {}
	var labels := {}
	for a in sim.get_actions():
		if not a.enabled:
			continue
		labels[a.id] = a.label
		if not attached.has(String(a.id)) and a.npcs.is_empty():
			general += 1
		if a.group in QUIET_GROUPS:
			continue
		meaningful += 1
		# The bed is always there; only bedtime is worth announcing.
		if a.group != "pokój" or a.id == "sleep":
			ids.append(a.id)
		# Above an NPC show the weightiest thing you can do with them.
		for n in a.npcs:
			if not npc_actions.has(n) or int(a.cost) > int(npc_cost[n]):
				npc_actions[n] = a.label
				npc_cost[n] = a.cost
	hint_actions.text = "Inne (%d)" % general if general > 0 else "Inne"
	hint_stash.visible = sim.can_use_stash()
	var unread := sim.get_phone_log().size() - phone_seen
	var badge: Control = hint_phone_badge.get_parent()
	badge.visible = unread > 0 and _has_item("phone")
	hint_phone_badge.text = str(unread)

	# While an action plays out we keep the old snapshot, so anything that
	# became possible during it is announced once the time-lapse ends.
	if busy:
		return
	if not first_refresh and not _overlay_open():
		if loc != last_loc:
			_show_banner(sim.get_location_name(loc), meaningful)
		else:
			for id in ids:
				if not last_action_ids.has(id):
					_toast("Nowa możliwość:  " + String(labels[id]), Color("#9fd8ff"), null, "Możesz teraz  ·  E")
	first_refresh = false
	last_loc = loc
	last_action_ids = ids


func _show_banner(title: String, count: int) -> void:
	banner_title.text = title
	if count == 0:
		banner_sub.text = ""
	elif count == 1:
		banner_sub.text = "Jedna rzecz do zrobienia  ·  podejdź i naciśnij E"
	else:
		banner_sub.text = "%d rzeczy do zrobienia  ·  podejdź i naciśnij E" % count
	var tw := create_tween()
	tw.tween_property(banner, "modulate:a", 1.0, 0.25)
	tw.tween_interval(1.8)
	tw.tween_property(banner, "modulate:a", 0.0, 0.6)


func _tint_log() -> void:
	# Narration colour follows the hidden warmth: blue-grey → cream.
	var w := clampf(sim.get_warmth() / 100.0, 0.0, 1.0)
	var col := Color(0.72, 0.78, 0.9).lerp(Color(1.0, 0.93, 0.8), w)
	var n := log_box.get_child_count()
	for i in n:
		var l: Label = log_box.get_child(i)
		if String(l.get_meta("kind", "narr")) == "narr":
			l.add_theme_color_override("font_color", col)
		l.modulate.a = lerpf(0.35, 1.0, float(i + 1) / n)


# ================================================================== world prompts

func _prompt_chip(key_text: String, text: String, sub := "") -> PanelContainer:
	var p := PanelContainer.new()
	var s := _style(0.86, 7)
	s.content_margin_left = 8
	s.content_margin_right = 12
	s.content_margin_top = 5
	s.content_margin_bottom = 5
	p.add_theme_stylebox_override("panel", s)
	p.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", 8)
	h.mouse_filter = Control.MOUSE_FILTER_IGNORE
	p.add_child(h)
	var cap := _keycap(key_text, 13)
	cap.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	h.add_child(cap)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 1)
	v.mouse_filter = Control.MOUSE_FILTER_IGNORE
	h.add_child(v)
	v.add_child(_label(text, 15, Color.WHITE, true))
	if not sub.is_empty():
		# Fixed width, otherwise autowrap collapses the label to one letter per line.
		var sl := _label(sub, 12, Color(1.0, 0.72, 0.62))
		sl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		sl.custom_minimum_size = Vector2(240, 0)
		v.add_child(sl)
	p.set_meta("style", s)
	return p


## Collects what can be done right here: doors, stairs, the bus and NPCs.
## Returns key -> {cap, text, sub, pos, active, ok, order}.
func _collect_prompts() -> Dictionary:
	var out := {}
	var seg := sim.get_player_seg()
	var segd: Dictionary = world.get_segment(seg)
	var px := player.global_position.x
	var py := float(segd.get("y", 0.0))
	var pz := float(segd.get("z", 0.0))
	var links: Array = world.find_links(seg, px, 3.0)
	# The link W/S would use right now: nearest per key within reach (same rule as the player).
	var chosen := {}
	for l in links:
		var d := absf(float(l.x) - px)
		if d <= 1.6 and (not chosen.has(l.key) or d < float(chosen[l.key].d)):
			chosen[l.key] = {"id": l.id, "from_a": l.from_a, "d": d}
	for l in links:
		var def: Dictionary = world.get_link_def(l.id)
		var anim := String(def.get("anim", ""))
		var up: bool = l.key != "down"
		var text: String = l.label
		if anim == "stairs":
			text = ("▲  " if up else "▼  ") + text
		elif anim == "bus":
			text = "🚌  " + text
		var active: bool = chosen.has(l.key) and chosen[l.key].id == l.id and chosen[l.key].from_a == l.from_a
		var sub := ""
		if not l.ok:
			text = "🔒  " + text
			if active:
				sub = String(l.why)
		out["link:%s:%s" % [l.id, l.from_a]] = {
			"cap": "W" if up else "S", "text": text, "sub": sub,
			"pos": Vector3(float(l.x), py + 2.2, pz), "active": active, "ok": l.ok,
			# Stack bottom-up: stairs down, stairs up (kept as a pair), then doors.
			"order": (1 if up else 0) if anim == "stairs" else 2,
		}
	for id in npc_actions:
		if true:
			continue  # people now get the focus card instead
		var a: Dictionary = world.get_npc_anchor(id)
		if not a.visible:
			continue
		var p: Vector3 = a.pos
		if absf(p.x - px) > 3.0 or absf(p.y - 2.2 - py) > 1.0:
			continue
		out["npc:" + id] = {
			"cap": "E", "text": String(npc_actions[id]), "sub": "",
			"pos": p + Vector3(0, 0.55, 0), "active": absf(p.x - px) < 1.8, "ok": true, "order": 3,
		}
	return out


func _update_prompts() -> void:
	var cam := get_viewport().get_camera_3d()
	var wanted := {}
	if cam != null and not _any_menu() and not busy and not player.is_animating() and not sim.is_finished():
		wanted = _collect_prompts()
	for key in prompt_chips.keys():
		if not wanted.has(key):
			var old: Control = prompt_chips[key]
			prompt_chips.erase(key)
			var tw := create_tween()
			tw.tween_property(old, "modulate:a", 0.0, 0.12)
			tw.tween_callback(old.queue_free)
	var keys := wanted.keys()
	keys.sort_custom(func(a: String, b: String) -> bool: return int(wanted[a].order) < int(wanted[b].order))
	var placed: Array[Rect2] = []
	var dt := get_process_delta_time()
	for key in keys:
		var w: Dictionary = wanted[key]
		var sig := "%s|%s|%s" % [w.cap, w.text, w.sub]
		var chip: PanelContainer = prompt_chips.get(key)
		if chip == null or String(chip.get_meta("sig", "")) != sig:
			var alpha := 0.0
			if chip != null:
				alpha = chip.modulate.a
				chip.queue_free()
			chip = _prompt_chip(w.cap, w.text, w.sub)
			chip.set_meta("sig", sig)
			chip.modulate.a = alpha
			prompt_layer.add_child(chip)
			prompt_chips[key] = chip
		var size := chip.get_combined_minimum_size()
		var screen := cam.unproject_position(w.pos)
		var rect := Rect2(screen - Vector2(size.x / 2.0, size.y), size)
		# Stack chips that would overlap (e.g. stairs up and down at the same spot).
		for _i in 8:
			var bumped := false
			for r in placed:
				if r.grow(3.0).intersects(rect):
					rect.position.y = r.position.y - rect.size.y - 6.0
					bumped = true
			if not bumped:
				break
		placed.append(rect)
		var target := rect.position.round()
		chip.position = target if chip.modulate.a < 0.05 else chip.position.lerp(target, minf(1.0, dt * 18.0))
		chip.size = size
		var target_alpha := 1.0 if w.active else 0.5
		chip.modulate.a = move_toward(chip.modulate.a, target_alpha, dt * 5.0)
		var st: StyleBoxFlat = chip.get_meta("style")
		st.border_color = (ACCENT if w.ok else Color(1.0, 0.5, 0.45)) if w.active else Color(1, 1, 1, 0.07)
		st.set_border_width_all(2 if w.active else 1)


# ================================================================== log & notices

func _on_message(kind: String, from: String, text: String) -> void:
	var l := _label("", 16)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(480, 0)
	l.add_theme_constant_override("outline_size", 5)
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.75))
	l.set_meta("kind", kind)
	match kind:
		"phone":
			l.text = "✉ %s: %s" % [from, text]
			l.add_theme_color_override("font_color", Color("#ffb3d1"))
			if _has_item("phone"):
				_toast(from + ":  " + text, Color("#ffb3d1"), _icon("phone"), "Nowa wiadomość  ·  P")
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


func _ensure_interactables() -> void:
	if not interactables.is_empty() or world == null:
		return
	interactables = world.get_interactables()
	for it in interactables:
		for id in it.acts:
			attached[String(id)] = true


## The thing or person the player is standing at, with what can be done there now.
func _find_focus() -> Dictionary:
	_ensure_interactables()
	var by_id := {}
	for a in sim.get_actions():
		by_id[String(a.id)] = a
	var pp: Vector3 = player.global_position
	var best := {}
	var best_d := 1e9
	for it in interactables:
		var acts: Array = []
		for id in it.acts:
			if by_id.has(String(id)):
				acts.append(by_id[String(id)])
		if acts.is_empty():
			continue
		var pos: Vector3 = it.pos
		if pos.y < pp.y or pos.y - pp.y > 3.2:
			continue
		var dx := absf(pos.x - pp.x)
		var dz := absf(pos.z - pp.z)
		if dx > 1.4 or dz > 3.2:
			continue
		var d := dx + 0.25 * dz
		if d < best_d:
			best_d = d
			best = {"key": "prop:%d" % int(it.index), "index": int(it.index), "name": String(it.name), "pos": pos, "acts": acts}
	# People: their actions that aren't tied to a prop.
	var per_npc := {}
	for a in by_id.values():
		if attached.has(String(a.id)) or a.npcs.is_empty():
			continue
		for n in a.npcs:
			if not per_npc.has(n):
				per_npc[n] = []
			per_npc[n].append(a)
	var names := _npc_names()
	for n in per_npc:
		var anc: Dictionary = world.get_npc_anchor(n)
		if not anc.visible:
			continue
		var pos: Vector3 = anc.pos
		var dx := absf(pos.x - pp.x)
		var dz := absf(pos.z - pp.z)
		if dx > 1.6 or absf(pos.y - 2.2 - pp.y) > 1.0 or dz > 3.2:
			continue
		var d := dx + 0.2 * dz
		if d < best_d:
			best_d = d
			best = {"key": "npc:" + String(n), "index": -1, "name": String(names.get(n, n)), "pos": pos + Vector3(0, 0.45, 0), "acts": per_npc[n]}
	return best


func _focus_card_build() -> PanelContainer:
	var card := PanelContainer.new()
	var st := _style(0.9, 9)
	st.content_margin_left = 12
	st.content_margin_right = 12
	st.content_margin_top = 8
	st.content_margin_bottom = 8
	st.border_color = ACCENT
	st.set_border_width_all(2)
	card.add_theme_stylebox_override("panel", st)
	card.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 3)
	v.mouse_filter = Control.MOUSE_FILTER_IGNORE
	card.add_child(v)
	v.add_child(_label(String(focus.name).to_upper(), 12, Color(1, 1, 1, 0.5), true))
	var acts: Array = focus.acts
	for i in acts.size():
		var a: Dictionary = acts[i]
		var sel := i == focus_sel
		var h := HBoxContainer.new()
		h.add_theme_constant_override("separation", 8)
		h.mouse_filter = Control.MOUSE_FILTER_IGNORE
		v.add_child(h)
		var cap_text := "E" if sel else " "
		if sel:
			h.add_child(_keycap(cap_text, 12))
		else:
			var spacer := Control.new()
			spacer.custom_minimum_size = Vector2(22, 0)
			h.add_child(spacer)
		var enabled: bool = a.enabled
		var col := Color.WHITE if enabled else Color(1, 1, 1, 0.45)
		if sel and enabled:
			col = ACCENT
		var cost := "do rana" if a.id == "sleep" else _format_cost(a.cost)
		h.add_child(_label(("🔒 " if not enabled else "") + String(a.label), 16, col, sel))
		h.add_child(_label(cost, 12, Color(1, 1, 1, 0.45)))
		if sel and not enabled and not String(a.why).is_empty():
			var why := _label(String(a.why), 12, Color(1.0, 0.65, 0.55))
			why.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			why.custom_minimum_size = Vector2(260, 0)
			v.add_child(why)
	if acts.size() > 1:
		v.add_child(_label("F – następna opcja", 11, Color(1, 1, 1, 0.35)))
	return card


func _update_focus() -> void:
	var cam := get_viewport().get_camera_3d()
	var f := {}
	if cam != null and not _any_menu() and not busy and not player.is_animating() and not sim.is_finished():
		f = _find_focus()
	var old_key := String(focus.get("key", ""))
	if String(f.get("key", "")) != old_key:
		# Start on the first thing you can actually do.
		focus_sel = 0
		var fa: Array = f.get("acts", [])
		for i in fa.size():
			if fa[i].enabled:
				focus_sel = i
				break
	focus = f
	world.set_highlight(int(focus.get("index", -1)) if not focus.is_empty() else -1)
	if focus.is_empty():
		if focus_card != null:
			focus_card.queue_free()
			focus_card = null
			focus_sig = ""
		return
	var acts: Array = focus.acts
	focus_sel = clampi(focus_sel, 0, acts.size() - 1)
	var sig := "%s|%d" % [focus.key, focus_sel]
	for a in acts:
		sig += "|%s:%s" % [a.id, a.enabled]
	if sig != focus_sig or focus_card == null:
		if focus_card != null:
			focus_card.queue_free()
		focus_card = _focus_card_build()
		prompt_layer.add_child(focus_card)
		focus_sig = sig
	var size := focus_card.get_combined_minimum_size()
	var screen := cam.unproject_position(focus.pos)
	focus_card.size = size
	focus_card.position = (screen - Vector2(size.x / 2.0, size.y + 6.0)).round()


func _use_focus() -> void:
	var acts: Array = focus.acts
	var a: Dictionary = acts[focus_sel]
	if not a.enabled:
		_notice(String(a.why))
		return
	_on_action(String(a.id))


func _process(delta: float) -> void:
	if not pending_changes.is_empty() and not busy and not player.is_animating():
		_flush_changes()
	if notice_time > 0.0:
		notice_time -= delta
		notice_label.modulate.a = clampf(notice_time, 0.0, 1.0)
	_update_prompts()
	_update_focus()


func _on_day_started(day: int, day_name: String) -> void:
	if day > 0 and not sim.is_finished():
		sim.save_game(SAVE_PATH)
	day_label.text = day_name
	var t := create_tween()
	t.tween_property(fade, "color:a", 0.85, 0.25)
	t.parallel().tween_property(day_label, "modulate:a", 1.0, 0.25)
	t.tween_interval(1.2)
	t.tween_property(fade, "color:a", 0.0, 0.8)
	t.parallel().tween_property(day_label, "modulate:a", 0.0, 0.8)


func _on_screen_fade(alpha: float, duration: float) -> void:
	create_tween().tween_property(fade, "color:a", alpha, duration)


# ================================================================== action menu

func _fill_actions() -> void:
	for ch in action_list.get_children():
		action_list.remove_child(ch)
		ch.queue_free()
	action_buttons.clear()
	var c := sim.get_clock()
	action_title.text = sim.get_location_name(sim.get_location())
	action_sub.text = "%s, %s" % [c.day_name, c.time]
	_ensure_interactables()
	var acts: Array = []
	for a in sim.get_actions():
		if not attached.has(String(a.id)) and a.npcs.is_empty():
			acts.append(a)
	if acts.is_empty():
		action_list.add_child(_label("Podejdź do czegoś albo do kogoś – zobaczysz, co możesz zrobić.", 16, MUTED))
		return
	acts.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var ga: int = GROUPS.get(a.group, ["", "", 99])[2]
		var gb: int = GROUPS.get(b.group, ["", "", 99])[2]
		if ga != gb:
			return ga < gb
		return int(a.enabled) > int(b.enabled))
	var names := _npc_names()
	var current_group := "__"
	var i := 0
	for a in acts:
		action_labels[a.id] = a.label
		if a.group != current_group:
			current_group = a.group
			var meta: Array = GROUPS.get(a.group, [String(a.group).capitalize(), "•", 99])
			var gh := _label(String(meta[0]).to_upper(), 11, Color(1, 1, 1, 0.35), true)
			var gm := MarginContainer.new()
			gm.add_theme_constant_override("margin_left", 12)
			gm.add_theme_constant_override("margin_top", 8 if i > 0 else 2)
			gm.add_child(gh)
			action_list.add_child(gm)
		i += 1
		var b := _action_row(a, i, names)
		action_list.add_child(b)
		action_buttons.append(b)
	var scroll: ScrollContainer = action_panel.get_child(0).get_node("Scroll")
	scroll.custom_minimum_size.y = mini(action_list.get_combined_minimum_size().y + 20, 520)
	for b in action_buttons:
		if not b.disabled:
			b.grab_focus()
			break


func _action_row(a: Dictionary, index: int, names: Dictionary) -> Button:
	var b := Button.new()
	b.custom_minimum_size = Vector2(596, 54)
	b.focus_mode = Control.FOCUS_ALL
	var normal := StyleBoxFlat.new()
	normal.bg_color = Color(1, 1, 1, 0.0)
	normal.set_corner_radius_all(8)
	var hover: StyleBoxFlat = normal.duplicate()
	hover.bg_color = Color(1, 1, 1, 0.07)
	var focus: StyleBoxFlat = normal.duplicate()
	focus.bg_color = Color(1, 0.81, 0.48, 0.12)
	focus.border_color = Color(1, 0.81, 0.48, 0.6)
	focus.set_border_width_all(1)
	b.add_theme_stylebox_override("normal", normal)
	b.add_theme_stylebox_override("hover", hover)
	b.add_theme_stylebox_override("pressed", focus)
	b.add_theme_stylebox_override("focus", focus)
	b.add_theme_stylebox_override("disabled", normal)
	b.mouse_entered.connect(func() -> void:
		if not b.disabled:
			b.grab_focus())

	var row := HBoxContainer.new()
	row.set_anchors_preset(Control.PRESET_FULL_RECT)
	row.add_theme_constant_override("separation", 12)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var pad := MarginContainer.new()
	pad.set_anchors_preset(Control.PRESET_FULL_RECT)
	pad.add_theme_constant_override("margin_left", 12)
	pad.add_theme_constant_override("margin_right", 12)
	pad.mouse_filter = Control.MOUSE_FILTER_IGNORE
	pad.add_child(row)
	b.add_child(pad)

	var enabled: bool = a.enabled
	var dim := 1.0 if enabled else 0.45
	var num := _label(str(index) if index <= 9 else "", 12, Color(1, 1, 1, 0.3 * dim), true)
	num.custom_minimum_size = Vector2(12, 0)
	num.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(num)
	var glyph := _label(GROUPS.get(a.group, ["", "•", 99])[1], 22, Color(ACCENT, dim))
	glyph.custom_minimum_size = Vector2(28, 0)
	glyph.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	glyph.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(glyph)

	var text := VBoxContainer.new()
	text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	text.alignment = BoxContainer.ALIGNMENT_CENTER
	text.add_theme_constant_override("separation", 0)
	text.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(text)
	text.add_child(_label(a.label, 17, Color(1, 1, 1, dim), true))
	var sub := ""
	if not enabled:
		sub = "🔒 " + String(a.why)
	elif not a.npcs.is_empty():
		var who: Array = []
		for n in a.npcs:
			who.append(names.get(n, n))
		sub = "z: " + ", ".join(who)
	if not sub.is_empty():
		var sl := _label(sub, 13, Color(1.0, 0.65, 0.55) if not enabled else MUTED)
		sl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		text.add_child(sl)
		if not enabled:
			b.custom_minimum_size.y = 62

	var chip := PanelContainer.new()
	var cs := StyleBoxFlat.new()
	cs.bg_color = Color(1, 1, 1, 0.08 * dim)
	cs.set_corner_radius_all(10)
	cs.content_margin_left = 10
	cs.content_margin_right = 10
	cs.content_margin_top = 2
	cs.content_margin_bottom = 2
	chip.add_theme_stylebox_override("panel", cs)
	chip.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	chip.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var cost_text := "do rana" if a.id == "sleep" else "⏱ " + _format_cost(a.cost)
	chip.add_child(_label(cost_text, 13, Color(1, 1, 1, 0.75 * dim)))
	row.add_child(chip)

	b.disabled = not enabled
	b.pressed.connect(_on_action.bind(a.id))
	return b


func _move_action_focus(step: int) -> void:
	if action_buttons.is_empty():
		return
	var idx := -1
	for i in action_buttons.size():
		if action_buttons[i].has_focus():
			idx = i
	for _n in action_buttons.size():
		idx = wrapi(idx + step, 0, action_buttons.size())
		if not action_buttons[idx].disabled:
			action_buttons[idx].grab_focus()
			var scroll: ScrollContainer = action_panel.get_child(0).get_node("Scroll")
			scroll.ensure_control_visible(action_buttons[idx])
			return


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
	_update_input()
	sim.perform(id)
	var after := sim.get_clock()
	await _play_timelapse(String(action_labels.get(id, "")), before, after)
	player.busy = false
	busy = false
	_update_input()
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
	tl_t0 = t0
	tl_ticks = ticks
	tl_day = after.day_name
	var tw := create_tween()
	tw.tween_property(timelapse, "modulate:a", 1.0, 0.15)
	tw.parallel().tween_method(_timelapse_step, 0.0, 1.0, duration).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	tw.tween_interval(0.25)
	tw.tween_property(timelapse, "modulate:a", 0.0, 0.2)
	await tw.finished
	timelapse.visible = false


# ================================================================== change popups

func _on_changes(list: Array) -> void:
	pending_changes.append_array(list)
	if not busy and not player.is_animating():
		_flush_changes()


func _flush_changes() -> void:
	if pending_changes.is_empty():
		return
	var list := pending_changes.duplicate()
	pending_changes.clear()
	var merged := {}
	var order: Array = []
	for ch in list:
		var key: String = "%s:%s:%s" % [ch.kind, ch.key, ch.label]
		if String(ch.kind).begins_with("item"):
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
				_toast("✦  " + ch.label, Color("#ffd98a"), null, "Konsekwencja")


func _player_screen_pos() -> Vector2:
	var cam := get_viewport().get_camera_3d()
	if cam == null:
		return Vector2(800, 400)
	return cam.unproject_position(player.global_position + Vector3(0, 2.4, 0))


func _float_text(text: String, color: Color, index: int) -> void:
	var l := _label(text, 22, color, true)
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


func _toast(text: String, color: Color, icon: Texture2D = null, heading := "") -> void:
	if toast_box.get_child_count() >= MAX_TOASTS:
		if toast_queue.size() < 6:
			toast_queue.append([text, color, icon, heading])
		return
	var p := PanelContainer.new()
	var style := _style(0.88)
	style.border_color = Color(color, 0.7)
	style.border_width_left = 4
	p.add_theme_stylebox_override("panel", style)
	p.mouse_filter = Control.MOUSE_FILTER_IGNORE
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
	if not heading.is_empty():
		col.add_child(_label(heading, 12, Color(1, 1, 1, 0.5)))
	var l := _label(text, 16, color, not heading.is_empty())
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(250, 0)
	col.add_child(l)
	toast_box.add_child(p)
	p.modulate.a = 0.0
	p.position.x = 40
	var tw := create_tween()
	tw.tween_property(p, "modulate:a", 1.0, 0.25)
	tw.tween_interval(4.5 if not heading.is_empty() else 3.2)
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


# ================================================================== stash & phone

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
		b.custom_minimum_size = Vector2(164, 54)
		b.tooltip_text = it.desc
		b.pressed.connect(func() -> void: sim.take_from_stash(i))
		stash_grid.add_child(b)
	if items.is_empty():
		stash_grid.add_child(_label("Pusto.", 16, MUTED))


func _fill_phone() -> void:
	for ch in phone_list.get_children():
		ch.queue_free()
	if not _has_item("phone"):
		phone_list.add_child(_label("Telefon leży w twoim pokoju.\nWiadomości poczekają. Ludzie – niekoniecznie.", 16, MUTED))
		return
	var msgs := sim.get_phone_log()
	phone_seen = msgs.size()
	_update_hints(sim.get_location())
	if msgs.is_empty():
		phone_list.add_child(_label("Brak wiadomości. Jak zwykle.", 16, MUTED))
	for i in range(msgs.size() - 1, -1, -1):
		var m: Dictionary = msgs[i]
		var bubble := PanelContainer.new()
		var bs := _style(1.0, 10, Color(0.22, 0.14, 0.2))
		bubble.add_theme_stylebox_override("panel", bs)
		var v := VBoxContainer.new()
		bubble.add_child(v)
		v.add_child(_label("%s  ·  %s" % [m.from, m.sent], 12, Color(1, 0.8, 0.9, 0.7), true))
		var l := _label(m.text, 15, Color("#ffe3ee"))
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		l.custom_minimum_size = Vector2(390, 0)
		v.add_child(l)
		phone_list.add_child(bubble)


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


# ================================================================== input

func _unhandled_input(event: InputEvent) -> void:
	if end_panel.visible:
		return
	if pause_menu.visible:
		if settings_panel.visible:
			return
		if event.is_action_pressed("ui_cancel"):
			if help_panel.visible:
				help_panel.visible = false
			else:
				_close_pause()
		elif event.is_action_pressed("move_up"):
			_focus_step(pause_buttons, -1)
		elif event.is_action_pressed("move_down"):
			_focus_step(pause_buttons, 1)
		elif event.is_action_pressed("act"):
			_press_focused(pause_buttons)
		else:
			return
		get_viewport().set_input_as_handled()
		return
	if busy:
		return
	if action_panel.visible:
		if event.is_action_pressed("ui_cancel") or (event.is_action_pressed("act") and not _focused_action()):
			_set_menu(action_panel, false)
		elif event.is_action_pressed("move_up"):
			_move_action_focus(-1)
		elif event.is_action_pressed("move_down"):
			_move_action_focus(1)
		elif event.is_action_pressed("act"):
			var f := _focused_action()
			if f:
				f.pressed.emit()
		elif event is InputEventKey and event.pressed and not event.echo and event.physical_keycode >= KEY_1 and event.physical_keycode <= KEY_9:
			var n: int = event.physical_keycode - KEY_1
			if n < action_buttons.size() and not action_buttons[n].disabled:
				action_buttons[n].pressed.emit()
		else:
			return
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("ui_cancel"):
		if _any_menu():
			_close_panels()
		else:
			_open_pause()
	elif event.is_action_pressed("help"):
		_set_menu(help_panel, not help_panel.visible)
	elif event.is_action_pressed("act") and not focus.is_empty():
		_use_focus()
	elif event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_F and not focus.is_empty():
		focus_sel = (focus_sel + 1) % (focus.acts as Array).size()
	elif event.is_action_pressed("act"):
		_set_menu(action_panel, true)
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
	elif event is InputEventKey and event.pressed and not event.echo and event.physical_keycode >= KEY_1 and event.physical_keycode <= KEY_6:
		_on_slot(event.physical_keycode - KEY_1)
	else:
		return
	get_viewport().set_input_as_handled()


func _focused_action() -> Button:
	for b in action_buttons:
		if b.has_focus():
			return b
	return null


func _focus_step(box: Container, step: int) -> void:
	var buttons: Array = []
	for c in box.get_children():
		if c is Button and not c.disabled:
			buttons.append(c)
	if buttons.is_empty():
		return
	var idx := -1
	for i in buttons.size():
		if buttons[i].has_focus():
			idx = i
	buttons[wrapi(idx + step, 0, buttons.size())].grab_focus()


func _press_focused(box: Container) -> void:
	for c in box.get_children():
		if c is Button and c.has_focus() and not c.disabled:
			c.pressed.emit()
			return


# ================================================================== ending

func _restart_week() -> void:
	Session.delete_save()
	Session.start_new()


func _on_finished() -> void:
	_close_panels()
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
	left.add_child(_label("KONIEC TYGODNIA", 14, Color(1, 1, 1, 0.5), true))
	left.add_child(_label(e.title, 44, Color.WHITE, true))
	var body := _label(e.text, 18, Color(0.92, 0.9, 0.86))
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	left.add_child(body)
	for line in e.epilogue:
		var l := _label("·  " + line, 16, Color(0.8, 0.8, 0.85))
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		left.add_child(l)
	var buttons := VBoxContainer.new()
	left.add_child(buttons)
	var again := _menu_button("Jeszcze raz ten tydzień", 22)
	again.pressed.connect(_restart_week)
	buttons.add_child(again)
	var menu := _menu_button("Menu główne", 22)
	menu.pressed.connect(func() -> void:
		Session.delete_save()
		_to_menu())
	buttons.add_child(menu)

	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cols.add_child(right)
	right.add_child(_label("Dlaczego tak wyszło", 22, Color.WHITE, true))
	var why := RichTextLabel.new()
	why.size_flags_vertical = Control.SIZE_EXPAND_FILL
	why.custom_minimum_size = Vector2(560, 500)
	why.add_theme_font_override("normal_font", font_regular)
	why.add_theme_font_size_override("normal_font_size", 14)
	why.add_theme_color_override("default_color", Color(0.78, 0.82, 0.9))
	why.text = "\n".join(e.why) if not e.why.is_empty() else "Nic szczególnego. Tydzień po prostu się wydarzył."
	right.add_child(why)

	end_panel.modulate.a = 0.0
	end_panel.visible = true
	create_tween().tween_property(end_panel, "modulate:a", 1.0, 1.5)
	again.grab_focus()
