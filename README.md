# ESP32-S3 Claude Desk Buddy

A desk companion for **Claude Code**: an ESP32-S3 board with a round AMOLED display that shows
**Clawd**, the Claude Code mascot, and changes its behavior according to what Claude is doing.
It is more than a notification: it is a small pet that works, thinks, gets bored, gets tired,
levels up, and tells you at a glance what is happening in your sessions.

```
Claude Code ──hook / statusLine──▶ host/buddy.py ──USB serial ─┐
                                                  └─ BLE (daemon) ──┴▶ ESP32-S3 ──▶ screen + audio
```

Unofficial project, not affiliated with Anthropic (see [Trademarks and license](#trademarks-and-license)).
The on-screen texts are available in **Italian, English, French, German and Spanish** (pick it in the
settings menu); the code comments are in Italian and the documentation is in English.

## What it does

### Clawd reacts to Claude Code

| Situation | What you see |
|---|---|
| Idle | walks around and waves; every so often it picks a **random activity** (plays with a ball, reads a book, has a coffee, dances to music); after 1 minute it gets bored and yawns; at night it says "Buonanotte" and falls asleep early |
| Thinking | hand on chin and a thought bubble |
| Working | picks one of **six ways of working** at random and changes every 12-22 s (see below); a **scrolling line** below shows the detail (command, file, pattern...) |
| Oops! | a tool failed: shakes its head with a "?" |
| Needs you! | jumps, pulsing yellow background and a message ("Permesso: Bash") |
| Done! | jumps for joy among confetti |
| Commit / Push / Tests pass | party with a green badge / a rocket taking off / thumbs up |
| Git errors | **push rejected** (the rocket rises, runs out of fuel and falls back smoking), **conflict** (two branches collide and spark), **commit failed** (red cross badge, Clawd shakes its head) |
| Compaction | squashes down and recovers while Claude compacts the context |
| Memory full | puffs up when the context goes over 90% |
| Cold cache | shivers when the prompt cache has expired |
| Tired | heavy eyes and sweat when usage is close to the limit; **red alarm** at 90% and "Puoi ripartire!" when it resets |
| Sub-agents | one mini Clawd per active agent (up to 4), placed around the screen in the free corners and sides according to the scene, doing a small version of what Clawd is doing (typing, dancing with headphones, hunting the bug, hammering, turning a gear); while Clawd is thinking they wait with blinking dots |
| Task list | "Task 3/7" with a bar, from `TodoWrite` |
| Low battery | slumps, sweats and shows a blinking battery with "Batteria scarica!" below 15% and again below 5% |
| Charge complete | celebrates with a full battery and "Carica completa!" |

**Ways of working** (random, never the same twice in a row; the classic one is the most frequent): *typing*
on the keyboard with sparks, *turbo* (frowning, fast hands, sweat), *headphones* (sways to the music with notes
floating up), *bug hunt* (chases a bug along the keyboard with a magnifying glass), *hammer* (strikes an anvil with
sparks) and *gears* (watches three gears turn above its head). The variant does not change between tool calls,
only when its time is up or after a long pause. Each one has its own entry in the BOOT animation demo.

Clawd **levels up** with use (cap at level 3, glasses at 6, crown at 10, colors to unlock; more accessories later: top hat 4, chef hat 5, propeller beanie 7, wizard hat 8, heart glasses 4, 3D glasses 5, sunglasses 8, monocle 9) and also
reacts to you: a tap pets it, it laughs after 3 taps, gets annoyed after 6, gets dizzy if you shake
it, and celebrates at midnight. Every main event has its own little tune, played by a small
synthesizer (bell-like timbre, chords, glides and soft envelopes instead of plain beeps): power on,
attention, done, error, commit, push, tests passed, compaction, usage alarm, limit reset, level up...

**Usage ring:** on the Clawd screen two concentric rings run around the edge of the display: the thick
**outer** one is the 5-hour limit, the thin **inner** one the weekly limit (two different limits, not a
total). Both start at 9 o'clock and fill clockwise; a small legend at the start says which is which. The
color shifts along the ring from green through amber (65%) to red (100%), so the color at the tip tells
you how close you are to the limit. The bars on the Usage page use the same gradient. Nothing is drawn
without usage data (the limits only come with a Claude.ai subscription).

### Pages (swipe horizontally)

Clawd · **Sessions** · **Usage** · **Today** · **Timer** · **Message** · **Style** · **Info**

- **Sessions**: one row per Claude Code session, with the folder (color-coded icon per project),
  state, tool in use, context %, model/effort and the prompt-cache countdown.
- **Usage**: 5-hour and weekly limits, context, today's cost and lines, battery.
- **Today**: prompts, tools, working hours, cost, lines, level.
- **Timer**: 25/5 pomodoro (tap = start/pause, long press = reset).
- **Message**: Claude's last reply, with vertical scrolling.
- **Style**: four buttons (Color, Hat, Glasses incl. pixel "Thug Life" shades, Sound; 8 hats and 8 glasses in total, locked ones are skipped), each changes its value on every tap. Also
  opens by long-pressing Clawd.
- **Info**: PC and chip status (touch, audio, motion, RTC, PMU, BLE, USB), memory, version.

A **battery indicator** (icon and percentage, with a bolt while charging) is always visible on the
Clawd screen.

### Buttons, settings and demo

| Button | Gesture | Effect |
|---|---|---|
| **BOOT** | short press | plays the **next Clawd animation** (38 in total) as a demo/test |
| **BOOT** | long press (~1 s) | opens/closes the **settings menu** |
| **POWER** | short press | **standby** (screen off) / wake |
| **POWER** | long press (1.5 s) | **power off** (press POWER to turn on again) |
| **POWER** | 6 s | hardware forced shutdown by the PMU (last resort) |

**Settings menu** (long-press BOOT): *Volume*, *Auto vol.*, *Brightness*, *Rotation*, *Standby* and *Language* (IT/EN/FR/DE/ES)
have **-** / **+** buttons (on *Auto vol.* and *Rotation* either one toggles ON/OFF), followed by the *Pair PC*, *Forget*, *Power off* and *Exit* buttons. Everything works by
touch; with the physical buttons, a short BOOT press moves to the next control and a short POWER
press activates it (on values it steps them forward). It closes by itself after 30 s and the values
are saved to flash.

**Auto volume** (off by default): the board's microphones (ES7210 ADC, initialized by the firmware) listen to the room every couple of seconds
while it is not playing a sound, and scales the *Volume* setting between 45% (quiet room, about 40 dB)
and 120% (noisy room, about 66 dB), never above 100%. *Volume* therefore becomes the level you want in
a normal room. The Info page shows the measured level and the volume actually used. The thresholds are
estimates: send `micdebug 1` on the serial port to print the raw readings (`MIC samples=... rms=...`)
and adjust the 40/66 dB constants in `micMeasure()` (`hw.cpp`) if needed. If the microphone returns
only silence three times in a row, auto volume gives up (volume stays as set) and retries after 5 minutes.

**Auto rotation** (off by default): with *Rotation* ON the accelerometer decides which edge of the
display is up, and both the drawing and the touch coordinates rotate in steps of 90 degrees (the board
keeps its current orientation while it lies flat, and never turns in the middle of a touch). The axes of
the accelerometer relative to the display are not documented, so the mapping is in `IMU_EDGE_AXIS` /
`IMU_EDGE_SIGN` in `board.h`: send `orientdebug 1` on the serial port, tilt the board and compare
`gx`/`gy` with the edge that is up. If the screen turns the wrong way, swap the signs of the right and
left edges (or of top and bottom).

**Animation demo:** every short BOOT press (outside the menu) shows the next animation with its
number and name (35 in total: states, idle activities, bored/tired/memory full/cold cache, battery cases,
commit/push/tests/compaction, level up, usage alarm, petting, laughing, annoyed, dizzy, midnight...). Timed effects
repeat, and the demo ends after one minute without presses. While it runs, the PC does not change
the scene.

**Standby:** the screen turns off and the CPU drops to 80 MHz, but the BLE radio stays on, so the
display wakes up by itself when a session needs you (*Needs you!*) or finishes (*Done!*), and goes back
to standby 15 s later if you do not touch it. BOOT wakes it too. **On battery**
(no USB) there is also an **automatic standby** after 10 minutes at rest with no touches (change it
in the menu, `0` = never). The pomodoro timer is paused in standby. From the PC:
`python host/buddy.py standby | wake | off | autostandby 15`.

Notes: with the USB cable connected the PMU is powered from outside, so "power off" falls back to
standby. Standby uses much less power than normal operation, but it is not a minimum-power state
(use power off for that).

## Requirements

**Hardware:** [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
family: the **1.75**, **1.75-B** and **1.75C** (CO5300 466x466 display, CST9217 touch, ES8311 codec,
QMI8658 IMU, AXP2101 PMU, RTC). USB-C cable to the PC, or BLE only (see below).

**Board variant:** `BOARD_175C` in `claude_buddy/board.h` is `1` for the **1.75C** (default) and `0`
for the 1.75 / 1.75-B. It changes the display and touch reset pins and the audio master clock.
The 1.75C has 32 MB of flash; leaving *Flash Size 16MB* is fine. Check the *Info* page after the
first boot, and if the audio does not play, check the I2S/PA pins in `hw.cpp`.

**Software:**
- Arduino IDE or `arduino-cli`, with the **esp32 by Espressif core 3.3.x** (tested with 3.3.12)
- Libraries: *GFX Library for Arduino* (with the CO5300 driver; Waveshare ships it in
  `examples/arduino/libraries` of its repository), *SensorLib*, *XPowersLib*
- **Python 3** in `PATH` (no external dependencies for USB; BLE needs `pip install bleak`)
- Claude Code with **hooks** enabled (tested on Windows; see the notes below for Linux/macOS)

## Installation

### 1. Firmware

Open `claude_buddy/claude_buddy.ino` (together with the other files in the folder) and set under *Tools*:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | **Enabled** |
| PSRAM | **OPI PSRAM** (needed for the ~430 KB canvas) |
| Flash Size | 16MB |
| Partition Scheme | `app3M_fat9M_16MB` |

Or from the command line:

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" claude_buddy
arduino-cli upload  -p COM7 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" claude_buddy
```

If the board does not enter download mode: hold BOOT, press RESET, release BOOT.
If the swipe goes the wrong way, set `SWIPE_INVERT` to 1 in `claude_buddy.ino`.

### 2. Serial port

```
copy host\port.example.txt host\port.txt      (Windows)
cp host/port.example.txt host/port.txt        (Linux/macOS)
```

By default the port is **detected automatically** (Espressif USB device, VID `303A`; on
Linux/macOS the first `/dev/ttyACM*` / `/dev/cu.usbmodem*`), so `host/port.txt` can be left
empty or contain `auto`. To force a port, write it there (e.g. `COM7`, or `/dev/ttyACM0`); if
that port no longer exists (the board re-enumerated), detection takes over. **Close the IDE's
Serial Monitor**: the port can only be opened by one application at a time.

### 2b. BLE connection (optional, no cable)

The firmware always exposes the *Nordic UART* BLE service with the name **Claude Buddy**, in
parallel with USB. To use it from the PC:

```
pip install bleak
copy host\transport.example.txt host\transport.txt      (it contains: ble)
```

`host/transport.txt` accepts `serial` (default if the file is missing), `ble` or `auto` (BLE if the
board is connected, otherwise USB). With `ble`/`auto` the first hook starts the **daemon**
`host/ble_bridge.py` by itself; it keeps the connection open and receives updates from the hooks
over a local socket (`127.0.0.1` only), so hooks stay fast. The first connection can take a few
seconds (scan). The daemon exits after 2 hours without hooks and restarts on its own. If the board is visible but the connection keeps failing (typical after flashing), the daemon relaunches itself (at most once every 10 minutes); if the board is off it just scans less and less often, without restarting.

```
python host/buddy.py ble status    # daemon running? board connected?
python host/buddy.py ble start|stop
```

**Pairing (once).** BLE writes are protected: the board only accepts commands from a PC paired
with a passkey, and **new pairings are only possible after enabling them from the menu**: long-press
BOOT and tap *Pair PC* (the window stays open for 2 minutes; touching the screen or pressing BOOT
cancels). Then, on Windows 11: *Settings → Bluetooth & devices → Add device → Bluetooth →
"Claude Buddy"*. A **6-digit code** appears on the board's screen: type it on the PC. From then on
the pairing is saved (bonding) on both sides, and an already paired PC reconnects without opening
the window. To re-pair, or to hand the board to another PC, tap *Forget* in the menu (or run
`python host/buddy.py unpair` over USB) and remove "Claude Buddy" from Windows' devices.

The link is encrypted (LE Secure Connections with an on-screen passkey): someone in range can only
attempt to pair by seeing the code on the board, and only while you have opened the pairing window.
`unpair` is a text command like the others, so over BLE it only works from an already paired PC.
Log: `host/state/ble.log`.

### 3. Claude Code hooks and statusLine

In `host/hooks.json` replace the placeholder `/PERCORSO/DEL/REPO` with the real path of the repository (with `/` also on Windows, e.g.
`C:/Users/name/Documents/ESP32-S3_Claude-Desk-Buddy`), then merge the `hooks` and `statusLine`
sections into your `~/.claude/settings.json` (without overwriting the ones already there) and
**restart Claude Code**.

The hooks used are `SessionStart`, `SessionEnd`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse`,
`PostToolUseFailure`, `Notification`, `Stop`, `SubagentStart`, `SubagentStop` and `PreCompact`; all of
them call `python -I host/buddy.py hook`. The statusLine calls `buddy.py statusline`: it extracts
usage, context, cost, model and cache data, and also prints a status line inside Claude Code.

A failing hook never disturbs Claude Code: the script always exits cleanly. Hooks print nothing to
the model, so this setup does not consume any tokens.

### Trying it without Claude Code

```
python host/buddy.py show              # print the snapshot that would be sent to the board
python host/buddy.py send "attention"  # send a raw line to the board
python host/buddy.py clear             # forget all sessions
python host/buddy.py standby|wake|off  # power control
```

## How it works

Each Claude Code session has a file in `host/state/` (not versioned). On every event `buddy.py`
updates the state, recomputes the overall one (priority: *needs you* > *working* > *error* >
*thinking* > *done* > *idle*) and sends the board a **snapshot** of text lines:

| Line | Meaning |
|---|---|
| `idle`, `thinking`, `working [tool]`, `attention [msg]`, `done`, `error`, `sleep` | overall state |
| `sessions N` · `sess i S ctx cache model effort fast name\|tool` | open sessions |
| `usage 5h% 5h_min 7d% 7d_min ctx%` | usage limits (`-1` = not available) |
| `agents N` · `todo done total` · `pet xp` | sub-agents, task list, experience |
| `stats cents lines+ lines-` · `today prompts tools minutes` | today's cost and counters |
| `msg age session\|text` · `tick text` | last message and scrolling line |
| `fx seq age name` | one-shot event (commit, push, testok, testfail, compact, pushfail, conflict, commitfail) |
| `epoch seconds` · `time HH:MM` · `bright 0-255` · `sound 0\|1` | clock, brightness, sound |
| `autovol 0\|1` · `micdebug 0\|1` | auto volume on/off; print microphone readings on the serial port |
| `unlockall 0\|1` · `style H G` | preview: unlock every accessory / show hat H and glasses G (0-7, not saved) |
| `demo N` | start entry N (1-38) of the animation demo, `demo 0` leaves it |
| `autorotate 0\|1` · `orientdebug 0\|1` | auto rotation on/off; print accelerometer readings on the serial port |
| `standby` · `wake` · `off` · `autostandby minutes` | power (see above) |
| `unpair` | forget the PCs paired over BLE |
| `touchdebug 0\|1` | print touch coordinates on the serial port (diagnostics) |
| `@perm <tool>` (inside `attention`/`sess` detail) | permission request; the board translates it to the selected language |

The protocol is plain text, over USB serial (115200) or BLE (one line per command), so you can try
it with a regular serial monitor. Details are in the comment at the top of `host/buddy.py`.

## Repository layout

```
claude_buddy/
  claude_buddy.ino   firmware: display, Clawd, pages, touch, menus, protocol
  board.h            board variant (1.75C vs 1.75 / 1.75-B): reset and clock pins
  hw.h / hw.cpp      audio (ES8311), motion (QMI8658), RTC (PCF85063), battery and POWER key (AXP2101)
  ble.h / ble.cpp    BLE Nordic UART server, pairing window
  lang.h             on-screen texts in 5 languages (add a language = add a column)
host/
  buddy.py           hook/statusLine bridge -> serial or BLE
  ble_bridge.py      BLE daemon (needs bleak)
  hooks.json         hooks and statusLine to merge into ~/.claude/settings.json
  port.example.txt   serial port example
  transport.example.txt   transport example (ble)
LICENSE              MIT
THIRD-PARTY-NOTICES.md   credits and third-party licenses
```

## Languages

The language is stored in flash and chosen from the settings menu. All on-screen texts live in
`claude_buddy/lang.h` as a table (`STR_LIST`) with one column per language; to add one, add a
column, a code in `LANG_CODES` and an entry in the `Lang` enum. Use ASCII only (the base font has no
accents) and keep texts short enough for the round display.

## Known limits and common problems

- **The PC sends nothing / the port is busy**: close the Serial Monitor and check `host/port.txt`.
- **BLE: "Insufficient Authentication" in the log**: the board is not paired yet. Open the pairing
  window from the menu (*Pair PC*) and add it in Windows. Make sure no other app (e.g. a phone) is
  connected to the board.
- **Usage and limits**: the 5-hour and weekly limits come from the statusLine and only for
  subscribers; they appear after Claude's first reply. The cost is an estimate.
- **Task list**: reads `TodoWrite`; if your Claude Code version uses other tools for tasks,
  "Task N/M" will not appear.
- **Commit/test events**: inferred from the command and output text of Bash, so unusual commands
  may be missed.
- **Messages**: converted to ASCII (no accents or markdown) and limited to ~380 characters.
- **Touch**: oriented as in the TamaPoke project (XY mirrored); if taps appear mirrored it needs a tweak.
  `touchdebug 1` prints the raw coordinates over the serial port.
- **Linux/macOS**: the script writes directly to the serial device; the project has only been tested on Windows.
  BLE on Windows needs a Bluetooth 4.0+ adapter.
- **Cable**: no longer needed for normal use (BLE), but still required for flashing. Wi-Fi and approving
  permissions from the board are ideas not yet implemented.
- **Power**: the power-key behavior (standby/off) and the pairing window depend on the PMU and the
  BLE stack of your board; if something misbehaves, open an issue with the *Info* page values.

## Trademarks and license

This is an unofficial, independent project, **not affiliated with or endorsed by Anthropic**.
"Claude", "Claude Code" and the "Clawd" mascot belong to Anthropic; the Clawd drawing in the
firmware is a simple block reproduction.

The code in this repository is released under the **MIT** license (see [`LICENSE`](LICENSE)).
Credits and licenses of third-party components are in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
