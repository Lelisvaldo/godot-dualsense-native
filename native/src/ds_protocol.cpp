#include "ds_protocol.h"

#include <cstring>

namespace ds {

namespace {

uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Offsets inside the data block (Linux struct dualsense_input_report).
constexpr size_t OFF_SEQ = 6;
constexpr size_t OFF_BUTTONS = 7;
constexpr size_t OFF_GYRO = 15;
constexpr size_t OFF_ACCEL = 21;
constexpr size_t OFF_TIMESTAMP = 27;
constexpr size_t OFF_TOUCH = 32;
constexpr size_t OFF_STATUS = 52;
constexpr size_t DATA_SIZE = 63;

void parse_touch(const uint8_t *p, TouchPoint &t) {
    // contact: bit 7 = no touch, bits 0-6 = touch id.
    // x: 12 bits = x_lo | (low nibble of p[2]) << 8; y: 12 bits = (high nibble of p[2]) | y_hi << 4.
    t.active = (p[0] & 0x80) == 0;
    t.id = p[0] & 0x7F;
    t.x = p[1] | ((p[2] & 0x0F) << 8);
    t.y = (p[2] >> 4) | (p[3] << 4);
}

}  // namespace

uint32_t crc32_with_seed(uint8_t seed, const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    auto feed = [&crc](uint8_t byte) {
        crc ^= byte;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    };
    feed(seed);
    for (size_t i = 0; i < len; i++) feed(data[i]);
    return ~crc;
}

bool check_bt_crc(uint8_t seed, const uint8_t *report, size_t len) {
    if (len < 5) return false;
    return crc32_with_seed(seed, report, len - 4) == le32(report + len - 4);
}

bool parse_input_report(const uint8_t *report, size_t len, InputState &out) {
    const uint8_t *d = nullptr;
    if (len >= INPUT_REPORT_USB_SIZE && report[0] == INPUT_REPORT_USB) {
        d = report + 1;
    } else if (len >= INPUT_REPORT_BT_SIZE && report[0] == INPUT_REPORT_BT) {
        d = report + 2;
    } else {
        return false;
    }
    out.left_x = d[0];
    out.left_y = d[1];
    out.right_x = d[2];
    out.right_y = d[3];
    out.l2 = d[4];
    out.r2 = d[5];
    out.seq = d[OFF_SEQ];
    std::memcpy(out.buttons, d + OFF_BUTTONS, 4);
    for (int i = 0; i < 3; i++) {
        out.gyro[i] = int16_t(le16(d + OFF_GYRO + 2 * i));
        out.accel[i] = int16_t(le16(d + OFF_ACCEL + 2 * i));
    }
    out.sensor_timestamp = le32(d + OFF_TIMESTAMP);
    parse_touch(d + OFF_TOUCH, out.touch[0]);
    parse_touch(d + OFF_TOUCH + 4, out.touch[1]);

    // status[0]: low nibble = charge in tenths (0-10), high nibble = charging state.
    const uint8_t s0 = d[OFF_STATUS];
    const int capacity = s0 & 0x0F;
    switch (s0 >> 4) {
        case 0x0:
            out.battery_status = BatteryStatus::Discharging;
            out.battery_percent = capacity * 10 + 5 > 100 ? 100 : capacity * 10 + 5;
            break;
        case 0x1:
            out.battery_status = BatteryStatus::Charging;
            out.battery_percent = capacity * 10 + 5 > 100 ? 100 : capacity * 10 + 5;
            break;
        case 0x2:
            out.battery_status = BatteryStatus::Full;
            out.battery_percent = 100;
            break;
        case 0xA: case 0xB: case 0xF:
            out.battery_status = BatteryStatus::Error;
            out.battery_percent = 0;
            break;
        default:
            out.battery_status = BatteryStatus::Unknown;
            out.battery_percent = 0;
    }
    // status[1]: bit 0 = headphones, bit 1 = external microphone, bit 2 = mute on.
    const uint8_t s1 = d[OFF_STATUS + 1];
    out.headphones = (s1 & 0x01) != 0;
    out.microphone = (s1 & 0x02) != 0;
    out.mic_muted = (s1 & 0x04) != 0;
    static_assert(OFF_STATUS + 3 <= DATA_SIZE, "status dentro do bloco");
    return true;
}

bool parse_firmware_info(const uint8_t *report, size_t len, FirmwareInfo &out) {
    if (len < FEATURE_REPORT_FIRMWARE_INFO_SIZE || report[0] != FEATURE_REPORT_FIRMWARE_INFO) return false;
    // Bytes 1-11: build date ("Mmm dd yyyy"); 12-19: build time ("hh:mm:ss").
    out.build_date.assign(reinterpret_cast<const char *>(report + 1), 11);
    out.build_time.assign(reinterpret_cast<const char *>(report + 12), 8);
    out.hw_version = le32(report + 24);
    out.fw_version = le32(report + 28);
    out.update_version = le16(report + 44);
    return true;
}

namespace {

void put_u16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}

}  // namespace

