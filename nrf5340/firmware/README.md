# nRF5340 BLE Dev Board — firmware & example projects

This folder is the public home for the board's firmware source. The board
page's "View source" buttons point here.

| Project          | What it is                                                              | Status      |
|------------------|-------------------------------------------------------------------------|-------------|
| `Demo/`          | The BLE demo the board ships with                                        | coming soon |
| `PowerProfile/`  | Sleep-current walkthrough — J2 header + System ON idle / System OFF      | coming soon |
| `QwiicBeacon/`   | Read a Qwiic SHT4x sensor and broadcast it over BLE advertising          | coming soon |

Until those land, see
[`../docs/DVT_Helper_nRF5340.zip`](../docs/DVT_Helper_nRF5340.zip) — the
board's bring-up and self-test firmware — for working example code touching
every subsystem: GPIO headers, ADC, the Qwiic I²C port, the QSPI flash
(including deep power-down), native USB CDC, BLE advertising with TX-power
control, and the System ON idle / System OFF low-power states.

## Building

Every project targets the **nRF Connect SDK v2.9+** (Zephyr) with sysbuild:

```
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild
```

The board is pin-compatible with the official nRF5340-DK for `uart0`, `i2c1`,
and the QSPI flash, so DK samples run as-is. The one board-specific setting —
already present in the helper's `prj.conf` — is the LFXO internal load
capacitance:

```
CONFIG_SOC_LFXO_CAP_INT_6PF=y
```

**Flash from the terminal** (probe-rs, `west flash --runner pyocd`, or
OpenOCD) — the nRF Connect VS Code extension's Flash/Debug buttons expect a
J-Link and won't see the board's onboard CMSIS-DAP probe. Debug in VS Code
via the Cortex-Debug extension. Full commands, the first-connect unlock, and
network-core flashing are in
[`../docs/DVT_Firmware_Guide.docx`](../docs/DVT_Firmware_Guide.docx).
