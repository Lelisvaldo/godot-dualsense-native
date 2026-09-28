extends Control
## Controller test: everything Godot sees about the connected gamepads plus everything the native
## DualSense extension reads over HID. Open with `--controller-test`.
##
## Gamepad shortcuts: Triangle/Y weak rumble, Square/X strong rumble (Godot API), L1 next lightbar
## color (Godot API), R1 toggle motion sensors (Godot API), R3 beep on the controller speaker
## (native extension). Esc quits.

const DualSensePadScript := preload("res://core/DualSensePad.gd")
const SampleSounds := preload("res://core/SampleSounds.gd")

const BUTTON_NAMES := {
	JOY_BUTTON_A: "A/Cross", JOY_BUTTON_B: "B/Circle", JOY_BUTTON_X: "X/Square", JOY_BUTTON_Y: "Y/Triangle",
	JOY_BUTTON_BACK: "Back/Create", JOY_BUTTON_GUIDE: "Guide/PS", JOY_BUTTON_START: "Start/Options",
	JOY_BUTTON_LEFT_STICK: "L3", JOY_BUTTON_RIGHT_STICK: "R3",
	JOY_BUTTON_LEFT_SHOULDER: "L1", JOY_BUTTON_RIGHT_SHOULDER: "R1",
	JOY_BUTTON_DPAD_UP: "Up", JOY_BUTTON_DPAD_DOWN: "Down", JOY_BUTTON_DPAD_LEFT: "Left", JOY_BUTTON_DPAD_RIGHT: "Right",
	JOY_BUTTON_MISC1: "Misc1/Mic", JOY_BUTTON_PADDLE1: "P1", JOY_BUTTON_PADDLE2: "P2",
	JOY_BUTTON_PADDLE3: "P3", JOY_BUTTON_PADDLE4: "P4", JOY_BUTTON_TOUCHPAD: "Touchpad",
}
const AXIS_NAMES := ["LX", "LY", "RX", "RY", "L2", "R2"]
const LIGHT_COLORS: Array[Color] = [Color.RED, Color.GREEN, Color.BLUE, Color.WHITE, Color(0.1, 0.1, 0.1)]
const FONT_SIZE := 12

var _label: Label
var _pad: Node
var _light_index := -1


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	_label = Label.new()
	_label.set_anchors_preset(Control.PRESET_FULL_RECT)
	_label.position = Vector2(12, 8)
	_label.add_theme_font_size_override("font_size", FONT_SIZE)
	add_child(_label)
	_pad = DualSensePadScript.new()
	add_child(_pad)
	print("ControllerTest ready")


func _process(_delta: float) -> void:
	_label.text = build_report()


func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		get_tree().quit()
	elif event is InputEventJoypadButton and event.pressed:
		var device: int = event.device
		match event.button_index:
			JOY_BUTTON_Y:
				Input.start_joy_vibration(device, 0.6, 0.0, 0.4)
			JOY_BUTTON_X:
				Input.start_joy_vibration(device, 0.0, 0.8, 0.4)
			JOY_BUTTON_LEFT_SHOULDER:
				if Input.has_joy_light(device):
					_light_index = (_light_index + 1) % LIGHT_COLORS.size()
					Input.set_joy_light(device, LIGHT_COLORS[_light_index])
			JOY_BUTTON_RIGHT_SHOULDER:
				if Input.has_joy_motion_sensors(device):
					Input.set_joy_motion_sensors_enabled(device, not Input.is_joy_motion_sensors_enabled(device))
			JOY_BUTTON_RIGHT_STICK:
				if _pad != null and _pad.is_open() and _pad.device.speaker_supported():
					_pad.device.speaker_play(SampleSounds.frames("beep"))


## Full report text (pure function over the current Input state and the native device).
func build_report() -> String:
	var lines := PackedStringArray(["CONTROLLER TEST  (Esc quits)",
		"Triangle weak rumble  Square strong rumble  L1 lightbar  R1 sensors  R3 speaker beep", ""])
	lines.append_array(describe_native())
	var pads := Input.get_connected_joypads()
	if pads.is_empty():
		lines.append("")
		lines.append("Godot: no gamepad connected.")
	for device in pads:
		lines.append("")
		lines.append_array(describe_joypad(device))
	return "\n".join(lines)


func describe_native() -> PackedStringArray:
	if not DualSensePadScript.available():
		return PackedStringArray(["Native DualSense: extension not loaded."])
	var native: RefCounted = _pad.device if _pad != null else null
	if _pad == null or not _pad.is_open():
		var err: String = native.get_last_error() if native != null else ""
		return PackedStringArray(["Native DualSense: no controller open over HID." + ("  (" + err + ")" if not err.is_empty() else "")])
	var info: Dictionary = native.get_info()
	var fw: Dictionary = native.get_firmware()
	var st: Dictionary = native.get_state()
	var out := PackedStringArray()
	out.append("Native DualSense: %s%s  reports %d  CRC errors %d  audio -> %s" % [
		"Bluetooth" if info["connection"] == "bluetooth" else "USB", "  (Edge)" if info["edge"] else "",
		info["reports"], info["crc_errors"],
		info["audio_route"] if info["audio_route"] != "" else "-"])
	if fw.is_empty():
		out.append("firmware: not read  " + native.get_last_error())
	else:
		out.append("firmware: update %s (0x%04X)  fw 0x%08X  hw 0x%08X  build %s %s  rumble v2=%s" % [
			fw["update_version_text"], fw["update_version"], fw["fw_version"], fw["hw_version"],
			fw["build_date"], fw["build_time"], fw["vibration_v2"]])
	if st.is_empty():
		out.append("waiting for input reports...")
		return out
	out.append("battery %d%% (%s)  headphones=%s  external mic=%s  mute=%s" % [
		st["battery_percent"], st["battery_status"], st["headphones"], st["microphone"], st["mic_muted"]])
	var touches := PackedStringArray()
	for t in st["touch"]:
		touches.append("#%d (%d, %d)" % [t["id"], t["x"], t["y"]] if t["active"] else "-")
	out.append("touchpad: %s   PS=%s  mute button=%s  L2=%d  R2=%d" % [
		"  ".join(touches), st["ps_button"], st["mute_button"], st["l2"], st["r2"]])
	out.append("gyro %s  accel %s" % [st["gyro"], st["accel"]])
	return out


func describe_joypad(device: int) -> PackedStringArray:
	var out := PackedStringArray()
	out.append("Godot #%d %s" % [device, Input.get_joy_name(device)])
	out.append("GUID %s  known=%s" % [Input.get_joy_guid(device), Input.is_joy_known(device)])
	out.append("rumble=%s  LED=%s  sensors=%s" % [
		Input.has_joy_vibration(device), Input.has_joy_light(device), Input.has_joy_motion_sensors(device)])
	var pressed := PackedStringArray()
	for b in JOY_BUTTON_SDL_MAX:
		if Input.is_joy_button_pressed(device, b):
			pressed.append(button_name(b))
	out.append("buttons: %s" % (" ".join(pressed) if not pressed.is_empty() else "-"))
	var axes := PackedStringArray()
	for a in AXIS_NAMES.size():
		axes.append("%s%+.2f" % [AXIS_NAMES[a], Input.get_joy_axis(device, a)])
	out.append(" ".join(axes))
	if Input.has_joy_motion_sensors(device) and Input.is_joy_motion_sensors_enabled(device):
		out.append("gyro %s  accel %s" % [Input.get_joy_gyroscope(device), Input.get_joy_accelerometer(device)])
	return out


static func button_name(index: int) -> String:
	return BUTTON_NAMES.get(index, "B%d" % index)
