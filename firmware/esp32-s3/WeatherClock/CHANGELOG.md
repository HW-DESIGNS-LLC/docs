# Weather Clock changelog

## 2.1.6 (2026-10-28)

- **Charging icon:** on USB with the battery already full, or with no battery
  fitted, the charger stays idle and the icon used to keep showing
  "charging". It now shows 4 bars after 20 minutes ("idle" on the info page).
  Replayed on four recorded V2.0 charge logs (not yet checked live): no
  change to when "topping off" and "full" appear.

## 2.1.5 (2026-10-28)

Tested on a Crescent City V2.0 board.

- **Location setup:** country, state or province, and city (a ZIP or postal
  code also works), or "latitude, longitude". The board shows the place it
  found, and reopens the setup page if it finds nothing. US state and Canadian
  province codes work with or without the country filled in.
- **Info page:** a short press on BOOT shows the location, firmware version,
  IP address and battery for 8 seconds.
- **Battery saver:** on battery the Wi-Fi radio is off between weather updates
  and the processor runs at 80 MHz: about 41 mA, roughly 24 hours per
  1000 mAh (was about 70 mA). On USB the board stays connected.
- **Low-battery shutdown:** "LOW" warning at 3.45 V, then "Battery low /
  charge me" and deep sleep at 3.30 V (measured at the battery connector).
  Plugging in USB wakes the board; a 10-minute check backs this up. After a
  shutdown, a short USB connection that does not recharge the battery sends
  the board back to sleep.
- **Battery gauge:** on battery the percentage is the charge left before the
  shutdown point. On USB the icon shows charging, topping off (3 bars, 4th
  blinking) and full.
- **Clock:** the time sync is retried until it succeeds, so the clock is right
  within seconds after a start on battery.
- **Calibration:** optional per-board battery-reading offset, set over the
  Serial Monitor (`cal <volts>`, `cal reset`, `info`).

## 1.0 (2026-10-06)

- First public release with the board's Crescent City name.
