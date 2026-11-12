# platform-gr551x

PlatformIO platform for the **Goodix GR551x** Bluetooth LE SoC (Cortex-M4F), with an Arduino core built on the
Goodix GR551x SDK. First board: **Seeed SenseCAP T1000-A** (GR5515 + Semtech LR1110 + Airoha AG3335).

Status: experimental. Built for running [MeshCore](https://github.com/meshcore-dev/MeshCore) on the T1000-A.

## Usage

```ini
[env:t1000-a]
platform = https://github.com/Artemia76/platform-gr551x.git
board = t1000-a
framework = arduino
```

- `pio run` produces `.pio/build/<env>/firmware_dfu.bin`: the raw image with the Goodix image trailer
  (48 bytes) expected by the bootloader.
- `pio run -t upload` flashes it over **BLE** through the original Seeed bootloader (`tools/t1000_ble.py`,
  needs the `bleak` Python package and a Bluetooth adapter). With `custom_upload_enter_dfu = <BLE name>` the
  tool first asks the running sketch (`BleConsole` "dfu" command) to restart into the bootloader.

## Layout

| Path | Content |
|---|---|
| `builder/` | SCons scripts: compile, link, DFU image, upload |
| `cores/gr551x/` | Arduino core: `main()`, pins, interrupts, time, `Serial` (debug sink), BLE event dispatch |
| `cores/gr551x/api/` | [ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) 1.5.2 (+ `Print::printf`) |
| `variants/t1000-a/` | Pin map, SDK `custom_config.h`, linker script, boot safety and fault recorder |
| `libraries/SPI` | SPI master (`app_spi`) |
| `libraries/BleConsole` | Text console over BLE (Goodix UART service) with `dfu` / `reboot` commands |
| `tools/` | `mkimage.py` (DFU image), `t1000_ble.py` (BLE flasher) |
| `examples/blink` | LED + BLE console |

## Core notes

- **Startup**: the SDK startup code does not run `.init_array`, and puts `system_platform_init` in it. The core's
  `main()` runs the C++ constructors itself and skips that entry.
- **Time**: `millis()`/`micros()` use the DWT cycle counter. This counter stops while the CPU sleeps, so the core
  keeps the SoC in active mode for now.
- **Pins**: 0–31 GPIO, `0x20 + n` AON n, `0x30 + n` MSIO n (the numbering used by Seeed's firmware).
- **BLE**: the ROM stack is started before `setup()`. Libraries subscribe with `gr551x_ble_add_handler()` and
  add their GATT services from `gr551x_ble_on_ready()`.

## T1000-A specifics

Flash map (application area of the Seeed bootloader):

| Range | Use |
|---|---|
| `0x0101B000`–`0x010C1FFF` | Application (668 KB) |
| `0x010C2000`–`0x010E1FFF` | File system (128 KB) |
| `0x010E2000` | Diagnostics (boot breadcrumbs, last hard fault) |
| `0x010E3000` | Boot guard counter |
| `0x010E4000` | "Enter DFU" flag read by the bootloader |

The device has no serial port and no SWD access without opening it, so the variant guarantees a way back to the
bootloader:

- 3 consecutive short boots (crash or watchdog before 2 min of uptime or a BLE connection) → DFU;
- button held 6 s → DFU;
- 8 s AON watchdog, refreshed by `loop()` and the core's waits.
- Last resort (hardware): plug the charging cable while holding the button.

## Licenses

- Platform code: MIT (see `LICENSE`).
- `cores/gr551x/api/`: LGPL-2.1 (ArduinoCore-API).
- Goodix GR551x SDK (installed as the `framework-gr551x-sdk` package): BSD-3-Clause, © Goodix.
