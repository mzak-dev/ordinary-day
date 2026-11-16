extends Control
## Main menu scene. The game itself is only loaded after "Nowy tydzień" or
## "Kontynuuj", so the menu stays cheap on laptops.

const BG_PATH := "res://assets/ui/menu_bg.png"

var buttons: VBoxContainer
var continue_btn: Button
var new_btn: Button
var save_label: Label
var settings: SettingsPanel
var help: PanelContainer
var fade: ColorRect
var bg: TextureRect
var confirm_new := false
var leaving := false


func _ready() -> void:
	theme = Theme.new()
	theme.default_font = UIKit.font()
	_build()
	fade.color.a = 1.0
	create_tween().tween_property(fade, "color:a", 0.0, 0.8)
	_start_pan()
	await get_tree().process_frame
	(continue_btn if not continue_btn.disabled else new_btn).grab_focus()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="):
			await get_tree().create_timer(2.5).timeout
			if "--settings" in OS.get_cmdline_user_args():
				_open_settings()
				await get_tree().create_timer(0.6).timeout
			get_viewport().get_texture().get_image().save_png(arg.substr(7))
			get_tree().quit()


func _build() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var base := ColorRect.new()
	base.color = Color(0.05, 0.05, 0.08)
	base.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(base)

	bg = TextureRect.new()
	if ResourceLoader.exists(BG_PATH):
		bg.texture = load(BG_PATH)
	bg.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	bg.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(bg)

	var shade_tex := GradientTexture2D.new()
	var g := Gradient.new()
	g.set_color(0, Color(0.03, 0.03, 0.05, 0.95))
	g.set_color(1, Color(0.03, 0.03, 0.05, 0.0))
	shade_tex.gradient = g
	shade_tex.fill_from = Vector2(0.3, 0)
	shade_tex.fill_to = Vector2(1.0, 0)
	var shade := TextureRect.new()
	shade.texture = shade_tex
	shade.set_anchors_preset(Control.PRESET_LEFT_WIDE)
	shade.custom_minimum_size = Vector2(900, 0)
	shade.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	shade.stretch_mode = TextureRect.STRETCH_SCALE
	shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(shade)

	var motes := CPUParticles2D.new()
	motes.amount = 40
	motes.lifetime = 12.0
	motes.preprocess = 12.0
	motes.emission_shape = CPUParticles2D.EMISSION_SHAPE_RECTANGLE
	motes.emission_rect_extents = Vector2(900, 500)
	motes.position = Vector2(800, 450)
	motes.direction = Vector2(1, -0.3)
	motes.spread = 60
	motes.gravity = Vector2.ZERO
	motes.initial_velocity_min = 4
	motes.initial_velocity_max = 14
	motes.scale_amount_min = 1.5
	motes.scale_amount_max = 3.5
	motes.color = Color(1, 0.92, 0.8, 0.35)
	add_child(motes)

	var col := VBoxContainer.new()
	col.set_anchors_preset(Control.PRESET_LEFT_WIDE)
	col.position = Vector2(110, 0)
	col.custom_minimum_size = Vector2(540, 0)
	col.alignment = BoxContainer.ALIGNMENT_CENTER
	col.add_theme_constant_override("separation", 6)
	add_child(col)
	var title := VBoxContainer.new()
	title.add_theme_constant_override("separation", -30)
	title.add_child(UIKit.label("ORDINARY", 92, Color.WHITE, true))
	title.add_child(UIKit.label("DAY", 92, UIKit.ACCENT, true))
	col.add_child(title)
	col.add_child(UIKit.label("Tydzień jak każdy inny. Prawie.", 21, UIKit.MUTED))
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(0, 40)
	col.add_child(spacer)

	buttons = VBoxContainer.new()
	buttons.add_theme_constant_override("separation", 2)
	col.add_child(buttons)
	continue_btn = UIKit.menu_button("Kontynuuj")
	continue_btn.disabled = not Session.has_save() or Session.save_summary() == "Tydzień zakończony"
	continue_btn.pressed.connect(_on_continue)
	buttons.add_child(continue_btn)
	save_label = UIKit.label(Session.save_summary(), 14, Color(1, 1, 1, 0.45))
	save_label.visible = not continue_btn.disabled
	var sl_margin := MarginContainer.new()
	sl_margin.add_theme_constant_override("margin_top", -8)
	sl_margin.add_theme_constant_override("margin_bottom", 8)
	sl_margin.add_child(save_label)
	buttons.add_child(sl_margin)
	new_btn = UIKit.menu_button("Nowy tydzień")
	new_btn.pressed.connect(_on_new)
	new_btn.focus_exited.connect(_reset_confirm)
	buttons.add_child(new_btn)
	var settings_btn := UIKit.menu_button("Ustawienia")
	settings_btn.pressed.connect(_open_settings)
	buttons.add_child(settings_btn)
	var help_btn := UIKit.menu_button("Jak grać")
	help_btn.pressed.connect(_open_help)
	buttons.add_child(help_btn)
	var quit_btn := UIKit.menu_button("Wyjdź")
	quit_btn.pressed.connect(func() -> void: get_tree().quit())
	buttons.add_child(quit_btn)

	var foot := UIKit.label("Godot 4 · C++ · woksele i piksele", 13, Color(1, 1, 1, 0.3))
	foot.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	foot.position = Vector2(110, -50)
	add_child(foot)

	settings = SettingsPanel.new()
	settings.visible = false
	settings.closed.connect(func() -> void: buttons.get_child(3).grab_focus())
	add_child(settings)

	help = _build_help()
	add_child(help)

	fade = ColorRect.new()
	fade.color = Color(0, 0, 0, 0)
	fade.set_anchors_preset(Control.PRESET_FULL_RECT)
	fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(fade)