void trigger_off(uint8_t out[TRIGGER_EFFECT_SIZE]) {
    std::memset(out, 0, TRIGGER_EFFECT_SIZE);
    out[0] = TRIGGER_OFF;
}

bool trigger_feedback(int position, int strength, uint8_t out[TRIGGER_EFFECT_SIZE]) {
    trigger_off(out);
    if (position < 0 || position > 9 || strength < 1 || strength > 8) return strength == 0;
    uint32_t zones = 0, forces = 0;
    for (int i = position; i < 10; i++) {
        zones |= 1u << i;
        forces |= uint32_t(strength - 1) << (3 * i);
    }
    out[0] = TRIGGER_FEEDBACK;
    put_u16(out + 1, zones);
    put_u32(out + 3, forces);
    return true;
}

bool trigger_weapon(int start, int end, int strength, uint8_t out[TRIGGER_EFFECT_SIZE]) {
    trigger_off(out);
    if (start < 2 || start > 7 || end <= start || end > 8 || strength < 1 || strength > 8) return strength == 0;
    out[0] = TRIGGER_WEAPON;
    put_u16(out + 1, (1u << start) | (1u << end));
    out[3] = uint8_t(strength - 1);
    return true;
}

bool trigger_vibration(int position, int amplitude, int frequency, uint8_t out[TRIGGER_EFFECT_SIZE]) {
    trigger_off(out);
    if (position < 0 || position > 9 || amplitude < 1 || amplitude > 8 || frequency < 1 || frequency > 255) {
        return amplitude == 0 || frequency == 0;
    }
    uint32_t zones = 0, amps = 0;
    for (int i = position; i < 10; i++) {
        zones |= 1u << i;
        amps |= uint32_t(amplitude - 1) << (3 * i);
    }
    out[0] = TRIGGER_VIBRATION;
    put_u16(out + 1, zones);
    put_u32(out + 3, amps);
    out[9] = uint8_t(frequency);
    return true;
}

bool trigger_machine(int start, int end, int amplitude_a, int amplitude_b, int frequency, int period,
        uint8_t out[TRIGGER_EFFECT_SIZE]) {
    trigger_off(out);
    if (start < 0 || start > 8 || end <= start || end > 9 || amplitude_a < 0 || amplitude_a > 7 ||
            amplitude_b < 0 || amplitude_b > 7 || frequency < 1 || frequency > 255 || period < 0 || period > 255) {
        return false;
    }
    out[0] = TRIGGER_MACHINE;
    put_u16(out + 1, (1u << start) | (1u << end));
    put_u32(out + 3, uint32_t(amplitude_a) | (uint32_t(amplitude_b) << 3));
    out[7] = uint8_t(frequency);
    out[8] = uint8_t(period);
    return true;
}

