class_name UIKit
extends RefCounted
## Shared look for menus: fonts, panels, labels, keycaps and menu buttons.

const ACCENT := Color("#ffcf7a")
const MUTED := Color(0.72, 0.72, 0.78)

static var _regular: SystemFont
static var _bold: SystemFont


static func font(bold := false) -> SystemFont:
	if _regular == null:
		var names := PackedStringArray(["Avenir Next", "Segoe UI", "Noto Sans", "Helvetica Neue", "Sans-Serif"])
		_regular = SystemFont.new()
		_regular.font_names = names
		_regular.font_weight = 500
		_bold = SystemFont.new()
		_bold.font_names = names
		_bold.font_weight = 700
	return _bold if bold else _regular


static func panel_style(alpha := 0.9, radius := 12, bg := Color(0.06, 0.06, 0.09)) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = Color(bg.r, bg.g, bg.b, alpha)
	s.set_corner_radius_all(radius)
	s.set_content_margin_all(18)
	s.border_color = Color(1, 1, 1, 0.07)
	s.set_border_width_all(1)
	return s


static func label(text := "", size := 18, color := Color.WHITE, bold := false) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_override("font", font(bold))
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l


static func keycap(key: String, size := 13) -> PanelContainer:
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
	p.add_child(label(key, size, Color(0.12, 0.12, 0.14), true))
	p.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return p


static func menu_button(text: String, size := 26) -> Button:
	var b := Button.new()
	b.text = text
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.flat = true
	b.add_theme_font_override("font", font(true))
	b.add_theme_font_size_override("font_size", size)
	b.add_theme_color_override("font_color", Color(0.86, 0.86, 0.9))
	b.add_theme_color_override("font_hover_color", ACCENT)
	b.add_theme_color_override("font_focus_color", ACCENT)
	b.add_theme_color_override("font_hover_pressed_color", ACCENT)
	b.add_theme_color_override("font_disabled_color", Color(1, 1, 1, 0.25))
	var empty := StyleBoxEmpty.new()
	for st in ["normal", "hover", "pressed", "focus", "disabled", "hover_pressed"]:
		b.add_theme_stylebox_override(st, empty)
	b.focus_entered.connect(func() -> void: b.create_tween().tween_property(b, "position:x", 14.0, 0.12))
	b.focus_exited.connect(func() -> void: b.create_tween().tween_property(b, "position:x", 0.0, 0.12))
	b.mouse_entered.connect(func() -> void:
		if not b.disabled:
			b.grab_focus())
	return b


## Segmented choice: returns an HBox of toggle buttons; `on_pick(index)` fires on change.
static func segmented(options: Array, selected: int, on_pick: Callable) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	var group := ButtonGroup.new()
	for i in options.size():
		var b := Button.new()
		b.text = str(options[i])
		b.toggle_mode = true
		b.button_group = group
		b.button_pressed = i == selected
		b.custom_minimum_size = Vector2(108, 38)
		b.add_theme_font_override("font", font(true))
		b.add_theme_font_size_override("font_size", 15)
		var normal := StyleBoxFlat.new()
		normal.bg_color = Color(1, 1, 1, 0.06)
		normal.set_corner_radius_all(8)
		var hover: StyleBoxFlat = normal.duplicate()
		hover.bg_color = Color(1, 1, 1, 0.12)
		var on: StyleBoxFlat = normal.duplicate()
		on.bg_color = Color(ACCENT, 0.22)
		on.border_color = ACCENT
		on.set_border_width_all(1)
		var focus: StyleBoxFlat = normal.duplicate()
		focus.bg_color = Color(0, 0, 0, 0)
		focus.border_color = Color(1, 1, 1, 0.5)
		focus.set_border_width_all(1)
		b.add_theme_stylebox_override("normal", normal)
		b.add_theme_stylebox_override("hover", hover)
		b.add_theme_stylebox_override("pressed", on)
		b.add_theme_stylebox_override("hover_pressed", on)
		b.add_theme_stylebox_override("focus", focus)
		b.add_theme_color_override("font_pressed_color", ACCENT)
		b.add_theme_color_override("font_hover_pressed_color", ACCENT)
		b.pressed.connect(func() -> void: on_pick.call(i))
		row.add_child(b)
	return row


static func focus_step(box: Container, step: int) -> void:
	var buttons: Array = []
	for c in box.get_children():
		if c is Button and not c.disabled and c.visible:
			buttons.append(c)
	if buttons.is_empty():
		return
	var idx := -1
	for i in buttons.size():
		if buttons[i].has_focus():
			idx = i
	buttons[wrapi(idx + step, 0, buttons.size())].grab_focus()
