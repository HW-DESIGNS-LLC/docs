/*
 * The Crescent City — Weather Clock (the firmware the board ships with)
 * ============================================================================
 * A self-contained WiFi weather clock for the HW Designs ESP32-S3 board.
 * It shows outdoor conditions (Open-Meteo, no API key required), an indoor
 * temperature/humidity reading from an SHT41 on the Qwiic connector, an
 * NTP-synced clock, and a battery gauge — all on a 0.96" SSD1306 OLED.
 *
 * Setup is done entirely on-device through a WiFi captive portal — no cloud
 * account and no companion app required.
 *
 * FIRST-TIME WIFI SETUP
 *   1. On first boot the board creates a WiFi network named "HW-WeatherClock".
 *   2. Join it from a phone or laptop — a setup page opens automatically
 *      (or browse to http://192.168.4.1).
 *   3. Choose your WiFi network, enter the password, and set your City and
 *      temperature unit (F or C). The board saves this and connects.
 *   To change WiFi or city later, hold the BOOT button for 3 seconds to clear
 *   settings and reopen the setup portal.
 *
 * LOW BATTERY (running on battery only; ignored while USB is plugged in)
 *   - Battery nearly empty: the battery icon blinks and "LOW" appears next to it.
 *   - Battery empty: the screen shows "Battery low / charge me" for 10 s, then
 *     the board switches Wi-Fi, the RGB LED and the screen off and goes into
 *     deep sleep. Plug in USB to wake it and charge; it starts up normally.
 *     Pressing RESET (EN) on a still-empty battery shows the message again.
 *   Thresholds are in the LOW-BATTERY CUTOFF block below.
 *
 * BUILD (Arduino IDE)
 *   Board:            "ESP32S3 Dev Module"
 *   USB CDC On Boot:  Enabled
 *   Flash Size:       8MB
 *   Partition Scheme: Default (or any scheme with >= 1.5 MB app space)
 *   Serial Monitor:   115200 baud
 *
 * LIBRARIES (Library Manager)
 *   U8g2, WiFiManager (tzapu), ArduinoJson (v7),
 *   Adafruit SHT4x (pulls in Adafruit BusIO + Adafruit Unified Sensor).
 *   The RGB LED uses the core's built-in neopixelWrite() — no NeoPixel library.
 *
 * The indoor sensor is isolated behind sensorInit()/sensorRead() (marked
 * [SWAP]) so a different I2C temp/RH part can be dropped in without touching
 * the rest of the sketch.
 *
 * Version 2026-10-06
 * Copyright (c) 2026 HW DESIGNS LLC — https://docs.hwdesigns.us
 * SPDX-License-Identifier: MIT (see ../LICENSE)
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
#include <esp_sleep.h>                    // low-battery deep sleep
#include <driver/gpio.h>                  // pad hold during deep sleep

// ==================== USER-FACING CONFIG ====================
#define DEMO_MODE     false               // true = show sample data, WiFi off
#define WIFI_AP_NAME  "HW-WeatherClock"   // captive-portal SSID for first-time setup

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
#define PIN_VBAT_SENSE   4   // R10/R11 divider -> U2.4
#define PIN_VBUS_SENSE   2   // R15/R16 divider -> U2.38
#define VBAT_DIV   2.0f      // 100k/100k
#define VBUS_DIV   2.0f
// Note: the MCP73831 charge-status pin drives the CHG LED only and is not
// wired to a GPIO on this board, so charge-vs-full state is not readable.
// USB-present is shown instead (bolt icon).

// ==================== LOW-BATTERY CUTOFF (Rev B firmware) ====================
// Voltages are the battery voltage at the battery connector J2 (+ vs. board
// GND): the ADC reading plus VBAT_CAL_OFFSET. Acts only on battery (USB absent).
// Thresholds are provisional and will be confirmed on Rev B hardware.
#define LOWBATT_ENABLE      true     // false = no warning, no cutoff (baseline battery runs)
#define VBAT_CAL_OFFSET     0.070f   // V; the ADC reads ~70 mV below J2 + (bench, board 1)
#define VBAT_WARN_V         3.45f    // filtered reading below this: "LOW" warning on the OLED
#define VBAT_WARN_CLEAR_V   3.55f    // ...warning clears above this (hysteresis)
#define VBAT_CUTOFF_V       3.30f    // filtered reading below this: shut down to deep sleep
#define VBAT_RESUME_V       3.50f    // at boot on battery: below this, go straight back to sleep
#define VBAT_FILTER_N       10       // median of the last 10 readings = 30 s at POWER_MS (3 s)
#define LOWBATT_MSG_MS      10000    // "Battery low" stays on screen this long before sleep
#define PORTAL_BATT_S       300      // on battery, the setup portal gives up after 5 min idle

// ---- User button ----
#define PIN_BUTTON       0   // BOOT (SW4) -> U2.27

// SSD1306 driver (raw 0.96" panel via 30-pin FPC, BS0-2 grounded = 4-wire SPI)
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
    U8G2_R0, PIN_OLED_SCLK, PIN_OLED_MOSI, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RST);

// ==================== intervals ====================
const uint32_t WEATHER_MS  = 10UL * 60 * 1000;  // refresh weather every 10 min
const uint32_t WX_RETRY_MS =          15000;    // ...retry every 15s until first success
const uint32_t NTP_MS      = 60UL * 60 * 1000;
const uint32_t POWER_MS    =        3UL * 1000;
const uint32_t SENSOR_MS   =       10UL * 1000; // indoor temp/RH read
const uint32_t DRAW_MS     =             250;

// ==================== config / state ====================
Preferences prefs;
char   cfgCity[48] = "New Orleans";
char   cfgUnits[2] = "F";                 // 'F' or 'C'
double lat = 0, lon = 0;
long   utcOffset = 0;                      // seconds, from Open-Meteo

struct Weather { float tempC=NAN, feelsC=NAN, hum=NAN, hiC=NAN, loC=NAN; int code=-1; bool ok=false; } wx;
struct Power   { bool usb=false; int pct=0; float vbat=0, vraw=0; } pw;   // vbat = calibrated (J2), vraw = ADC x divider
struct Indoor  { float tC=NAN, rh=NAN; bool ok=false; } in_;

uint32_t tWeather=0, tNtp=0, tPower=0, tSens=0, tDraw=0;
float battEMA = -1;

// ==================== helpers ====================
static String urlEncode(const char* s){
  String o; char b[4];
  for (; *s; ++s){ char c=*s;
    if (isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.'||c=='~') o+=c;
    else { sprintf(b,"%%%02X",(unsigned char)c); o+=b; } }
  return o;
}
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

// ==================== INDOOR SENSOR (SHT41 on Qwiic, addr 0x44) ====================
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
static float readVbat(){ return readVbatRaw() + VBAT_CAL_OFFSET; }   // battery voltage at J2
static bool usbPresent(){
  float v = analogReadMilliVolts(PIN_VBUS_SENSE)/1000.0f * VBUS_DIV;
  return v > 4.0f;
}
static int lipoPct(float v){
  static const float P[][2]={{4.20,100},{4.13,90},{4.06,80},{3.98,70},{3.92,60},
    {3.87,50},{3.82,40},{3.79,30},{3.75,20},{3.70,10},{3.60,5},{3.40,0}};
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
  pw.vbat = pw.vraw + VBAT_CAL_OFFSET;
  int p = lipoPct(pw.vbat);
  if(battEMA<0) battEMA=p;
  battEMA = 0.8f*battEMA + 0.2f*p;
  pw.pct = (int)(battEMA+0.5f);
}
// 0..4 bars with hysteresis so ADC/IR jitter can't flicker the display.
// Gain a bar rising past up[]; lose it falling below down[].
static int battBars(){
  static int lv = -1;
  const int up[]   = {14,29,49,74};
  const int down[] = {10,25,45,70};
  if(lv < 0){ lv = 0; while(lv < 4 && pw.pct > down[lv]) lv++; }
  while(lv < 4 && pw.pct >= up[lv])   lv++;
  while(lv > 0 && pw.pct <  down[lv-1]) lv--;
  return lv;
}

// ==================== LOW-BATTERY CUTOFF (Rev B firmware) ====================
// Without this, a board on battery keeps running until the supply sags so far
// that it browns out and reset-loops (powered but not working) until the
// battery's own protection switches off. This block warns first, then puts the
// board into deep sleep with a clear message, and wakes it when USB is plugged in.
struct LowBatt { float buf[VBAT_FILTER_N]; int n=0, head=0; float vf=NAN; bool warn=false; } lb;

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
// in (VUSB_SENSE on GPIO2 goes high; GPIO2 is an RTC GPIO) or RESET is pressed.
static void lowBatteryShutdown(float v){
  Serial.printf("[batt] %.3f V at J2 - low battery, shutting down\n", v);
  if(WiFi.getMode() != WIFI_OFF){ WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF); } // keeps saved WiFi
  neopixelWrite(PIN_RGB,0,0,0);
  lowBatteryScreen(v);
  for(uint32_t t0=millis(); millis()-t0 < LOWBATT_MSG_MS; ){
    if(usbPresent()){ Serial.println("[batt] USB plugged in - restarting"); delay(50); ESP.restart(); }
    delay(100);
  }
  u8g2.setPowerSave(1);                                   // OLED display off (sleep)
  holdPinsForSleep();
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_VBUS_SENSE, ESP_EXT1_WAKEUP_ANY_HIGH);
  Serial.flush();
  esp_deep_sleep_start();                                 // does not return
}

// Boot-time check (before WiFi starts): a battery that is still empty after a
// reset goes straight back to sleep instead of starting WiFi.
static void bootBatteryCheck(){
  if(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1)
    Serial.println("[boot] woke from low-battery sleep: USB plugged in");
  if(!LOWBATT_ENABLE || usbPresent()) return;
  float acc = 0; const int N = 10;
  for(int i=0;i<N;i++){ acc += readVbat(); delay(50); }
  float v = acc / N;
  Serial.printf("[batt] boot check %.3f V at J2 (resume above %.2f V)\n", v, VBAT_RESUME_V);
  if(v < VBAT_RESUME_V) lowBatteryShutdown(v);
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
  if(!lb.warn && lb.vf < VBAT_WARN_V)            { lb.warn = true;  Serial.printf("[batt] low warning, %.3f V\n", lb.vf); }
  else if(lb.warn && lb.vf > VBAT_WARN_CLEAR_V)  { lb.warn = false; }
  if(lb.vf < VBAT_CUTOFF_V) lowBatteryShutdown(lb.vf);
}

// ==================== TIME ====================
static void syncTime(){ configTime(0,0,"pool.ntp.org","time.nist.gov"); }  // non-blocking
static struct tm localNow(){
  time_t t = time(nullptr) + utcOffset;
  struct tm tm; gmtime_r(&t,&tm); return tm;
}

// ==================== WEATHER (Open-Meteo, keyless, plain HTTP) ====================
static bool httpGetJson(const String& url, JsonDocument& doc){
  if(WiFi.status()!=WL_CONNECTED) return false;
  WiFiClient client;                        // plain HTTP — no TLS handshake
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.begin(client, url);
  int code=http.GET();
  bool ok=false;
  if(code==200){
    String payload = http.getString();      // read full body first (reliable)
    Serial.printf("[http] 200, %u bytes, heap %u\n",
                  (unsigned)payload.length(), (unsigned)ESP.getFreeHeap());
    DeserializationError e = deserializeJson(doc, payload);
    if(e) Serial.printf("[json] parse error: %s\n", e.c_str());
    else  ok=true;
  } else {
    Serial.printf("[http] GET failed: %d (%s)\n", code, http.errorToString(code).c_str());
  }
  http.end();
  return ok;
}
static void geocodeCity(){
  String url="http://geocoding-api.open-meteo.com/v1/search?count=1&name="+urlEncode(cfgCity);
  JsonDocument d;
  if(httpGetJson(url,d) && d["results"].size()>0){
    lat=d["results"][0]["latitude"]; lon=d["results"][0]["longitude"];
    prefs.putDouble("lat",lat); prefs.putDouble("lon",lon);
    Serial.printf("[geo] %s -> %.4f, %.4f\n", cfgCity, lat, lon);
  } else {
    Serial.printf("[geo] could not resolve city \"%s\"\n", cfgCity);
  }
}
static void fetchWeather(){
  if(lat==0&&lon==0){ geocodeCity(); if(lat==0&&lon==0) return; }
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
    Serial.printf("[wx] %.1fC code %d  hi %.1f lo %.1f\n", wx.tempC, wx.code, wx.hiC, wx.loC);
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
// 20x10 battery outline, 4 fill bars. Bolt to the left = USB power present
// (charge vs. full is not distinguishable on this board — STAT is LED-only).
static void drawBattery(int x,int y){
  const int w=20,h=10;
  bool warn = lowBattWarning();
  if(!warn || (millis()/500)%2==0){                       // low battery: outline blinks
    u8g2.drawFrame(x,y,w,h); u8g2.drawBox(x+w,y+3,2,h-6);
  }
  if(warn){ u8g2.setFont(u8g2_font_5x7_tr); u8g2.drawStr(x-17, y+8, "LOW"); }
  int lv = battBars();
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
  u8g2.sendBuffer();
}
static void portalScreen(){
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0,12,"WiFi setup needed:");
  u8g2.drawStr(0,30,"1. Join network");
  char apq[36];
  snprintf(apq, sizeof(apq), "\"%s\"", WIFI_AP_NAME);
  u8g2.drawStr(8,42,apq);
  u8g2.drawStr(0,60,"2. Open 192.168.4.1");
  u8g2.sendBuffer();
}
static void render(){
  struct tm tm = localNow();
  char hhmm[6]; strftime(hhmm,sizeof(hhmm),"%H:%M",&tm);
  char date[16]; strftime(date,sizeof(date),"%a %d %b",&tm);

  u8g2.clearBuffer();

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
    if(WiFi.status()==WL_CONNECTED){
      u8g2.drawStr(0,30,"Connected.");
      u8g2.drawStr(0,48,"Fetching weather...");
    } else {
      u8g2.drawStr(0,30,"Connecting WiFi...");
    }
    // Indoor works with no WiFi — show it while we wait
    if(in_.ok){
      char inl[16];
      snprintf(inl,sizeof(inl),"In %.0f%c %.0f%%", cToDisp(in_.tC), cfgUnits[0], in_.rh);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(0,62,inl);
    }
  }
  u8g2.sendBuffer();
}

// ==================== WiFi provisioning (captive portal) ====================
static void startWiFi(){
  WiFiManager wm;
  WiFiManagerParameter pCity("city","City (e.g. New Orleans)",cfgCity,sizeof(cfgCity));
  WiFiManagerParameter pUnits("units","Temperature unit: F or C",cfgUnits,sizeof(cfgUnits));
  wm.addParameter(&pCity); wm.addParameter(&pUnits);
  // On battery the portal gives up after PORTAL_BATT_S idle and the board
  // restarts, so the boot battery check runs again instead of the portal
  // running the battery flat. On USB it waits indefinitely.
  bool onBatt = LOWBATT_ENABLE && !usbPresent();
  wm.setConfigPortalTimeout(onBatt ? PORTAL_BATT_S : 0);
  wm.setConnectTimeout(20);
  wm.setAPCallback([](WiFiManager*){ portalScreen(); });
  showStatus("Connecting to","saved WiFi...");
  bool ok = wm.autoConnect(WIFI_AP_NAME);
  if(!ok && onBatt){ Serial.println("[wifi] setup portal timed out on battery - restarting"); delay(50); ESP.restart(); }

  // Persist any changed settings. Only invalidate the cached coordinates when
  // the city actually changes, so a WiFi-only reconfigure doesn't force a
  // redundant geocoding lookup on the next fetch.
  char newCity[sizeof(cfgCity)];
  strncpy(newCity, pCity.getValue(), sizeof(newCity)-1);
  newCity[sizeof(newCity)-1] = 0;
  strncpy(cfgUnits, pUnits.getValue(), sizeof(cfgUnits)-1);
  cfgUnits[0] = toupper(cfgUnits[0]);
  if(strcmp(newCity, cfgCity) != 0){        // city changed -> force re-geocode
    strncpy(cfgCity, newCity, sizeof(cfgCity)-1);
    cfgCity[sizeof(cfgCity)-1] = 0;
    prefs.putDouble("lat",0); prefs.putDouble("lon",0);
    lat = lon = 0;
  }
  prefs.putString("city",cfgCity);
  prefs.putString("units",cfgUnits);
  Serial.printf("[wifi] %s, IP %s\n", ok?"connected":"FAILED",
                WiFi.localIP().toString().c_str());
}
static void handleButton(){                              // long-press BOOT -> forget WiFi
  static uint32_t down=0;
  if(digitalRead(PIN_BUTTON)==LOW){
    if(!down) down=millis();
    else if(millis()-down>3000){
      WiFiManager wm; wm.resetSettings();
      prefs.clear(); ESP.restart();
    }
  } else down=0;
}

// ==================== DEMO seed ====================
static void setupDemo(){
  struct timeval tv; tv.tv_sec = 1782311520; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  utcOffset = 0;
  strcpy(cfgUnits,"F");
  wx.tempC=22.2; wx.feelsC=23.0; wx.hum=55; wx.code=2;          // 72F, Partly Cloudy
  wx.hiC=27.2; wx.loC=17.8; wx.ok=true;                         // hi 81 / lo 64
  in_.tC=23.3; in_.rh=47; in_.ok=true;                          // In 74F 47%
  pw.usb=false; pw.pct=76; pw.vbat=3.95;                        // 4 bars
  updateLed();
  render();
  Serial.println("[DEMO] Representative values shown; WiFi disabled.");
}

// ==================== setup / loop ====================
void setup(){
  releaseSleepHolds();                       // after a low-battery sleep
  Serial.begin(115200);
  Serial.println("\n[boot] Weather Clock (S3 product board)");

  pinMode(PIN_BUTTON,INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_VBUS_SENSE, ADC_11db);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);      // Qwiic bus
  u8g2.begin();
  neopixelWrite(PIN_RGB,0,0,0);
  splash(); delay(1200);
  if(!DEMO_MODE) bootBatteryCheck();         // empty battery after a reset -> back to sleep

  readIndoor();                              // works with or without WiFi

  if(DEMO_MODE){ setupDemo(); return; }

  prefs.begin("wxclock",false);
  prefs.getString("city",cfgCity,sizeof(cfgCity));
  prefs.getString("units",cfgUnits,sizeof(cfgUnits));
  lat=prefs.getDouble("lat",0); lon=prefs.getDouble("lon",0);

  startWiFi();
  if(WiFi.status()==WL_CONNECTED) showStatus("Connected!","Getting weather...");
  else                            showStatus("WiFi not connected","retrying...");
  syncTime();
  delay(500);
  fetchWeather();
  updateLed();
  tWeather = millis();
}

void loop(){
  uint32_t now=millis();
  if(!DEMO_MODE){
    handleButton();
    uint32_t wxInterval = wx.ok ? WEATHER_MS : WX_RETRY_MS;
    if(now-tWeather > wxInterval){ fetchWeather(); updateLed(); tWeather=now; }
    if(now-tPower  >POWER_MS ){ readPower(); lowBattUpdate(); tPower=now; }
    if(now-tSens   >SENSOR_MS){ readIndoor(); tSens =now; }
    if(now-tNtp    >NTP_MS   ){ syncTime();   tNtp  =now; }
  }
  if(now-tDraw>DRAW_MS){ render(); tDraw=now; }
  delay(5);
}
