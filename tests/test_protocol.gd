extends "base_test.gd"
## DualSense HID protocol checks (parsing, report building, CRC). No hardware needed.

const CLASS := &"DualSense"


func _call(method: StringName, args: Array) -> Variant:
	return ClassDB.class_call_static.callv([CLASS, method] + args)


func test_extension_is_loaded() -> void:
	assert_true(ClassDB.class_exists(CLASS), "DualSense GDExtension loaded (run ./build.sh first)")


func test_crc32_known_answer() -> void:
	# Standard CRC-32 of "123456789" is 0xCBF43926; here the seed is the first byte ("1").
	assert_eq(_call(&"crc32_with_seed", [0x31, "23456789".to_ascii_buffer()]), 0xCBF43926, "CRC-32 (zlib)")


static func _input_report(bluetooth: bool) -> PackedByteArray:
	var r := PackedByteArray()
	r.resize(78 if bluetooth else 64)
	r[0] = 0x31 if bluetooth else 0x01
	var o := 2 if bluetooth else 1
	r[o + 9] = 0x04    # buttons[2]: mute button
	r[o + 32] = 0x05   # touch 1 active, id 5
	r[o + 33] = 0xE8   # x = 1000
	r[o + 34] = 0x43
	r[o + 35] = 0x1F   # y = 500
	r[o + 36] = 0x80   # touch 2 inactive
	r[o + 52] = 0x17   # charging, 7 -> 75%
	r[o + 53] = 0x05   # headphones + mute on
	return r


func test_parse_usb_and_bluetooth_input() -> void:
	for bt in [false, true]:
		var st: Dictionary = _call(&"parse_input_report", [_input_report(bt)])
		assert_eq(st.get("touch", [{}])[0], {"active": true, "id": 5, "x": 1000, "y": 500}, "touch 1 (bt=%s)" % bt)
		assert_eq(st.get("battery_percent"), 75, "battery (bt=%s)" % bt)
		assert_eq(st.get("battery_status"), "charging", "battery status (bt=%s)" % bt)
		assert_true(st.get("headphones", false) and st.get("mic_muted", false), "headphones/mute (bt=%s)" % bt)
	var short := PackedByteArray()
	short.resize(10)
	short[0] = 0x01
	assert_true((_call(&"parse_input_report", [short]) as Dictionary).is_empty(), "short BT report ignored")


func test_firmware_info() -> void:
	var r := PackedByteArray()
	r.resize(64)
	r[0] = 0x20
	r.encode_u16(44, 0x0630)
	var fw: Dictionary = _call(&"parse_firmware_info", [r])
	assert_eq(fw.get("update_version_text"), "06.30", "update version text")
	assert_true(fw.get("vibration_v2", false), "0x0630 >= 2.21 uses v2 rumble")


func test_trigger_effects() -> void:
	assert_eq(_call(&"trigger_weapon", [3, 6, 8]), PackedByteArray([0x25, 0x48, 0, 7, 0, 0, 0, 0, 0, 0, 0]), "weapon")
	assert_eq(_call(&"trigger_vibration", [0, 8, 30]), PackedByteArray([0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 30, 0]), "vibration")
	assert_eq(_call(&"trigger_feedback", [8, 1]), PackedByteArray([0x21, 0, 0x03, 0, 0, 0, 0, 0, 0, 0, 0]), "feedback")
	assert_eq(_call(&"trigger_machine", [1, 9, 7, 3, 20, 2]), PackedByteArray([0x27, 0x02, 0x02, 0x1F, 0, 0, 0, 20, 2, 0, 0]), "machine")
	assert_eq(_call(&"trigger_weapon", [1, 6, 8]), PackedByteArray([0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]), "invalid -> off")


func test_output_reports() -> void:
	var dev: RefCounted = ClassDB.instantiate(CLASS)
	dev.set_rumble(1.0, 0.5)
	dev.set_player_leds(0b10001, 1)
	dev.set_mute_led(2)
	dev.set_lightbar(Color(1, 0, 0))
	var usb: PackedByteArray = dev.build_output_report(false, 0)
	assert_eq(usb.size(), 63, "USB 0x02 size")
	assert_eq([usb[0], usb[1], usb[2], usb[3], usb[4], usb[9]], [0x02, 0x0E, 0x15, 128, 255, 2], "flags, motors, mute LED")
	assert_eq([usb[43], usb[44], usb[45], usb[46], usb[47]], [1, 0x31, 255, 0, 0], "brightness, player LEDs, RGB")
	var bt: PackedByteArray = dev.build_output_report(true, 5)
	assert_eq([bt.size(), bt[0], bt[1], bt[2]], [78, 0x31, 0x50, 0x10], "BT 0x31 header")
	assert_eq(bt.decode_u32(74), _call(&"crc32_with_seed", [0xA2, bt.slice(0, 74)]), "BT 0x31 CRC")
	dev.set_rumble_enabled(false)
	assert_eq((dev.build_output_report(false, 0) as PackedByteArray)[1], 0x0C, "no rumble bits in HD mode")


func test_haptics_report_0x32() -> void:
	var frames := PackedVector2Array([Vector2(1, -1)])
	var r: PackedByteArray = _call(&"build_haptics_report", [frames, 7])
	assert_eq(r.size(), 142, "0x32 size")
	assert_eq(r.slice(0, 13), PackedByteArray([0x32, 0x70, 0x91, 7, 0xFE, 0, 0, 0, 0, 0xFF, 7, 0x92, 64]), "header and packets")
	assert_eq([r.decode_s8(13), r.decode_s8(14)], [127, -127], "first frame")
	assert_eq(r.decode_u32(138), _call(&"crc32_with_seed", [0xA2, r.slice(0, 138)]), "CRC")


func test_speaker_report_0x39() -> void:
	var tone := PackedVector2Array()
	for i in 960:
		var v := 0.3 * sin(TAU * 1000.0 * i / 48000.0)
		tone.append(Vector2(v, v))
	var r: PackedByteArray = _call(&"build_audio_report", [PackedVector2Array(), tone, 5])
	assert_eq(r.size(), 547, "0x39 size")
	assert_eq(r.slice(0, 12), PackedByteArray([0x39, 0x50, 0x91, 6, 0x7E, 48, 48, 48, 48, 2, 0xD2, 64]), "header, 0x11, 0x12")
	assert_eq([r[140], r[141], r[142], r[342]], [0xD3, 200, 0xF4, 0xF4], "0x13 with two Opus 10 ms stereo frames")
	assert_eq(r.decode_u32(543), _call(&"crc32_with_seed", [0xA2, r.slice(0, 543)]), "CRC")


func test_audio_route_reports() -> void:
	var usb: PackedByteArray = _call(&"build_audio_route_report", [true])
	assert_eq([usb[0], usb[1], usb[2], usb[6], usb[8], usb[38]], [0x02, 0xA0, 0x80, 0x64, 0x30, 0x02], "USB speaker path")
	var bt: PackedByteArray = _call(&"build_audio_route_report_bt", [true, 3])
	assert_eq(bt.slice(3, 50), usb.slice(1, 48), "same common block over BT")
	assert_eq(bt.decode_u32(74), _call(&"crc32_with_seed", [0xA2, bt.slice(0, 74)]), "BT CRC")
