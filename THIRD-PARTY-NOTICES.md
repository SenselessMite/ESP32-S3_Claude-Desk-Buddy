# Third-party components

The code in this repository is released under the MIT license (see `LICENSE`). The project uses or
is inspired by the following, each under its own license.

## TamaPoke (MIT)

Some parts of the firmware (`claude_buddy/hw.cpp`, touch initialization in `claude_buddy.ino`)
follow the sequences verified on the board in the **TamaPoke** project: ES8311 codec configuration
and tone synthesis, CST9217 touch interrupt handling, direct-register reads of the QMI8658 sensor,
RTC and PMU reads.

```
MIT License

Copyright (c) 2026 Quique Tortosa

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Libraries (not included in the repository, install separately)

| Library | Use | License |
|---|---|---|
| GFX Library for Arduino (moononournation) | CO5300 display driver | BSD-style, see the library |
| SensorLib (Lewis He) | CST9217 touch, PCF85063 RTC | MIT |
| XPowersLib (Lewis He) | AXP2101 PMU (battery, POWER key) | MIT |
| Core esp32 (Espressif) | framework, I2S, Preferences, BLE (NimBLE) | LGPL-2.1 / Apache-2.0 |
| bleak (Henrik Blidh) | BLE client on the PC (`host/ble_bridge.py`), installed with pip | MIT |

The ES8311 codec initialization sequence derives from the official Espressif driver (esp-bsp,
Apache-2.0) and from the TamaPoke project.

## Trademarks

This is an unofficial, independent project, **not affiliated with or endorsed by Anthropic**.
"Claude", "Claude Code" and the "Clawd" mascot belong to Anthropic. The Clawd drawing in the
firmware is a simple block reproduction intended for personal use.
