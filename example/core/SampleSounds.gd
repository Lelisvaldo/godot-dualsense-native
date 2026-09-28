extends RefCounted
## Sample sounds for the speaker demo, synthesized at 48 kHz (no audio files).
## `frames(kind)` returns stereo frames for `DualSense.speaker_play()`; `make(kind)` returns an
## AudioStreamWAV to play the same sound on the PC.
##
## Math: sums of sine oscillators, sin(2*pi*f*t), shaped by exponential envelopes e^(-t/tau); a 5 ms
## fade in/out avoids clicks.

const RATE := 48000
const KINDS := ["beep", "arpeggio", "chime", "noise"]


static func frames(kind: String) -> PackedVector2Array:
	var s := render(kind)
	var out := PackedVector2Array()
	out.resize(s.size())
	for i in s.size():
		out[i] = Vector2(s[i], s[i])
	return out


static func make(kind: String) -> AudioStreamWAV:
	var s := render(kind)
	var data := PackedByteArray()
	data.resize(s.size() * 2)
	for i in s.size():
		data.encode_s16(i * 2, roundi(clampf(s[i], -1.0, 1.0) * 32767.0))
	var wav := AudioStreamWAV.new()
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	wav.mix_rate = RATE
	wav.data = data
	return wav


static func render(kind: String) -> PackedFloat32Array:
	match kind:
		"beep":
			return _render(0.25, func(t, _n): return 0.5 * sin(TAU * 1000.0 * t))
		"arpeggio":
			# C5, E5, G5, C6: 150 ms each.
			return _render(0.6, func(t, _n):
				var f: float = [523.25, 659.25, 783.99, 1046.5][mini(int(t / 0.15), 3)]
				return 0.45 * sin(TAU * f * t) * exp(-fposmod(t, 0.15) / 0.08))
		"chime":
			return _render(1.2, func(t, _n): return (0.35 * sin(TAU * 1320.0 * t) + 0.2 * sin(TAU * 1980.0 * t)) * exp(-t / 0.35))
		"noise":
			return _render(0.4, func(t, n): return 0.5 * n * exp(-t / 0.12))
	return PackedFloat32Array()


static func _render(seconds: float, fn: Callable) -> PackedFloat32Array:
	var count := int(seconds * RATE)
	var rng := RandomNumberGenerator.new()
	rng.seed = 12345
	var out := PackedFloat32Array()
	out.resize(count)
	var ramp := 0.005 * RATE
	for i in count:
		var v: float = fn.call(float(i) / RATE, rng.randf_range(-1.0, 1.0))
		out[i] = clampf(v * minf(1.0, i / ramp) * minf(1.0, (count - 1 - i) / ramp), -1.0, 1.0)
	return out
