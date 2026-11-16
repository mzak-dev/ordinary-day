class_name SettingsPanel
extends PanelContainer
## Graphics and display settings, shared by the main menu and the pause menu.

signal closed

const QUALITY_HINTS := [
	"Bez globalnego oświetlenia, odbić i mgły wolumetrycznej. Niższa rozdzielczość 3D (FSR). Dla laptopów.",
	"Mgła, cienie słońca i głębia ostrości; bez odbić i GI. Rozdzielczość 3D ok. 77% (FSR).",
	"Wszystko: globalne oświetlenie, odbicia w kałużach, cienie lamp, pełna rozdzielczość.",
]

var _hint: Label
var _first: Control


func _init() -> void:
	add_theme_stylebox_override("panel", UIKit.panel_style(0.94, 14))
	set_anchors_preset(Control.PRESET_CENTER)
	grow_horizontal = Control.GROW_DIRECTION_BOTH
	grow_vertical = Control.GROW_DIRECTION_BOTH
	custom_minimum_size = Vector2(560, 0)

	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 14)
	add_child(v)
	v.add_child(UIKit.label("Ustawienia", 30, Color.WHITE, true))

	v.add_child(UIKit.label("JAKOŚĆ GRAFIKI", 12, Color(1, 1, 1, 0.45), true))
	var q := UIKit.segmented(Session.QUALITY_NAMES, Session.quality, _on_quality)
	v.add_child(q)
	_first = q.get_child(Session.quality)
	_hint = UIKit.label(QUALITY_HINTS[Session.quality], 13, UIKit.MUTED)
	_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_hint.custom_minimum_size = Vector2(520, 0)
	v.add_child(_hint)

	v.add_child(UIKit.label("LIMIT KLATEK", 12, Color(1, 1, 1, 0.45), true))
	v.add_child(UIKit.segmented(["30", "60", "Bez limitu"], Session.fps_index, func(i: int) -> void: Session.set_fps_index(i)))

	v.add_child(UIKit.label("EKRAN", 12, Color(1, 1, 1, 0.45), true))
	v.add_child(UIKit.segmented(["Okno", "Pełny ekran"], 1 if Session.fullscreen else 0, func(i: int) -> void: Session.set_fullscreen(i == 1)))

	var back := UIKit.menu_button("Wróć", 20)
	back.pressed.connect(close)
	v.add_child(back)


func open() -> void:
	visible = true
	modulate.a = 0.0
	create_tween().tween_property(self, "modulate:a", 1.0, 0.15)
	if _first:
		_first.grab_focus()


func close() -> void:
	visible = false
	closed.emit()


func _on_quality(i: int) -> void:
	Session.set_quality(i)
	_hint.text = QUALITY_HINTS[i]


func _unhandled_input(event: InputEvent) -> void:
	if visible and event.is_action_pressed("ui_cancel"):
		close()
		get_viewport().set_input_as_handled()
