# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A PlatformIO project for a Seeed Xiao ESP32-C6, built on **ESP-IDF directly** (`framework = espidf` in `platformio.ini`) — not Arduino. There is no `Serial`, no `setup()`/`loop()` Arduino entry points, and no Arduino libraries available unless explicitly added. Entry point is `extern "C" void app_main(void)` in `src/main.cpp`.

The board drives a relay wired to GPIO1, switched on/off by MQTT command messages received over WiFi (TLS, mutual auth). More relay-controller devices sharing the same broker/topics are expected in the future — the command payload always carries a target device id.

## Commands

- Build: `pio run`
- Upload + monitor: `pio run -t upload -t monitor` (device shows up as e.g. `/dev/cu.usbmodem*`; list candidates with `pio device list`)
- Clean: `pio run -t clean` (rarely needed; only if CMake/component-manager state gets stuck)
- Monitor only: `pio device monitor -b 115200`
- Regenerate `compile_commands.json` for IDE/clangd: `pio run -t compiledb`

No lint or test commands — this is a hardware-flashing-only embedded project with no unit test suite (per repo convention, do not add one).

## Architecture

- `src/main.cpp` — `app_main()` wires modules together: bring up the relay GPIO, bring up Wi-Fi, bring up MQTT, publish an `iamalive` presence message and the initial relay state, then loop polling for MQTT commands every `COMMAND_POLL_INTERVAL_MS` (100ms) and applying/echoing any new relay state. Modules don't call each other directly — `main.cpp` is the only place that reads one module's output and feeds it into another.
- `src/wifi_manager.{h,cpp}` — `wifi_manager_setup()` blocks until the station connects (or exhausts `WIFI_MAXIMUM_RETRY` retries), using an `EventGroupHandle_t` + ESP-IDF `WIFI_EVENT`/`IP_EVENT` handlers. Explicitly calls `esp_wifi_set_ps(WIFI_PS_NONE)` after `esp_wifi_start()` — the device is mains-powered, and the default modem-sleep power save causes periodic missed beacons / full reassociation cycles that reset every open TCP connection (MQTT). Don't reintroduce power save without solving that tradeoff.
- `src/mqtt_client_manager.{h,cpp}`:
  - `mqtt_client_manager_setup()` connects over TLS (`mqtts://`) with mutual authentication — `broker.verification.certificate` (root CA, validates the broker) plus `credentials.authentication.certificate`/`.key` (this device's client cert/key, presented to the broker) alongside the existing username/password. Connection is event-group-gated on `MQTT_EVENT_CONNECTED`, and on connect it subscribes to `MQTT_TOPIC_RELAY_COMMAND`.
  - `mqtt_client_manager_publish_iamalive()` builds the alive-message JSON by hand (fixed fields, no cJSON needed) and publishes it.
  - `mqtt_client_manager_publish_relay_state()` publishes the current relay state retained (QoS 1, retain flag set) to `MQTT_TOPIC_RELAY_STATE`, so subscribers/this device can read last-known state on reconnect.
  - Command handling: `MQTT_EVENT_DATA` payloads are parsed by hand (`extract_json_string_field` does a literal substring search — no cJSON). **The command topic is shared by every relay controller on the broker**, so `handle_command_data()` first extracts the payload's `"device"` field and compares it case-insensitively against this device's own id (`device_identity_get_id()`); anything not addressed to this device is silently ignored (`ESP_LOGD`, not a warning). Only then is `"state":"on"/"off"` parsed and pushed into `s_command_queue` (a length-1 `xQueueOverwrite` queue — a command received while `main.cpp`'s loop hasn't drained the previous one collapses to "apply the most recent"). `main.cpp` drains it via `mqtt_client_manager_get_pending_command()`.
  - **Command payload must be strict, compact JSON** — the parser does a literal substring search for `"device":"` / `"state":"` (quoted keys, no space after the colon, no pretty-printing). Example: `{"state":"on","device":"MCUDEVICE-9CFEFFA3BD10"}`. Loosely-formatted JSON (unquoted keys, spaces after colons) will silently fail to match and the command is dropped with a warning log.
- `src/relay_controller.{h,cpp}` — owns GPIO1 as the relay output. `relay_controller_setup()` configures the pin; `relay_controller_set_state()`/`_get_state()` drive/read it. This is the only module allowed to touch the relay GPIO directly.
- `src/device_identity.{h,cpp}` — `device_identity_get_id()` derives `MCUDEVICE-<12 hex>` from `esp_efuse_mac_get_default()` with the 6 MAC bytes **byte-reversed** before hex-encoding — this matches the `ESP.getEfuseMac()`-based convention used by the existing (Arduino-based) device fleet, where the byte order comes from how the MAC is packed into a little-endian `uint64_t`. Don't "fix" the reversal — it's intentional fleet-format compatibility, not a bug. `device_identity_get_mac()` returns the same bytes in normal order, colon-separated, lowercase, via `esp_wifi_get_mac()`.

### Config file pattern

Secrets/environment config live in `include/*_config.h`, gitignored, with a committed `*_config.example.h` template alongside (see `wifi_config.h`, `mqtt_broker_config.h`). When adding a new config value, add it to both the real file and the example, and confirm `.gitignore` covers the real one.

`mqtt_broker_config.h` currently defines: `MQTT_BROKER_HOST`, `MQTT_BROKER_PORT` (8883, TLS), `MQTT_BROKER_USERNAME`/`PASSWORD`, `MQTT_TOPIC_IAMALIVE`, `MQTT_TOPIC_RELAY_COMMAND`, `MQTT_TOPIC_RELAY_STATE`, and the TLS material `MQTT_BROKER_ROOT_CA`, `MQTT_BROKER_CERTIFICATE`, `MQTT_BROKER_PRIVATE_KEY` (PEM strings, `\n`-per-line, no trailing `;` inside the `#define` — a stray `;` or `=` inside these multi-line macros silently breaks the field assignment or fails to compile).

**Naming gotcha:** the MQTT broker config file is named `mqtt_broker_config.h`, not `mqtt_config.h` — the `espressif/mqtt` managed component ships its own internal header literally called `mqtt_config.h` (`managed_components/espressif__mqtt/lib/include/mqtt_config.h`), which sits earlier on the include search path and silently shadows a project file of the same name (no "file not found" error — it just resolves to the wrong file and macros go undeclared). Keep this in mind before naming any new project header `*_config.h`; check for collisions with managed component internals first.

### Adding ESP-IDF components

This ESP-IDF version (6.0.1) has **MQTT removed from core** — it's fetched as a managed component via the component manager. Pattern for adding another managed component:
1. Add it under `dependencies:` in `src/idf_component.yml` (the manifest lives next to `src/CMakeLists.txt` since `src` is the component ESP-IDF registers here — PlatformIO adds it as `EXTRA_COMPONENT_DIRS`, not as `main`).
2. Add its component name to `PRIV_REQUIRES` in `src/CMakeLists.txt` (currently `esp_wifi esp_netif esp_event nvs_flash mqtt esp_driver_gpio`). Explicit `PRIV_REQUIRES` is required — this project's `src` component is not named `main`, so it doesn't get ESP-IDF's "main auto-requires-everything" convenience.
3. First build after adding a dependency needs network access (component manager fetches into `managed_components/`); `dependencies.lock` records the resolved versions.

Note: steps 1 and 3 are only for *managed* components fetched via the component manager. Components that ship built into the ESP-IDF framework itself (e.g. `esp_driver_gpio`) skip both — just add the component name to `PRIV_REQUIRES` in step 2.

### Known constraints

- Partition table: `platformio.ini` sets `board_build.partitions = partitions_singleapp_large.csv` (a 1.5MB app partition, no OTA) — the default `partitions_singleapp.csv` (1MB) leaves the build at ~99% full once the `mqtt` managed component is linked in.
  - **Partition table gotcha:** the partition table PlatformIO actually builds with — and uses for the `checkprogsize` size check — comes from `board_build.partitions` in `platformio.ini`, not from ESP-IDF's own `CONFIG_PARTITION_TABLE_*` sdkconfig options. Editing `sdkconfig.seeed_xiao_esp32c6` directly has no effect, even across a full `pio run -t clean` — PlatformIO's espidf builder always overrides it from `board_build.partitions`. Change the partition table only via `platformio.ini`.
- `sdkconfig.seeed_xiao_esp32c6` is configured for 4MB flash; PlatformIO warns the detected board flash is 2MB. Not yet resolved — check via `menuconfig` before relying on OTA or large partitions.
