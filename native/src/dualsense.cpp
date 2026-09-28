#include "dualsense.h"

#include <hidapi.h>
#include <opus.h>

#include <chrono>
#include <cstring>

#include <godot_cpp/core/math.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector3i.hpp>

using namespace godot;

namespace {

bool is_dualsense(uint16_t product) {
    return product == ds::PRODUCT_DUALSENSE || product == ds::PRODUCT_DUALSENSE_EDGE;
}

String wide(const wchar_t *s) { return s ? String(s) : String(); }

}  // namespace

DualSense::~DualSense() { close(); }

void DualSense::_bind_methods() {
    ClassDB::bind_static_method("DualSense", D_METHOD("list_devices"), &DualSense::list_devices);
    ClassDB::bind_method(D_METHOD("open", "path"), &DualSense::open);
    ClassDB::bind_method(D_METHOD("close"), &DualSense::close);
    ClassDB::bind_method(D_METHOD("is_open"), &DualSense::is_open);
    ClassDB::bind_method(D_METHOD("is_bluetooth"), &DualSense::is_bluetooth);
    ClassDB::bind_method(D_METHOD("poll"), &DualSense::poll);
    ClassDB::bind_method(D_METHOD("get_state"), &DualSense::get_state);
    ClassDB::bind_method(D_METHOD("get_firmware"), &DualSense::get_firmware);
    ClassDB::bind_method(D_METHOD("get_info"), &DualSense::get_info);
    ClassDB::bind_method(D_METHOD("get_last_error"), &DualSense::get_last_error);
    ClassDB::bind_method(D_METHOD("set_audio_route", "speaker"), &DualSense::set_audio_route);
    ClassDB::bind_method(D_METHOD("get_audio_route"), &DualSense::get_audio_route);
    ClassDB::bind_static_method("DualSense", D_METHOD("build_audio_route_report", "speaker"), &DualSense::build_audio_route_report);
    ClassDB::bind_method(D_METHOD("set_rumble", "strong", "weak"), &DualSense::set_rumble);
    ClassDB::bind_method(D_METHOD("set_trigger_effect", "right", "effect"), &DualSense::set_trigger_effect);
    ClassDB::bind_method(D_METHOD("set_player_leds", "mask", "brightness"), &DualSense::set_player_leds);
    ClassDB::bind_method(D_METHOD("set_mute_led", "mode"), &DualSense::set_mute_led);
    ClassDB::bind_method(D_METHOD("set_lightbar", "color"), &DualSense::set_lightbar);
    ClassDB::bind_method(D_METHOD("send_output"), &DualSense::send_output);
    ClassDB::bind_method(D_METHOD("reset_outputs"), &DualSense::reset_outputs);
    ClassDB::bind_method(D_METHOD("build_output_report", "bluetooth", "seq"), &DualSense::build_output_report);
    ClassDB::bind_method(D_METHOD("haptics_supported"), &DualSense::haptics_supported);
    ClassDB::bind_method(D_METHOD("haptics_play", "frames"), &DualSense::haptics_play);
    ClassDB::bind_method(D_METHOD("haptics_clear"), &DualSense::haptics_clear);
    ClassDB::bind_method(D_METHOD("speaker_play", "frames"), &DualSense::speaker_play);
    ClassDB::bind_method(D_METHOD("speaker_supported"), &DualSense::speaker_supported);
    ClassDB::bind_method(D_METHOD("speaker_set_gain", "gain"), &DualSense::speaker_set_gain);
    ClassDB::bind_method(D_METHOD("speaker_level"), &DualSense::get_speaker_level);
    ClassDB::bind_static_method("DualSense", D_METHOD("build_audio_report", "haptics", "speaker", "seq"), &DualSense::build_audio_report);
    ClassDB::bind_static_method("DualSense", D_METHOD("build_audio_route_report_bt", "speaker", "seq"), &DualSense::build_audio_route_report_bt);
    ClassDB::bind_method(D_METHOD("haptics_set_gain", "gain"), &DualSense::haptics_set_gain);
    ClassDB::bind_method(D_METHOD("haptics_reports_sent"), &DualSense::haptics_reports_sent);
    ClassDB::bind_method(D_METHOD("haptics_levels"), &DualSense::haptics_levels);
    ClassDB::bind_method(D_METHOD("haptics_debug"), &DualSense::haptics_debug);
    ClassDB::bind_method(D_METHOD("set_rumble_enabled", "enabled"), &DualSense::set_rumble_enabled);
    ClassDB::bind_method(D_METHOD("haptics_set_transport", "transport"), &DualSense::haptics_set_transport);
    ClassDB::bind_method(D_METHOD("imu_capture_start"), &DualSense::imu_capture_start);
    ClassDB::bind_method(D_METHOD("send_raw", "data", "transport", "fix_crc"), &DualSense::send_raw);
    ClassDB::bind_method(D_METHOD("imu_capture_stop"), &DualSense::imu_capture_stop);
    ClassDB::bind_static_method("DualSense", D_METHOD("build_haptics_report", "frames", "counter"), &DualSense::build_haptics_report);
    ClassDB::bind_static_method("DualSense", D_METHOD("trigger_off"), &DualSense::trigger_off);
    ClassDB::bind_static_method("DualSense", D_METHOD("trigger_feedback", "position", "strength"), &DualSense::trigger_feedback);
    ClassDB::bind_static_method("DualSense", D_METHOD("trigger_weapon", "start", "end", "strength"), &DualSense::trigger_weapon);
    ClassDB::bind_static_method("DualSense", D_METHOD("trigger_vibration", "position", "amplitude", "frequency"), &DualSense::trigger_vibration);
    ClassDB::bind_static_method("DualSense", D_METHOD("trigger_machine", "start", "end", "amplitude_a", "amplitude_b", "frequency", "period"), &DualSense::trigger_machine);
    ClassDB::bind_static_method("DualSense", D_METHOD("parse_input_report", "report"), &DualSense::parse_input_report);
    ClassDB::bind_static_method("DualSense", D_METHOD("parse_firmware_info", "report"), &DualSense::parse_firmware_info);
    ClassDB::bind_static_method("DualSense", D_METHOD("crc32_with_seed", "seed", "data"), &DualSense::crc32_with_seed);
}

