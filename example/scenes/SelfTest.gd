extends Node
## DualSense self-test without a person. Open with `--selftest` (works with `--headless`); writes a
## JSON report to `--selftest-out=file` and quits.
##
## Idea: the controller's own accelerometer "feels" its vibration. With the controller resting on a
## table, measure the tremor at rest and while each output runs.
##
## Math: for N accelerometer samples a_i (raw units, 8192 = 1 g) the tremor is the RMS deviation
## around the mean, r = sqrt(sum |a_i - mean|^2 / N), converted to mg (x 1000/8192). Gravity is
## constant and disappears when the mean is subtracted. A step "vibrated" when r is at least
## VIBRATION_RATIO times the rest value. Classic rumble is the positive control: if it doesn't pass,
## the method isn't reliable (e.g. the controller is being held).
##
## Options: `--selftest-freqs=40,60,...` (HD haptics frequency sweep),
## `--selftest-plan=plan.json` (run a list of steps, see _run_plan()).

const DualSensePadScript := preload("res://core/DualSensePad.gd")
const HAPTICS_RATE := 3000
const VIBRATION_RATIO := 3.0
const ACCEL_PER_G := 8192.0

var _pad: Node
var _out_path := "user://dualsense_selftest.json"


func _ready() -> void:
	Engine.max_fps = 500
	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if arg.begins_with("--selftest-out="):
			_out_path = arg.get_slice("=", 1)
	_pad = DualSensePadScript.new()
	_pad.auto_audio_route = false
	add_child(_pad)
	print("SelfTest ready")
	_run.call_deferred()


func _run() -> void:
	var result := {"steps": []}
	var waited := 0.0
	while not _pad.is_open() and waited < 6.0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
	if not _pad.is_open():
		result["error"] = "no DualSense opened over HID"
		_finish(result)
		return
	var dev: RefCounted = _pad.device
	result["connection"] = dev.get_info()["connection"]
	result["firmware"] = dev.get_firmware()
	var st: Dictionary = dev.get_state()
	result["battery"] = "%s%% (%s)" % [st.get("battery_percent", "?"), st.get("battery_status", "?")]
	dev.reset_outputs()
	dev.set_rumble_enabled(false)
	dev.set_lightbar(Color(0.2, 0.2, 0.2))
	dev.send_output()
	await get_tree().create_timer(0.8).timeout

	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if arg.begins_with("--selftest-plan="):
			await _run_plan(arg.get_slice("=", 1), result)
			_restore(dev)
			_finish(result)
			return

	var rest_step := await _measure("rest", 1.5, func(): pass)
	result["steps"].append(rest_step)
	var classic := await _measure("classic rumble (motors at 100%)", 1.2, func():
		dev.set_rumble_enabled(true)
		dev.set_rumble(1.0, 1.0)
		dev.send_output())
	dev.set_rumble(0.0, 0.0)
	dev.set_rumble_enabled(false)
	dev.send_output()
	result["steps"].append(classic)
	await get_tree().create_timer(0.8).timeout

	if dev.haptics_supported():
		for hz in _freqs():
			var step := await _measure("HD haptics %d Hz" % hz, 1.5, func(): dev.haptics_play(_tone(hz, 1.5)))
			step["haptics"] = dev.haptics_debug()
			result["steps"].append(step)
			await get_tree().create_timer(1.0).timeout
	else:
		result["note"] = "HD haptics not available on this connection"

	var rest: float = maxf(rest_step["tremor_mg"], 0.01)
	for step in result["steps"]:
		step["ratio"] = snappedf(step["tremor_mg"] / rest, 0.01)
		step["vibrated"] = step["ratio"] >= VIBRATION_RATIO
	result["method_ok"] = classic["tremor_mg"] / rest >= VIBRATION_RATIO
	_restore(dev)
	_finish(result)


func _freqs() -> Array:
	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if arg.begins_with("--selftest-freqs="):
			return Array(arg.get_slice("=", 1).split(",")).map(func(f): return float(f))
	return [60.0, 150.0]


