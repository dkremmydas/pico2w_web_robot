# pico2w_web_robot

Control a 4-wheel robot using an embedded web server running on a Raspberry Pi
Pico 2 W. The Pico joins your WiFi network, hosts a small control-panel web
page, and drives two L298N motor controllers (one per side pair of wheels) in
response to button presses (mouse, touch, or keyboard) from that page.

## Hardware

- Raspberry Pi Pico 2 W
- 2x L298N motor driver boards, one per axle pair:
  - Controller 1: front-left wheel + front-right wheel
  - Controller 2: back-left wheel + back-right wheel
- 4x DC gear motors (one per wheel)

### Wiring

Pin numbers are Pico GPIO numbers (physical pin number in parentheses), defined
in [custom.h](custom.h):

| Wheel | ENA/B (PWM) | IN1 | IN2 |
| --- | --- | --- | --- |
| Front left | GP6 (9) | GP7 (10) | GP8 (11) |
| Front right | GP2 (4) | GP3 (5) | GP4 (6) |
| Back left | GP13 (17) | GP14 (18) | GP15 (19) |
| Back right | GP10 (14) | GP11 (15) | GP12 (16) |

Each ENA/ENB pin is driven with PWM for speed control; IN1/IN2 set direction.
See [this video](https://www.youtube.com/watch?v=dyjo_ggEtVU) for wiring an L298N.

## Prerequisites

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) and ARM
  toolchain, most easily installed via the **Raspberry Pi Pico VS Code
  extension** (this repo's `.vscode/` config targets it), which manages CMake,
  Ninja, the toolchain, and `picotool` under `~/.pico-sdk`.
- A 2.4GHz WiFi network (the board connects in station mode).

## Setup

1. Copy the WiFi credentials template and fill in your network:

   ```bash
   cp wifi_config.cmake.example wifi_config.cmake
   ```

   Edit `wifi_config.cmake` and set `WIFI_SSID` / `WIFI_PASSWORD`. This file is
   gitignored — your credentials are never committed.
2. Open the project folder in VS Code with the Raspberry Pi Pico extension
   installed (it should auto-detect `CMakeLists.txt` and configure the build).

## Build & flash

Using the VS Code extension's tasks (Terminal > Run Task):

- **Compile Project** — builds with Ninja into `build/`
- **Run Project** — loads and runs the built firmware over USB via `picotool`
- **Flash** — programs the board over a CMSIS-DAP debug probe via OpenOCD

Or from a shell with the SDK/toolchain on `PATH`:

```bash
cmake -G Ninja -B build
ninja -C build
```

This produces `build/picow_httpd_background.uf2` (drag-and-drop onto the Pico
in BOOTSEL mode) along with `.elf`/`.hex`/`.bin` outputs.

Serial output is available over USB (`pico_enable_stdio_usb`) and logs WiFi
connection status, received commands, and vehicle speed — useful for
debugging without a debug probe attached.

## Usage

1. Power on the robot. It connects to the configured WiFi network and starts
   an HTTP server on port 80. Watch the USB serial output for the IP address
   it's assigned (`Ready, running httpd at <ip>`).
2. Open that IP address in a browser (desktop or mobile) to load the control
   panel.
3. Press and hold a direction button (or focus it and hold Space/Enter) to
   drive; release to stop. Holding a button repeats the command roughly every
   300ms, which ramps the vehicle's speed up the longer it's held; releasing
   ramps it back down. "STOP NOW" halts the motors immediately.

### Controls

| | | |
| --- | --- | --- |
| Front Left | Go Front | Front Right |
| Left | **STOP NOW** | Right |
| Back Left | Go Back | Back Right |

- **Go Front / Go Back**: all four wheels drive together.
- **Left / Right**: wheels on one side drive forward, the other side reverses,
  pivoting the robot in place.
- **Diagonal buttons** (Front Left, Front Right, Back Left, Back Right): one
  side drives at full speed and the other at half speed, arcing the robot
  toward the slower side while still moving forward/backward.

### Safety

If the robot stops receiving commands for about a second (WiFi drops, the
browser tab closes, etc.) while still moving, it automatically stops the
motors rather than continuing to drive indefinitely.

## Project structure

- [pico_httpd.c](pico_httpd.c) — WiFi bring-up, PWM/motor control, the
  `/control.cgi` command handler, and the speed/direction state machine.
- [custom.h](custom.h) — motor GPIO pin mappings and lwIP httpd feature flags,
  force-included into every compile unit.
- [lwipopts.h](lwipopts.h) — lwIP network stack configuration.
- [content/](content/) — the static web control panel (`index.html`) served
  by the Pico, baked into the firmware image at build time.
- [wifi_config.cmake.example](wifi_config.cmake.example) — template for your
  local, gitignored `wifi_config.cmake`.

See [CLAUDE.md](CLAUDE.md) for a deeper architecture walkthrough.
