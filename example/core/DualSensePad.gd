extends Node
## Keeps a DualSense open through the native extension: finds and opens it over HID, reopens it after
## a disconnect, reads the input reports and, on USB, routes the audio like a console does
## (no headphones -> built-in speaker; headphones plugged -> headphone jack).
##
## Everything goes through ClassDB: without the extension this script still loads and
## `available()` returns false.

signal connected(info: Dictionary)
signal disconnected

const NATIVE_CLASS := &"DualSense"
## Seconds between attempts to find/open a controller while none is open.
const RETRY_SECONDS := 1.0

## The native DualSense instance (null until a controller is found).
var device: RefCounted
## Automatic audio routing on USB (disable if your game wants to control the path itself).
var auto_audio_route := true

var _retry := 0.0
var _routed_headphones := -1
var _was_open := false


static func available() -> bool:
	return ClassDB.class_exists(NATIVE_CLASS)


func is_open() -> bool:
	return device != null and device.is_open()


func is_bluetooth() -> bool:
	return is_open() and device.is_bluetooth()


func _process(delta: float) -> void:
	update(delta)


func _exit_tree() -> void:
	# Don't leave the controller with trigger resistance, LEDs or rumble on exit.
	if is_open():
		device.reset_outputs()
		device.send_output()


## One step: read the open controller or try to open a new one.
func update(delta: float) -> void:
	if not available():
		return
	if is_open():
		device.poll()
		if not device.is_open():
			_lost()
			return
		if auto_audio_route:
			_route_audio()
		return
	if _was_open:
		_lost()
	_retry -= delta
	if _retry > 0.0:
		return
	_retry = RETRY_SECONDS
	var devices: Array = ClassDB.class_call_static(NATIVE_CLASS, &"list_devices")
	if devices.is_empty():
		return
	if device == null:
		device = ClassDB.instantiate(NATIVE_CLASS)
	if device.open(devices[0]["path"]):
		_routed_headphones = -1
		_was_open = true
		connected.emit(device.get_info())


func _lost() -> void:
	_was_open = false
	_routed_headphones = -1
	disconnected.emit()


func _route_audio() -> void:
	if device.is_bluetooth():
		return
	var st: Dictionary = device.get_state()
	if st.is_empty():
		return
	var hp := 1 if st["headphones"] else 0
	if hp != _routed_headphones and device.set_audio_route(hp == 0):
		_routed_headphones = hp