void build_output_common(const OutputState &s, uint8_t out[OUTPUT_COMMON_SIZE]) {
    constexpr uint8_t FLAG0_COMPATIBLE_VIBRATION = 1 << 0;
    constexpr uint8_t FLAG0_HAPTICS_SELECT = 1 << 1;
    constexpr uint8_t FLAG0_RIGHT_TRIGGER = 1 << 2;
    constexpr uint8_t FLAG0_LEFT_TRIGGER = 1 << 3;
    constexpr uint8_t FLAG1_MIC_MUTE_LED = 1 << 0;
    constexpr uint8_t FLAG1_LIGHTBAR = 1 << 2;
    constexpr uint8_t FLAG1_PLAYER_INDICATOR = 1 << 4;
    constexpr uint8_t FLAG2_LIGHTBAR_SETUP = 1 << 1;
    constexpr uint8_t FLAG2_COMPATIBLE_VIBRATION2 = 1 << 2;
    constexpr uint8_t LIGHTBAR_SETUP_LIGHT_OUT = 1 << 1;
    constexpr uint8_t PLAYER_LEDS_INSTANT = 1 << 5;  // no default fade (SDL does the same)
    std::memset(out, 0, OUTPUT_COMMON_SIZE);
    out[0] = FLAG0_RIGHT_TRIGGER | FLAG0_LEFT_TRIGGER;
    if (s.rumble_enabled) out[0] |= FLAG0_HAPTICS_SELECT | (s.vibration_v2 ? 0 : FLAG0_COMPATIBLE_VIBRATION);
    out[1] = FLAG1_MIC_MUTE_LED | FLAG1_LIGHTBAR | FLAG1_PLAYER_INDICATOR;
    out[2] = s.motor_right;
    out[3] = s.motor_left;
    out[8] = s.mute_led;
    std::memcpy(out + 10, s.right_trigger, TRIGGER_EFFECT_SIZE);
    std::memcpy(out + 21, s.left_trigger, TRIGGER_EFFECT_SIZE);
    out[38] = ((s.rumble_enabled && s.vibration_v2) ? FLAG2_COMPATIBLE_VIBRATION2 : 0) |
            (s.lightbar_setup ? FLAG2_LIGHTBAR_SETUP : 0);
    out[41] = s.lightbar_setup ? LIGHTBAR_SETUP_LIGHT_OUT : 0;
    out[42] = s.led_brightness;
    out[43] = uint8_t((s.player_leds & 0x1F) | PLAYER_LEDS_INSTANT);
    out[44] = s.red;
    out[45] = s.green;
    out[46] = s.blue;
}

size_t build_output_report(const OutputState &s, bool bluetooth, uint8_t seq, uint8_t *out) {
    if (!bluetooth) {
        std::memset(out, 0, OUTPUT_REPORT_USB_SIZE);
        out[0] = OUTPUT_REPORT_USB;
        build_output_common(s, out + 1);
        return OUTPUT_REPORT_USB_SIZE;
    }
    std::memset(out, 0, OUTPUT_REPORT_BT_SIZE);
    out[0] = OUTPUT_REPORT_BT;
    out[1] = uint8_t((seq & 0x0F) << 4);
    out[2] = OUTPUT_TAG_BT;
    build_output_common(s, out + 3);
    put_u32(out + OUTPUT_REPORT_BT_SIZE - 4, crc32_with_seed(CRC_SEED_OUTPUT, out, OUTPUT_REPORT_BT_SIZE - 4));
    return OUTPUT_REPORT_BT_SIZE;
}

void build_haptics_report_bt(const int8_t samples[2 * HAPTICS_FRAMES_PER_REPORT], uint8_t counter, uint8_t seq,
        uint8_t out[HAPTICS_REPORT_BT_SIZE]) {
    constexpr uint8_t SIZED = 0x80;
    std::memset(out, 0, HAPTICS_REPORT_BT_SIZE);
    out[0] = HAPTICS_REPORT_BT;
    out[1] = uint8_t((seq & 0x0F) << 4);  // seq in bits 4-7, like 0x31
    const uint8_t control[7] = {0xFE, 0, 0, 0, 0, 0xFF, counter};
    out[2] = 0x11 | SIZED;
    out[3] = sizeof(control);
    std::memcpy(out + 4, control, sizeof(control));
    out[11] = 0x12 | SIZED;
    out[12] = 2 * HAPTICS_FRAMES_PER_REPORT;
    std::memcpy(out + 13, samples, 2 * HAPTICS_FRAMES_PER_REPORT);
    put_u32(out + HAPTICS_REPORT_BT_SIZE - 4, crc32_with_seed(CRC_SEED_OUTPUT, out, HAPTICS_REPORT_BT_SIZE - 4));
}