## Plan: JSON list of steps, run in order.
##   {"op": "output", "rumble_enabled": bool, "rumble": [strong, weak]}   -> full output report
##   {"op": "raw", "hex": "31 00 10 ...", "transport": 0|1|2, "fix_crc": bool}
##   {"op": "haptic", "hz": 150, "seconds": 1.5, "amp": 0.9, "label": "..."}   -> plays and measures
##   {"op": "speaker", "hz": 1000, "seconds": 2, "amp": 0.3}   -> tone on the controller speaker
##   {"op": "measure", "seconds": 1.5, "label": "..."}   {"op": "wait", "seconds": 1.0}
## Every measurement gets its ratio against the first "measure".
func _run_plan(path: String, result: Dictionary) -> void:
	var dev: RefCounted = _pad.device
	var plan: Array = JSON.parse_string(FileAccess.get_file_as_string(path))
	var rest := -1.0
	for step in plan:
		match String(step.get("op", "")):
			"output":
				dev.set_rumble_enabled(step.get("rumble_enabled", false))
				var r: Array = step.get("rumble", [0.0, 0.0])
				dev.set_rumble(r[0], r[1])
				dev.send_output()
			"raw":
				var bytes := PackedByteArray()
				for h in String(step["hex"]).split(" ", false):
					bytes.append(h.hex_to_int())
				var n: int = dev.send_raw(bytes, int(step.get("transport", 0)), bool(step.get("fix_crc", true)))
				result["steps"].append({"step": "raw " + String(step["hex"]).left(20), "returned": n})
			"haptic":
				var frames := _tone(float(step.get("hz", 150.0)), float(step.get("seconds", 1.5)), float(step.get("amp", 0.9)))
				var m := await _measure(step.get("label", "haptics"), float(step.get("seconds", 1.5)), func(): dev.haptics_play(frames))
				m["haptics"] = dev.haptics_debug()
				result["steps"].append(m)
			"speaker":
				var seconds := float(step.get("seconds", 2.0))
				var n := int(seconds * 48000)
				var hz := float(step.get("hz", 1000.0))
				var amp := float(step.get("amp", 0.3))
				var tone := PackedVector2Array()
				tone.resize(n)
				for i in n:
					var v := amp * sin(TAU * hz * i / 48000.0) * minf(1.0, minf(i / 480.0, (n - 1 - i) / 480.0))
					tone[i] = Vector2(v, v)
				var ok: bool = dev.speaker_play(tone)
				await get_tree().create_timer(seconds + 0.3).timeout
				result["steps"].append({"step": step.get("label", "speaker %d Hz" % hz), "accepted": ok, "haptics": dev.haptics_debug()})
			"measure":
				var m := await _measure(step.get("label", "measure"), float(step.get("seconds", 1.5)), func(): pass)
				if rest < 0.0:
					rest = maxf(m["tremor_mg"], 0.01)
				result["steps"].append(m)
			"wait":
				await get_tree().create_timer(float(step.get("seconds", 1.0))).timeout
	for m in result["steps"]:
		if m.has("tremor_mg") and rest > 0.0:
			m["ratio"] = snappedf(m["tremor_mg"] / rest, 0.01)
			m["vibrated"] = m["ratio"] >= VIBRATION_RATIO


## Captures the IMU for `seconds` while `action` runs; returns the tremor (mg) and the sample rate.
func _measure(label: String, seconds: float, action: Callable) -> Dictionary:
	var dev: RefCounted = _pad.device
	dev.imu_capture_start()
	action.call()
	await get_tree().create_timer(seconds).timeout
	var accel: PackedVector3Array = dev.imu_capture_stop()["accel"]
	return {"step": label, "samples": accel.size(), "samples_per_s": roundi(accel.size() / seconds),
		"tremor_mg": snappedf(tremor_mg(accel), 0.01)}


static func tremor_mg(samples: PackedVector3Array) -> float:
	if samples.size() < 2:
		return 0.0
	var mean := Vector3.ZERO
	for a in samples:
		mean += a
	mean /= samples.size()
	var acc := 0.0
	for a in samples:
		acc += (a - mean).length_squared()
	return sqrt(acc / samples.size()) * 1000.0 / ACCEL_PER_G


static func _tone(hz: float, seconds: float, amp: float = 0.9) -> PackedVector2Array:
	var n := int(seconds * HAPTICS_RATE)
	var frames := PackedVector2Array()
	frames.resize(n)
	for i in n:
		var v := amp * sin(TAU * hz * i / HAPTICS_RATE)
		frames[i] = Vector2(v, v)
	return frames


func _restore(dev: RefCounted) -> void:
	dev.reset_outputs()
	dev.set_rumble_enabled(false)
	dev.send_output()


func _finish(result: Dictionary) -> void:
	var text := JSON.stringify(result, "  ")
	var f := FileAccess.open(_out_path, FileAccess.WRITE)
	if f != null:
		f.store_string(text)
		f.close()
	print(text)
	get_tree().quit()
