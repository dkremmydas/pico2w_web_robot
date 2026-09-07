# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Firmware for a Raspberry Pi Pico 2 W that drives a 4-wheel robot (two L298N motor
controllers) and serves a mobile-friendly web control panel over WiFi via lwIP's
`httpd`. The browser page sends drive commands to a CGI endpoint; the Pico decodes
them, ramps a single vehicle speed up/down, derives per-wheel PWM duty/direction
from that speed, and drives the motors. There is no OS — this runs directly on the
Pico SDK (`NO_SYS=1`).

## Build / flash / run

This is a Raspberry Pi Pico project built with CMake + Ninja, normally driven
through the **Raspberry Pi Pico VS Code extension** (see `.vscode/`). The extension
sets up `PICO_SDK_PATH`/toolchain env vars and provides these tasks (Terminal >
Run Task):

- **Compile Project** — `ninja -C build` (the actual build)
- **Run Project** — `picotool load <target> -fx` (load + execute over USB)
- **Flash** — program via `openocd` + CMSIS-DAP debug probe
- **Rescue Reset** / **RISC-V Reset (RP2350)** — recovery tasks for a bricked/unresponsive board

To build from a shell manually (requires the Pico SDK toolchain, CMake, and Ninja
from `~/.pico-sdk` on PATH, as configured in `.vscode/settings.json`):

```
cmake -G Ninja -B build
ninja -C build
```

Output artifacts (`.uf2`, `.elf`, `.hex`, `.bin`) land in `build/`. There are no
unit tests, linter, or CI in this repo — verification is done by flashing to real
hardware and exercising the web UI, or by reading serial output.

Serial/USB debug output is enabled (`pico_enable_stdio_usb`); UART stdio is
disabled. `printf` calls in `pico_httpd.c` are the primary way to observe runtime
behavior (connection status, received commands, PWM duty per wheel).

## Architecture

**`pico_httpd.c`** is the entire application. Key pieces:

- **WiFi bring-up** (`main`): initializes `cyw43_arch`, connects in station mode
  using `WIFI_SSID`/`WIFI_PASSWORD` (injected as compile definitions from
  `CMakeLists.txt` — **do not commit real credentials here**, they're currently
  hardcoded in `CMakeLists.txt` as `target_compile_definitions`), sets a hostname
  derived from the MAC address, then starts `httpd_init()` and polls forever in
  `cyw43_arch_poll()` / `sleep_ms` loop.
- **Web server**: lwIP's `httpd` app serves static files from `content/` (built
  into the flash image at compile time via `pico_set_lwip_httpd_content` in
  `CMakeLists.txt` — adding a new static file requires listing it there). SSI is
  disabled; CGI is enabled.
- **Command protocol**: the browser polls `/control.cgi?command=XXX` on an
  interval (see `content/index.html`, `time_resolution = 300ms`) while a
  direction button is held (pointer or keyboard), and sends `NON` (idle/no
  command) the rest of the time. `cgi_control()` in `pico_httpd.c` parses the
  `command` param into a `CommandType` enum (`FWD`, `BWD`, `LFT`, `RGT`, `FLT`,
  `FRT`, `BLT`, `BRT`, `STP`, or none) and hands it to `update_vehicle()`.
- **Speed/direction state machine** (`update_vehicle()`): tracks `last_command`
  and `prev_command` (a 2-entry history). Repeating the same command ramps
  `vehicle_speed` up by 1 each poll (capped at `MAX_VEHICLE_SPEED`); a new/
  different command resets speed to 1 and sets each wheel's direction `coef`
  (±1.0 for straight, ±0.5 on the inner wheels for turns); `STP` zeroes speed
  immediately. Per-wheel PWM duty is `vehicle_speed * |coef|`, scaled to the
  0–1000 PWM wrap value, with direction set via each motor's IN1/IN2 GPIO pins.
- **Virtual JSON file** (`fs_open_custom`/`fs_read_custom`/`fs_close_custom`):
  `cgi_control` writes a status JSON blob into a fixed `json_response` buffer
  and returns `/json_response` as the CGI redirect target; these `fs_*_custom`
  hooks (enabled via `LWIP_HTTPD_CUSTOM_FILES` in `custom.h`) serve that buffer
  as if it were a file, which is how the AJAX call in `index.html` gets a JSON
  reply without a filesystem round-trip.
- **`custom.h`**: the project's lwIP httpd feature flags (CGI on, SSI off, custom
  files on, dynamic headers) plus all hardware pin mappings for the two motor
  controllers (front-left/right, back-left/right ENA/IN1/IN2 GPIOs) and
  `MAX_VEHICLE_SPEED`. This file is force-included into every compile via
  `-include custom.h` (`CMakeLists.txt`), so it acts like a global config header
  without needing `#include "custom.h"` everywhere (it's included in
  `pico_httpd.c` explicitly anyway, but other TUs would get it for free).
- **`lwipopts.h`**: standard lwIP configuration for the Pico W background/poll
  examples (buffer sizes, enabled protocols, debug flags). Change with care —
  this affects TCP/DHCP/memory behavior for the whole network stack.
- **`content/index.html`**: the entire UI (jQuery-based). Pointer events (mouse/
  touch/pen unified) and keyboard (Space/Enter) both start/stop sending the held
  button's command on an interval, and fall back to sending `NON` when idle so
  the firmware's ramp-down logic engages.

When changing the command set or wheel geometry, keep three places in sync:
the `CommandType` enum + `get_command_enum()`/`cgi_control` parsing in
`pico_httpd.c`, the per-command `coef` assignments in `update_vehicle()`, and
the `data-command` values / buttons in `content/index.html`.

The `build/` directory is a generated CMake/Ninja build tree (including a vendored
copy of pico-sdk paths) — never hand-edit files under it.
