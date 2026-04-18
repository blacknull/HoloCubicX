# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HoloCubicX is ESP32-S3 firmware for a desktop "Holocubic" device, refactored from `peng-zhihui/HoloCubic` and `ClimbSnail/HoloCubic_AIO`. Hardware differences from the original: MCU is `ESP32-S3` (not ESP32-pico), IMU is `QMI8658A` (not MPU6050). Built on Arduino framework + LVGL v8.3, PlatformIO is the build system. README.md is in Chinese and describes the higher-level refactor goals.

## Build / Flash / Debug

PlatformIO project — environment is `esp32-s3-devkitm-1`, board `esp32-s3-devkitc-1`. Common commands (run from repo root):

- `pio run` — build
- `pio run -t upload` — build + flash
- `pio device monitor` — serial monitor (115200 baud, with `esp32_exception_decoder` and `log2file` filters enabled; monitor logs go to `logs/`)
- `pio run -t clean`

JTAG debug uses ESP32-S3's built-in USB-JTAG. `debug_tool = esp-builtin` and `build_type = debug` in `platformio.ini` are commented out by default — uncomment to debug. Driver install link is in `README.md`.

Flash layout is 16 MB QIO with OPI PSRAM. Default partition table is the stock Arduino one; `large_spiffs_16MB.csv` / `FLASH_8MB.csv` are available alternatives (swap `board_build.partitions` in `platformio.ini`).

Build flag `CORE_DEBUG_LEVEL` controls log verbosity (currently 3 = INFO). `ARDUINO_USB_CDC_ON_BOOT=1` means logs come out over the native USB CDC, not UART0.

## Architecture

Entry point is [src/HoloCubicX.cpp](src/HoloCubicX.cpp) (`setup()` / `loop()`). The system is structured around a single `AppController` that owns a list of pluggable **APPs** and dispatches IMU events and inter-APP messages.

### Three layers

- **`src/driver/`** — hardware drivers: [display](src/driver/display.h), [imu](src/driver/imu.h) (QMI8658A), [rgb_led](src/driver/rgb_led.h), [sd_card](src/driver/sd_card.h), [ambient](src/driver/ambient.h) light sensor, [flash_fs](src/driver/flash_fs.h) (SPIFFS config persistence), plus LVGL porting glue (`lv_port_indev.c`, `lv_fs_fatfs.c`).
- **`src/sys/`** — the [AppController](src/sys/app_controller.h), its launcher GUI (`app_controller_gui.*`, `app_loading.c`), config read/write, and the [APP_OBJ interface](src/sys/interface.h) every APP implements.
- **`src/app/<app_name>/`** — individual APPs. Each exposes a single `APP_OBJ` (e.g. `weather_app`, `tomato_app`) that provides `app_init`, `main_process`, `exit_callback`, and `message_handle` callbacks.

### APP lifecycle and registration

Every APP is compiled in/out via a `APP_*_USE` macro in [src/app/app_conf.h](src/app/app_conf.h). When adding a new APP:
1. Create `src/app/<name>/` with an `APP_OBJ` definition.
2. Add a `#define APP_<NAME>_USE 0/1` + conditional include in `app_conf.h`.
3. Add a matching `app_controller->app_install(&<name>_app)` block in `setup()` in `HoloCubicX.cpp`.
4. Reserve an `app_name` in [src/app/app_name.h](src/app/app_name.h) — this string is the addressing key used by `send_to()`.

Only one APP is active at a time. Switching is triggered by an IMU gesture (**large head-down tilt** — changed from long-press in the refactor to avoid accidental swaps). `AppController::main_process` is called every `loop()` iteration; if an APP is running it delegates to that APP's `main_process`, otherwise it runs the launcher GUI.

### Messaging

APPs talk to each other and to the system via `AppController::send_to(from, to, type, message, ext_info, isSync)`. Key points:
- Messages always go through the event queue (`eventList`), even sync ones — unified in the refactor.
- **Async** messages (e.g. `APP_MESSAGE_WIFI_STA`) are processed in the main loop by `req_event_deal`.
- **Sync** messages (`APP_MESSAGE_GET_PARAM`, `APP_MESSAGE_SET_PARAM`, `APP_MESSAGE_READ_CFG`, `APP_MESSAGE_WRITE_CFG`) invoke the target's `message_handle` immediately on the calling stack — used for config reads the caller needs to block on.
- Wifi is addressed as the pseudo-APP `WIFI_SYS_NAME` and owns sta/ap lifecycle; see [src/network.cpp](src/network.cpp). `start_conn_wifi` / AP-close are exposed as discrete events (`APP_MESSAGE_WIFI_STA`, `APP_MESSAGE_WIFI_AP_CLOSE`, `APP_MESSAGE_WIFI_DISABLE`).

### Concurrency model

Two relevant FreeRTOS tasks beyond `loop()`:
- `refreshScreen` task (priority `TASK_LVGL_PRIORITY`): calls `lv_task_handler()` at ~60 Hz under `LVGL_OPERATE_LOCK`. **All LVGL calls from APP code must be wrapped in `LVGL_OPERATE_LOCK(...)`** ([src/gui_lock.h](src/gui_lock.h)) — a recent fix (commit `1a8009b`) forgot this in AppController init and caused lockups.
- `xTimerAction` software timer: 200 ms tick that sets `isCheckAction` so `loop()` polls the IMU for gesture changes.

Note: MPU init (`mpu.init(...)`) and actual gesture reads in `loop()` are currently commented out — `act_info` is stubbed to `UNKNOWN`. Don't assume gestures work until that's re-enabled.

### Screen saver / RGB coupling

System-level: when idle, screen saver engages and RGB is forced off. An APP can call `AppController::setSaverDisable(true)` to suppress screen saver while it runs; on exit, System restores both the saver and RGB state. `tomato` uses this to keep the screen on during a pomodoro. Keep this contract in mind for any APP that runs long-lived foreground UI.

### Config

Three persisted structs are loaded at boot and passed around by pointer: `sys_cfg` (wifi creds, rotation, backlight), `mpu_cfg`, `rgb_cfg`. Stored in SPIFFS via [flash_fs](src/driver/flash_fs.h). APP-specific config is read/written via sync `APP_MESSAGE_READ_CFG` / `APP_MESSAGE_WRITE_CFG` messages dispatched to the APP itself — the APP owns serializing its own keys (see the server APP for the canonical pattern).

## Third-party libs vendored under `lib/`

LVGL v8.3, TFT_eSPI (display setup picked via `User_Setups/Setup24_ST7789.h` — note this file is modified locally), TJpg_Decoder, ArduinoJson 6.x, FastLED, PubSubClient (MQTT), Qmi8658c, NTPClient, ESP32Time. Prefer editing the vendored copies over pulling upstream versions — display pinout and config live in the local TFT_eSPI setup header.
