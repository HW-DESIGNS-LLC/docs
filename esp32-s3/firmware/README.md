# Crescent City (ESP32-S3 dev board): firmware

This folder is the public home for the board's firmware source. The board
page's "View source" buttons point here. All code here is MIT licensed (see
[LICENSE](LICENSE)).

| Folder | What it is | Status |
|---|---|---|
| [`WeatherClock/`](WeatherClock/) | The firmware the board ships with: Wi-Fi weather, clock, indoor temperature/humidity and battery gauge on the OLED, with low-battery protection | available |
| [`esphome/`](esphome/) | ESPHome configuration for Home Assistant | available |
| [`QwiicSensor/`](QwiicSensor/) | Example: live readings and a 2-minute chart from a Qwiic SHT4x temperature/humidity sensor | available |
| [`BatteryLogger/`](BatteryLogger/) | Example: battery-powered logger that sleeps between readings and keeps 24 h of data; BOOT shows the screen | available |

For example code touching every subsystem (OLED, Qwiic I²C, RGB LED,
battery/USB sensing, Wi-Fi, BLE and deep sleep), see the board's bring-up and
self-test sketch [`../docs/DVT_Helper.ino`](../docs/DVT_Helper.ino).

## Weather Clock

First-time setup is done on the board itself, with no app or account needed:

1. Join the Wi-Fi network **HW-WeatherClock** from a phone or laptop.
2. A setup page opens (or browse to 192.168.4.1).
3. Pick your Wi-Fi network, enter the password, your city and F or C.

Hold **BOOT** for 3 seconds to clear the settings and run setup again.

Weather data comes from Open-Meteo (no API key). Time and daylight saving
follow the city you enter.

### Build it yourself (Arduino IDE)

- Core: **arduino-esp32 3.3.6**. Versions 3.3.7 to 3.3.10 have a Bluetooth
  regression.
- Tools menu: Board **ESP32S3 Dev Module**, USB CDC On Boot **Enabled**, Flash
  Size **8MB (64Mb)**, Partition Scheme **Default** (or any scheme with at
  least 1.5 MB for the app).
- Libraries (Library Manager): **U8g2**, **WiFiManager** (tzapu),
  **ArduinoJson** (v7), **Adafruit SHT4x** (also installs Adafruit BusIO and
  Adafruit Unified Sensor).

## Examples

Both examples need a Qwiic / STEMMA QT SHT40, SHT41 or SHT45 sensor on the
side connector, the same board settings as the Weather Clock, and the **U8g2**
and **Adafruit SHT4x** libraries. Neither uses Wi-Fi.

- **QwiicSensor**: temperature and humidity at the top of the screen, a chart
  of the last 2 minutes below. BOOT switches the chart. Readings also go to the
  Serial Plotter.
- **BatteryLogger**: on battery it wakes every 15 minutes, logs one reading and
  goes back to deep sleep. Press BOOT to see the screen and a chart of the last
  24 hours. On USB it stays awake; type `d` in the Serial Monitor for the log
  as CSV. It stops logging and sleeps when the battery is low, and wakes when
  USB is plugged in.

## ESPHome

`esphome/crescent-city.yaml` turns the board into a Home Assistant display and
sensor node with the same low-battery protection. Use it in ESPHome Builder, or
include it as a package:

```yaml
packages:
  hwdesigns.crescent-city: github://HW-DESIGNS-LLC/docs/esp32-s3/firmware/esphome/crescent-city.yaml@main
```

## Adding a sketch

Keep each sketch in a folder matching its `.ino` file name, as the Arduino IDE
requires (`WeatherClock/WeatherClock.ino`).
