// DualSense HID protocol: report parsing and building, CRC32. No Godot dependency.
//
// Layouts cross-checked against the Linux hid-playstation driver (struct dualsense_input_report,
// struct dualsense_output_report_common) and SDL (SDL_hidapi_ps5.c). Offsets are relative to the
// data block after the report header (USB: 1 byte; Bluetooth: 2 or 3 bytes, see each report).
// Sources and licenses: see THIRD_PARTY.md and docs/PROTOCOL.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ds {

constexpr uint16_t VENDOR_SONY = 0x054C;
constexpr uint16_t PRODUCT_DUALSENSE = 0x0CE6;
constexpr uint16_t PRODUCT_DUALSENSE_EDGE = 0x0DF2;

// Full input reports.
constexpr uint8_t INPUT_REPORT_USB = 0x01;
constexpr size_t INPUT_REPORT_USB_SIZE = 64;
constexpr uint8_t INPUT_REPORT_BT = 0x31;
constexpr size_t INPUT_REPORT_BT_SIZE = 78;

// Feature reports.
constexpr uint8_t FEATURE_REPORT_CALIBRATION = 0x05;
constexpr size_t FEATURE_REPORT_CALIBRATION_SIZE = 41;
constexpr uint8_t FEATURE_REPORT_FIRMWARE_INFO = 0x20;
constexpr size_t FEATURE_REPORT_FIRMWARE_INFO_SIZE = 64;

// Bluetooth CRC32 seeds: the CRC covers [seed, report...].
constexpr uint8_t CRC_SEED_INPUT = 0xA1;
constexpr uint8_t CRC_SEED_OUTPUT = 0xA2;
constexpr uint8_t CRC_SEED_FEATURE = 0xA3;

// Touchpad area in sensor units.
constexpr int TOUCHPAD_WIDTH = 1920;
constexpr int TOUCHPAD_HEIGHT = 1080;

// Output reports. USB 0x02: 63 bytes, common block starts at byte 1.
// Bluetooth 0x31: 78 bytes = [id, seq<<4, tag 0x10, common block (47), 24 reserved, CRC32].
constexpr uint8_t OUTPUT_REPORT_USB = 0x02;
constexpr size_t OUTPUT_REPORT_USB_SIZE = 63;
constexpr uint8_t OUTPUT_REPORT_BT = 0x31;
constexpr size_t OUTPUT_REPORT_BT_SIZE = 78;
constexpr uint8_t OUTPUT_TAG_BT = 0x10;
constexpr size_t OUTPUT_COMMON_SIZE = 47;
constexpr size_t TRIGGER_EFFECT_SIZE = 11;

// Speaker volume and preamp gain used by the Linux driver (0x64 = 100; 2 = +6 dB).
constexpr uint8_t SPEAKER_VOLUME_DEFAULT = 0x64;
constexpr uint8_t SPEAKER_PREAMP_GAIN_DEFAULT = 0x2;

// Firmware from which "v2" rumble emulation is the default (update_version >= 2.21 = 0x0215).
constexpr uint16_t VIBRATION_V2_MIN_UPDATE_VERSION = 0x0215;

struct TouchPoint {
    bool active = false;
    int id = 0;
    int x = 0;
    int y = 0;
};

enum class BatteryStatus { Discharging, Charging, Full, Error, Unknown };

struct InputState {
    uint8_t left_x = 128, left_y = 128, right_x = 128, right_y = 128;
    uint8_t l2 = 0, r2 = 0;
    uint8_t seq = 0;
    uint8_t buttons[4] = {0, 0, 0, 0};
    int16_t gyro[3] = {0, 0, 0};
    int16_t accel[3] = {0, 0, 0};
    uint32_t sensor_timestamp = 0;
    TouchPoint touch[2];
    int battery_percent = 0;  // 0-100
    BatteryStatus battery_status = BatteryStatus::Unknown;
    bool headphones = false;
    bool microphone = false;
    bool mic_muted = false;
};

struct FirmwareInfo {
    std::string build_date;  // e.g. "Jun 12 2025"
    std::string build_time;  // e.g. "10:22:33"
    uint32_t hw_version = 0;
    uint32_t fw_version = 0;
    uint16_t update_version = 0;
};