Array DualSense::list_devices() {
    Array out;
    hid_device_info *list = hid_enumerate(ds::VENDOR_SONY, 0);
    for (hid_device_info *it = list; it; it = it->next) {
        if (!is_dualsense(it->product_id)) continue;
        Dictionary d;
        d["path"] = String(it->path);
        d["product_id"] = it->product_id;
        d["edge"] = it->product_id == ds::PRODUCT_DUALSENSE_EDGE;
        d["connection"] = it->bus_type == HID_API_BUS_BLUETOOTH ? "bluetooth" : (it->bus_type == HID_API_BUS_USB ? "usb" : "unknown");
        d["serial"] = wide(it->serial_number);
        d["product"] = wide(it->product_string);
        d["interface"] = it->interface_number;
        d["usage_page"] = it->usage_page;
        d["usage"] = it->usage;
        out.push_back(d);
    }
    hid_free_enumeration(list);
    return out;
}

bool DualSense::read_feature(uint8_t id, size_t size, uint8_t *buf) {
    buf[0] = id;
    const int n = hid_get_feature_report(device, buf, size);
    return n >= int(size) - 1 && buf[0] == id;
}

bool DualSense::open(const String &p_path) {
    close();
    last_error = String();
    hid_device *dev = hid_open_path(p_path.utf8().get_data());
    if (!dev) {
        last_error = "hid_open_path failed: " + wide(hid_error(nullptr));
        return false;
    }
    device = dev;
    path = p_path;
    hid_set_nonblocking(device, 1);
    if (const hid_device_info *info = hid_get_device_info(device)) {
        bluetooth = info->bus_type == HID_API_BUS_BLUETOOTH;
        edge = info->product_id == ds::PRODUCT_DUALSENSE_EDGE;
    }
    // Reading calibration (0x05) and firmware (0x20) switches the DualSense, over Bluetooth, from
    // the simple 0x01 report to the full 0x31 report (same sequence as Linux and SDL).
    uint8_t buf[64] = {0};
    read_feature(ds::FEATURE_REPORT_CALIBRATION, ds::FEATURE_REPORT_CALIBRATION_SIZE, buf);
    uint8_t fw[64] = {0};
    has_firmware = read_feature(ds::FEATURE_REPORT_FIRMWARE_INFO, ds::FEATURE_REPORT_FIRMWARE_INFO_SIZE, fw) &&
            ds::parse_firmware_info(fw, sizeof(fw), firmware);
    if (!has_firmware) last_error = "could not read firmware (feature report 0x20)";
    output.vibration_v2 = !has_firmware || firmware.update_version >= ds::VIBRATION_V2_MIN_UPDATE_VERSION || edge;
    lightbar_released = false;
    return true;
}

int DualSense::hid_write_locked(const uint8_t *data, size_t len) {
    std::lock_guard<std::mutex> lock(hid_mutex);
    return device ? hid_write(device, data, len) : -1;
}

void DualSense::close() {
    haptics_stop();
    if (opus_encoder) {
        opus_encoder_destroy(static_cast<OpusEncoder *>(opus_encoder));
        opus_encoder = nullptr;
    }
    if (device) hid_close(device);
    audio_route = String();
    device = nullptr;
    has_state = false;
    has_firmware = false;
}

bool DualSense::set_audio_route(bool p_speaker) {
    if (!device) {
        last_error = "controller not open";
        return false;
    }
    uint8_t report[ds::OUTPUT_REPORT_BT_SIZE];
    size_t n;
    if (bluetooth) {
        ds::build_audio_route_bt(p_speaker, ds::SPEAKER_VOLUME_DEFAULT, ds::SPEAKER_PREAMP_GAIN_DEFAULT, next_bt_seq(), report);
        n = ds::OUTPUT_REPORT_BT_SIZE;
    } else {
        ds::build_audio_route_usb(p_speaker, ds::SPEAKER_VOLUME_DEFAULT, ds::SPEAKER_PREAMP_GAIN_DEFAULT, report);
        n = ds::OUTPUT_REPORT_USB_SIZE;
    }
    if (hid_write_locked(report, n) < 0) {
        last_error = "hid_write failed";
        return false;
    }
    audio_route = p_speaker ? "speaker" : "headphones";
    return true;
}

