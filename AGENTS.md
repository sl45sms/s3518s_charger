# AGENTS.md

Guidance for automated agents (and humans) working in this repository.

## Repository scope

This project is an Arduino-based custom charger with:
- a local display for voltage/current graphs, and
- a web server for remote monitoring with graphs + browser local-storage logging.

Agent goals:
- keep changes small, reviewable, and hardware-aware.
- avoid breaking builds for common Arduino toolchains.

## Before you change anything

1. Identify the target board/MCU and toolchain assumptions (Arduino IDE vs `arduino-cli`) by looking at `config.json`.

2. Locate configuration points (pins, I2C addresses, display type, WiFi creds handling).
3. Prefer minimal diffs; do not reformat unrelated code.

If any of the above is unknown, ask for:
- board type (e.g., ESP32/ESP8266/AVR),
- display module (driver + resolution),
- sensor/ADC setup used to measure voltage/current,
- current build/flash method.

## Working rules

### Don’ts
- Don’t commit secrets (WiFi SSID/password, API tokens). Use placeholders and document where to put secrets locally.
- Don’t change pin mappings, calibration constants, or safety limits without explicitly calling it out.
- Don’t introduce heavy dependencies unless necessary (flash/RAM are constrained).

### Do’s
- Prefer `const`, `constexpr`, and `static` where appropriate.
- Keep ISR code minimal; avoid dynamic allocation on embedded targets.
- Ensure web UI changes degrade gracefully (no reliance on external CDNs unless explicitly desired).

## Code style (pragmatic)

- Keep functions short and single-purpose.
- Name things by role (`measuredVoltageMv`, `chargerState`, `renderGraph()`).
- Avoid `String` on constrained targets if fragmentation is a risk; prefer fixed buffers / `snprintf` where appropriate.
- Add comments for hardware assumptions (units, scaling, calibration).

## File organization (keep it clean)

- Keep the `.ino` as a thin orchestrator when possible (setup/loop + wiring).
- When functionality becomes non-trivial, split it into separate modules:
  - `*.h` for public interfaces (types, constants, function/class declarations).
  - `*.cpp` (or `*.c`) for implementations.
- Prefer one “responsibility” per module (e.g., `display_*`, `web_*`, `measure_*`, `storage_*`).
- Avoid circular includes; include only what you use.
- Keep headers lightweight:
  - forward-declare where practical,
  - avoid heavy Arduino/library includes in headers unless required,
  - use include guards or `#pragma once` consistently.
- Don’t move code into new files unless it improves readability/testability and reduces `loop()` complexity.

## Web server: async + non-blocking requirements

The web server must not stall measurement/control or display refresh.

### Non-blocking rules (apply regardless of library)
- No `delay(...)` in request handlers.
- No long loops waiting for I/O (WiFi, sensors, storage). Use a state machine + `millis()` timers.
- Keep handler work O(1): read latest cached measurements, format a response, return.
- Any “expensive” work (graph resampling, filesystem writes, JSON building) should be:
  - precomputed incrementally in the main loop, or
  - done in a dedicated task (ESP32-class), with bounded execution time.
- Logging must be bounded (ring buffer / capped files) and must not block the main loop.

### Prefer async server (ESP32-class) when feasible
If the target has enough flash/RAM and adding dependencies is acceptable, prefer an async model:
- ESP32 Arduino typically uses an async HTTP server (e.g., `ESPAsyncWebServer` + appropriate async TCP backend).
- Async handlers should still avoid dynamic allocation bursts; reuse buffers where practical.

If adding that dependency is not acceptable, keep the stock synchronous server but structure it non-blockingly:
- call `handleClient()` frequently from `loop()`
- serve from cached data; avoid recomputing on-demand
- consider chunked/streamed responses if payloads are large

### ESP32 CPU / tasking guidance (important)
- Only *dual-core* ESP32 variants can truly “use the second CPU” by pinning tasks (e.g., `xTaskCreatePinnedToCore`).
- **ESP32-C6 is single-core but have a LP core** 
For ESP32-C6:
  - use the LP core for I2C comunication to SS3518S and GPIO handling
  - run the main loop and web server on the main core
  - avoid assumptions about multi-core parallelism.
  - you can still use FreeRTOS tasks, but they all run on the same core
  - keep web handlers short and yield-friendly (avoid blocking calls)
  - consider a low/medium priority “web task” that only formats/sends from cached state

When touching tasking:
- document task priorities and what each task is responsible for
- ensure shared data is safe (atomic/critical section/mutex) and copying is bounded
- avoid starving the main control loop; ensure periodic `yield()`/`vTaskDelay(1)` where appropriate

## Build / flash (fill in as project specifics become clear)

If using Arduino CLI, typical flow is:

```sh
# arduino-cli core update-index
# arduino-cli core install <vendor>:<arch>
# arduino-cli compile --fqbn <vendor>:<arch>:<board> <sketch_dir>
# arduino-cli upload  --fqbn <vendor>:<arch>:<board> -p <port> <sketch_dir>
```

Agent instruction:
- If you add build steps to docs, keep them generic and parameterized (`<...>`).
- Do not claim a specific FQBN/board unless it exists in-repo.

## Validation checklist (minimum)

When making changes, ensure:
- Sketch compiles (or clearly state what’s needed to compile).
- Display still updates without flicker/regressions (if touched).
- Web server still serves main page and updates values (if touched).
- Logging does not grow unbounded and does not block the main loop.

For measurement-related changes:
- confirm units (V/A vs mV/mA) and scaling constants.
- ensure range/overflow is handled (ADC max, graph scaling).

## Documentation expectations

If you change behavior, also update documentation:
- what changed, why, and how to verify on hardware.
- any new configuration knobs (pins, calibration factors, compile-time flags).

## Output format for agents

When proposing changes:
- describe approach in 1–4 steps,
- group diffs by file,
- avoid repeating unchanged code (use `...existing code...` placeholders),
- keep responses short and actionable.