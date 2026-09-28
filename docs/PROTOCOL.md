# DualSense HID protocol notes

Everything below is implemented in `native/src/ds_protocol.*` (pure C++, no Godot) and
`native/src/dualsense.*` (Godot class, threads, transports). Offsets refer to the byte position in
the report including the report ID unless stated otherwise.

## Connection and input

| Report | USB | Bluetooth |
| -- | -- | -- |
| Full input | `0x01`, 64 bytes, data from byte 1 | `0x31`, 78 bytes, data from byte 2, CRC32 in the last 4 bytes |
| Firmware (feature) | `0x20`, 64 bytes | same |
| Calibration (feature) | `0x05`, 41 bytes | same |

* **Bluetooth activation**: the controller starts sending a short `0x01` report (10 bytes). It
  switches to the full `0x31` report after the host reads feature `0x05` or `0x20`; `open()`
  reads both.
* **CRC (Bluetooth)**: zlib CRC-32 (reflected polynomial `0xEDB88320`) over
  `[seed, report without the last 4 bytes]`, little endian at the end. Seeds: input `0xA1`, output
  `0xA2`, feature `0xA3`.
* **Touchpad**: 2 points of 4 bytes at data offset 32. `contact` bit 7 = no touch, bits 0–6 = id;
  `x = b1 | (b2 & 0x0F) << 8`, `y = (b2 >> 4) | b3 << 4` (12 bits each), area 1920×1080.
* **Status** (data offset 52): `status[0]` low nibble = charge in tenths
  (percent = min(10·c + 5, 100)), high nibble = state (0 discharging, 1 charging, 2 full,
  A/B/F error); `status[1]` bit 0 headphones, bit 1 external mic, bit 2 mute.
* **Firmware** (`0x20`): bytes 1–11 build date, 12–19 build time; `hw_version` LE32 at 24,
  `fw_version` LE32 at 28, `update_version` LE16 at 44 (e.g. `0x0630` = "06.30").

## Output (lightbar, LEDs, triggers, rumble)

USB `0x02` (63 bytes, common block from byte 1); Bluetooth `0x31` (78 bytes:
`[0x31, seq<<4, 0x10, common block, 24 reserved, CRC32 seed 0xA2]`). 47-byte common block:

| Offset | Field |
| -- | -- |
| 0 | flag0: bit 0 compatible vibration (v1), bit 1 haptics select, bit 2/3 right/left trigger, bit 5 speaker volume, bit 7 audio control |
| 1 | flag1: bit 0 mute LED, bit 2 lightbar, bit 4 player LEDs, bit 7 audio control 2 |
| 2 / 3 | weak / strong motor |
| 5 | speaker volume (`0x64`) |
| 7 | audio control: bits 4–5 output path (0 = headphones L+R, 3 = speaker gets the right channel) |
| 8 | mute LED: 0 off, 1 on, 2 pulsing |
| 10–20 / 21–31 | right / left trigger effect (11 bytes) |
| 37 | audio control 2: bits 0–2 speaker preamp gain (2 = +6 dB) |
| 38 | flag2: bit 1 lightbar setup, bit 2 compatible vibration v2 (firmware ≥ 2.21) |
| 41–46 | lightbar setup (`0x02` = light out, once), LED brightness, player LEDs (bits 0–4, bit 5 = no fade), R, G, B |

* **Single sequence counter**: the 4-bit `seq` is shared by every Bluetooth output report
  (`0x31`, `0x32`, `0x39`).
* **Rumble vs HD haptics**: setting the rumble bits (flag0 bits 0–1 or flag2 bit 2) switches the
  actuators to rumble emulation and silences audio haptics. Leaving them off restores audio haptics.

### Adaptive triggers (11 bytes, byte 0 = mode, 10 zones)

Bytes 1–2 = zone mask (bit i = zone i); bytes 3–6 = 3 bits of force/amplitude per zone (zone i
in bits 3i..3i+2).

