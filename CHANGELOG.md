# Changelog

## Unreleased

## 1.0.0-rc.1

Release candidate for 1.0.0: every T1000-A function used by the MeshCore companion (LoRa, BLE, GNSS, sensors,
buzzer, LED, button, power off, BLE and button DFU) works on the board and in the digital twin. 1.0.0 will follow
after a period of daily use with no new defect, without code changes.

- **PWM shared clock**: PWM0 and PWM1 share one clock (`PWM_HCLK`), and `app_pwm_deinit()` gates it even while
  the other module is running. Releasing one module (end of a `tone()`, or `pinMode()` on an `analogWrite()` pin)
  now restores the clock for the other. On the T1000-A, the end of the buzzer start-up tune left the charge LED
  dark until the cable was plugged in again, and any buzzer sound did the same (found 2026-11-21).

## 0.3.1

- **T1000-A pins**: `PIN_WIRE_SDA` = GPIO8 and `PIN_WIRE_SCL` = GPIO9 (they were swapped), as Seeed's software I2C
  drives them and as checked on the board by reading the QMA6100P accelerometer (2026-11-20).

## 0.3.0

- **Scheduler probe**: `gr551x_sched_stats_get()` / `gr551x_sched_stats_reset()` (macro `GR551X_SCHED_STATS`):
  time spent in `pwr_mgmt_schedule()` (ROM) in active and idle mode, how often an application interrupt arrived
  meanwhile, and time in `loop()` (average, max, histogram from 1 ms to 1 s). Costs two reads of the 32 kHz
  calendar per loop pass (≈ 3 µs). Measured on the T1000-A (2026-11-17): in IDLE the ROM returns at every BLE
  radio event (≈ 16/s while advertising, sleeps ≤ 159 ms); in active mode it returns in ≈ 10 µs. These
  measurements calibrate the digital twin's sleep and timing model.

## 0.2.0

Everything the T1000-A MeshCore firmware gained after 0.1.0: power measurement, real power off, PWM LED.

- **Power statistics**: `gr551x_power_stats_get()` / `gr551x_power_stats_reset()` (time in IDLE, BLE connected,
  advertising, sleeps, refused sleeps, wake-up interrupts, `loop()` passes, BLE connections).
- **Power off**: `gr551x_power_off()` puts the chip in ultra deep sleep; wake on AON pins only (button, charger),
  through the bootloader. `gr551x_power_off_state()` / `gr551x_power_off_clear()` and a wake counter tell the
  application it was off. The T1000-A keeps that state in the boot guard sector and stops the watchdog.
- **`analogWrite()`**: real 1 kHz PWM on the pins that have one (PWM0/PWM1), kept in IDLE. Fixes found on the
  board: AON pad output enable, compare value written directly, rising edge on `UPDATE.SA`.
- **`gr551x_wake_in(ms)`**: one-shot wake of the loop.
- **Diagnostics**: the watchdog pre-alert records the PC/LR of the stuck code (`diag_wdt_install()`), written in a
  single Flash operation; the reset flag is reported as unreliable.
- ArduinoCore-API changes kept as patches (`patches/arduinocore-api/`), re-applied by
  `tools/update-arduinocore-api.sh`; monthly update workflow and CI build of `examples/blink`.
- `t1000_ble.py`:
  - `flash` refuses a firmware that fails the digital twin tests (`--skip-twin` to override), runs them in
    parallel when pytest-xdist is present, and finds the twin as `emulator/twin` of the T1000A repository or
    through `T1000_TWIN_DIR` (symlinks followed);
  - `flash` checks the image end against the file system start (`--limit`, default `0x010C2000`, filled in from
    the board's `build.app_limit` on upload); it used to accept images up to `0x010E2000`, over LittleFS.
    `mkimage.py` had the same default;
  - `console --cmd` runs commands non-interactively; `--enter-dfu` on a firmware without console explains the
    6 s button hold.
- The digital twin now lives in its own repository, [t1000a-twin](https://github.com/Artemia76/t1000a-twin).
- LICENSE: MIT text only.

Known limitations: `analogRead()` returns 0 (use `analogReadMilliVolts()`); `Wire` is a placeholder (NACK).

## 0.1.0

First tagged release. Enough to run MeshCore (companion over BLE, repeater) on the Seeed SenseCAP T1000-A.

- Arduino core on the Goodix GR551x SDK (pinned to Artemia76/GR551x.SDK `b1bb11f`), ArduinoCore-API 1.5.2.
- GPIO / AON / MSIO pins, interrupts, `analogReadMilliVolts`, `tone()` on hardware PWM.
- Time base on the AON calendar; light sleep on request.
- `Serial` (debug sink, also fed by the BLE console), `Serial1` (UART), `SPI`, `Wire`.
- BLE: ROM stack started before `setup()`, event dispatch for libraries; `BleConsole` (text console, `dfu`, `reboot`).
- T1000-A variant: pin map, linker script, boot guard (DFU after 3 short boots or a 6 s button press), AON watchdog,
  fault recorder.
- Build produces `firmware_dfu.bin`; `pio run -t upload` flashes over BLE through the stock Seeed bootloader.
