# platform-gr551x

PlatformIO platform for the **Goodix GR551x** Bluetooth LE SoC (Cortex-M4F), with an Arduino core built on the
Goodix GR551x SDK. First board: **Seeed SenseCAP T1000-A** (GR5515 + Semtech LR1110 + Airoha AG3335).

Status: experimental, community-maintained (not supported by Goodix or PlatformIO). Built for running
[MeshCore](https://github.com/meshcore-dev/MeshCore) on the T1000-A
(fork: [Artemia76/MeshCore](https://github.com/Artemia76/MeshCore), branch `feature/t1000a-gr5515`).
Only the subset of the Arduino API that MeshCore needs is implemented. See `CHANGELOG.md`.

## Usage

```ini
[env:t1000-a]
platform = https://github.com/Artemia76/platform-gr551x.git#v0.2.0
board = t1000-a
framework = arduino
```

- `pio run` produces `.pio/build/<env>/firmware_dfu.bin`: the raw image with the Goodix image trailer
  (48 bytes) expected by the bootloader.
- `pio run -t upload` flashes it over **BLE** through the original Seeed bootloader (`tools/t1000_ble.py`,
  needs the `bleak` Python package and a Bluetooth adapter). With `custom_upload_enter_dfu = <BLE name>` the
  tool first asks the running sketch (`BleConsole` "dfu" command) to restart into the bootloader.
- Before any DFU, the upload tool runs the firmware's ELF through the T1000-A digital twin's test suite
  (an emulator of the board, [t1000a-twin](https://github.com/Artemia76/t1000a-twin), found as `emulator/twin` of the T1000A repository or through `T1000_TWIN_DIR`). If a
  test fails, or if the twin can't be found, the upload is refused. Set `custom_upload_skip_twin = yes` to
  flash without this check. If PlatformIO's Python lacks `unicorn`, `pyelftools` and `pytest`, point
  `T1000_TWIN_PYTHON` to an interpreter that has them.

## Layout

| Path | Content |
|---|---|
| `builder/` | SCons scripts: compile, link, DFU image, upload |
| `cores/gr551x/` | Arduino core: `main()`, pins, interrupts, time, `Serial` (debug sink), BLE event dispatch, ADC (`adc.cpp`), PWM (`tone.cpp`), power statistics and power off |
| `cores/gr551x/api/` | [ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) 1.5.2 (+ `Print::printf`) |
| `variants/t1000-a/` | Pin map, SDK `custom_config.h`, linker script, boot safety and fault recorder |
| `libraries/SPI` | SPI master (`app_spi`) |
| `libraries/Wire` | Placeholder I2C: no transaction, every address answers NACK (lets bus-probing libraries build) |
| `libraries/BleConsole` | Text console over BLE (Goodix UART service) with `dfu` / `reboot` commands |
| `tools/` | `mkimage.py` (DFU image), `t1000_ble.py` (BLE flasher and console), `check_handlers.py` (link-order guard for exception handlers), `update-arduinocore-api.sh` (refresh `cores/gr551x/api`) |
| `patches/arduinocore-api/` | Our changes to ArduinoCore-API, re-applied on each update, and the vendored version |
| `examples/blink` | LED + BLE console |

## Maintenance

- `cores/gr551x/api/` is never edited by hand: change or add a patch in `patches/arduinocore-api/`, then run
  `tools/update-arduinocore-api.sh <version>`. The monthly `ArduinoCore-API` workflow does it when a new
  version is released and opens a pull request once `examples/blink` builds.
- The `CI` workflow builds `examples/blink` on every push and pull request.

## Core notes

- **Startup**: the SDK startup code does not run `.init_array`, and puts `system_platform_init` in it. The core's
  `main()` runs the C++ constructors itself and skips that entry.
- **Time**: `millis()`/`micros()` read the AON calendar counter (32 768 Hz), which keeps running in sleep. The DWT
  cycle counter is only used for short busy waits (`delayMicroseconds()`).
- **Sleep**: `gr551x_idle_request()` puts the CPU in IDLE (WFI) at the end of a `loop()` pass, unless an interrupt
  (pin, BLE, UART, 1 s timer, button) arrived during that pass.
- **Power statistics**: `gr551x_power_stats_get()` returns, since boot or `gr551x_power_stats_reset()`, the time
  spent in IDLE, with a BLE connection open and advertising (32 kHz ticks), the number of sleeps, the sleeps refused
  (interrupt during the pass, or `gr551x_stay_awake()` / tone), wake-up interrupts and `loop()` passes. Meant to
  estimate the power budget from the time spent in each state.
- **Timed wake**: `gr551x_wake_in(ms)` wakes the loop once after `ms` milliseconds without keeping it awake (for
  periodic work that has no interrupt of its own).
- **ADC**: `analogReadMilliVolts()` reads the MSIO pins; `analogRead()` is still a stub that returns 0.
- **PWM**: `tone()` and `analogWrite()` use the PWM0/PWM1 modules, one pin per module, on the pins that have a PWM
  function. `analogWrite()` runs at 1 kHz with 1 % resolution (0–255 is scaled to the `app_pwm` duty), keeps
  running in IDLE, and `pinMode()`/`digitalWrite()` give the pin back to GPIO. On other pins, or on a module busy
  with `tone()`, it falls back to on/off.
- **Power off**: `gr551x_power_off(&cfg)` stops BLE, calls the variant's `variantPowerOff()` (watchdog off, state
  saved), then enters ultra deep sleep (`pwr_mgmt_ultra_sleep`), woken by AON pins at a high or low level. Neither
  RAM nor the CPU state is kept: a wake goes through the bootloader and a full start (about 2–3 s on the T1000-A),
  and `gr551x_power_off_state()` tells the application it was off. Found on the T1000-A: the MSIO comparator does
  not wake the chip from this mode, and the AON calendar restarts from zero. On the T1000-A the state lives in the
  boot guard sector, so wakes that do not lead to a full start are not counted as failed boots.
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
- 8 s AON watchdog, refreshed by `loop()` and the core's waits. With `diag_wdt_install()`, its pre-alert
  interrupt records the PC/LR of the stuck code in the diagnostics sector before the reset (`diag_report()`).
- Last resort (hardware): plug the charging cable while holding the button.

## Licenses

- Platform code: MIT (see `LICENSE`).
- `cores/gr551x/api/`: LGPL-2.1 (ArduinoCore-API).
- Goodix GR551x SDK (installed as the `framework-gr551x-sdk` package): BSD-3-Clause, © Goodix.