PackedByteArray DualSense::to_bytes(const uint8_t *p, size_t n) {
    PackedByteArray out;
    out.resize(int64_t(n));
    std::memcpy(out.ptrw(), p, n);
    return out;
}

bool DualSense::from_effect(const PackedByteArray &p_effect, uint8_t out[ds::TRIGGER_EFFECT_SIZE]) {
    if (p_effect.size() != int64_t(ds::TRIGGER_EFFECT_SIZE)) return false;
    std::memcpy(out, p_effect.ptr(), ds::TRIGGER_EFFECT_SIZE);
    return true;
}

void DualSense::set_rumble(double p_strong, double p_weak) {
    output.motor_left = uint8_t(CLAMP(p_strong, 0.0, 1.0) * 255.0 + 0.5);
    output.motor_right = uint8_t(CLAMP(p_weak, 0.0, 1.0) * 255.0 + 0.5);
    output_dirty = true;
}

bool DualSense::set_trigger_effect(bool p_right, const PackedByteArray &p_effect) {
    if (!from_effect(p_effect, p_right ? output.right_trigger : output.left_trigger)) {
        last_error = "trigger effect must be 11 bytes";
        return false;
    }
    output_dirty = true;
    return true;
}

void DualSense::set_player_leds(int p_mask, int p_brightness) {
    output.player_leds = uint8_t(p_mask & 0x1F);
    output.led_brightness = uint8_t(CLAMP(p_brightness, 0, 2));
    output_dirty = true;
}

void DualSense::set_mute_led(int p_mode) {
    output.mute_led = uint8_t(CLAMP(p_mode, 0, 2));
    output_dirty = true;
}

