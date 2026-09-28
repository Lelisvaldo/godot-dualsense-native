// DualSense class exposed to GDScript. Talks to the controller over HID (hidapi): discovery,
// connection type, firmware, battery, touchpad, audio jack, adaptive triggers, LEDs, lightbar,
// rumble, HD haptics and the built-in speaker, over USB and Bluetooth.
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ds_protocol.h"
#include "usb_haptics.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

struct hid_device_;

namespace godot {

class DualSense : public RefCounted {
    GDCLASS(DualSense, RefCounted)

    hid_device_ *device = nullptr;
    String path;
    bool bluetooth = false;
    bool edge = false;
    ds::InputState state;
    bool has_state = false;
    ds::FirmwareInfo firmware;
    bool has_firmware = false;
    int64_t report_count = 0;
    int64_t crc_errors = 0;
    String last_error;
    String audio_route;
    ds::OutputState output;
    bool output_dirty = false;
    bool lightbar_released = false;
    // Single 4-bit counter shared by every Bluetooth output report (0x31, 0x32, 0x39).
    std::atomic<uint32_t> bt_seq{0};
    uint8_t next_bt_seq();
    int64_t outputs_sent = 0;

    // Every HID read/write goes through this mutex: the audio thread writes concurrently.
    std::mutex hid_mutex;
    int hid_write_locked(const uint8_t *data, size_t len);

    // HD haptics: stereo mixing ring at 3 kHz. haptics_play() adds into the ring from the read position
    // (overlapping effects are mixed); the audio thread consumes it at the transport's pace.
    static constexpr int HAPTICS_RING_FRAMES = ds::HAPTICS_SAMPLE_RATE * 4;
    std::mutex haptics_mutex;
    std::vector<float> haptics_ring;  // interleaved L/R
    int64_t haptics_read = 0;         // frames consumed since start
    std::thread haptics_thread;
    std::atomic<bool> haptics_running{false};
    std::atomic<int64_t> haptics_reports{0};
    std::atomic<int64_t> haptics_write_errors{0};
    std::atomic<int> haptics_last_write{0};
    std::atomic<float> haptics_level_l{0.0f}, haptics_level_r{0.0f};
    double haptics_gain = 1.0;
    std::atomic<int> haptics_transport{0};  // 0 = hid_write (interrupt channel), 1 = SetOutputReport
    int haptics_write(const uint8_t *data, size_t len);
    // IMU capture for the self-test (the accelerometer "feels" the controller's own vibration).
    bool imu_capturing = false;
    PackedVector3Array imu_accel, imu_gyro;
    PackedInt64Array imu_time;
    ds::UsbHapticsInfo usb_info;  // guarded by haptics_mutex
    // Speaker: stereo mixing ring at 48 kHz, guarded by haptics_mutex.
    static constexpr int SPEAKER_RING_FRAMES = ds::SPEAKER_SAMPLE_RATE * 2;
    std::vector<float> speaker_ring;
    int64_t speaker_read = 0;
    bool speaker_pending = false;
    double speaker_gain = 1.0;
    std::atomic<float> speaker_level{0.0f};
    std::atomic<int64_t> speaker_reports{0};
    void *opus_encoder = nullptr;  // OpusEncoder*
    bool encode_opus(const float *frames480, uint8_t out[ds::SPEAKER_OPUS_BYTES]);
    static bool encode_opus_with(void *&encoder, const float *frames480, uint8_t out[ds::SPEAKER_OPUS_BYTES]);
    bool claim_audio_thread_locked();
    void start_audio_thread();
    void haptics_loop();
    void haptics_loop_bt();
    void haptics_loop_usb();
    void haptics_stop();

    static PackedByteArray to_bytes(const uint8_t *p, size_t n);
    static bool from_effect(const PackedByteArray &p_effect, uint8_t out[ds::TRIGGER_EFFECT_SIZE]);

    bool read_feature(uint8_t id, size_t size, uint8_t *buf);

protected:
    static void _bind_methods();

public:
    ~DualSense() override;

