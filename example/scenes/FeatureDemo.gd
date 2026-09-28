extends Control
## DualSense feature demo: one page per feature, each running on its own so you can feel it.
## Left/Right (D-pad or arrow keys): change page. Cross/A or Enter: next variant on the page.
## Esc quits. Works over USB and Bluetooth (HD haptics and speaker: Bluetooth on every platform,
## USB on Windows).

const DualSensePadScript := preload("res://core/DualSensePad.gd")
const SampleSounds := preload("res://core/SampleSounds.gd")
const SampleHaptics := preload("res://core/SampleHaptics.gd")

const PAGES := ["Lightbar", "LEDs", "Triggers", "Rumble", "HD haptics", "Speaker", "Inputs"]
const BG := Color(0.07, 0.08, 0.11)
const PANEL := Color(0.14, 0.16, 0.21)
const DIM := Color(0.6, 0.63, 0.7)
const LED_OFF := Color(0.22, 0.24, 0.28)
const ACCENT := Color(0.35, 0.6, 1.0)
const NAMED_COLORS := [["Red", Color(1, 0, 0)], ["Green", Color(0, 1, 0)], ["Blue", Color(0, 0.2, 1)],
	["Purple", Color(0.6, 0, 1)], ["Orange", Color(1, 0.45, 0)], ["White", Color(1, 1, 1)]]
## [name, description, left effect args, right effect args]; see DualSense.trigger_* for ranges.
const TRIGGER_EFFECTS := [
	["Off", "no resistance", ["trigger_off"]],
	["Feedback", "constant resistance from 30% of travel, strength 6", ["trigger_feedback", 3, 6]],
	["Weapon", "resistance from zone 3 to 6, then a click (lever feel)", ["trigger_weapon", 3, 6, 8]],
	["Vibration", "the trigger buzzes at 15 Hz from zone 1", ["trigger_vibration", 1, 8, 15]],
	["Machine", "alternating amplitudes 7/3 at 20 Hz between zones 1 and 9", ["trigger_machine", 1, 9, 7, 3, 20, 2]],
]
const LED_PATTERNS := ["sweep", "player 1", "player 2", "player 3", "player 4", "all"]
const PLAYER_MASKS := {"player 1": 0b00100, "player 2": 0b01010, "player 3": 0b10101, "player 4": 0b11011, "all": 0b11111}
const RUMBLE_MODES := ["strong motor", "weak motor", "both, pulsing"]

var _pad: Node
var _font: Font
var _page := 0
var _variant := 0
var _t := 0.0
var _next_event := 0.0
var _color := Color.BLACK
var _leds := 0
var _mute := 0
var _rumble := Vector2.ZERO
var _last_label := ""
var _speaker_cache := {}
var _haptics_cache := {}
## Screenshot for docs: `--page=N --capture=file.png` saves the window after ~2 s and quits.
var _capture_path := ""
var _clock := 0.0


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	_font = ThemeDB.fallback_font
	_pad = DualSensePadScript.new()
	_pad.connected.connect(func(_info): _enter_page())
	add_child(_pad)
	print("FeatureDemo ready")
	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if arg.begins_with("--page="):
			_page = clampi(int(arg.get_slice("=", 1)) - 1, 0, PAGES.size() - 1)
		elif arg.begins_with("--capture="):
			_capture_path = arg.get_slice("=", 1)
	_enter_page()


func _input(event: InputEvent) -> void:
	var step := 0
	var action := false
	if event is InputEventKey and event.pressed and not event.echo:
		match event.physical_keycode:
			KEY_ESCAPE:
				get_tree().quit()
			KEY_RIGHT:
				step = 1
			KEY_LEFT:
				step = -1
			KEY_ENTER, KEY_SPACE:
				action = true
	elif event is InputEventJoypadButton and event.pressed:
		match event.button_index:
			JOY_BUTTON_DPAD_RIGHT:
				step = 1
			JOY_BUTTON_DPAD_LEFT:
				step = -1
			JOY_BUTTON_A:
				action = true
	if step != 0:
		_page = posmod(_page + step, PAGES.size())
		_variant = 0
		_enter_page()
	elif action:
		_variant += 1
		_enter_page()


func _dev() -> RefCounted:
	return _pad.device if _pad != null and _pad.is_open() else null


## Resets every output and applies what the new page needs.
func _enter_page() -> void:
	_t = 0.0
	_next_event = 0.0
	var dev := _dev()
	if dev == null:
		return
	dev.reset_outputs()
	# Rumble emulation must stay off while HD haptics or the speaker are in use, otherwise the
	# actuators leave audio-haptics mode.
	dev.set_rumble_enabled(PAGES[_page] == "Rumble")
	if PAGES[_page] == "Triggers":
		var fx: Array = TRIGGER_EFFECTS[posmod(_variant, TRIGGER_EFFECTS.size())][2]
		var bytes: PackedByteArray = ClassDB.class_call_static.callv([&"DualSense", StringName(fx[0])] + fx.slice(1))
		dev.set_trigger_effect(false, bytes)
		dev.set_trigger_effect(true, bytes)
	dev.send_output()


