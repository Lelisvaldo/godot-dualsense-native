# Third-party components and references

## Linked into the extension binaries

### libopus 1.6.1 — BSD-3-Clause

Statically linked to encode Bluetooth speaker audio. https://opus-codec.org

Copyright 2001–2023 Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry,
CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo, Mozilla, Amazon.

Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met: (1) redistributions of source code must retain the
above copyright notice, this list of conditions and the following disclaimer; (2) redistributions
in binary form must reproduce the above copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided with the distribution; (3) neither
the name of Internet Society, IETF or IETF Trust, nor the names of specific contributors, may be
used to endorse or promote products derived from this software without specific prior written
permission. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.

### hidapi 0.15.0 — BSD-3-Clause (one of hidapi's license options)

Compiled from source into the extension. https://github.com/libusb/hidapi

### godot-cpp 10.0.0 — MIT

Godot C++ bindings. https://github.com/godotengine/godot-cpp

## Protocol references (no code copied)

* SAxense (Sdore, MPL-2.0) — Bluetooth HD haptics report: https://apps.sdore.me/SAxense
* dualsense-bt-haptics (awalol, MIT) — 0x32 report size and sequence on Windows.
* DS5Dongle (awalol, MIT) — Bluetooth speaker report 0x39 and Opus settings.
* Linux `hid-playstation` (GPL-2.0, read as documentation only) — report layouts and audio routing.
* SDL `SDL_hidapi_ps5.c` (zlib) — effects, rumble emulation vs audio haptics.
* Nielk1 "TriggerEffectGenerator" (MIT) — adaptive trigger effect encoding.
* chiaki-ng — haptics PCM format hints.
