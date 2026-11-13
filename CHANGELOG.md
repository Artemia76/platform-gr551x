# Changelog

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