void DualSense::set_lightbar(const Color &p_color) {
    output.red = uint8_t(CLAMP(p_color.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    output.green = uint8_t(CLAMP(p_color.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    output.blue = uint8_t(CLAMP(p_color.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    output_dirty = true;
}

bool DualSense::send_output() {
    if (!device) return false;
    ds::OutputState s = output;
    s.lightbar_setup = !lightbar_released;
    uint8_t report[ds::OUTPUT_REPORT_BT_SIZE];
    const size_t n = ds::build_output_report(s, bluetooth, next_bt_seq(), report);
    if (hid_write_locked(report, n) < 0) {
        last_error = "hid_write failed";
        return false;
    }
    lightbar_released = true;
    output_dirty = false;
    outputs_sent++;
    return true;
}

void DualSense::reset_outputs() {
    const bool v2 = output.vibration_v2;
    output = ds::OutputState();
    output.vibration_v2 = v2;
    output_dirty = true;
}

PackedByteArray DualSense::build_output_report(bool p_bluetooth, int p_seq) const {
    uint8_t report[ds::OUTPUT_REPORT_BT_SIZE];
    const size_t n = ds::build_output_report(output, p_bluetooth, uint8_t(p_seq), report);
    return to_bytes(report, n);
}

bool DualSense::haptics_play(const PackedVector2Array &p_frames) {
    if (!haptics_supported()) {
        last_error = "HD haptics unavailable on this connection";
        return false;
    }
    bool need_start = false;
    {
        std::lock_guard<std::mutex> lock(haptics_mutex);
        if (haptics_ring.empty()) haptics_ring.assign(size_t(2 * HAPTICS_RING_FRAMES), 0.0f);
        // Start one report ahead of the read position so the effect start isn't cut.
        const int64_t start = haptics_read + ds::HAPTICS_FRAMES_PER_REPORT;
        const int64_t count = MIN(int64_t(p_frames.size()), int64_t(HAPTICS_RING_FRAMES - 2 * ds::HAPTICS_FRAMES_PER_REPORT));
        for (int64_t i = 0; i < count; i++) {
            const size_t k = size_t(((start + i) % HAPTICS_RING_FRAMES) * 2);
            haptics_ring[k] += p_frames[i].x;
            haptics_ring[k + 1] += p_frames[i].y;
        }
        need_start = claim_audio_thread_locked();
    }
    if (need_start) start_audio_thread();
    return true;
}

bool DualSense::speaker_play(const PackedVector2Array &p_frames) {
    if (!speaker_supported()) {
        last_error = "speaker unavailable on this connection";
        return false;
    }
    bool need_start = false;
    {
        std::lock_guard<std::mutex> lock(haptics_mutex);
        if (speaker_ring.empty()) speaker_ring.assign(size_t(2 * SPEAKER_RING_FRAMES), 0.0f);
        if (haptics_ring.empty()) haptics_ring.assign(size_t(2 * HAPTICS_RING_FRAMES), 0.0f);
        // Start 20 ms ahead of the read position (one 0x39 report).
        const int64_t start = speaker_read + 2 * ds::SPEAKER_OPUS_FRAME;
        const int64_t count = MIN(int64_t(p_frames.size()), int64_t(SPEAKER_RING_FRAMES - 4 * ds::SPEAKER_OPUS_FRAME));
        for (int64_t i = 0; i < count; i++) {
            const size_t k = size_t(((start + i) % SPEAKER_RING_FRAMES) * 2);
            speaker_ring[k] += p_frames[i].x;
            speaker_ring[k + 1] += p_frames[i].y;
        }
        speaker_pending = true;
        need_start = claim_audio_thread_locked();
    }
    if (need_start) start_audio_thread();
    return true;
}

bool DualSense::claim_audio_thread_locked() {
    // Decided under the same mutex the thread uses to stop: no effect is lost.
    const bool need_start = !haptics_running.load();
    if (need_start) haptics_running = true;
    return need_start;
}

void DualSense::start_audio_thread() {
    if (haptics_thread.joinable()) haptics_thread.join();
    haptics_thread = std::thread(&DualSense::haptics_loop, this);
}

void DualSense::haptics_clear() {
    std::lock_guard<std::mutex> lock(haptics_mutex);
    std::fill(haptics_ring.begin(), haptics_ring.end(), 0.0f);
}

void DualSense::haptics_stop() {
    haptics_running = false;
    if (haptics_thread.joinable()) haptics_thread.join();
}

void DualSense::haptics_loop() {
    if (bluetooth) {
        haptics_loop_bt();
    } else {
        haptics_loop_usb();
    }
}

void DualSense::haptics_loop_usb() {
    // Linear resampling to the device rate: v = a*(1 - f) + b*f, with a and b the neighbouring ring
    // samples and f the fractional position. Haptics: 3 kHz -> 48 kHz (16 frames per sample);
    // speaker: 48 kHz -> 48 kHz (step 1) with the default Windows configuration.
    double hfrac = 0.0, sfrac = 0.0;
    int silent_frames = 0;
    auto pull = [&](float *q, int frames, int rate) -> bool {
        std::lock_guard<std::mutex> lock(haptics_mutex);
        const double hstep = double(ds::HAPTICS_SAMPLE_RATE) / double(rate);
        const double sstep = double(ds::SPEAKER_SAMPLE_RATE) / double(rate);
        const bool has_speaker = !speaker_ring.empty();
        float peak_l = 0.0f, peak_r = 0.0f, peak_s = 0.0f;
        bool silent = true;
        for (int i = 0; i < frames; i++) {
            float out[4] = {0, 0, 0, 0};
            if (has_speaker) {
                const size_t a = size_t((speaker_read % SPEAKER_RING_FRAMES) * 2);
                const size_t b = size_t(((speaker_read + 1) % SPEAKER_RING_FRAMES) * 2);
                for (int c = 0; c < 2; c++) {
                    const double v = double(speaker_ring[a + c]) * (1.0 - sfrac) + double(speaker_ring[b + c]) * sfrac;
                    out[c] = float(CLAMP(v * speaker_gain, -1.0, 1.0));
                }
                sfrac += sstep;
                while (sfrac >= 1.0) {
                    const size_t k = size_t((speaker_read % SPEAKER_RING_FRAMES) * 2);
                    speaker_ring[k] = speaker_ring[k + 1] = 0.0f;
                    speaker_read++;
                    sfrac -= 1.0;
                }
            }
            const size_t k0 = size_t((haptics_read % HAPTICS_RING_FRAMES) * 2);
            const size_t k1 = size_t(((haptics_read + 1) % HAPTICS_RING_FRAMES) * 2);
            for (int c = 0; c < 2; c++) {
                const double v = double(haptics_ring[k0 + c]) * (1.0 - hfrac) + double(haptics_ring[k1 + c]) * hfrac;
                out[2 + c] = float(CLAMP(v * haptics_gain, -1.0, 1.0));
            }
            hfrac += hstep;
            while (hfrac >= 1.0) {
                const size_t k = size_t((haptics_read % HAPTICS_RING_FRAMES) * 2);
                haptics_ring[k] = haptics_ring[k + 1] = 0.0f;
                haptics_read++;
                hfrac -= 1.0;
            }
            for (int c = 0; c < 4; c++) {
                q[4 * i + c] = out[c];
                if (out[c] != 0.0f) silent = false;
            }
            peak_s = MAX(peak_s, MAX(ABS(out[0]), ABS(out[1])));
            peak_l = MAX(peak_l, ABS(out[2]));
            peak_r = MAX(peak_r, ABS(out[3]));
        }
        haptics_level_l = peak_l;
        haptics_level_r = peak_r;
        speaker_level = peak_s;
        haptics_reports++;
        if (peak_s > 0.0f) speaker_reports++;
        silent_frames = silent ? silent_frames + frames : 0;
        if (silent_frames > rate / 2) {
            haptics_running = false;
            return false;
        }
        return true;
    };
    ds::UsbHapticsInfo info;
    const bool ok = ds::usb_haptics_run(haptics_running, pull, info);
    std::lock_guard<std::mutex> lock(haptics_mutex);
    usb_info = info;
    if (!ok) {
        haptics_write_errors++;
        haptics_running = false;
    }
}

void DualSense::haptics_loop_bt() {
    using clock = std::chrono::steady_clock;
    // Haptics only: one 0x32 report every 32 frames / 3000 Hz = 10.67 ms (period accumulated in ns).
    const auto haptic_period = std::chrono::nanoseconds(int64_t(1e9) * ds::HAPTICS_FRAMES_PER_REPORT / ds::HAPTICS_SAMPLE_RATE);
    // With speaker audio: one 0x39 report every 20 ms (2 Opus frames of 10 ms + 2 haptic blocks).
    const auto audio_period = std::chrono::milliseconds(20);
    auto next = clock::now();
    uint8_t haptic_counter = 0, audio_counter = 0;
    // Silence time (haptics and speaker) and time since the last audio, in ms.
    double silent_ms = 0.0, since_audio_ms = 1e9;
    bool route_sent = false;
    while (haptics_running.load()) {
        bool audio_mode;
        int8_t hap[4 * ds::HAPTICS_FRAMES_PER_REPORT];
        float spk[2 * 2 * ds::SPEAKER_OPUS_FRAME];
        bool silent = true, spk_silent = true;
        {
            std::lock_guard<std::mutex> lock(haptics_mutex);
            if (silent_ms > 500.0) {
                haptics_running = false;
                break;
            }
            // 0x39 mode stays on while there is audio and for 300 ms after it (avoids flapping).
            // A new speaker_play() resets the clock: audio starts 20 ms ahead and the first frames read
            // are still silent, so the mode must hold until the audio arrives.
            if (speaker_pending) since_audio_ms = 0.0;
            speaker_pending = false;
            audio_mode = !speaker_ring.empty() && since_audio_ms < 300.0;
            const int hframes = audio_mode ? 2 * ds::HAPTICS_FRAMES_PER_REPORT : ds::HAPTICS_FRAMES_PER_REPORT;
            for (int i = 0; i < hframes; i++) {
                const size_t k = size_t(((haptics_read + i) % HAPTICS_RING_FRAMES) * 2);
                for (int c = 0; c < 2; c++) {
                    const float v = haptics_ring.empty() ? 0.0f : float(CLAMP(double(haptics_ring[k + c]) * haptics_gain, -1.0, 1.0));
                    hap[2 * i + c] = int8_t(Math::round(v * 127.0f));
                    if (hap[2 * i + c] != 0) silent = false;
                    if (!haptics_ring.empty()) haptics_ring[k + c] = 0.0f;
                }
            }
            haptics_read += hframes;
            if (audio_mode) {
                for (int i = 0; i < 2 * ds::SPEAKER_OPUS_FRAME; i++) {
                    const size_t k = size_t(((speaker_read + i) % SPEAKER_RING_FRAMES) * 2);
                    for (int c = 0; c < 2; c++) {
                        const float v = float(CLAMP(double(speaker_ring[k + c]) * speaker_gain, -1.0, 1.0));
                        spk[2 * i + c] = v;
                        if (v != 0.0f) spk_silent = false;
                        speaker_ring[k + c] = 0.0f;
                    }
                }
                speaker_read += 2 * ds::SPEAKER_OPUS_FRAME;
            }
        }
        int peak_l = 0, peak_r = 0;
        const int hframes = audio_mode ? 2 * ds::HAPTICS_FRAMES_PER_REPORT : ds::HAPTICS_FRAMES_PER_REPORT;
        for (int i = 0; i < hframes; i++) {
            peak_l = MAX(peak_l, ABS(int(hap[2 * i])));
            peak_r = MAX(peak_r, ABS(int(hap[2 * i + 1])));
        }
        haptics_level_l = float(peak_l) / 127.0f;
        haptics_level_r = float(peak_r) / 127.0f;
        int written;
        if (audio_mode) {
            if (!route_sent) {
                // Enable the speaker path (same common block as USB, inside a 0x31 report).
                uint8_t route[ds::OUTPUT_REPORT_BT_SIZE];
                ds::build_audio_route_bt(true, ds::SPEAKER_VOLUME_DEFAULT, ds::SPEAKER_PREAMP_GAIN_DEFAULT, next_bt_seq(), route);
                hid_write_locked(route, sizeof(route));
                route_sent = true;
            }
            uint8_t opus[2][ds::SPEAKER_OPUS_BYTES];
            if (!encode_opus(spk, opus[0]) || !encode_opus(spk + 2 * ds::SPEAKER_OPUS_FRAME, opus[1])) {
                haptics_write_errors++;
                std::lock_guard<std::mutex> lock(haptics_mutex);
                haptics_running = false;
                break;
            }
            float peak_s = 0.0f;
            for (int i = 0; i < 4 * ds::SPEAKER_OPUS_FRAME; i++) peak_s = MAX(peak_s, ABS(spk[i]));
            speaker_level = peak_s;
            uint8_t report[ds::AUDIO_REPORT_BT_SIZE];
            audio_counter = uint8_t(audio_counter + 2);
            ds::build_audio_report_bt(hap, opus[0], opus[1], state.headphones, next_bt_seq(), audio_counter, report);
            written = haptics_write(report, sizeof(report));
            if (written >= 0) speaker_reports++;
            since_audio_ms = spk_silent ? since_audio_ms + 20.0 : 0.0;
            silent_ms = (silent && spk_silent) ? silent_ms + 20.0 : 0.0;
            next += audio_period;
        } else {
            speaker_level = 0.0f;
            uint8_t report[ds::HAPTICS_REPORT_BT_SIZE];
            ds::build_haptics_report_bt(hap, haptic_counter, next_bt_seq(), report);
            haptic_counter++;
            written = haptics_write(report, sizeof(report));
            silent_ms = silent ? silent_ms + 10.67 : 0.0;
            next += haptic_period;
        }
        haptics_last_write = written;
        if (written < 0) {
            haptics_write_errors++;
            std::lock_guard<std::mutex> lock(haptics_mutex);
            haptics_running = false;
            break;
        }
        haptics_reports++;
        std::this_thread::sleep_until(next);
    }
}

uint8_t DualSense::next_bt_seq() {
    return uint8_t(bt_seq.fetch_add(1) & 0x0F);
}

bool DualSense::encode_opus(const float *frames480, uint8_t out[ds::SPEAKER_OPUS_BYTES]) {
    return encode_opus_with(opus_encoder, frames480, out);
}

bool DualSense::encode_opus_with(void *&opus_encoder, const float *frames480, uint8_t out[ds::SPEAKER_OPUS_BYTES]) {
    if (!opus_encoder) {
        int err = 0;
        OpusEncoder *enc = opus_encoder_create(ds::SPEAKER_SAMPLE_RATE, 2, OPUS_APPLICATION_AUDIO, &err);
        if (err != OPUS_OK || !enc) return false;
        opus_encoder_ctl(enc, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
        opus_encoder_ctl(enc, OPUS_SET_BITRATE(int(ds::SPEAKER_OPUS_BYTES) * 8 * 100));
        opus_encoder_ctl(enc, OPUS_SET_VBR(0));
        opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(0));
        opus_encoder = enc;
    }
    std::memset(out, 0, ds::SPEAKER_OPUS_BYTES);
    const int n = opus_encode_float(static_cast<OpusEncoder *>(opus_encoder), frames480, ds::SPEAKER_OPUS_FRAME, out,
            int(ds::SPEAKER_OPUS_BYTES));
    return n > 0;
}

PackedByteArray DualSense::build_audio_report(const PackedVector2Array &p_haptics, const PackedVector2Array &p_speaker, int p_seq) {
    int8_t hap[4 * ds::HAPTICS_FRAMES_PER_REPORT] = {0};
    for (int i = 0; i < 2 * ds::HAPTICS_FRAMES_PER_REPORT && i < p_haptics.size(); i++) {
        hap[2 * i] = int8_t(Math::round(CLAMP(p_haptics[i].x, -1.0f, 1.0f) * 127.0f));
        hap[2 * i + 1] = int8_t(Math::round(CLAMP(p_haptics[i].y, -1.0f, 1.0f) * 127.0f));
    }
    std::vector<float> spk(size_t(4 * ds::SPEAKER_OPUS_FRAME), 0.0f);
    for (int i = 0; i < 2 * ds::SPEAKER_OPUS_FRAME && i < p_speaker.size(); i++) {
        spk[size_t(2 * i)] = p_speaker[i].x;
        spk[size_t(2 * i + 1)] = p_speaker[i].y;
    }
    void *enc = nullptr;
    uint8_t opus[2][ds::SPEAKER_OPUS_BYTES];
    encode_opus_with(enc, spk.data(), opus[0]);
    encode_opus_with(enc, spk.data() + 2 * ds::SPEAKER_OPUS_FRAME, opus[1]);
    if (enc) opus_encoder_destroy(static_cast<OpusEncoder *>(enc));
    uint8_t report[ds::AUDIO_REPORT_BT_SIZE];
    ds::build_audio_report_bt(hap, opus[0], opus[1], false, uint8_t(p_seq), 2, report);
    return to_bytes(report, sizeof(report));
}

PackedByteArray DualSense::build_audio_route_report_bt(bool p_speaker, int p_seq) {
    uint8_t report[ds::OUTPUT_REPORT_BT_SIZE];
    ds::build_audio_route_bt(p_speaker, ds::SPEAKER_VOLUME_DEFAULT, ds::SPEAKER_PREAMP_GAIN_DEFAULT, uint8_t(p_seq), report);
    return to_bytes(report, sizeof(report));
}

Dictionary DualSense::haptics_debug() const {
    Dictionary d;
    d["running"] = haptics_running.load();
    d["reports"] = haptics_reports.load();
    d["last_write"] = haptics_last_write.load();
    d["write_errors"] = haptics_write_errors.load();
    d["rumble_enabled"] = output.rumble_enabled;
    d["transport"] = bluetooth ? "bluetooth 0x32/0x39" : "usb audio (4 channels)";
    d["speaker_reports"] = speaker_reports.load();
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(haptics_mutex));
        d["usb_device"] = String::utf8(usb_info.device.c_str());
        d["usb_format"] = usb_info.channels > 0 ? String::num_int64(usb_info.channels) + " channels, " +
                        String::num_int64(usb_info.sample_rate) + " Hz, " + (usb_info.float_samples ? "float" : "16-bit") : String();
        d["usb_error"] = String::utf8(usb_info.error.c_str());
    }
    return d;
}

int DualSense::haptics_write(const uint8_t *data, size_t len) {
    if (haptics_transport.load() == 0) return hid_write_locked(data, len);
    std::lock_guard<std::mutex> lock(hid_mutex);
    return device ? hid_send_output_report(device, data, len) : -1;
}

int DualSense::send_raw(const PackedByteArray &p_data, int p_transport, bool p_fix_crc) {
    if (!device || p_data.is_empty()) return -1;
    std::vector<uint8_t> buf(p_data.ptr(), p_data.ptr() + p_data.size());
    if (p_fix_crc && buf.size() > 4) {
        const uint32_t crc = ds::crc32_with_seed(ds::CRC_SEED_OUTPUT, buf.data(), buf.size() - 4);
        for (int i = 0; i < 4; i++) buf[buf.size() - 4 + i] = uint8_t(crc >> (8 * i));
    }
    std::lock_guard<std::mutex> lock(hid_mutex);
    switch (p_transport) {
        case 1: return hid_send_output_report(device, buf.data(), buf.size());
        case 2: return hid_send_feature_report(device, buf.data(), buf.size());
        default: return hid_write(device, buf.data(), buf.size());
    }
}

void DualSense::imu_capture_start() {
    imu_accel.clear();
    imu_gyro.clear();
    imu_time.clear();
    imu_capturing = true;
}

Dictionary DualSense::imu_capture_stop() {
    imu_capturing = false;
    Dictionary d;
    d["accel"] = imu_accel;
    d["gyro"] = imu_gyro;
    d["timestamp"] = imu_time;
    return d;
}

void DualSense::set_rumble_enabled(bool p_enabled) {
    output.rumble_enabled = p_enabled;
    output_dirty = true;
}

PackedByteArray DualSense::build_haptics_report(const PackedVector2Array &p_frames, int p_counter) {
    int8_t samples[2 * ds::HAPTICS_FRAMES_PER_REPORT] = {0};
    for (int i = 0; i < ds::HAPTICS_FRAMES_PER_REPORT && i < p_frames.size(); i++) {
        samples[2 * i] = int8_t(Math::round(CLAMP(p_frames[i].x, -1.0f, 1.0f) * 127.0f));
        samples[2 * i + 1] = int8_t(Math::round(CLAMP(p_frames[i].y, -1.0f, 1.0f) * 127.0f));
    }
    uint8_t report[ds::HAPTICS_REPORT_BT_SIZE];
    ds::build_haptics_report_bt(samples, uint8_t(p_counter), uint8_t(p_counter), report);
    return to_bytes(report, sizeof(report));
}

PackedByteArray DualSense::trigger_off() {
    uint8_t e[ds::TRIGGER_EFFECT_SIZE];
    ds::trigger_off(e);
    return to_bytes(e, sizeof(e));
}

PackedByteArray DualSense::trigger_feedback(int p_position, int p_strength) {
    uint8_t e[ds::TRIGGER_EFFECT_SIZE];
    ds::trigger_feedback(p_position, p_strength, e);
    return to_bytes(e, sizeof(e));
}

PackedByteArray DualSense::trigger_weapon(int p_start, int p_end, int p_strength) {
    uint8_t e[ds::TRIGGER_EFFECT_SIZE];
    ds::trigger_weapon(p_start, p_end, p_strength, e);
    return to_bytes(e, sizeof(e));
}

PackedByteArray DualSense::trigger_vibration(int p_position, int p_amplitude, int p_frequency) {
    uint8_t e[ds::TRIGGER_EFFECT_SIZE];
    ds::trigger_vibration(p_position, p_amplitude, p_frequency, e);
    return to_bytes(e, sizeof(e));
}

PackedByteArray DualSense::trigger_machine(int p_start, int p_end, int p_amplitude_a, int p_amplitude_b,
        int p_frequency, int p_period) {
    uint8_t e[ds::TRIGGER_EFFECT_SIZE];
    ds::trigger_machine(p_start, p_end, p_amplitude_a, p_amplitude_b, p_frequency, p_period, e);
    return to_bytes(e, sizeof(e));
}

PackedByteArray DualSense::build_audio_route_report(bool p_speaker) {
    uint8_t report[ds::OUTPUT_REPORT_USB_SIZE];
    ds::build_audio_route_usb(p_speaker, ds::SPEAKER_VOLUME_DEFAULT, ds::SPEAKER_PREAMP_GAIN_DEFAULT, report);
    PackedByteArray out;
    out.resize(sizeof(report));
    std::memcpy(out.ptrw(), report, sizeof(report));
    return out;
}

int DualSense::poll() {
    if (!device) return 0;
    if (output_dirty) send_output();
    uint8_t buf[128];
    int count = 0;
    for (;;) {
        int n;
        {
            std::lock_guard<std::mutex> lock(hid_mutex);
            n = hid_read(device, buf, sizeof(buf));
        }
        if (n < 0) {
            last_error = "hid_read failed (controller disconnected?)";
            close();
            break;
        }
        if (n == 0) break;
        if (buf[0] == ds::INPUT_REPORT_BT && size_t(n) >= ds::INPUT_REPORT_BT_SIZE &&
                !ds::check_bt_crc(ds::CRC_SEED_INPUT, buf, ds::INPUT_REPORT_BT_SIZE)) {
            crc_errors++;
            continue;
        }
        if (ds::parse_input_report(buf, size_t(n), state)) {
            if (imu_capturing) {
                imu_accel.push_back(Vector3(state.accel[0], state.accel[1], state.accel[2]));
                imu_gyro.push_back(Vector3(state.gyro[0], state.gyro[1], state.gyro[2]));
                imu_time.push_back(int64_t(state.sensor_timestamp));
            }
            has_state = true;
            report_count++;
            count++;
        }
    }
    return count;
}

Dictionary DualSense::state_to_dictionary(const ds::InputState &s) {
    Dictionary d;
    Array touch;
    for (const ds::TouchPoint &t : s.touch) {
        Dictionary p;
        p["active"] = t.active;
        p["id"] = t.id;
        p["x"] = t.x;
        p["y"] = t.y;
        touch.push_back(p);
    }
    d["touch"] = touch;
    d["battery_percent"] = s.battery_percent;
    d["battery_status"] = String(ds::battery_status_name(s.battery_status));
    d["headphones"] = s.headphones;
    d["microphone"] = s.microphone;
    d["mic_muted"] = s.mic_muted;
    d["l2"] = s.l2;
    d["r2"] = s.r2;
    d["seq"] = s.seq;
    d["mute_button"] = (s.buttons[2] & 0x04) != 0;
    d["touchpad_button"] = (s.buttons[2] & 0x02) != 0;
    d["ps_button"] = (s.buttons[2] & 0x01) != 0;
    d["gyro"] = Vector3i(s.gyro[0], s.gyro[1], s.gyro[2]);
    d["accel"] = Vector3i(s.accel[0], s.accel[1], s.accel[2]);
    return d;
}

Dictionary DualSense::firmware_to_dictionary(const ds::FirmwareInfo &f) {
    Dictionary d;
    d["build_date"] = String(f.build_date.c_str());
    d["build_time"] = String(f.build_time.c_str());
    d["hw_version"] = int64_t(f.hw_version);
    d["fw_version"] = int64_t(f.fw_version);
    d["update_version"] = int64_t(f.update_version);
    d["update_version_text"] = String::num_int64(f.update_version >> 8, 16).pad_zeros(2) + "." +
            String::num_int64(f.update_version & 0xFF, 16).pad_zeros(2);
    d["vibration_v2"] = f.update_version >= ds::VIBRATION_V2_MIN_UPDATE_VERSION;
    return d;
}

Dictionary DualSense::get_state() const {
    return has_state ? state_to_dictionary(state) : Dictionary();
}

Dictionary DualSense::get_firmware() const {
    return has_firmware ? firmware_to_dictionary(firmware) : Dictionary();
}

Dictionary DualSense::get_info() const {
    Dictionary d;
    d["open"] = device != nullptr;
    d["path"] = path;
    d["connection"] = bluetooth ? "bluetooth" : "usb";
    d["edge"] = edge;
    d["reports"] = report_count;
    d["crc_errors"] = crc_errors;
    d["audio_route"] = audio_route;
    d["outputs_sent"] = outputs_sent;
    d["vibration_v2"] = output.vibration_v2;
    d["haptics_reports"] = haptics_reports.load();
    return d;
}

Dictionary DualSense::parse_input_report(const PackedByteArray &p_report) {
    ds::InputState s;
    if (!ds::parse_input_report(p_report.ptr(), size_t(p_report.size()), s)) return Dictionary();
    return state_to_dictionary(s);
}

Dictionary DualSense::parse_firmware_info(const PackedByteArray &p_report) {
    ds::FirmwareInfo f;
    if (!ds::parse_firmware_info(p_report.ptr(), size_t(p_report.size()), f)) return Dictionary();
    return firmware_to_dictionary(f);
}

int64_t DualSense::crc32_with_seed(int p_seed, const PackedByteArray &p_data) {
    return int64_t(ds::crc32_with_seed(uint8_t(p_seed), p_data.ptr(), size_t(p_data.size())));
}