func _process(delta: float) -> void:
	_t += delta
	var dev := _dev()
	_color = Color(0.1, 0.1, 0.12)
	_leds = 0
	_mute = 0
	_rumble = Vector2.ZERO
	match PAGES[_page]:
		"Lightbar":
			if posmod(_variant, 2) == 0:
				_color = Color.from_hsv(fposmod(_t * 0.15, 1.0), 1.0, 1.0)
				_last_label = "rainbow (hue %d deg)" % roundi(fposmod(_t * 0.15, 1.0) * 360.0)
			else:
				var c: Array = NAMED_COLORS[int(_t / 1.5) % NAMED_COLORS.size()]
				_color = c[1]
				_last_label = c[0]
		"LEDs":
			var pattern: String = LED_PATTERNS[posmod(_variant, LED_PATTERNS.size())]
			if pattern == "sweep":
				var pos := int(_t * 6.0) % 8
				_leds = 1 << (pos if pos < 5 else 8 - pos)
			else:
				_leds = PLAYER_MASKS[pattern]
			_mute = 2
			_last_label = pattern + " + mute LED pulsing"
			_color = Color(0.2, 0.2, 0.25)
		"Triggers":
			_last_label = TRIGGER_EFFECTS[posmod(_variant, TRIGGER_EFFECTS.size())][0]
		"Rumble":
			var mode: String = RUMBLE_MODES[posmod(_variant, RUMBLE_MODES.size())]
			var on := fposmod(_t, 1.2) < 0.6
			match mode:
				"strong motor":
					_rumble = Vector2(0.8 if on else 0.0, 0.0)
				"weak motor":
					_rumble = Vector2(0.0, 0.8 if on else 0.0)
				_:
					_rumble = Vector2(0.7, 0.7) * (0.5 + 0.5 * sin(TAU * 1.5 * _t))
			_last_label = mode
		"HD haptics":
			var kind: String = SampleHaptics.KINDS[posmod(_variant, SampleHaptics.KINDS.size())]
			_last_label = kind + " (alternating left / right)"
			if dev != null and _t >= _next_event:
				var pan := -0.9 if int(_t / 1.2) % 2 == 0 else 0.9
				var key := "%s|%.1f" % [kind, pan]
				if not _haptics_cache.has(key):
					_haptics_cache[key] = SampleHaptics.make(kind, pan)
				dev.haptics_play(_haptics_cache[key])
				_next_event = _t + 1.2
		"Speaker":
			var kind: String = SampleSounds.KINDS[posmod(_variant, SampleSounds.KINDS.size())]
			_last_label = kind
			if dev != null and _t >= _next_event:
				if not _speaker_cache.has(kind):
					_speaker_cache[kind] = SampleSounds.frames(kind)
				dev.speaker_play(_speaker_cache[kind])
				_next_event = _t + 2.0
		"Inputs":
			_last_label = "touch, sticks, triggers, motion, battery"
	if dev != null:
		dev.set_lightbar(_color)
		dev.set_player_leds(_leds, 0)
		dev.set_mute_led(_mute)
		dev.set_rumble(_rumble.x, _rumble.y)
		dev.send_output()
	queue_redraw()
	_clock += delta
	if not _capture_path.is_empty() and _clock > 2.0:
		get_viewport().get_texture().get_image().save_png(_capture_path)
		get_tree().quit()


# --- Drawing -------------------------------------------------------------------------------

