extends RefCounted
## Sample HD-haptics waveforms: stereo frames at 3000 Hz in [-1, 1]; x = left actuator, y = right.
##
## Math:
## * Variable-frequency oscillator: phase = 2*pi * integral of f dt; for a linear sweep
##   f(t) = f0 + (f1 - f0)*t/T the phase is 2*pi*(f0*t + (f1 - f0)*t^2/(2T)).
## * Exponential envelope e^(-t/tau) with a 3 ms attack.
## * Pan in [-1, 1]: left gain min(1, 1 - pan), right gain min(1, 1 + pan).
## * "Low" noise: 6-sample moving average of white noise (simple low-pass, ~250 Hz at 3 kHz).
## The actuators respond most strongly around 60 Hz (measured with the IMU self-test).

const RATE := 3000
const KINDS := ["tick", "heartbeat", "thump", "texture", "sweep"]


static func make(kind: String, pan: float = 0.0) -> PackedVector2Array:
	match kind:
		"tick":
			return _render(0.04, pan, func(t, _n): return 0.9 * sin(TAU * 170.0 * t) * exp(-t / 0.012))
		"heartbeat":
			return _render(0.45, pan, func(t, _n):
				var beat: float = t if t < 0.2 else t - 0.2
				return sin(TAU * 50.0 * beat) * exp(-beat / 0.04))
		"thump":
			return _render(0.25, pan, func(t, _n): return sin(TAU * (60.0 * t - 40.0 * t * t / 0.5)) * exp(-t / 0.09))
		"texture":
			return _render(0.8, pan, func(t, n): return (0.6 * n + 0.3 * sin(TAU * 80.0 * t)) * exp(-t / 0.3))
		"sweep":
			return _render(1.0, pan, func(t, _n): return 0.8 * sin(TAU * (40.0 * t + 160.0 * t * t / 2.0)))
	return PackedVector2Array()


static func _render(seconds: float, pan: float, fn: Callable) -> PackedVector2Array:
	var count := int(seconds * RATE)
	var rng := RandomNumberGenerator.new()
	rng.seed = 12345
	var window: Array[float] = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
	var gl := minf(1.0, 1.0 - pan)
	var gr := minf(1.0, 1.0 + pan)
	var attack := 0.003 * RATE
	var out := PackedVector2Array()
	out.resize(count)
	for i in count:
		window[i % 6] = rng.randf_range(-1.0, 1.0)
		var n := 0.0
		for v in window:
			n += v
		var s: float = fn.call(float(i) / RATE, n / 3.0) * minf(1.0, i / attack)
		out[i] = Vector2(clampf(s * gl, -1.0, 1.0), clampf(s * gr, -1.0, 1.0))
	return out