| Mode | Function | Notes |
| -- | -- | -- |
| `0x05` | `trigger_off()` | |
| `0x21` | `trigger_feedback(pos, strength)` | constant resistance from `pos` |
| `0x25` | `trigger_weapon(start, end, strength)` | resistance then a click (lever) |
| `0x26` | `trigger_vibration(pos, amplitude, hz)` | frequency in byte 9 |
| `0x27` | `trigger_machine(start, end, ampA, ampB, hz, period)` | frequency byte 7, period byte 8 |

## HD haptics

* **USB (Windows)**: the controller is a 4-channel 48 kHz USB audio device ("DualSense Wireless
  Controller"). Channels 3–4 drive the left/right actuators. A shared-mode WASAPI stream writes
  them (mixed with anything else playing on that device). Samples at 3 kHz are resampled linearly,
  `v = a·(1 − f) + b·f`.
* **Bluetooth**: report **`0x32`, 142 bytes**, one every 32/3000 s ≈ 10.67 ms:

| Byte | Content |
| -- | -- |
| 0 / 1 | `0x32` / seq << 4 |
| 2–10 | packet `0x11`: `0x91` (pid 0x11 + "sized" bit 7), 7, `FE 00 00 00 00 FF`, counter |
| 11–76 | packet `0x12`: `0x92`, 64, 32 interleaved stereo frames, signed 8-bit PCM at 3 kHz |
| 77–137 | zeros |
| 138–141 | CRC32 (seed `0xA2`) of bytes 0–137 |

Only the interrupt channel works (`hid_write`); `HidD_SetOutputReport` is accepted by Windows but
the controller ignores it. Windows pads writes to the largest output report (547 bytes); the
controller accepts that.

## Speaker

* **USB (Windows)**: channels 1–2 of the same WASAPI stream. The speaker is mono and plays the
  right channel. Routing: report `0x02` with the audio fields above (speaker path 3, volume `0x64`,
  preamp gain 2); headphones use path 0.
* **Bluetooth**: report **`0x39`, 547 bytes, every 20 ms**, preceded once by a `0x31` carrying the
  same routing fields:

| Byte | Content |
| -- | -- |
| 0 / 1 | `0x39` / seq << 4 |
| 2–9 | packet `0x11`: `0x91`, 6, `0x7E`, 4 × audio buffer length (48), counter (+2 per report) |
| 10–139 | packet `0x12`: `0xD2` (bit 6 = two blocks), 64, 2 × 32 stereo s8 haptic frames |
| 140–541 | packet `0x13`: `0xD3` (`0xD6` = headphones), 200, two Opus frames of 200 bytes |
| 542 | 0 |
| 543–546 | CRC32 (seed `0xA2`) of bytes 0–542 |

Opus settings: 48 kHz stereo, 10 ms frames (480 samples), 160 kbit/s CBR (= 200 bytes),
complexity 0, `OPUS_APPLICATION_AUDIO`; each frame starts with TOC `0xF4` (CELT fullband, 10 ms,
stereo). Without speaker audio the extension goes back to the `0x32` report. In `0x39` the haptic
stream is consumed at 64 frames per 20 ms.

## Self-test measurements (firmware 06.30, Windows 11)

Tremor = RMS of the accelerometer around its mean, in mg (8192 raw = 1 g), controller resting on
a table:

| Output | Bluetooth | USB |
| -- | -- | -- |
| Rest | ~2 mg | ~2 mg |
| Classic rumble 100% | ~770 mg | ~690 mg |
| HD haptics 60 Hz (amplitude 0.9) | ~1300 mg | ~1350 mg |
| HD haptics 150 Hz | ~450 mg | ~250 mg |
| HD haptics + 60 output reports/s | unchanged | unchanged |

The actuators peak around 60 Hz. The IMU samples at ~450 Hz, so values above ~225 Hz are aliased.

## Sources

See the README "Credits and sources" section and [THIRD_PARTY.md](../THIRD_PARTY.md).