void build_audio_state_common(bool speaker, uint8_t speaker_volume, uint8_t preamp_gain,
        uint8_t common[OUTPUT_COMMON_SIZE]) {
    constexpr uint8_t FLAG0_SPEAKER_VOLUME_ENABLE = 1 << 5;
    constexpr uint8_t FLAG0_AUDIO_CONTROL_ENABLE = 1 << 7;
    constexpr uint8_t FLAG1_AUDIO_CONTROL2_ENABLE = 1 << 7;
    std::memset(common, 0, OUTPUT_COMMON_SIZE);
    common[0] = FLAG0_AUDIO_CONTROL_ENABLE;
    if (speaker) {
        common[0] |= FLAG0_SPEAKER_VOLUME_ENABLE;
        common[1] = FLAG1_AUDIO_CONTROL2_ENABLE;
        common[5] = speaker_volume;                 // speaker_volume
        common[7] = uint8_t(0x3 << 4);              // audio_control: OUTPUT_PATH_SEL (bits 5-4) = 3
        common[37] = uint8_t(preamp_gain & 0x07);   // audio_control2: SP_PREAMP_GAIN (bits 2-0)
    }
    // Without speaker: path 0 (L+R to headphones, speaker muted).
}

void build_audio_route_usb(bool speaker, uint8_t speaker_volume, uint8_t preamp_gain,
        uint8_t out[OUTPUT_REPORT_USB_SIZE]) {
    std::memset(out, 0, OUTPUT_REPORT_USB_SIZE);
    out[0] = OUTPUT_REPORT_USB;
    build_audio_state_common(speaker, speaker_volume, preamp_gain, out + 1);
}

void build_audio_route_bt(bool speaker, uint8_t speaker_volume, uint8_t preamp_gain, uint8_t seq,
        uint8_t out[OUTPUT_REPORT_BT_SIZE]) {
    std::memset(out, 0, OUTPUT_REPORT_BT_SIZE);
    out[0] = OUTPUT_REPORT_BT;
    out[1] = uint8_t((seq & 0x0F) << 4);
    out[2] = OUTPUT_TAG_BT;
    build_audio_state_common(speaker, speaker_volume, preamp_gain, out + 3);
    put_u32(out + OUTPUT_REPORT_BT_SIZE - 4, crc32_with_seed(CRC_SEED_OUTPUT, out, OUTPUT_REPORT_BT_SIZE - 4));
}

void build_audio_report_bt(const int8_t haptics[4 * HAPTICS_FRAMES_PER_REPORT], const uint8_t *opus_a,
        const uint8_t *opus_b, bool headphones, uint8_t seq, uint8_t counter, uint8_t out[AUDIO_REPORT_BT_SIZE]) {
    std::memset(out, 0, AUDIO_REPORT_BT_SIZE);
    out[0] = AUDIO_REPORT_BT;
    out[1] = uint8_t((seq & 0x0F) << 4);
    out[2] = 0x11 | 0x80;
    out[3] = 6;
    out[4] = 0x7E;
    for (int i = 5; i <= 8; i++) out[i] = AUDIO_BUFFER_LENGTH_DEFAULT;
    out[9] = counter;
    out[10] = 0x12 | 0x40 | 0x80;
    out[11] = 2 * HAPTICS_FRAMES_PER_REPORT;
    std::memcpy(out + 12, haptics, 4 * HAPTICS_FRAMES_PER_REPORT);
    out[140] = uint8_t((headphones ? 0x16 : 0x13) | 0x40 | 0x80);
    out[141] = uint8_t(SPEAKER_OPUS_BYTES);
    if (opus_a) std::memcpy(out + 142, opus_a, SPEAKER_OPUS_BYTES);
    if (opus_b) std::memcpy(out + 142 + SPEAKER_OPUS_BYTES, opus_b, SPEAKER_OPUS_BYTES);
    put_u32(out + AUDIO_REPORT_BT_SIZE - 4, crc32_with_seed(CRC_SEED_OUTPUT, out, AUDIO_REPORT_BT_SIZE - 4));
}

const char *battery_status_name(BatteryStatus s) {
    switch (s) {
        case BatteryStatus::Discharging: return "discharging";
        case BatteryStatus::Charging: return "charging";
        case BatteryStatus::Full: return "full";
        case BatteryStatus::Error: return "error";
        default: return "unknown";
    }
}

}  // namespace ds
