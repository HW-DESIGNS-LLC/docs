# ESP32-S3 Dev Board — firmware & example sketches

This folder is the public home for the board's firmware source. The board
page's "View source" buttons point here.

| Sketch | What it is | Status |
|--------------|-----------------------------------------------------------------|-------------|
| `WeatherClock/`  | The demo the board ships with — Wi-Fi weather + clock on the OLED       | coming soon |
| `QwiicSensor/`   | Chart a Qwiic SHT4x temperature/humidity sensor on the screen           | coming soon |
| `BatteryLogger/` | Qwiic sensor + OLED with deep sleep between readings, for battery life  | coming soon |

Until those sketches land, see
[`../docs/DVT_Helper.ino`](../docs/DVT_Helper.ino) — the board's bring-up and
self-test firmware — for working example code touching every subsystem: the
OLED (U8g2, 4-wire SPI), the Qwiic I²C port, the RGB LED, ADC battery/USB
sensing, Wi-Fi, BLE, and deep sleep.

## Adding a sketch

Keep each sketch in a folder matching its `.ino` filename — the Arduino IDE
requires it (`WeatherClock/WeatherClock.ino`).

Build settings for every sketch (Arduino IDE → Tools): Board **ESP32S3 Dev
Module** · USB CDC On Boot **Enabled** · Flash Size **8MB (64Mb)** · Partition
Scheme **8M with spiffs** · PSRAM **Disabled**. The only library needed from
the Library Manager is **U8g2**; everything else ships with the arduino-esp32
core (3.x).