func _build_help() -> PanelContainer:
	var p := PanelContainer.new()
	p.add_theme_stylebox_override("panel", UIKit.panel_style(0.94, 14))
	p.set_anchors_preset(Control.PRESET_CENTER)
	p.grow_horizontal = Control.GROW_DIRECTION_BOTH
	p.grow_vertical = Control.GROW_DIRECTION_BOTH
	p.custom_minimum_size = Vector2(640, 0)
	p.visible = false
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 12)
	p.add_child(v)
	v.add_child(UIKit.label("Jak grać", 30, Color.WHITE, true))
	v.add_child(UIKit.label("Tydzień z życia kogoś, kto woli zostać w pokoju.", 16, UIKit.MUTED))
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 16)
	grid.add_theme_constant_override("v_separation", 8)
	v.add_child(grid)
	for row in [["A  D", "chodzenie"], ["W  S", "drzwi, schody, autobus"], ["E", "co robisz w tym miejscu (akcje zjadają czas)"],
			["Q", "szuflada w twoim pokoju – 6 kieszeni"], ["P", "telefon – wiadomości przychodzą, gdy go masz"],
			["1–6", "slot ekwipunku"], ["Esc", "pauza, zapis, ustawienia"]]:
		var keys := HBoxContainer.new()
		keys.add_theme_constant_override("separation", 4)
		for k in row[0].split("  "):
			keys.add_child(UIKit.keycap(k))
		grid.add_child(keys)
		grid.add_child(UIKit.label(row[1], 15, Color(0.88, 0.88, 0.92)))
	var tip := UIKit.label("Czas płynie tylko wtedy, gdy coś robisz. Każdy wybór coś zamyka – i coś otwiera.", 14, UIKit.ACCENT)
	tip.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	tip.custom_minimum_size = Vector2(600, 0)
	v.add_child(tip)
	var back := UIKit.menu_button("Wróć", 20)
	back.pressed.connect(_close_help)
	v.add_child(back)
	return p


func _start_pan() -> void:
	# Slow Ken Burns drift over the backdrop.
	bg.pivot_offset = get_viewport_rect().size / 2.0
	bg.scale = Vector2(1.06, 1.06)
	var tw := create_tween().set_loops()
	tw.tween_property(bg, "position", Vector2(-40, -10), 18.0).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	tw.tween_property(bg, "position", Vector2(30, 8), 18.0).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)


func _leave(then: Callable) -> void:
	if leaving:
		return
	leaving = true
	var tw := create_tween()
	tw.tween_property(fade, "color:a", 1.0, 0.45)
	tw.tween_callback(then)


func _on_continue() -> void:
	_leave(Session.start_continue)


func _on_new() -> void:
	if Session.has_save() and not continue_btn.disabled and not confirm_new:
		confirm_new = true
		new_btn.text = "Nowy tydzień – na pewno? Zapis zniknie"
		return
	Session.delete_save()
	_leave(Session.start_new)


func _reset_confirm() -> void:
	confirm_new = false
	new_btn.text = "Nowy tydzień"


func _open_settings() -> void:
	settings.open()


func _open_help() -> void:
	help.visible = true
	help.get_child(0).get_child(help.get_child(0).get_child_count() - 1).grab_focus()


func _close_help() -> void:
	help.visible = false
	buttons.get_child(4).grab_focus()


func _unhandled_input(event: InputEvent) -> void:
	if settings.visible or leaving:
		return
	if help.visible:
		if event.is_action_pressed("ui_cancel"):
			_close_help()
			get_viewport().set_input_as_handled()
		return
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	match event.physical_keycode:
		KEY_W:
			UIKit.focus_step(buttons, -1)
		KEY_S:
			UIKit.focus_step(buttons, 1)
		KEY_E:
			var f := get_viewport().gui_get_focus_owner()
			if f is Button:
				f.pressed.emit()
		_:
			return
	get_viewport().set_input_as_handled()