// CRC-32 (reflected polynomial 0xEDB88320, same as zlib) started with the seed byte.
uint32_t crc32_with_seed(uint8_t seed, const uint8_t *data, size_t len);

// Checks the CRC stored in the last 4 bytes (little endian) of a Bluetooth report.
bool check_bt_crc(uint8_t seed, const uint8_t *report, size_t len);

// Parses a full input report (including the ID byte): USB 0x01 (64 bytes) or Bluetooth 0x31
// (78 bytes). Returns false for anything else, e.g. the short 0x01 report (10 bytes) that the
// DualSense sends over Bluetooth before being switched to full reports.
bool parse_input_report(const uint8_t *report, size_t len, InputState &out);

// Parses feature report 0x20 (including the ID byte, 64 bytes).
bool parse_firmware_info(const uint8_t *report, size_t len, FirmwareInfo &out);

const char *battery_status_name(BatteryStatus s);

// Adaptive trigger effects: 11 bytes, byte 0 = mode. The travel is split into 10 zones
// (0 = released, 9 = fully pressed). Encoding as documented by the community (Nielk1,
// "TriggerEffectGenerator", MIT) and SDL:
//   bytes 1-2: zone mask (u16 LE, bit i = zone i);
//   bytes 3-6: force/amplitude per zone, 3 bits each (u32 LE, zone i in bits 3i..3i+2);
//   Vibration: frequency (Hz) in byte 9; Machine: frequency in byte 7 and period in byte 8.
enum TriggerMode : uint8_t {
    TRIGGER_OFF = 0x05,
    TRIGGER_FEEDBACK = 0x21,
    TRIGGER_WEAPON = 0x25,
    TRIGGER_VIBRATION = 0x26,
    TRIGGER_MACHINE = 0x27,
};
void trigger_off(uint8_t out[TRIGGER_EFFECT_SIZE]);
// Continuous resistance from zone `position` (0-9) with strength 1-8.
bool trigger_feedback(int position, int strength, uint8_t out[TRIGGER_EFFECT_SIZE]);
// Resistance between start (2-7) and end (start+1..8) with strength 1-8, then a "click" when the
// resistance is overcome (a lever / trigger-pull feeling).
bool trigger_weapon(int start, int end, int strength, uint8_t out[TRIGGER_EFFECT_SIZE]);
// Trigger vibration from zone `position` (0-9), amplitude 1-8, frequency 1-255 Hz.
bool trigger_vibration(int position, int amplitude, int frequency, uint8_t out[TRIGGER_EFFECT_SIZE]);
// Vibration between start (0-8) and end (start+1..9) alternating amplitudes A and B (0-7) every
// `period` tenths of a second, frequency 1-255 Hz.
bool trigger_machine(int start, int end, int amplitude_a, int amplitude_b, int frequency, int period,
        uint8_t out[TRIGGER_EFFECT_SIZE]);

// State of every HID output. The report always carries all sections (rumble, triggers, mute LED,
// player LEDs, lightbar); audio fields are left untouched so the chosen audio path is kept.
struct OutputState {
    uint8_t motor_left = 0;   // strong (low-frequency) motor, 0-255
    uint8_t motor_right = 0;  // weak (high-frequency) motor, 0-255
    uint8_t right_trigger[TRIGGER_EFFECT_SIZE] = {TRIGGER_OFF};
    uint8_t left_trigger[TRIGGER_EFFECT_SIZE] = {TRIGGER_OFF};
    uint8_t mute_led = 0;         // 0 off, 1 on, 2 pulsing
    uint8_t player_leds = 0;      // bits 0-4 = the 5 LEDs below the touchpad, left to right
    uint8_t led_brightness = 0;   // 0 high, 1 medium, 2 low
    uint8_t red = 0, green = 0, blue = 0;
    bool vibration_v2 = true;     // firmware >= 2.21
    bool lightbar_setup = false;  // send LIGHT_OUT (release the lightbar from the default blue) in this report
    // Rumble emulation (classic motors). When false the report does not set the rumble bits
    // (flag0 bits 0-1, flag2 bit 2), so the actuators stay in audio-haptics (HD) mode.
    bool rumble_enabled = true;
};
// 47-byte common block (Linux struct dualsense_output_report_common).
void build_output_common(const OutputState &s, uint8_t out[OUTPUT_COMMON_SIZE]);
// Full report; `out` needs 78 bytes. Returns the size (63 USB, 78 Bluetooth).
size_t build_output_report(const OutputState &s, bool bluetooth, uint8_t seq, uint8_t *out);

