# Crescent City (ESP32-S3 dev board): firmware

Source code for the firmware on Crescent City. The board page's "View source"
buttons point here. All code here is MIT licensed (see [LICENSE](../LICENSE)).

## What's here

| Folder | What it is |
|---|---|
| [`WeatherClock/`](WeatherClock/) | **The firmware the board ships with.** Wi-Fi weather, clock and battery gauge on the OLED, with low-battery protection. Indoor temperature/humidity with an optional Qwiic SHT4x sensor (not included). |
| [`esphome/`](esphome/) | ESPHome configuration for Home Assistant. Install it from your browser on the [ESPHome page](https://docs.hwdesigns.us/esp32-s3/esphome/). |
| [`QwiicSensor/`](QwiicSensor/) | Example: live readings and a 2-minute chart from a Qwiic SHT4x temperature/humidity sensor. |
| [`BatteryLogger/`](BatteryLogger/) | Example: battery-powered logger that sleeps between readings and keeps 24 h of data; BOOT shows the screen. |

## Weather Clock

### First-time setup

Setup is done on the board itself, with no app or account needed:

1. Join the Wi-Fi network **HW-WeatherClock** from a phone or laptop.
2. A setup page opens (or browse to 192.168.4.1).
3. Pick your Wi-Fi network, enter the password, then your location: country,
   state or province, and city (a ZIP or postal code also works), or
   "latitude, longitude". Choose F or C.
4. The board shows the place it found. If it finds nothing, the setup page
   opens again so you can correct the entry.

Weather data comes from Open-Meteo (no API key). Time and daylight saving
follow the location you enter.

### Buttons

- Short press **BOOT**: info page (location, firmware version, IP address,
  battery).
- Hold **BOOT** for 3 seconds: clear the settings and run setup again.

### On battery

On battery the Weather Clock switches the Wi-Fi radio off between weather
updates (every 10 minutes) and runs the processor at 80 MHz, so the board
draws about 41 mA instead of about 70 mA. It is not reachable on your network
between updates. "Battery low / charge me" on the screen means: plug in USB.
The board sleeps until you do. On USB it stays connected.

What changed in each version: [`WeatherClock/CHANGELOG.md`](WeatherClock/CHANGELOG.md).

## Re-flash or build it yourself

Use the Arduino IDE with the **esp32 by Espressif Systems** core. Tested with
arduino-esp32 **3.3.6**. Open `WeatherClock/WeatherClock.ino`, choose the board
settings below, install the libraries and upload.

To run ESPHome instead, use the [ESPHome page](https://docs.hwdesigns.us/esp32-s3/esphome/).

<details>
<summary>Board settings (Tools menu)</summary>

- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Enabled**
- Flash Size: **8MB (64Mb)**
- Partition Scheme: **8M with spiffs (3MB APP/1.5MB SPIFFS)**
- Serial Monitor: 115200 baud

</details>

<details>
<summary>Libraries (Library Manager)</summary>

- **U8g2**
- **WiFiManager** (tzapu)
- **ArduinoJson** (v7)
- **Adafruit SHT4x** (also installs Adafruit BusIO and Adafruit Unified Sensor)

The RGB LED uses the core's built-in `neopixelWrite()`, so no NeoPixel library
is needed.

</details>

<details>
<summary>Battery calibration (optional, Serial Monitor)</summary>

The battery reading is corrected by an offset stored on the board (default
0.070 V). It survives the BOOT-button reset. To set it, measure the battery
connector J2 with a meter and type in the Serial Monitor (115200 baud):

- `cal 3.812`: the measured J2 voltage, here 3.812 V
- `cal reset`: back to the default
- `info`: prints firmware, location and battery readings

</details>

## Examples

Both examples need a Qwiic / STEMMA QT SHT40, SHT41 or SHT45 sensor (not
included) on the side connector, the same board settings as the Weather Clock,
and the **U8g2** and **Adafruit SHT4x** libraries. Neither uses Wi-Fi.

<details>
<summary>What the examples do</summary>

- **QwiicSensor**: temperature and humidity at the top of the screen, a chart
  of the last 2 minutes below. BOOT switches the chart. Readings also go to the
  Serial Plotter.
- **BatteryLogger**: on battery it wakes every 15 minutes, logs one reading and
  goes back to deep sleep. Press BOOT to see the screen and a chart of the last
  24 hours. On USB it stays awake; type `d` in the Serial Monitor for the log
  as CSV. It stops logging and sleeps when the battery is low, and wakes when
  USB is plugged in.

</details>

For example code touching every subsystem (OLED, Qwiic I²C, RGB LED,
battery/USB sensing, Wi-Fi, BLE and deep sleep), see the board's bring-up and
self-test sketch [`DVT_Helper/DVT_Helper.ino`](DVT_Helper/DVT_Helper.ino).

## ESPHome

`esphome/crescent-city.yaml` turns the board into a Home Assistant display and
sensor node. The easiest way to install it is the
[ESPHome page](https://docs.hwdesigns.us/esp32-s3/esphome/), straight from the
browser.

<details>
<summary>Use it as a package in ESPHome Builder</summary>

```yaml
packages:
  hwdesigns.crescent-city: github://HW-DESIGNS-LLC/docs/firmware/esp32-s3/esphome/crescent-city.yaml@main
```

</details>

## Adding a sketch

Keep each sketch in a folder matching its `.ino` file name, as the Arduino IDE
requires (`WeatherClock/WeatherClock.ino`).
