// USB audio (Windows, WASAPI): over USB the DualSense is a 4-channel audio device. Channels 1-2 feed
// the speaker / headphone jack and channels 3-4 drive the left and right haptic actuators.
#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace ds {

struct UsbHapticsInfo {
    std::string device;  // audio endpoint name
    int channels = 0;
    int sample_rate = 0;
    bool float_samples = false;
    std::string error;
};

// Runs until `running` becomes false or `pull` asks to stop. `pull(out, frames, rate)` fills `frames`
// 4-channel frames in [-1, 1] at `rate` (0-1: speaker/headphones; 2-3: left/right actuators) and
// returns false to stop. Returns false if the device could not be opened (see info.error).
// Windows only (shared-mode WASAPI); on other platforms it returns false.
bool usb_haptics_run(std::atomic<bool> &running,
        const std::function<bool(float *quad, int frames, int rate)> &pull, UsbHapticsInfo &info);

}  // namespace ds