// HD haptics over Bluetooth. Format from the clean-room research SAxense (Sdore, 2025, MPL-2.0,
// https://apps.sdore.me/SAxense), size/seq cross-checked with dualsense-bt-haptics (awalol, MIT);
// reimplemented here:
//   report 0x32, 142 bytes (id + 137 data + CRC32): [0x32, seq<<4, packet 0x11, packet 0x12, zeros...,
//   CRC32 (seed 0xA2) of bytes 0-137 stored in bytes 138-141];
//   packet header = pid (bits 0-5) | "sized" (bit 7), followed by the length;
//   0x11 (7 bytes): FE 00 00 00 00 FF <counter incremented every report>;
//   0x12 (64 bytes): 32 interleaved stereo frames (left, right) of signed 8-bit PCM at 3000 Hz.
//   The two channels drive the left and right actuators. One report every 32/3000 s = 10.67 ms.
constexpr uint8_t HAPTICS_REPORT_BT = 0x32;
constexpr size_t HAPTICS_REPORT_BT_SIZE = 142;
constexpr int HAPTICS_SAMPLE_RATE = 3000;
constexpr int HAPTICS_FRAMES_PER_REPORT = 32;
void build_haptics_report_bt(const int8_t samples[2 * HAPTICS_FRAMES_PER_REPORT], uint8_t counter, uint8_t seq,
        uint8_t out[HAPTICS_REPORT_BT_SIZE]);

// Audio path (USB report 0x02), like the Linux driver:
// speaker=true  -> headphones muted, RIGHT channel routed to the mono speaker, volume + preamp;
// speaker=false -> speaker muted, L+R routed to the headphone jack.
void build_audio_route_usb(bool speaker, uint8_t speaker_volume, uint8_t preamp_gain,
        uint8_t out[OUTPUT_REPORT_USB_SIZE]);
// Same audio-path common block, for any transport.
void build_audio_state_common(bool speaker, uint8_t speaker_volume, uint8_t preamp_gain,
        uint8_t common[OUTPUT_COMMON_SIZE]);
// Audio path over Bluetooth: 0x31 report (78 bytes) with the common block and CRC32 (seed 0xA2).
void build_audio_route_bt(bool speaker, uint8_t speaker_volume, uint8_t preamp_gain, uint8_t seq,
        uint8_t out[OUTPUT_REPORT_BT_SIZE]);

// Speaker over Bluetooth. Format from DS5Dongle (awalol, MIT, https://github.com/awalol/DS5Dongle,
// src/audio.cpp), reimplemented here:
//   report 0x39, 547 bytes, one every 20 ms:
//   [0] 0x39  [1] seq<<4
//   [2] 0x91 (packet 0x11, "sized")  [3] 6  [4] 0x7E  [5..8] audio buffer length  [9] counter (+2)
//   [10] 0xD2 (packet 0x12 with bit 6)  [11] 64  [12..139] haptics: 2 blocks of 32 stereo s8 frames
//   [140] 0xD3 (packet 0x13 = speaker; 0xD6 = headphones)  [141] 200  [142..541] 2 Opus frames of 200 bytes
//   [542] 0  [543..546] CRC32 (seed 0xA2) of bytes 0-542.
//   Opus: 48 kHz stereo, 10 ms frames (480 samples), 160 kbit/s CBR (= 200 bytes), complexity 0.
constexpr uint8_t AUDIO_REPORT_BT = 0x39;
constexpr size_t AUDIO_REPORT_BT_SIZE = 547;
constexpr int SPEAKER_SAMPLE_RATE = 48000;
constexpr int SPEAKER_OPUS_FRAME = 480;
constexpr size_t SPEAKER_OPUS_BYTES = 200;
constexpr uint8_t AUDIO_BUFFER_LENGTH_DEFAULT = 48;
void build_audio_report_bt(const int8_t haptics[4 * HAPTICS_FRAMES_PER_REPORT], const uint8_t *opus_a,
        const uint8_t *opus_b, bool headphones, uint8_t seq, uint8_t counter, uint8_t out[AUDIO_REPORT_BT_SIZE]);

}  // namespace ds
