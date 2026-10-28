/*
 * Crescent City - Weather Clock (the firmware the board ships with)
 * ============================================================================
 * A self-contained WiFi weather clock for the HW Designs ESP32-S3 board
 * (Crescent City). It shows outdoor conditions (Open-Meteo, no API key
 * required), an indoor temperature/humidity reading from an SHT4x on the Qwiic
 * connector, an NTP-synced clock, and a battery gauge on the 0.96" SSD1306 OLED.
 *
 * Setup is done entirely on-device through a WiFi captive portal - no cloud
 * account and no companion app required.
 *
 * FIRST-TIME SETUP
 *   1. On first boot the board creates a WiFi network named "HW-WeatherClock".
 *   2. Join it from a phone or laptop - a setup page opens automatically
 *      (or browse to http://192.168.4.1).
 *   3. Choose your WiFi network, enter the password, and fill in your location:
 *      Country, State / province and City (a ZIP / postal code also works in the
 *      City field). Optionally type "latitude, longitude" instead. Set the
 *      temperature unit (F or C). The board saves this and connects.
 *   4. The board looks the place up and shows the result ("City, State, CC").
 *      If the place cannot be found, the screen says so and the setup page
 *      opens again (join "HW-WeatherClock" again and open the "Setup" page).
 *   The clock's time zone comes from the location, so a correct location also
 *   gives the correct local time.
 *
 * BOOT BUTTON
 *   - Short press: info page for 8 s (location, firmware version, IP address,
 *     battery voltage, charging state).
 *   - Hold 3 s: clear WiFi and settings and reopen the setup portal.
 *
 * BATTERY ICON
 *   - On battery: 0-4 bars from the battery voltage.
 *   - On USB (bolt shown): the battery voltage reads high while charging, so no
 *     level is shown from it. Bars run in a loop while charging; 3 bars with the
 *     4th blinking while the charger tops off at 4.2 V; 4 steady bars when the
 *     charger stops (the reading drops), or when the reading stays still below
 *     the top-off level for 20 min: the charger is idle because the battery is
 *     already full or no battery is fitted (the CHG LED is off in both cases).
 *     Thresholds: CHARGE STATE block below.
 *
 * LOW BATTERY (running on battery only; ignored while USB is plugged in)
 *   - Battery nearly empty: the battery icon blinks and "LOW" appears next to it.
 *   - Battery empty: the screen shows "Battery low / charge me" for 10 s, then
 *     the board switches WiFi, the RGB LED and the screen off and goes into
 *     deep sleep. Plug in USB to wake it and charge; it starts up normally.
 *     As a backup for the USB wake, the board also wakes briefly every
 *     USB_POLL_S seconds to check for USB and goes straight back to sleep.
 *     Pressing RESET (EN) on a still-empty battery shows the message again.
 *   Thresholds are in the LOW-BATTERY CUTOFF block below.
 *
 * WIFI ON BATTERY (battery saver)
 *   On battery the WiFi radio is switched off between weather updates: every
 *   10 min the board connects (a few seconds), fetches the weather, syncs the
 *   clock once an hour, and switches the radio off again; the CPU also runs at
 *   80 MHz on battery. This cuts the average current (about 41 mA instead of
 *   about 70 mA on V2.0 boards) and stops network traffic from keeping the
 *   radio awake. On USB the board stays connected at 240 MHz. Set
 *   BATT_WIFI_MODE below: 0 = always connected (firmware 2.0 behaviour),
 *   1 = always connected with maximum modem sleep, 2 = radio off between
 *   updates (default).
 *
 * BATTERY CALIBRATION (per board, optional)
 *   The battery reading is corrected by an offset stored on the board (NVS
 *   namespace "hwcal", key "vbat_off"; default 0.070 V). It survives the
 *   BOOT-button reset. To set it on the bench, measure the battery connector J2
 *   (+ to board GND) with a meter and type in the Serial Monitor:
 *       cal 3.812        (the measured J2 voltage)
 *       cal reset        (back to the default)
 *       info             (prints firmware, location and battery readings)
 *
 * BUILD (Arduino IDE)
 *   Board:            "ESP32S3 Dev Module"
 *   USB CDC On Boot:  Enabled
 *   Flash Size:       8MB
 *   Partition Scheme: Default (or any scheme with >= 1.5 MB app space)
 *   Serial Monitor:   115200 baud
 *   Core:             esp32 by Espressif Systems 3.3.6 (not 3.3.7-3.3.10)
 *
 * LIBRARIES (Library Manager)
 *   U8g2, WiFiManager (tzapu), ArduinoJson (v7),
 *   Adafruit SHT4x (pulls in Adafruit BusIO + Adafruit Unified Sensor).
 *   The RGB LED uses the core's built-in neopixelWrite() - no NeoPixel library.
 *
 * The indoor sensor is isolated behind sensorInit()/sensorRead() (marked
 * [SWAP]) so a different I2C temp/RH part can be dropped in without touching
 * the rest of the sketch.
 *
 * Version 2.1.6 (2026-10-28) - see CHANGELOG.md
 * Copyright (c) 2026 HW DESIGNS LLC - https://docs.hwdesigns.us
 * SPDX-License-Identifier: MIT (see the LICENSE file in the repository)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_SHT4x.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <esp_sleep.h>                    // low-battery deep sleep
#include <esp_sntp.h>                     // wait for the clock sync before the radio goes off
#include <esp_system.h>                   // esp_reset_reason()
#include <driver/gpio.h>                  // pad hold during deep sleep

// ==================== BUILD VARIANT ====================
// TEST_BUILD 1 = battery-test build (WeatherClock_S3_HB): heartbeat on header
// pin 21 and a status log on the header TX pin. The shipping build is 0.
#ifndef TEST_BUILD
#define TEST_BUILD 0
#endif

#define FW_VERSION    "2.1.6"
#define FW_DATE       "2026-10-28"

// ==================== USER-FACING CONFIG ====================
#define DEMO_MODE     false               // true = show sample data, WiFi off
#define WIFI_AP_NAME  "HW-WeatherClock"   // captive-portal SSID for setup

// ---- Display: SSD1306 0.96" over 4-wire software (bit-banged) SPI ----
// Pin map verified against the board's as-built PCB netlist.
#define PIN_OLED_SCLK   12   // FPC.18 -> U2.20
#define PIN_OLED_MOSI   11   // FPC.19 -> U2.19
#define PIN_OLED_RST    14   // FPC.14 -> U2.22
#define PIN_OLED_DC      9   // FPC.15 -> U2.17
#define PIN_OLED_CS     10   // FPC.13 -> U2.18

// ---- Qwiic / I2C (J3) ----
#define PIN_I2C_SDA      8   // J3.3 -> U2.12
#define PIN_I2C_SCL     18   // J3.4 -> U2.11

// ---- Addressable RGB (SK6812) ----
#define PIN_RGB         48   // U2.25 -> U3.4
#define LED_BRIGHTNESS  30   // 0-255 (applied in software)

// ---- Power sensing ----
#define PIN_VBAT_SENSE   4   // R10/R11 divider from BATT+ -> U2.4
#define PIN_VBUS_SENSE   2   // R15/R16 divider from V_USB -> U2.38
#define VBAT_DIV   2.0f      // 100k/100k
#define VBUS_DIV   2.0f
// Note: the MCP73831 charge-status pin drives the CHG LED only and is not
// wired to a GPIO on this board, so the charge state is estimated from the
// battery voltage (CHARGE STATE block).

// ==================== LOW-BATTERY CUTOFF ====================
// Voltages are the battery voltage at the battery connector J2 (+ vs. board
// GND): the ADC reading plus the calibration offset. Acts only on battery.
// Thresholds set from full battery discharges of V2.0 boards: the cutoff
// leaves a margin above the point where the 3.3 V rail starts to sag.
#define LOWBATT_ENABLE      true     // false = no warning, no cutoff (baseline battery runs)
#define VBAT_CAL_OFFSET     0.070f   // V; default offset when the board has none stored
#define VBAT_WARN_V         3.45f    // filtered reading below this: "LOW" warning on the OLED
#define VBAT_WARN_CLEAR_V   3.55f    // ...warning clears above this (hysteresis)
#define VBAT_CUTOFF_V       3.30f    // filtered reading below this: shut down to deep sleep
#define VBAT_RESUME_V       3.50f    // at boot on battery: below this, go straight back to sleep
#define VBAT_FILTER_N       10       // median of the last 10 readings = 30 s at POWER_MS (3 s)
#define LOWBATT_MSG_MS      10000    // "Battery low" stays on screen this long before sleep
#define PORTAL_BATT_S       300      // on battery, the setup portal gives up after 5 min idle
#define USB_POLL_S          600      // in low-battery sleep, also wake every 10 min to check USB

// ==================== WIFI ON BATTERY ====================
#define BATT_WIFI_MODE      2        // 0 always on, 1 always on + max modem sleep, 2 off between updates
#define WIFI_CONNECT_MS     15000    // on-demand connect gives up after this
#define NTP_WAIT_MS         8000     // on-demand: wait this long at most for the clock sync
                                     // (4 s was sometimes too short for the first sync)

// ==================== CHARGE STATE (on USB) ====================
// On USB the battery reading is the charger output, which sits above the cell
// by charge current x path resistance, so it cannot show the charge level.
// Instead the phase is estimated from 1-minute averages of the reading.
// Set from three V2.0 charges (18650 cell, ~300 mA): while topping off, the
// 1-min average wanders up to +/-12 mV around its level (bumps of ~20 min).
// When the charger stops, the reading drops by an amount that depends on the
// resistance between the board and the cell (about 20 to 90 mV in tests; less
// with a cell plugged straight into the connector). So: a slow-moving top-off
// level that ignores bumps and dips, and "full" at 10 mV below it for 3 min.
// In tests, "full" came 2-3 min after the charger stopped. 10 s noise ~1 mV.
#define CHG_CV_V        4.15f    // 1-min average at/above this and flat -> topping off
#define CHG_FLAT_V      0.004f   // ...flat = changed less than this over CHG_WIN_MIN minutes
#define CHG_WIN_MIN     10       // minutes of history used for "flat"
#define CHG_FULL_MIN    600      // fallback only: show full after 10 h topping off (the drop normally comes first)
#define CHG_DROP_V      0.010f   // 1-min average this far below the top-off level...
#define CHG_DROP_MIN    3        // ...for this many minutes in a row -> charger stopped -> full
#define CHG_RECHARGE_V  4.00f    // shown full but 1-min average below this -> charging again
// Charger idle (battery already full when USB was plugged in, or no battery):
// the reading sits still below the top-off level. While charging, the 1-min
// averages always spread over 18 mV or more in 20 min (three V2.0 charges);
// an idle charger stays within about 3 mV.
#define CHG_IDLE_MIN    20       // minutes of history used for "idle"
#define CHG_IDLE_SPREAD 0.006f   // max - min of the 1-min averages over CHG_IDLE_MIN at or below this...
#define CHG_IDLE_LO     4.00f    // ...with the average between this and CHG_CV_V -> idle
#define CHG_IDLE_MOVE   0.015f   // idle, then the average moves this far -> charging again
#define CHG_HIST        (CHG_IDLE_MIN > CHG_WIN_MIN ? CHG_IDLE_MIN + 1 : CHG_WIN_MIN + 1)

// ---- User button ----
#define PIN_BUTTON       0   // BOOT (SW4) -> U2.27
#define INFO_MS       8000   // short press shows the info page this long

#if TEST_BUILD
#define PIN_HEARTBEAT   21   // header J5.10 -> logic input of the current profiler
#define HB_PERIOD_MS  5000   // toggle period while working (square wave, 10 s cycle)
#define HB_LOG_MS    10000   // UART0 status line interval
#endif

// SSD1306 driver (raw 0.96" panel via 30-pin FPC, BS0-2 grounded = 4-wire SPI)
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
    U8G2_R0, PIN_OLED_SCLK, PIN_OLED_MOSI, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RST);

// ==================== intervals ====================
const uint32_t WEATHER_MS  = 10UL * 60 * 1000;  // refresh weather every 10 min
const uint32_t WX_RETRY_MS =          15000;    // ...retry every 15s until first success
const uint32_t WX_RETRY_OD_MS =       60000;    // ...every 60 s when the radio is off between updates
const uint32_t NTP_MS      = 60UL * 60 * 1000;
const uint32_t POWER_MS    =        3UL * 1000;
const uint32_t SENSOR_MS   =       10UL * 1000; // indoor temp/RH read
const uint32_t DRAW_MS     =             250;

// ==================== config / state ====================
Preferences prefs;
char   cfgCountry[33] = "";               // "US", "United States", "GB", ...
char   cfgState[33]   = "";               // "Louisiana" or "LA"; may be empty
char   cfgCity[48]    = "";               // city name or ZIP / postal code
char   cfgLatLon[32]  = "";               // optional "29.95, -90.07" override
char   cfgUnits[2]    = "F";              // 'F' or 'C'
char   place[64]      = "";               // resolved "City, State, CC"
double lat = 0, lon = 0;
long   utcOffset = 0;                      // seconds, from Open-Meteo
bool   locError = false;                   // location could not be found
float  vbatCal = VBAT_CAL_OFFSET;          // per-board battery reading offset
uint32_t infoUntil = 0;                    // info page shown until this time

// Types used in function signatures are declared here, before the first
// function, so the Arduino IDE's automatic prototypes compile.
enum ChgState : uint8_t { CHG_OFF, CHG_CC, CHG_CV, CHG_FULL, CHG_IDLE };
enum GeoResult { GEO_OK, GEO_NOT_FOUND, GEO_NET_FAIL };

struct Weather { float tempC=NAN, feelsC=NAN, hum=NAN, hiC=NAN, loC=NAN; int code=-1; bool ok=false; } wx;
struct Power   { bool usb=false; int pct=0; float vbat=0, vraw=0; } pw;   // vbat = calibrated (J2), vraw = ADC x divider
struct Indoor  { float tC=NAN, rh=NAN; bool ok=false; } in_;

uint32_t tWeather=0, tNtp=0, tPower=0, tSens=0, tDraw=0;
float battEMA = -1;

// ==================== logging ====================
// Event log on the USB serial port; the test build also copies it to the
// header TX pin (prefixed "[HB] ") so it lands in the bench log.
static void logmsg(const char* fmt, ...){
  char buf[192];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  Serial.print(buf);
#if TEST_BUILD
  Serial0.print("[HB] "); Serial0.print(buf);
#endif
}

// ==================== helpers ====================
static String urlEncode(const char* s){
  String o; char b[4];
  for (; *s; ++s){ char c=*s;
    if (isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.'||c=='~') o+=c;
    else { sprintf(b,"%%%02X",(unsigned char)c); o+=b; } }
  return o;
}
static String trimmed(const char* s){ String t(s); t.trim(); return t; }
static void copyStr(char* dst, size_t n, const char* src){ strncpy(dst, src, n-1); dst[n-1]=0; }
static float cToDisp(float c){ return cfgUnits[0]=='F' ? c*9.0f/5.0f+32.0f : c; }

static void hsv(float h,float s,float v,uint8_t&r,uint8_t&g,uint8_t&b){
  float i=floor(h*6), f=h*6-i, p=v*(1-s), q=v*(1-f*s), t=v*(1-(1-f)*s), R,G,B;
  switch(((int)i)%6){
    case 0:R=v;G=t;B=p;break; case 1:R=q;G=v;B=p;break; case 2:R=p;G=v;B=t;break;
    case 3:R=p;G=q;B=v;break; case 4:R=t;G=p;B=v;break; default:R=v;G=p;B=q;break; }
  r=R*255; g=G*255; b=B*255;
}

// ==================== RGB (built-in, no library) ====================
static void ledRGB(uint8_t r,uint8_t g,uint8_t b){
  neopixelWrite(PIN_RGB,
    (uint16_t)r*LED_BRIGHTNESS/255,
    (uint16_t)g*LED_BRIGHTNESS/255,
    (uint16_t)b*LED_BRIGHTNESS/255);      // core handles GRB order + RMT
}
static void updateLed(){
  if(!wx.ok){ neopixelWrite(PIN_RGB,0,0,10); return; }   // idle: dim blue
  float t = constrain(wx.tempC, 0.0f, 35.0f);
  float hue = (1.0f - t/35.0f) * 0.66f;                  // blue(cold) -> red(hot)
  uint8_t r,g,b; hsv(hue,1.0,1.0,r,g,b);
  ledRGB(r,g,b);
}

// ==================== INDOOR SENSOR (SHT4x on Qwiic, addr 0x44) ====================
// [SWAP] To substitute a different temp/RH part, replace ONLY sensorInit()
// [SWAP] and sensorRead(). Nothing else in the sketch touches the hardware.
Adafruit_SHT4x sht4;
bool shtOK = false;

static bool sensorInit(){                                 // [SWAP]
  if(!sht4.begin(&Wire)) return false;
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  return true;
}
static bool sensorRead(float &tC, float &rh){             // [SWAP]
  sensors_event_t h, t;
  if(!sht4.getEvent(&h, &t)) return false;
  tC = t.temperature; rh = h.relative_humidity;
  return true;
}
static void readIndoor(){
  if(!shtOK) shtOK = sensorInit();      // keeps retrying: Qwiic is hot-pluggable
  in_.ok = shtOK && sensorRead(in_.tC, in_.rh);
  if(in_.ok) Serial.printf("[in] %.1fC %.0f%%\n", in_.tC, in_.rh);
}

// ==================== POWER / BATTERY ====================
static float readVbatRaw(){                              // ADC x divider, uncorrected
  const int N=16; uint32_t acc=0;
  for(int i=0;i<N;i++){ acc+=analogReadMilliVolts(PIN_VBAT_SENSE); delayMicroseconds(200);}
  return (acc/(float)N)/1000.0f * VBAT_DIV;
}
static float readVbat(){ return readVbatRaw() + vbatCal; }   // battery voltage at J2
static bool usbPresent(){
  float v = analogReadMilliVolts(PIN_VBUS_SENSE)/1000.0f * VBUS_DIV;
  return v > 4.0f;
}
// Battery % on battery = charge left before the low-battery cutoff (3.30 V at
// J2 = 0 %). Table measured on a V2.0 board during a full discharge (Oct 2026):
// J2 voltage against the charge still to come out, weather-clock load ~41 mA,
// 18650 cell. A generic LiPo table read up to 22 points low near full and
// showed an empty icon for the last hours of a run.
static int lipoPct(float v){
  static const float P[][2]={{4.10,100},{4.02,95},{4.00,90},{3.98,80},{3.96,70},
    {3.92,60},{3.86,50},{3.82,40},{3.80,30},{3.75,20},{3.71,15},{3.68,10},
    {3.61,5},{3.47,2},{3.30,0}};
  if(v>=P[0][0])return 100;
  for(size_t i=0;i<sizeof(P)/sizeof(P[0])-1;i++){
    if(v<=P[i][0]&&v>=P[i+1][0]){
      float vh=P[i][0],ph=P[i][1],vl=P[i+1][0],pl=P[i+1][1];
      return (int)(pl+(v-vl)*(ph-pl)/(vh-vl)+0.5f);
    }
  }
  return 0;
}
static void readPower(){
  bool usb = usbPresent();
  if(usb != pw.usb) battEMA = -1;       // regime change: re-seed, don't crawl
  pw.usb  = usb;
  pw.vraw = readVbatRaw();
  pw.vbat = pw.vraw + vbatCal;
  int p = lipoPct(pw.vbat);
  if(battEMA<0) battEMA=p;
  battEMA = 0.8f*battEMA + 0.2f*p;
  pw.pct = (int)(battEMA+0.5f);
}
// 0..4 bars with hysteresis so ADC/IR jitter can't flicker the display.
// Gain a bar rising past up[]; lose it falling below down[]. Battery only.
static int battBars(){
  static int lv = -1;
  const int up[]   = {8,29,49,74};        // 0 bars only in the last 5 % (~2 h at 41 mA)
  const int down[] = {5,25,45,70};
  if(lv < 0){ lv = 0; while(lv < 4 && pw.pct > down[lv]) lv++; }
  while(lv < 4 && pw.pct >= up[lv])   lv++;
  while(lv > 0 && pw.pct <  down[lv-1]) lv--;
  return lv;
}

// Per-board calibration offset, kept in its own NVS namespace so the
// BOOT-button reset (which clears the clock settings) does not erase it.
static void loadCal(){
  Preferences c;
  if(c.begin("hwcal", true)){ vbatCal = c.getFloat("vbat_off", VBAT_CAL_OFFSET); c.end(); }
  else vbatCal = VBAT_CAL_OFFSET;
}
static void saveCal(float off){
  Preferences c; c.begin("hwcal", false); c.putFloat("vbat_off", off); c.end();
  vbatCal = off;
}
static void resetCal(){
  Preferences c; c.begin("hwcal", false); c.remove("vbat_off"); c.end();
  vbatCal = VBAT_CAL_OFFSET;
}

// ==================== CHARGE STATE (on USB) ====================
struct Charge {
  ChgState st = CHG_OFF;
  uint32_t tState = 0, tMin = 0;
  float sum = 0; int cnt = 0;
  float hist[CHG_HIST]; int nHist = 0;           // 1-minute averages, oldest first
  float avg = NAN, plateau = 0;                  // plateau: top-off level, or the idle level
  int below = 0;                                 // consecutive minutes below the drop level
} chg;

static const char* chgName(){
  switch(chg.st){ case CHG_CC: return "cc"; case CHG_CV: return "cv"; case CHG_FULL: return "full"; case CHG_IDLE: return "idle"; default: return "off"; }
}
static void chgSet(ChgState s){
  if(chg.st == s) return;
  chg.st = s; chg.tState = millis();
  logmsg("[chg] state %s, 1-min avg %.3f V\n", chgName(), chg.avg);
}
// Called after every readPower() (every POWER_MS).
static void chargeUpdate(){
  uint32_t now = millis();
  if(!pw.usb){
    if(chg.st != CHG_OFF){ chg = Charge(); logmsg("[chg] USB removed\n"); }
    return;
  }
  if(chg.st == CHG_OFF){ chg = Charge(); chg.st = CHG_CC; chg.tState = chg.tMin = now; logmsg("[chg] USB present, charging\n"); }
  chg.sum += pw.vbat; chg.cnt++;
  if(now - chg.tMin < 60000UL) return;
  chg.avg = chg.sum / chg.cnt; chg.sum = 0; chg.cnt = 0; chg.tMin = now;
  if(chg.nHist < CHG_HIST) chg.hist[chg.nHist++] = chg.avg;
  else { memmove(chg.hist, chg.hist + 1, (CHG_HIST - 1) * sizeof(float)); chg.hist[CHG_HIST - 1] = chg.avg; }
  const int last = chg.nHist - 1;
  bool haveWin = chg.nHist >= CHG_WIN_MIN + 1;
  float change = haveWin ? fabsf(chg.hist[last] - chg.hist[last - CHG_WIN_MIN]) : 1.0f;
  switch(chg.st){
    case CHG_CC:
      if(haveWin && chg.avg >= CHG_CV_V && change <= CHG_FLAT_V){ chg.plateau = chg.avg; chg.below = 0; chgSet(CHG_CV); }
      else if(chg.nHist >= CHG_IDLE_MIN + 1 && chg.avg >= CHG_IDLE_LO && chg.avg < CHG_CV_V){
        float lo = chg.hist[last], hi = lo;
        for(int i = last - CHG_IDLE_MIN; i < last; i++){ lo = fminf(lo, chg.hist[i]); hi = fmaxf(hi, chg.hist[i]); }
        if(hi - lo <= CHG_IDLE_SPREAD){ chg.plateau = chg.avg; chgSet(CHG_IDLE); }
      }
      break;
    case CHG_CV:
      // Top-off level follows the reading slowly, so a short bump cannot raise it.
      {                                                   // top-off level: follows small changes only,
        float dif = chg.avg - chg.plateau;                // rises slowly (bumps), falls a bit faster (drift)
        if(dif > -CHG_DROP_V / 2) chg.plateau += (dif > 0 ? 0.03f : 0.10f) * dif;
      }
      chg.below = (chg.avg <= chg.plateau - CHG_DROP_V) ? chg.below + 1 : 0;
      if(now - chg.tState >= CHG_FULL_MIN * 60000UL || chg.below >= CHG_DROP_MIN) chgSet(CHG_FULL);
      break;
    case CHG_FULL:
      if(chg.avg < CHG_RECHARGE_V) chgSet(CHG_CC);
      break;
    case CHG_IDLE:                                        // reading moved: charger started (battery fitted)
      if(fabsf(chg.avg - chg.plateau) > CHG_IDLE_MOVE){ chg.nHist = 0; chgSet(CHG_CC); }
      break;
    default: break;
  }
}
static int usbBars(){
  switch(chg.st){
    case CHG_CV:   return 3 + (int)((millis()/500) % 2);   // 3 steady, 4th blinks
    case CHG_FULL:
    case CHG_IDLE: return 4;                               // charger stopped or idle
    default:       return (int)((millis()/600) % 5);       // charging: bars run in a loop
  }
}
static int shownBars(){ return pw.usb ? usbBars() : battBars(); }

// ==================== LOW-BATTERY CUTOFF ====================
// Without this, a board on battery keeps running until the supply sags so far
// that it browns out and reset-loops (powered but not working) until the
// battery's own protection switches off. This block warns first, then puts the
// board into deep sleep with a clear message, and wakes it when USB is plugged in.
struct LowBatt { float buf[VBAT_FILTER_N]; int n=0, head=0; float vf=NAN; bool warn=false; } lb;
// Set at a low-battery shutdown, kept through deep sleep and restarts, cleared
// at power-on. While set, the board on battery needs VBAT_RESUME_V to keep
// running: a short USB connection that wakes it but does not recharge the cell
// sends it back to sleep after the 30 s filter instead of running it down to
// the cutoff again.
RTC_DATA_ATTR bool lbAfterCut = false;

static bool lowBattWarning(){ return LOWBATT_ENABLE && lb.warn && !pw.usb; }

// Release the pad holds set before the last deep sleep (no-op after power-on).
static void releaseSleepHolds(){
  gpio_hold_dis((gpio_num_t)PIN_RGB);
  gpio_hold_dis((gpio_num_t)PIN_OLED_CS);
  gpio_hold_dis((gpio_num_t)PIN_OLED_RST);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  gpio_deep_sleep_hold_dis();
#endif
}

// Park the pins that could wake a load during deep sleep: RGB data low (so the
// LED cannot latch a colour from noise), OLED chip-select high (panel ignores
// the bus) and OLED reset high (panel stays in its sleep state).
static void holdPinsForSleep(){
  pinMode(PIN_RGB, OUTPUT);      digitalWrite(PIN_RGB, LOW);
  pinMode(PIN_OLED_CS, OUTPUT);  digitalWrite(PIN_OLED_CS, HIGH);
  pinMode(PIN_OLED_RST, OUTPUT); digitalWrite(PIN_OLED_RST, HIGH);
  gpio_hold_en((gpio_num_t)PIN_RGB);
  gpio_hold_en((gpio_num_t)PIN_OLED_CS);
  gpio_hold_en((gpio_num_t)PIN_OLED_RST);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  gpio_deep_sleep_hold_en();
#endif
}

// Wake sources for the low-battery sleep: USB (GPIO2 high, an RTC GPIO) and,
// as a backup in case GPIO2 does not reach its logic-high level, a timer that
// checks USB on the ADC every USB_POLL_S seconds.
static void sleepUntilUsb(){
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_VBUS_SENSE, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup((uint64_t)USB_POLL_S * 1000000ULL);
  esp_deep_sleep_start();                                 // does not return
}

static void lowBatteryScreen(float v){
  u8g2.setPowerSave(0);
  u8g2.clearBuffer();
  u8g2.drawFrame(47,2,30,14); u8g2.drawBox(77,6,3,6);       // empty battery
  u8g2.setFont(u8g2_font_helvB10_tr);
  const char* l1 = "Battery low";
  u8g2.drawStr((128 - u8g2.getStrWidth(l1))/2, 34, l1);
  u8g2.setFont(u8g2_font_6x12_tr);
  const char* l2 = "charge me";
  u8g2.drawStr((128 - u8g2.getStrWidth(l2))/2, 50, l2);
  char vs[12]; snprintf(vs, sizeof(vs), "%.2f V", v);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr((128 - u8g2.getStrWidth(vs))/2, 63, vs);
  u8g2.sendBuffer();
}

// Show the message, switch everything off and deep-sleep until USB is plugged
// in or RESET is pressed.
static void lowBatteryShutdown(float v){
  lbAfterCut = true;
  logmsg("[batt] %.3f V at J2 - low battery, shutting down\n", v);
#if TEST_BUILD
  Serial0.printf("[HB] lowbatt shutdown vf=%.3f\n", v);
#endif
  if(WiFi.getMode() != WIFI_OFF){ WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF); } // keeps saved WiFi
  neopixelWrite(PIN_RGB,0,0,0);
  lowBatteryScreen(v);
  for(uint32_t t0=millis(); millis()-t0 < LOWBATT_MSG_MS; ){
    if(usbPresent()){ logmsg("[batt] USB plugged in - restarting\n"); delay(50); ESP.restart(); }
    delay(100);
  }
  u8g2.setPowerSave(1);                                   // OLED display off (sleep)
  holdPinsForSleep();
  Serial.flush();
#if TEST_BUILD
  Serial0.flush();
#endif
  sleepUntilUsb();
}

// Timer wake from the low-battery sleep: check USB before anything else
// (no screen, no WiFi); without USB go straight back to sleep. The pin holds
// set before the sleep are still active, so nothing else needs doing.
static void timerWakeCheck(){
  if(esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) return;
  analogSetPinAttenuation(PIN_VBUS_SENSE, ADC_11db);
  if(!usbPresent()) sleepUntilUsb();
}

// Boot-time check (before WiFi starts): a battery that is still empty after a
// reset goes straight back to sleep instead of starting WiFi.
static void bootBatteryCheck(){
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  if(c == ESP_SLEEP_WAKEUP_EXT1)  logmsg("[boot] woke from low-battery sleep: USB plugged in\n");
  if(c == ESP_SLEEP_WAKEUP_TIMER) logmsg("[boot] woke from low-battery sleep: USB found on timer check\n");
  if(!LOWBATT_ENABLE || usbPresent()) return;
  float acc = 0; const int N = 10;
  for(int i=0;i<N;i++){ acc += readVbat(); delay(50); }
  float v = acc / N;
  logmsg("[batt] boot check %.3f V at J2 (resume above %.2f V)\n", v, VBAT_RESUME_V);
  if(v < VBAT_RESUME_V) lowBatteryShutdown(v);
  lbAfterCut = false;                                     // charged enough on battery
}

// Called after every readPower() (every POWER_MS). Median of the last
// VBAT_FILTER_N readings rejects WiFi-burst dips; nothing happens until the
// window is full, so the first decision comes 30 s after boot or unplugging USB.
static void lowBattUpdate(){
  if(pw.usb){ lb.n = 0; lb.head = 0; lb.vf = NAN; lb.warn = false; return; }
  lb.buf[lb.head] = pw.vbat; lb.head = (lb.head + 1) % VBAT_FILTER_N;
  if(lb.n < VBAT_FILTER_N){ lb.n++; if(lb.n < VBAT_FILTER_N) return; }
  float s[VBAT_FILTER_N];
  memcpy(s, lb.buf, sizeof(s));
  for(int i=1;i<VBAT_FILTER_N;i++){ float k=s[i]; int j=i-1; while(j>=0 && s[j]>k){ s[j+1]=s[j]; j--; } s[j+1]=k; }
  lb.vf = 0.5f * (s[(VBAT_FILTER_N-1)/2] + s[VBAT_FILTER_N/2]);
  if(!LOWBATT_ENABLE) return;
  if(lbAfterCut){
    if(lb.vf >= VBAT_RESUME_V) lbAfterCut = false;      // recharged enough: normal rules again
    else {
      logmsg("[batt] on battery at %.3f V after a low-battery shutdown (resume above %.2f V) - back to sleep\n",
             lb.vf, VBAT_RESUME_V);
      lowBatteryShutdown(lb.vf);
    }
  }
  if(!lb.warn && lb.vf < VBAT_WARN_V)            { lb.warn = true;  logmsg("[batt] low warning, %.3f V\n", lb.vf); }
  else if(lb.warn && lb.vf > VBAT_WARN_CLEAR_V)  { lb.warn = false; }
  if(lb.vf < VBAT_CUTOFF_V) lowBatteryShutdown(lb.vf);
}

// ==================== TIME ====================
// Clock sync. sntp_get_sync_status() resets to RESET after it once returns
// COMPLETED, so a second read always says "not done" - use a callback flag.
static volatile bool ntpDone = false;
static void onNtpSync(struct timeval*){ ntpDone = true; }
static bool clockSet(){ return time(nullptr) > 1704067200; }   // after 1 Jan 2024 = synced at least once
static void syncTime(){
  ntpDone = false;
  sntp_set_time_sync_notification_cb(onNtpSync);
  configTime(0,0,"pool.ntp.org","time.nist.gov");     // non-blocking
}
static struct tm localNow(){
  time_t t = time(nullptr) + utcOffset;
  struct tm tm; gmtime_r(&t,&tm); return tm;
}

// ==================== HTTP (Open-Meteo, keyless, plain HTTP) ====================
static bool httpGetJson(const String& url, JsonDocument& doc, JsonDocument* filter = nullptr){
  if(WiFi.status()!=WL_CONNECTED) return false;
  uint32_t t0 = millis();
  WiFiClient client;                        // plain HTTP - no TLS handshake
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.begin(client, url);
  int code=http.GET();
  bool ok=false;
  if(code==200){
    String payload = http.getString();      // read full body first (reliable)
    DeserializationError e = filter
      ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
      : deserializeJson(doc, payload);
    if(e) logmsg("[json] parse error: %s\n", e.c_str());
    else  ok=true;
    logmsg("[http] 200, %u bytes, %lu ms, heap %u\n", (unsigned)payload.length(),
         (unsigned long)(millis()-t0), (unsigned)ESP.getFreeHeap());
  } else {
    logmsg("[http] GET failed: %d (%s), %lu ms\n", code, http.errorToString(code).c_str(),
         (unsigned long)(millis()-t0));
  }
  http.end();
  return ok;
}

// ==================== LOCATION (country / state / city) ====================
// US states and Canadian provinces may be typed as their 2-letter codes;
// everything else is matched against the full name Open-Meteo returns.
static const char* const REGION_CODES[][3] = {
  {"US","AL","Alabama"},{"US","AK","Alaska"},{"US","AZ","Arizona"},{"US","AR","Arkansas"},
  {"US","CA","California"},{"US","CO","Colorado"},{"US","CT","Connecticut"},{"US","DE","Delaware"},
  {"US","DC","District of Columbia"},{"US","FL","Florida"},{"US","GA","Georgia"},{"US","HI","Hawaii"},
  {"US","ID","Idaho"},{"US","IL","Illinois"},{"US","IN","Indiana"},{"US","IA","Iowa"},
  {"US","KS","Kansas"},{"US","KY","Kentucky"},{"US","LA","Louisiana"},{"US","ME","Maine"},
  {"US","MD","Maryland"},{"US","MA","Massachusetts"},{"US","MI","Michigan"},{"US","MN","Minnesota"},
  {"US","MS","Mississippi"},{"US","MO","Missouri"},{"US","MT","Montana"},{"US","NE","Nebraska"},
  {"US","NV","Nevada"},{"US","NH","New Hampshire"},{"US","NJ","New Jersey"},{"US","NM","New Mexico"},
  {"US","NY","New York"},{"US","NC","North Carolina"},{"US","ND","North Dakota"},{"US","OH","Ohio"},
  {"US","OK","Oklahoma"},{"US","OR","Oregon"},{"US","PA","Pennsylvania"},{"US","RI","Rhode Island"},
  {"US","SC","South Carolina"},{"US","SD","South Dakota"},{"US","TN","Tennessee"},{"US","TX","Texas"},
  {"US","UT","Utah"},{"US","VT","Vermont"},{"US","VA","Virginia"},{"US","WA","Washington"},
  {"US","WV","West Virginia"},{"US","WI","Wisconsin"},{"US","WY","Wyoming"},{"US","PR","Puerto Rico"},
  {"CA","AB","Alberta"},{"CA","BC","British Columbia"},{"CA","MB","Manitoba"},{"CA","NB","New Brunswick"},
  {"CA","NL","Newfoundland and Labrador"},{"CA","NS","Nova Scotia"},{"CA","NT","Northwest Territories"},
  {"CA","NU","Nunavut"},{"CA","ON","Ontario"},{"CA","PE","Prince Edward Island"},{"CA","QC","Quebec"},
  {"CA","SK","Saskatchewan"},{"CA","YT","Yukon"},
};

// ISO-3166 alpha-2 code from what the user typed ("" if it is a country name
// that is not in the short alias list; then the name itself is matched).
static String countryCodeOf(const String& in){
  String s = in; s.trim();
  if(s.length()==2 && isalpha((unsigned char)s[0]) && isalpha((unsigned char)s[1])){ s.toUpperCase(); return s; }
  String l = s; l.toLowerCase(); l.replace(".", "");
  if(l=="usa"||l=="united states"||l=="united states of america"||l=="america") return "US";
  if(l=="uk"||l=="united kingdom"||l=="great britain"||l=="britain"||l=="england"||l=="scotland"||l=="wales") return "GB";
  if(l=="canada") return "CA";
  return "";
}
static String expandRegion(const String& st, const String& cc){
  if(st.length()==2){
    for(auto& r : REGION_CODES)
      if(cc.equalsIgnoreCase(r[0]) && st.equalsIgnoreCase(r[1])) return String(r[2]);
  }
  return st;
}
static bool parseLatLon(const char* s, double& a, double& b){
  if(!s || !*s) return false;
  if(sscanf(s, "%lf , %lf", &a, &b) != 2 && sscanf(s, "%lf %lf", &a, &b) != 2) return false;
  return a >= -90 && a <= 90 && b >= -180 && b <= 180 && !(a == 0 && b == 0);
}
static void saveLocationResult(){
  prefs.putDouble("lat",lat); prefs.putDouble("lon",lon);
  prefs.putString("place", place);
}

static GeoResult geocode(){
  double a, b;
  if(parseLatLon(cfgLatLon, a, b)){
    lat = a; lon = b;
    snprintf(place, sizeof(place), "%.3f, %.3f", a, b);
    saveLocationResult();
    logmsg("[geo] using coordinates %.4f, %.4f\n", lat, lon);
    return GEO_OK;
  }
  String city = trimmed(cfgCity), country = trimmed(cfgCountry), state = trimmed(cfgState);
  if(city.length() < 2){ logmsg("[geo] no city entered\n"); return GEO_NOT_FOUND; }
  String cc = countryCodeOf(country);
  if(cc.length()==0 && country.length()==0 && state.length()){
    // Country left empty but the state is a US state or Canadian province (code
    // or full name): search that country. Without it, "Paris" + "TX" found
    // nothing - the top 10 world results had no Paris in Texas, and "TX" is
    // only expanded to "Texas" when the country is known.
    for(auto& r : REGION_CODES)
      if(state.equalsIgnoreCase(r[1]) || state.equalsIgnoreCase(r[2])){ cc = r[0]; break; }
    if(cc.length()) logmsg("[geo] country not entered; \"%s\" is in %s\n", state.c_str(), cc.c_str());
  }
  String url = "http://geocoding-api.open-meteo.com/v1/search?count=10&language=en&format=json&name="
               + urlEncode(city.c_str());
  if(cc.length()) url += "&countryCode=" + cc;

  JsonDocument filter;                       // keep only what is used (US cities carry long postcode lists)
  JsonObject f = filter["results"].add<JsonObject>();
  f["name"] = true; f["latitude"] = true; f["longitude"] = true;
  f["country_code"] = true; f["country"] = true; f["admin1"] = true;
  JsonDocument d;
  if(!httpGetJson(url, d, &filter)) return GEO_NET_FAIL;

  JsonArray r = d["results"].as<JsonArray>();
  String stFull = expandRegion(state, cc);
  for(JsonObject o : r){
    String occ = o["country_code"] | "", ocn = o["country"] | "", oa1 = o["admin1"] | "";
    bool countryOK = country.length()==0 || (cc.length() ? cc.equalsIgnoreCase(occ) : country.equalsIgnoreCase(ocn));
    bool stateOK   = state.length()==0   || stFull.equalsIgnoreCase(oa1) || state.equalsIgnoreCase(oa1);
    if(!countryOK || !stateOK) continue;
    lat = o["latitude"]; lon = o["longitude"];
    String nm = o["name"] | city.c_str();
    if(oa1.length()) snprintf(place, sizeof(place), "%s, %s, %s", nm.c_str(), oa1.c_str(), occ.c_str());
    else             snprintf(place, sizeof(place), "%s, %s", nm.c_str(), occ.c_str());
    saveLocationResult();
    logmsg("[geo] %s -> %s (%.4f, %.4f)\n", city.c_str(), place, lat, lon);
    return GEO_OK;
  }
  logmsg("[geo] not found: city \"%s\", state \"%s\", country \"%s\" (%u candidates)\n",
       city.c_str(), state.c_str(), country.c_str(), (unsigned)r.size());
  return GEO_NOT_FOUND;
}

// ==================== WEATHER ====================
static void fetchWeather(){
  if(lat==0&&lon==0){
    if(locError) return;                                   // not found: wait for a new setup (BOOT 3 s)
    GeoResult g = geocode();
    locError = (g == GEO_NOT_FOUND);
    if(g != GEO_OK) return;
  }
  String url="http://api.open-meteo.com/v1/forecast?timezone=auto&forecast_days=1"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
             "&daily=temperature_2m_max,temperature_2m_min"
             "&latitude="+String(lat,4)+"&longitude="+String(lon,4);
  JsonDocument d;
  if(httpGetJson(url,d)){
    wx.tempC = d["current"]["temperature_2m"];
    wx.feelsC= d["current"]["apparent_temperature"];
    wx.hum   = d["current"]["relative_humidity_2m"];
    wx.code  = d["current"]["weather_code"];
    wx.hiC   = d["daily"]["temperature_2m_max"][0];
    wx.loC   = d["daily"]["temperature_2m_min"][0];
    utcOffset= d["utc_offset_seconds"] | 0;
    wx.ok=true;
    logmsg("[wx] %.1fC code %d  hi %.1f lo %.1f  utc%+ld s\n", wx.tempC, wx.code, wx.hiC, wx.loC, utcOffset);
  }
}
static const char* wmoText(int c){
  if(c==0) return "Clear";
  if(c<=2) return "Partly Cloudy";
  if(c==3) return "Overcast";
  if(c<=48) return "Fog";
  if(c<=57) return "Drizzle";
  if(c<=67) return "Rain";
  if(c<=77) return "Snow";
  if(c<=82) return "Showers";
  if(c<=86) return "Snow Showers";
  return "Storm";
}

// ==================== DISPLAY ====================
static void showStatus(const char* l1, const char* l2=nullptr){
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0,30,l1);
  if(l2) u8g2.drawStr(0,48,l2);
  u8g2.sendBuffer();
}
// Draw text in the 5x7 font, wrapped at 25 characters per line (max 'lines').
static int drawWrapped(int y, const char* s, int lines){
  char ln[26]; int n = strlen(s), i = 0, used = 0;
  while(i < n && used < lines){
    int take = min(25, n - i);
    if(i + take < n){ int sp = take; while(sp > 10 && s[i+sp] != ' ') sp--; if(s[i+sp] == ' ') take = sp; }
    memcpy(ln, s + i, take); ln[take] = 0;
    u8g2.drawStr(0, y, ln); y += 9; used++;
    i += take; while(i < n && s[i] == ' ') i++;
  }
  return y;
}
static const char* placeText(){ return place[0] ? place : (cfgCity[0] ? cfgCity : "(not set)"); }

// 20x10 battery outline, 4 fill bars. Bolt to the left = USB power present.
static void drawBattery(int x,int y){
  const int w=20,h=10;
  bool warn = lowBattWarning();
  if(!warn || (millis()/500)%2==0){                       // low battery: outline blinks
    u8g2.drawFrame(x,y,w,h); u8g2.drawBox(x+w,y+3,2,h-6);
  }
  if(warn){ u8g2.setFont(u8g2_font_5x7_tr); u8g2.drawStr(x-17, y+8, "LOW"); }
  int lv = shownBars();
  for(int i=0;i<lv;i++) u8g2.drawBox(x+2+i*4, y+2, 3, h-4);
  if(pw.usb){
    u8g2.drawLine(x-8,y+1,x-11,y+5); u8g2.drawLine(x-11,y+5,x-9,y+5);
    u8g2.drawLine(x-9,y+5,x-12,y+9);
  }
}
static void splash(){
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr); u8g2.drawStr(14,30,"Weather Clock");
  u8g2.setFont(u8g2_font_6x10_tr);    u8g2.drawStr(34,48,"booting");
  u8g2.setFont(u8g2_font_5x7_tr);
  const char* v = "v" FW_VERSION;
  u8g2.drawStr(128 - u8g2.getStrWidth(v), 63, v);
  u8g2.sendBuffer();
}
static void portalScreen(){
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0,12,"Setup needed:");
  u8g2.drawStr(0,30,"1. Join network");
  char apq[36];
  snprintf(apq, sizeof(apq), "\"%s\"", WIFI_AP_NAME);
  u8g2.drawStr(8,42,apq);
  u8g2.drawStr(0,60,"2. Open 192.168.4.1");
  u8g2.sendBuffer();
}
static void showLocation(){
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0,12,"Location set:");
  u8g2.setFont(u8g2_font_5x7_tr);
  drawWrapped(28, placeText(), 3);
  u8g2.sendBuffer();
}
static void drawInfo(){
  char l[40];
  u8g2.setFont(u8g2_font_5x7_tr);
  snprintf(l, sizeof(l), "Weather Clock v%s", FW_VERSION); u8g2.drawStr(0, 7, l);
  int y = drawWrapped(18, placeText(), 2);
  if(locError){ u8g2.drawStr(0, y, "Location not found"); y += 9; }
  if(WiFi.status()==WL_CONNECTED) snprintf(l, sizeof(l), "IP %s", WiFi.localIP().toString().c_str());
  else if(BATT_WIFI_MODE==2 && !pw.usb) snprintf(l, sizeof(l), "WiFi off between updates");
  else                            snprintf(l, sizeof(l), "WiFi not connected");
  u8g2.drawStr(0, y, l); y += 9;
  if(pw.usb) snprintf(l, sizeof(l), "Batt %.2f V USB %s", pw.vbat,
                      chg.st==CHG_FULL ? "full" : chg.st==CHG_IDLE ? "idle" : chg.st==CHG_CV ? "top-off" : "charging");
  else       snprintf(l, sizeof(l), "Batt %.2f V  %d%%", pw.vbat, pw.pct);
  u8g2.drawStr(0, y, l); y += 9;
  if(y <= 63) u8g2.drawStr(0, 63, "Hold BOOT 3 s: setup");
}
static void render(){
  u8g2.clearBuffer();
  if(millis() < infoUntil){ drawInfo(); u8g2.sendBuffer(); return; }

  struct tm tm = localNow();
  char hhmm[6]; strftime(hhmm,sizeof(hhmm),"%H:%M",&tm);
  char date[16]; strftime(date,sizeof(date),"%a %d %b",&tm);

  // Row 1: time (left) + 4-bar battery (right)
  u8g2.setFont(u8g2_font_logisoso16_tn);
  u8g2.drawStr(0,16,hhmm);
  drawBattery(106,4);

  if(wx.ok){
    // Big outdoor temperature (left): number + degree ring + unit letter
    char num[6]; snprintf(num,sizeof(num),"%.0f", cToDisp(wx.tempC));
    u8g2.setFont(u8g2_font_logisoso24_tn);
    u8g2.drawStr(0,46,num);
    int nw=u8g2.getStrWidth(num);
    u8g2.drawCircle(nw+6,26,2);
    char un[2]={cfgUnits[0],0};
    u8g2.setFont(u8g2_font_helvB18_tr);
    u8g2.drawStr(nw+11,46,un);

    // Right column: date + indoor temp/RH (right-aligned)
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(128 - u8g2.getStrWidth(date), 28, date);
    if(in_.ok){
      char inl[16];
      snprintf(inl,sizeof(inl),"In %.0f%c %.0f%%", cToDisp(in_.tC), cfgUnits[0], in_.rh);
      u8g2.drawStr(128 - u8g2.getStrWidth(inl), 42, inl);
    }

    // Row 3: condition (left) + hi/lo (right)
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0,62,wmoText(wx.code));
    char hl[14]; snprintf(hl,sizeof(hl),"H%.0f L%.0f",cToDisp(wx.hiC),cToDisp(wx.loC));
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(128 - u8g2.getStrWidth(hl), 62, hl);
  } else {
    u8g2.setFont(u8g2_font_6x12_tr);
    if(locError){
      u8g2.drawStr(0,30,"Location not found");
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(0,44,"Hold BOOT 3 s to redo setup");
    } else if(WiFi.status()==WL_CONNECTED){
      u8g2.drawStr(0,30,"Connected.");
      u8g2.drawStr(0,48,"Fetching weather...");
    } else {
      u8g2.drawStr(0,30,"Connecting WiFi...");
    }
    // Indoor works with no WiFi - show it while we wait
    if(in_.ok){
      char inl[16];
      snprintf(inl,sizeof(inl),"In %.0f%c %.0f%%", cToDisp(in_.tC), cfgUnits[0], in_.rh);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(0,62,inl);
    }
  }
  u8g2.sendBuffer();
}

// ==================== WiFi + location setup (captive portal) ====================
// Copies the portal fields into the settings. Returns true when any location
// field changed (the stored coordinates are then cleared and looked up again).
static bool applyPortalFields(const char* country, const char* state, const char* city,
                              const char* latlon, const char* units){
  bool changed = strcmp(country, cfgCountry) || strcmp(state, cfgState) ||
                 strcmp(city, cfgCity)       || strcmp(latlon, cfgLatLon);
  copyStr(cfgCountry, sizeof(cfgCountry), country);
  copyStr(cfgState,   sizeof(cfgState),   state);
  copyStr(cfgCity,    sizeof(cfgCity),    city);
  copyStr(cfgLatLon,  sizeof(cfgLatLon),  latlon);
  char u = toupper((unsigned char)units[0]);
  cfgUnits[0] = (u=='C') ? 'C' : 'F'; cfgUnits[1] = 0;
  prefs.putString("country", cfgCountry);
  prefs.putString("state",   cfgState);
  prefs.putString("city",    cfgCity);
  prefs.putString("latlon",  cfgLatLon);
  prefs.putString("units",   cfgUnits);
  if(changed){
    lat = lon = 0; place[0] = 0;
    prefs.putDouble("lat",0); prefs.putDouble("lon",0); prefs.putString("place","");
  }
  return changed;
}

static volatile bool paramsSaved = false;

// force=false: connect with saved WiFi, open the portal only if needed.
// force=true:  open the portal now (location fix), with a note at the top of
//              the Setup page. Returns true when WiFi is connected afterwards.
static bool runSetupPortal(bool force, const char* note){
  WiFiManager wm;
  static char noteHtml[200];
  snprintf(noteHtml, sizeof(noteHtml), "<p style='color:#b00'><b>%s</b></p>", note ? note : "");
  WiFiManagerParameter pNote(noteHtml);
  WiFiManagerParameter pHead("<p><b>Location</b> (used for weather and the clock's time zone)</p>");
  WiFiManagerParameter pCountry("country","Country (e.g. US, Canada, GB)",cfgCountry,sizeof(cfgCountry)-1);
  WiFiManagerParameter pState("state","State / province (e.g. LA or Louisiana)",cfgState,sizeof(cfgState)-1);
  WiFiManagerParameter pCity("city","City or ZIP / postal code (e.g. New Orleans)",cfgCity,sizeof(cfgCity)-1);
  WiFiManagerParameter pLatLon("latlon","Optional: latitude, longitude (overrides the above)",cfgLatLon,sizeof(cfgLatLon)-1);
  WiFiManagerParameter pUnits("units","Temperature unit: F or C",cfgUnits,sizeof(cfgUnits)-1);
  if(note) wm.addParameter(&pNote);
  wm.addParameter(&pHead);
  wm.addParameter(&pCountry); wm.addParameter(&pState); wm.addParameter(&pCity);
  wm.addParameter(&pLatLon);  wm.addParameter(&pUnits);

  // On battery the portal gives up after PORTAL_BATT_S idle and the board
  // restarts, so the boot battery check runs again instead of the portal
  // running the battery flat. On USB it waits indefinitely.
  bool onBatt = LOWBATT_ENABLE && !usbPresent();
  wm.setConnectTimeout(20);
  wm.setAPCallback([](WiFiManager*){ portalScreen(); });

  bool ok;
  if(!force){
    wm.setConfigPortalTimeout(onBatt ? PORTAL_BATT_S : 0);
    showStatus("Connecting to","saved WiFi...");
    ok = wm.autoConnect(WIFI_AP_NAME);
  } else {
    // Location fix: the fields are on their own "Setup" page, so the WiFi
    // password does not have to be typed again. Saving that page ends the portal.
    paramsSaved = false;
    wm.setParamsPage(true);
    std::vector<const char*> menu = {"param","wifi","exit"};
    wm.setMenu(menu);
    wm.setSaveParamsCallback([](){ paramsSaved = true; });
    wm.setConfigPortalBlocking(false);
    wm.startConfigPortal(WIFI_AP_NAME);
    portalScreen();
    uint32_t tLast = millis();
    while(!paramsSaved){
      if(wm.process()) break;                              // WiFi re-done on the WiFi page
      if(onBatt && millis() - tLast > PORTAL_BATT_S * 1000UL) break;
      if(WiFi.softAPgetStationNum() > 0) tLast = millis();  // someone is connected: not idle
      delay(10);
    }
    wm.stopConfigPortal();
    if(!paramsSaved && onBatt){ logmsg("[wifi] setup portal timed out on battery - restarting\n"); delay(50); ESP.restart(); }
    if(WiFi.status() != WL_CONNECTED){ WiFi.mode(WIFI_STA); WiFi.begin(); }
    for(int i=0; i<200 && WiFi.status()!=WL_CONNECTED; i++) delay(100);
    ok = WiFi.status() == WL_CONNECTED;
  }
  if(!ok && onBatt && !force){ logmsg("[wifi] setup portal timed out on battery - restarting\n"); delay(50); ESP.restart(); }

  applyPortalFields(pCountry.getValue(), pState.getValue(), pCity.getValue(),
                    pLatLon.getValue(), pUnits.getValue());
  logmsg("[wifi] %s, IP %s\n", ok?"connected":"FAILED", WiFi.localIP().toString().c_str());
  return ok;
}

// After WiFi is up: look the location up now; if it cannot be found, say so
// and reopen the setup portal until it can (or the portal times out).
static void resolveLocation(){
  if(lat!=0 || lon!=0) return;                             // already resolved
  for(int attempt = 0; attempt < 5; attempt++){
    GeoResult g = geocode();
    if(g == GEO_OK){ locError = false; showLocation(); delay(3000); return; }
    if(g == GEO_NET_FAIL){ return; }                       // fetchWeather() retries later
    locError = true;
    showStatus("Location not found.", "Reopening setup...");
    delay(3000);
    char note[120];
    snprintf(note, sizeof(note), "Location not found: \"%s\". Check country, state and city.", cfgCity);
    runSetupPortal(true, note);
  }
}

// BOOT button: short press = info page, hold 3 s = clear WiFi + settings.
static void handleButton(){
  static uint32_t down=0;
  if(digitalRead(PIN_BUTTON)==LOW){
    if(!down) down=millis();
    else if(millis()-down>3000){
      logmsg("[btn] BOOT held 3 s - clearing settings\n");
      WiFiManager wm; wm.resetSettings();
      prefs.clear(); ESP.restart();                        // keeps the "hwcal" calibration
    }
  } else {
    if(down && millis()-down > 40 && millis()-down < 1000) infoUntil = millis() + INFO_MS;
    down=0;
  }
}

// ==================== serial commands (USB Serial Monitor) ====================
static void printInfo(){
  Serial.printf("[info] Weather Clock v%s (%s)%s\n", FW_VERSION, FW_DATE, TEST_BUILD ? " TEST BUILD" : "");
  Serial.printf("[info] location: %s | country \"%s\" state \"%s\" city \"%s\" latlon \"%s\" -> %.4f, %.4f, utc%+ld s\n",
                placeText(), cfgCountry, cfgState, cfgCity, cfgLatLon, lat, lon, utcOffset);
  Serial.printf("[info] battery: raw %.3f V, offset %+.3f V, J2 %.3f V, usb %d, charge %s, bars %d\n",
                pw.vraw, vbatCal, pw.vbat, (int)pw.usb, chgName(), shownBars());
}
static void handleSerial(){
  static char line[40]; static int n = 0;
  while(Serial.available()){
    char c = Serial.read();
    if(c=='\r') continue;
    if(c!='\n'){ if(n < (int)sizeof(line)-1) line[n++] = c; continue; }
    line[n] = 0; n = 0;
    String cmd = trimmed(line);
    if(cmd == "info"){ printInfo(); }
    else if(cmd == "cal reset"){ resetCal(); Serial.printf("[cal] offset back to default %+.3f V\n", vbatCal); }
    else if(cmd.startsWith("cal ")){
      float meas = cmd.substring(4).toFloat();
      float raw = readVbatRaw();
      float off = meas - raw;
      if(meas > 2.5f && meas < 4.5f && fabsf(off) < 0.3f){
        saveCal(off);
        Serial.printf("[cal] J2 %.3f V, raw %.3f V -> offset %+.3f V saved\n", meas, raw, off);
      } else {
        Serial.printf("[cal] rejected: J2 %.3f V, raw %.3f V (offset %+.3f V out of range)\n", meas, raw, off);
      }
    }
    else if(cmd.length()) Serial.println("[cmd] commands: info | cal <J2 volts> | cal reset");
  }
}

#if TEST_BUILD
// ==================== battery-test instrumentation ====================
static void heartbeat(){
  static uint32_t tHb=0, tLog=0;
  static bool lvl=false;
  uint32_t now=millis();
  if((WiFi.status()==WL_CONNECTED || wx.ok) && now-tHb>=HB_PERIOD_MS){   // clock running (radio may be off on battery)
    lvl=!lvl; digitalWrite(PIN_HEARTBEAT, lvl ? HIGH : LOW); tHb=now;
  }
  if(now-tLog>=HB_LOG_MS){
    Serial0.printf("[HB] up=%lus wifi=%d rssi=%d vbat=%.3f usb=%d wx=%d vf=%.3f lb=%d chg=%s bars=%d pct=%d cal=%.3f\n",
                   (unsigned long)(now/1000), (int)WiFi.status(), (int)WiFi.RSSI(),
                   pw.vraw, (int)pw.usb, (int)wx.ok, lb.vf, (int)lb.warn,
                   chgName(), shownBars(), pw.pct, vbatCal);
    tLog=now;
  }
}
#endif

// ==================== WiFi on battery ====================
static bool wifiOnDemand(){ return BATT_WIFI_MODE == 2 && !pw.usb && !DEMO_MODE; }

// Keep the screen, button and heartbeat alive while waiting for the network.
static void waitUntil(uint32_t ms, bool (*done)()){
  uint32_t t0 = millis();
  while(millis() - t0 < ms){
    if(done && done()) return;
    handleButton(); render();
#if TEST_BUILD
    heartbeat();
#endif
    delay(DRAW_MS);
  }
}
static bool wifiUp(){
  if(WiFi.status()==WL_CONNECTED) return true;
  uint32_t t0 = millis();
  WiFi.mode(WIFI_STA); WiFi.begin();                     // saved credentials
  waitUntil(WIFI_CONNECT_MS, [](){ return WiFi.status()==WL_CONNECTED; });
  bool ok = WiFi.status()==WL_CONNECTED;
  logmsg("[wifi] on-demand connect %s in %lu ms\n", ok ? "ok" : "FAILED", (unsigned long)(millis()-t0));
  return ok;
}
static void wifiDown(){
  if(WiFi.getMode()==WIFI_OFF) return;
  WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF);    // keeps the saved WiFi
  logmsg("[wifi] radio off until the next update\n");
}
static void syncTimeWait(){
  syncTime();
  uint32_t t0 = millis();
  waitUntil(NTP_WAIT_MS, [](){ return (bool)ntpDone; });
  logmsg("[ntp] sync %s in %lu ms\n", ntpDone ? "done" : "FAILED, retry at the next update",
         (unsigned long)(millis()-t0));
  if(ntpDone) tNtp = millis();                            // failed: tNtp stays due -> next cycle retries
}
// One weather update. With the radio off between updates: connect, fetch,
// sync the clock if due, switch the radio off.
static void weatherCycle(){
  bool od = wifiOnDemand();
  if(od && !wifiUp()){ wifiDown(); return; }
  fetchWeather(); updateLed();
  if(od && (tNtp == 0 || !clockSet() || millis() - tNtp > NTP_MS)) syncTimeWait();
  if(od) wifiDown();
}
// Called every POWER_MS: the radio and the CPU clock follow the power source.
static void wifiPowerPolicy(){
  if(DEMO_MODE) return;
  static int cpuUsb = -1;
  if((int)pw.usb != cpuUsb){                              // 80 MHz on battery (enough for WiFi), 240 on USB
    setCpuFrequencyMhz(pw.usb ? 240 : 80);
    cpuUsb = pw.usb;
    logmsg("[pwr] CPU %d MHz\n", (int)getCpuFrequencyMhz());
  }
#if BATT_WIFI_MODE == 2
  if(pw.usb && WiFi.getMode()==WIFI_OFF){
    WiFi.mode(WIFI_STA); WiFi.begin();
    logmsg("[wifi] USB power: reconnecting and staying connected\n");
  } else if(!pw.usb && WiFi.status()==WL_CONNECTED && wx.ok){
    wifiDown();                                           // e.g. USB just unplugged
  }
#elif BATT_WIFI_MODE == 1
  static int lastUsb = -1;
  if(WiFi.status()==WL_CONNECTED && (int)pw.usb != lastUsb){
    WiFi.setSleep(pw.usb ? WIFI_PS_MIN_MODEM : WIFI_PS_MAX_MODEM);
    lastUsb = pw.usb;
  }
#endif
}

// ==================== DEMO seed ====================
static void setupDemo(){
  struct timeval tv; tv.tv_sec = 1782311520; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  utcOffset = 0;
  strcpy(cfgUnits,"F");
  copyStr(place, sizeof(place), "New Orleans, Louisiana, US");
  wx.tempC=22.2; wx.feelsC=23.0; wx.hum=55; wx.code=2;          // 72F, Partly Cloudy
  wx.hiC=27.2; wx.loC=17.8; wx.ok=true;                         // hi 81 / lo 64
  in_.tC=23.3; in_.rh=47; in_.ok=true;                          // In 74F 47%
  pw.usb=false; pw.pct=76; pw.vbat=3.95;                        // 4 bars
  updateLed();
  render();
  Serial.println("[DEMO] Representative values shown; WiFi disabled.");
}

// ==================== setup / loop ====================
static void loadSettings(){
  prefs.begin("wxclock",false);
  bool legacy = prefs.isKey("city") && !prefs.isKey("country");  // saved by v1.x (city only)
  prefs.getString("country",cfgCountry,sizeof(cfgCountry));
  prefs.getString("state",cfgState,sizeof(cfgState));
  prefs.getString("city",cfgCity,sizeof(cfgCity));
  prefs.getString("latlon",cfgLatLon,sizeof(cfgLatLon));
  prefs.getString("units",cfgUnits,sizeof(cfgUnits));
  prefs.getString("place",place,sizeof(place));
  lat=prefs.getDouble("lat",0); lon=prefs.getDouble("lon",0);
  if(legacy) logmsg("[cfg] settings from v1.x: city \"%s\" kept with its saved coordinates\n", cfgCity);
}

void setup(){
  timerWakeCheck();                          // low-battery sleep timer wake without USB -> back to sleep
  releaseSleepHolds();                       // after a low-battery sleep
  Serial.begin(115200);
#if TEST_BUILD
  Serial0.begin(115200);                     // UART0 on header TX/RX
  Serial0.printf("\n[HB] boot reset_reason=%d fw=%s\n", (int)esp_reset_reason(), FW_VERSION);
  pinMode(PIN_HEARTBEAT, OUTPUT);
  digitalWrite(PIN_HEARTBEAT, LOW);
#endif
  logmsg("[boot] Weather Clock v%s (%s)%s\n", FW_VERSION, FW_DATE, TEST_BUILD ? " - BATTERY TEST BUILD" : "");

  pinMode(PIN_BUTTON,INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_VBUS_SENSE, ADC_11db);
  loadCal();
  logmsg("[cal] battery offset %+.3f V\n", vbatCal);

  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info){
    if(e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      logmsg("[wifi] disconnected, reason %d\n", (int)info.wifi_sta_disconnected.reason);
    else if(e == ARDUINO_EVENT_WIFI_STA_GOT_IP)
      logmsg("[wifi] got IP %s\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
  });

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);      // Qwiic bus
  u8g2.begin();
  neopixelWrite(PIN_RGB,0,0,0);
  splash(); delay(1200);
  if(!DEMO_MODE) bootBatteryCheck();         // empty battery after a reset -> back to sleep

  readIndoor();                              // works with or without WiFi
  readPower();

  if(DEMO_MODE){ setupDemo(); return; }

  loadSettings();
  runSetupPortal(false, nullptr);
  if(WiFi.status()==WL_CONNECTED){
    resolveLocation();
    showStatus("Connected!","Getting weather...");
  } else {
    showStatus("WiFi not connected","retrying...");
  }
  if(wifiOnDemand() && WiFi.status()==WL_CONNECTED){
    fetchWeather(); updateLed();
    syncTimeWait();
    if(wx.ok) wifiDown();                                 // battery: radio off until the next update
  } else {
    syncTime(); tNtp = millis();
    delay(500);
    fetchWeather();
    updateLed();
  }
  tWeather = millis();
}

void loop(){
  uint32_t now=millis();
  if(!DEMO_MODE){
    handleButton();
    handleSerial();
    uint32_t wxInterval = (wx.ok && (clockSet() || !wifiOnDemand())) ? WEATHER_MS
                         : (wifiOnDemand() ? WX_RETRY_OD_MS : WX_RETRY_MS);   // clock not set yet: retry in 60 s
    if(now-tWeather > wxInterval){ weatherCycle(); tWeather=millis(); }
    if(now-tPower  >POWER_MS ){ readPower(); chargeUpdate(); lowBattUpdate(); wifiPowerPolicy(); tPower=now; }
    if(now-tSens   >SENSOR_MS){ readIndoor(); tSens =now; }
    if(now-tNtp    >NTP_MS && WiFi.status()==WL_CONNECTED){ syncTime(); tNtp=now; }   // connected: hourly
  }
  if(now-tDraw>DRAW_MS){
    render();
#if TEST_BUILD
    heartbeat();
#endif
    tDraw=now;
  }
  delay(5);
}