func _draw() -> void:
	draw_rect(Rect2(0, 0, 640, 360), BG)
	_text("DualSense feature demo", Vector2(16, 24), 16, Color.WHITE)
	for i in PAGES.size():
		var r := Rect2(16 + i * 87, 34, 83, 20)
		draw_rect(r, ACCENT if i == _page else PANEL)
		_text_center(PAGES[i], r.get_center() + Vector2(0, 4), 11, Color.WHITE)
	var body := Rect2(16, 62, 608, 236)
	draw_rect(body, PANEL)
	_text(PAGES[_page].to_upper() + "  -  " + _last_label, Vector2(28, 84), 13, Color.WHITE)
	var dev := _dev()
	match PAGES[_page]:
		"Lightbar":
			draw_rect(Rect2(28, 100, 584, 120), _color)
			_text_center("#" + _color.to_html(false).to_upper(), Vector2(320, 168), 20,
				Color.BLACK if _color.get_luminance() > 0.5 else Color.WHITE)
			_hint("Cross/A: rainbow <-> named colors")
		"LEDs":
			for i in 5:
				draw_circle(Vector2(236 + i * 42, 150), 14, Color.WHITE if _leds & (1 << i) else LED_OFF)
			var mute_on := fposmod(_t * 2.0, 1.0) < 0.5
			draw_circle(Vector2(488, 150), 9, Color(1.0, 0.55, 0.1) if mute_on else LED_OFF)
			_text_center("player LEDs", Vector2(320, 190), 11, DIM)
			_text_center("mute", Vector2(488, 190), 11, DIM)
			_hint("Cross/A: next pattern (sweep, player 1-4, all)")
		"Triggers":
			var fx: Array = TRIGGER_EFFECTS[posmod(_variant, TRIGGER_EFFECTS.size())]
			_text(fx[1], Vector2(28, 106), 12, DIM)
			var st: Dictionary = dev.get_state() if dev != null else {}
			_bar(Rect2(28, 130, 280, 40), "L2", st.get("l2", 0) / 255.0)
			_bar(Rect2(332, 130, 280, 40), "R2", st.get("r2", 0) / 255.0)
			_hint("Press L2 / R2 to feel it. Cross/A: next effect (off, feedback, weapon, vibration, machine)")
		"Rumble":
			_bar(Rect2(28, 120, 280, 30), "strong", _rumble.x)
			_bar(Rect2(332, 120, 280, 30), "weak", _rumble.y)
			_hint("Classic motor emulation. Cross/A: strong / weak / both")
		"HD haptics":
			var lv: Vector2 = dev.haptics_levels() if dev != null else Vector2.ZERO
			_bar(Rect2(28, 120, 280, 30), "left actuator", lv.x)
			_bar(Rect2(332, 120, 280, 30), "right actuator", lv.y)
			if dev != null:
				var dbg: Dictionary = dev.haptics_debug()
				_text("transport: %s   buffers sent: %d   errors: %d %s" % [dbg["transport"], dbg["reports"], dbg["write_errors"], dbg["usb_error"]],
					Vector2(28, 180), 11, DIM)
			_hint("Audio-based haptics (like on a console). Cross/A: next waveform")
		"Speaker":
			var lvl: float = dev.speaker_level() if dev != null else 0.0
			_bar(Rect2(28, 120, 584, 30), "speaker level", lvl)
			_hint("Sound plays on the controller's built-in speaker. Cross/A: next sound")
		"Inputs":
			_draw_inputs(dev)
	_text(_status(), Vector2(16, 322), 12, Color.WHITE)
	_text("Left/Right: page   Cross/A: variant   Esc: quit", Vector2(16, 344), 11, DIM)


func _draw_inputs(dev: RefCounted) -> void:
	var st: Dictionary = dev.get_state() if dev != null else {}
	var pad := Rect2(28, 100, 256, 144)
	draw_rect(pad, BG)
	_text_center("touchpad", pad.position + Vector2(128, 140), 10, DIM)
	for t in st.get("touch", []):
		if t["active"]:
			draw_circle(pad.position + Vector2(t["x"] / 1920.0 * pad.size.x, t["y"] / 1080.0 * pad.size.y), 8, ACCENT)
	_bar(Rect2(300, 100, 150, 18), "L2", st.get("l2", 0) / 255.0)
	_bar(Rect2(460, 100, 150, 18), "R2", st.get("r2", 0) / 255.0)
	var accel: Vector3i = st.get("accel", Vector3i.ZERO)
	var gyro: Vector3i = st.get("gyro", Vector3i.ZERO)
	_text("accel  %6d %6d %6d" % [accel.x, accel.y, accel.z], Vector2(300, 144), 12, Color.WHITE)
	_text("gyro   %6d %6d %6d" % [gyro.x, gyro.y, gyro.z], Vector2(300, 164), 12, Color.WHITE)
	_text("battery %s%% (%s)" % [st.get("battery_percent", "?"), st.get("battery_status", "?")], Vector2(300, 190), 12, Color.WHITE)
	_text("headphones %s   mute %s" % [st.get("headphones", "?"), st.get("mic_muted", "?")], Vector2(300, 210), 12, Color.WHITE)


func _status() -> String:
	if not DualSensePadScript.available():
		return "Native extension not loaded (build it with ./build.sh)."
	var dev := _dev()
	if dev == null:
		return "Looking for a DualSense... (USB or Bluetooth)"
	var fw: Dictionary = dev.get_firmware()
	return "%s   firmware %s   battery %s%%" % ["BLUETOOTH" if dev.is_bluetooth() else "USB",
		fw.get("update_version_text", "?"), dev.get_state().get("battery_percent", "?")]


func _hint(text: String) -> void:
	_text(text, Vector2(28, 286), 11, DIM)


func _bar(r: Rect2, title: String, value: float) -> void:
	draw_rect(r, BG)
	draw_rect(Rect2(r.position, Vector2(r.size.x * clampf(value, 0.0, 1.0), r.size.y)), ACCENT)
	_text_center(title, r.get_center() + Vector2(0, 4), 11, Color.WHITE)


func _text(text: String, pos: Vector2, size: int, color: Color) -> void:
	draw_string(_font, pos, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, color)


func _text_center(text: String, center: Vector2, size: int, color: Color) -> void:
	var w := _font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
	draw_string(_font, Vector2(center.x - w / 2.0, center.y), text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, color)