    static Array list_devices();
    bool open(const String &p_path);
    void close();
    bool is_open() const { return device != nullptr; }
    bool is_bluetooth() const { return bluetooth; }
    int poll();
    Dictionary get_state() const;
    Dictionary get_firmware() const;
    Dictionary get_info() const;
    String get_last_error() const { return last_error; }
    bool set_audio_route(bool p_speaker);

    // Outputs. Setters only update the state; poll()/send_output() send the report.
    void set_rumble(double p_strong, double p_weak);
    bool set_trigger_effect(bool p_right, const PackedByteArray &p_effect);
    void set_player_leds(int p_mask, int p_brightness);
    void set_mute_led(int p_mode);
    void set_lightbar(const Color &p_color);
    bool send_output();
    void reset_outputs();
    PackedByteArray build_output_report(bool p_bluetooth, int p_seq) const;

    // HD haptics: stereo frames in [-1, 1] at 3000 Hz (x = left actuator, y = right).
    // Bluetooth: report 0x32 (any platform). USB: channels 3-4 of the USB audio device (Windows for now).
    bool haptics_supported() const {
#ifdef _WIN32
        return device != nullptr;
#else
        return device != nullptr && bluetooth;
#endif
    }
    bool haptics_play(const PackedVector2Array &p_frames);
    void haptics_clear();
    void haptics_set_gain(double p_gain) { haptics_gain = p_gain; }
    int64_t haptics_reports_sent() const { return haptics_reports.load(); }
    // Peak |sample| of the last buffer sent, per actuator (0-1): for on-screen meters.
    Vector2 haptics_levels() const { return Vector2(haptics_level_l.load(), haptics_level_r.load()); }
    // Diagnostics: last haptics hid_write result (bytes written or -1), errors, USB device info.
    Dictionary haptics_debug() const;
    void set_rumble_enabled(bool p_enabled);
    void haptics_set_transport(int p_transport) { haptics_transport = p_transport; }
    void imu_capture_start();
    // Experiments (self-test): sends raw bytes. transport 0 = hid_write, 1 = SetOutputReport,
    // 2 = feature report. With fix_crc = true the CRC32 (seed 0xA2) in the last 4 bytes is recomputed.
    int send_raw(const PackedByteArray &p_data, int p_transport, bool p_fix_crc);
    Dictionary imu_capture_stop();
    static PackedByteArray build_haptics_report(const PackedVector2Array &p_frames, int p_counter);

    // Speaker: stereo frames at 48 kHz in [-1, 1]. The speaker is mono (it plays the right channel).
    // USB: channels 1-2 of the USB audio device (Windows); Bluetooth: Opus inside report 0x39.
    bool speaker_supported() const { return haptics_supported(); }
    bool speaker_play(const PackedVector2Array &p_frames);
    void speaker_set_gain(double p_gain) { speaker_gain = p_gain; }
    double get_speaker_level() const { return speaker_level.load(); }
    static PackedByteArray build_audio_report(const PackedVector2Array &p_haptics, const PackedVector2Array &p_speaker, int p_seq);
    static PackedByteArray build_audio_route_report_bt(bool p_speaker, int p_seq);

    static PackedByteArray trigger_off();
    static PackedByteArray trigger_feedback(int p_position, int p_strength);
    static PackedByteArray trigger_weapon(int p_start, int p_end, int p_strength);
    static PackedByteArray trigger_vibration(int p_position, int p_amplitude, int p_frequency);
    static PackedByteArray trigger_machine(int p_start, int p_end, int p_amplitude_a, int p_amplitude_b,
            int p_frequency, int p_period);
    String get_audio_route() const { return audio_route; }

    // Pure functions exposed for tests (no hardware needed).
    static Dictionary parse_input_report(const PackedByteArray &p_report);
    static Dictionary parse_firmware_info(const PackedByteArray &p_report);
    static int64_t crc32_with_seed(int p_seed, const PackedByteArray &p_data);
    static PackedByteArray build_audio_route_report(bool p_speaker);
    static Dictionary state_to_dictionary(const ds::InputState &s);
    static Dictionary firmware_to_dictionary(const ds::FirmwareInfo &f);
};

}  // namespace godot
