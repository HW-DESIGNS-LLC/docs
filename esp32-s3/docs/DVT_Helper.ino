/**********************************************************************
 * DVT_Helper.ino  —  ESP32-S3 board bring-up & self-test firmware
 * ============================================================================
 * A serial-menu diagnostic tool for the HW Designs ESP32-S3 board. Flash it,
 * open the Serial Monitor at 115200 baud, and press a menu key to run each
 * test: GPIO headers, I2C / SHT4x sensor, OLED, RGB LED, ADC rails, WiFi,
 * deep sleep, timekeeping, and board info. Useful for board bring-up,
 * debugging a suspect unit, or as the starting point for a factory test jig.
 *
 * Each menu mode is tagged with the matching test ID from the board's DVT
 * plan (e.g. GPIO-01, RF-W-02) so results map straight back to the record.
 *
 * Arduino IDE settings (Tools menu):
 *   Board:            ESP32S3 Dev Module
 *   USB CDC On Boot:  Enabled          <-- REQUIRED, or no serial output
 *   Flash Size:       8MB (64Mb)
 *   Partition Scheme: 8M with spiffs (3MB APP/1.5MB SPIFFS)  (any 8MB scheme works)
 *   PSRAM:            Disabled         <-- WROOM-1-N8 has no PSRAM
 *   Core Debug Level: Info
 *
 * Library required from Library Manager: U8g2 (oliver).
 * Everything else (including BLE) ships with the arduino-esp32 core (3.x).
 *
 * BLE NOTE: the BLE RSSI test (menu 'b') initializes the Bluetooth controller.
 * arduino-esp32 cores 3.3.7-3.3.10 have a known BT-controller/heap regression
 * that crashes BLE startup at boot (espressif/arduino-esp32 #12357). Build with
 * core 3.3.6, or a later release where it's fixed, if you use that test. All the
 * WiFi tests are unaffected on any 3.x core.
 *
 * Copyright (c) 2026 HW Designs LLC — https://docs.hwdesigns.us
 * Published as an open example. Add your preferred license (e.g. MIT) in the
 * repository before distributing.
 *********************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include "esp_sleep.h"
#include "esp_mac.h"

#define FW_NAME "DVT-Helper"
#define FW_VER  "1.0"

/* ------------- verified pin map, fab rev 2026-06-28 ------------- */
#define PIN_I2C_SDA     8    // Qwiic J3.3, 4.7k pull-up (R9)
#define PIN_I2C_SCL     18   // Qwiic J3.4, 4.7k pull-up (R13)
#define PIN_OLED_DC     9    // FPC1.15
#define PIN_OLED_CS     10   // FPC1.13
#define PIN_OLED_MOSI   11   // FPC1.19
#define PIN_OLED_SCLK   12   // FPC1.18
#define PIN_OLED_RST    14   // FPC1.14
#define PIN_RGB         48   // SK6812 DIN
#define PIN_BOOT_BTN    0    // SW4, internal pull-up only
#define PIN_VBAT_SENSE  4    // ADC1_CH3 = BATT+ / 2  (R10/R11 100k/100k)
#define PIN_VUSB_SENSE  2    // ADC1_CH1 = V_USB / 2  (R15/R16 100k/100k)

struct HdrPin { uint8_t gpio; const char *label; };
// Header GPIOs in physical order (UART0 pins J4.9/J4.10 deliberately excluded)
HdrPin HDR[] = {
  { 5, "J4.3" }, { 6, "J4.4" }, { 7, "J4.5" }, { 15, "J4.6" }, { 16, "J4.7" },
  { 1, "J5.3" }, { 13, "J5.4" }, { 39, "J5.5" },
  { 37, "J5.7" }, { 36, "J5.8" }, { 35, "J5.9" }, { 21, "J5.10" }
};
const int N_HDR = sizeof(HDR) / sizeof(HDR[0]);

// Physically adjacent signal-signal pairs (indices into HDR[]).
// J5.5<->J5.7 is NOT a pair: GND sits between them (J5.6).
const uint8_t ADJ[][2] = {
  {0,1},{1,2},{2,3},{3,4},        // J4: 5-6, 6-7, 7-15, 15-16
  {5,6},{6,7},                    // J5: 1-13, 13-39
  {8,9},{9,10},{10,11}            // J5: 37-36, 36-35, 35-21
};
const int N_ADJ = sizeof(ADJ) / sizeof(ADJ[0]);

/* ---------------------------- globals ---------------------------- */
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RST);
Preferences prefs;
bool bleOn = false;
uint32_t btnPresses = 0;
uint32_t lastStatus = 0;

/* --------------------------- utilities --------------------------- */
String macStr() {
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}
String macSuffix() {
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char b[5];
  snprintf(b, sizeof(b), "%02X%02X", m[4], m[5]);
  return String(b);
}

void fmtUptime(char *out, size_t n) {
  uint32_t s = millis() / 1000;
  snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
           (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}

const char *resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int-wdt";
    case ESP_RST_TASK_WDT:  return "task-wdt";
    case ESP_RST_WDT:       return "other-wdt";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_USB:       return "usb";
    default:                return "other/unknown";
  }
}

// Blocking single keypress
char waitKey() {
  while (!Serial.available()) delay(5);
  char c = Serial.read();
  while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')) Serial.read();
  return c;
}
// Non-blocking: returns 'q' if user pressed q/Q, else 0
char pollQuit() {
  if (!Serial.available()) return 0;
  char c = Serial.read();
  if (c == 'q' || c == 'Q') return 'q';
  return c;
}
// Line input with echo; handles CR, LF, CRLF and backspace
String readLine(const char *prompt) {
  Serial.print(prompt);
  String s = "";
  for (;;) {
    while (!Serial.available()) delay(5);
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') break;
    if (c == 8 || c == 127) {
      if (s.length()) { s.remove(s.length() - 1); Serial.print("\b \b"); }
      continue;
    }
    s += c;
    Serial.print(c);
  }
  Serial.println();
  return s;
}

float railVolts(int pin) {  // averaged, corrected for the /2 divider
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) { acc += analogReadMilliVolts(pin); delay(2); }
  return (acc / 16) * 2.0f / 1000.0f;
}

void releaseHeaderPins() {
  for (int i = 0; i < N_HDR; i++) pinMode(HDR[i].gpio, INPUT);
}

/* ----------------------------- WiFi ------------------------------ */
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  prefs.begin("dvt", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  if (ssid == "") {
    Serial.println("No WiFi credentials stored. Use menu 'w' first.");
    return false;
  }
  Serial.printf("Connecting to \"%s\" ", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(400); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("FAILED to connect (check SSID/password, 2.4 GHz only).");
    return false;
  }
  Serial.printf("Connected. IP=%s  RSSI=%d dBm  ch=%d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
  return true;
}

/* ------------------------- mode handlers ------------------------- */

// [1] GPIO-01 walking output
void modeWalk() {
  Serial.println("\n== GPIO-01 walking output ==");
  Serial.println("Remove anything attached to J4/J5. Each pin drives HIGH in turn;");
  Serial.println("verify ~3.3 V on it and ~0 V on every other signal pin with the DMM.");
  Serial.println("Keys: ENTER=next pin, r=repeat this pin, q=quit\n");
  for (int i = 0; i < N_HDR; i++) { pinMode(HDR[i].gpio, OUTPUT); digitalWrite(HDR[i].gpio, LOW); }
  for (int i = 0; i < N_HDR; i++) {
    for (int j = 0; j < N_HDR; j++) digitalWrite(HDR[j].gpio, LOW);
    digitalWrite(HDR[i].gpio, HIGH);
    Serial.printf("[%2d/%d]  GPIO%-2u (%s) = HIGH, all others LOW\n", i + 1, N_HDR, HDR[i].gpio, HDR[i].label);
    char c = waitKey();
    if (c == 'r' || c == 'R') { i--; continue; }
    if (c == 'q' || c == 'Q') break;
  }
  releaseHeaderPins();
  Serial.println("Walking test done; pins released to inputs.");
}

// [2] GPIO-02 input/pull-up report (+ BTN-01, BOOT shown live)
void modeInputs() {
  Serial.println("\n== GPIO-02 input / pull-up report ==");
  Serial.println("All header pins set INPUT_PULLUP: each should read 1.");
  Serial.println("Jumper any pin to GND -> it should read 0. BOOT (IO0) shown too. q=quit\n");
  for (int i = 0; i < N_HDR; i++) pinMode(HDR[i].gpio, INPUT_PULLUP);
  Serial.print("  ");
  for (int i = 0; i < N_HDR; i++) Serial.printf("%-6s", HDR[i].label);
  Serial.println("BOOT");
  for (;;) {
    Serial.print("  ");
    for (int i = 0; i < N_HDR; i++) Serial.printf("%-6d", digitalRead(HDR[i].gpio));
    Serial.printf("%d\n", digitalRead(PIN_BOOT_BTN));
    for (int t = 0; t < 10; t++) { if (pollQuit() == 'q') { releaseHeaderPins(); Serial.println("done."); return; } delay(50); }
  }
}

// [3] GPIO-03 adjacent-bridge test: automatic, then optional manual pattern
void modeAdjacent() {
  Serial.println("\n== GPIO-03 adjacent-pin bridge test ==");
  Serial.println("IMPORTANT: nothing may be attached to J4/J5 during the auto test.");
  Serial.println("Auto test: each pair is checked with pull-up-vs-LOW and pull-down-vs-HIGH.\n");
  int fails = 0;
  for (int p = 0; p < N_ADJ; p++) {
    uint8_t a = HDR[ADJ[p][0]].gpio, b = HDR[ADJ[p][1]].gpio;
    bool bad = false;
    for (int pass = 0; pass < 2; pass++) {
      uint8_t x = pass ? b : a, y = pass ? a : b;
      pinMode(x, INPUT_PULLUP);  pinMode(y, OUTPUT); digitalWrite(y, LOW);  delay(5);
      if (digitalRead(x) == LOW) bad = true;              // x dragged low by neighbor
      pinMode(x, INPUT_PULLDOWN); digitalWrite(y, HIGH);  delay(5);
      if (digitalRead(x) == HIGH) bad = true;             // x dragged high by neighbor
      pinMode(x, INPUT); pinMode(y, INPUT);
    }
    Serial.printf("  %-5s <-> %-6s (GPIO%u/GPIO%u): %s\n",
                  HDR[ADJ[p][0]].label, HDR[ADJ[p][1]].label, a, b, bad ? "*** BRIDGE ***" : "ok");
    if (bad) fails++;
  }
  Serial.printf("\nAuto result: %s (%d suspect pair%s)\n", fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
  Serial.println("Bridges to GND/3V3 planes are caught by modes 1 and 2 instead.");
  Serial.println("\nManual DMM pattern? p=alternating pattern hold, any other key=exit");
  if (waitKey() == 'p') {
    bool phase = false;
    for (;;) {
      for (int i = 0; i < N_HDR; i++) {
        pinMode(HDR[i].gpio, OUTPUT);
        digitalWrite(HDR[i].gpio, ((i % 2 == 0) ^ phase) ? HIGH : LOW);
      }
      Serial.printf("Pattern %c: even positions %s, odd positions %s.  ENTER=flip, q=quit\n",
                    phase ? 'B' : 'A', phase ? "LOW" : "HIGH", phase ? "HIGH" : "LOW");
      if (waitKey() == 'q') break;
      phase = !phase;
    }
  }
  releaseHeaderPins();
  Serial.println("done; pins released.");
}

// [4] I2C-01..06: bus scan + SHT4x raw read at 100 kHz and 400 kHz
uint8_t crc8(const uint8_t *d, int n) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}
bool sht4xRead(float &tC, float &rh) {
  Wire.beginTransmission(0x44);
  Wire.write(0xFD);                       // high-precision measurement
  if (Wire.endTransmission() != 0) return false;
  delay(12);
  if (Wire.requestFrom(0x44, 6) != 6) return false;
  uint8_t d[6];
  for (int i = 0; i < 6; i++) d[i] = Wire.read();
  if (crc8(d, 2) != d[2] || crc8(d + 3, 2) != d[5]) { Serial.println("  CRC error!"); return false; }
  uint16_t tRaw = (d[0] << 8) | d[1], hRaw = (d[3] << 8) | d[4];
  tC = -45.0f + 175.0f * tRaw / 65535.0f;
  rh = constrain(-6.0f + 125.0f * hRaw / 65535.0f, 0.0f, 100.0f);
  return true;
}
void modeI2C() {
  Serial.println("\n== I2C scan + SHT4x (Qwiic J3) ==");
  Wire.setClock(100000);
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("  device at 0x%02X\n", a); found++; }
  }
  if (!found) Serial.println("  (no devices found — is the breakout plugged in?)");
  float t, h;
  Serial.print("SHT4x @100 kHz: ");
  if (sht4xRead(t, h)) Serial.printf("T=%.2f C  RH=%.1f %%\n", t, h); else Serial.println("no response");
  Wire.setClock(400000);
  Serial.print("SHT4x @400 kHz: ");
  if (sht4xRead(t, h)) Serial.printf("T=%.2f C  RH=%.1f %%   (I2C-03 PASS)\n", t, h); else Serial.println("no response  (I2C-03 FAIL)");
  Wire.setClock(100000);
}

// [5] OLED-02..08 pattern cycle
void oledChecker() {
  u8g2.clearBuffer();
  for (int y = 0; y < 64; y += 8)
    for (int x = 0; x < 128; x += 8)
      if (((x + y) / 8) % 2 == 0) u8g2.drawBox(x, y, 8, 8);
  u8g2.sendBuffer();
}
void modeOLED() {
  Serial.println("\n== OLED pattern cycle (ENTER=next, q=quit) ==");
  const char *names[] = { "ALL PIXELS ON  (measure 3V3 current now: OLED-03 max)",
                          "ALL OFF        (measure 3V3 current now: OLED-03 min)",
                          "BORDER frame   (check for cut rows/cols at edges)",
                          "CROSSHAIR      (geometry / mirroring check)",
                          "CHECKERBOARD   (pixel-level uniformity)",
                          "CONTRAST SWEEP (0 -> 255, watch brightness ramp)",
                          "INFO TEXT" };
  for (int s = 0; s < 7; s++) {
    switch (s) {
      case 0: u8g2.clearBuffer(); u8g2.drawBox(0, 0, 128, 64); u8g2.sendBuffer(); break;
      case 1: u8g2.clearBuffer(); u8g2.sendBuffer(); break;
      case 2: u8g2.clearBuffer(); u8g2.drawFrame(0, 0, 128, 64); u8g2.sendBuffer(); break;
      case 3: u8g2.clearBuffer(); u8g2.drawFrame(0, 0, 128, 64);
              u8g2.drawLine(64, 0, 64, 63); u8g2.drawLine(0, 32, 127, 32); u8g2.sendBuffer(); break;
      case 4: oledChecker(); break;
      case 5: u8g2.clearBuffer(); u8g2.drawBox(0, 0, 128, 64); u8g2.sendBuffer();
              for (int c = 0; c <= 255; c += 5) { u8g2.setContrast(c); delay(60); }
              u8g2.setContrast(255); break;
      case 6: u8g2.clearBuffer(); u8g2.setFont(u8g2_font_7x14B_tf);
              u8g2.drawStr(0, 14, "DVT OK");
              u8g2.setFont(u8g2_font_6x10_tf);
              u8g2.drawStr(0, 30, macStr().c_str());
              u8g2.drawStr(0, 44, FW_NAME " v" FW_VER);
              u8g2.sendBuffer(); break;
    }
    Serial.printf("[%d/7] %s\n", s + 1, names[s]);
    if (waitKey() == 'q') break;
  }
  Serial.println("OLED cycle done (display left as-is).");
}

// [6] RGB-01..04, step on ENTER so current can be logged per color
void modeRGB() {
  Serial.println("\n== RGB LED cycle (ENTER=next, q=quit) ==");
  struct { uint8_t r, g, b; const char *n; } steps[] = {
    {128, 0, 0,   "RED  50%"}, {0, 128, 0, "GREEN 50%"}, {0, 0, 128, "BLUE 50%"},
    {255, 255, 255, "WHITE 100%  (RGB-03: log 3V3 current now)"}, {0, 0, 0, "OFF"}
  };
  for (int i = 0; i < 5; i++) {
    rgbLedWrite(PIN_RGB, steps[i].r, steps[i].g, steps[i].b);   // core 3.x (2.x: neopixelWrite)
    Serial.printf("[%d/5] %s\n", i + 1, steps[i].n);
    if (waitKey() == 'q') { rgbLedWrite(PIN_RGB, 0, 0, 0); break; }
  }
  rgbLedWrite(PIN_RGB, 0, 0, 0);
  Serial.println("RGB done (off).");
}

// [7] ADC-01..05 dump
void modeADC() {
  Serial.println("\n== ADC dump: VBAT / VUSB every 500 ms (q=quit) ==");
  Serial.println("Compare against the DMM at each PSU setpoint per the plan.\n");
  Serial.println("   VBAT_pin(mV)  BATT+(V)   VUSB_pin(mV)  V_USB(V)");
  for (;;) {
    uint32_t vb = 0, vu = 0;
    for (int i = 0; i < 16; i++) { vb += analogReadMilliVolts(PIN_VBAT_SENSE); vu += analogReadMilliVolts(PIN_VUSB_SENSE); delay(1); }
    vb /= 16; vu /= 16;
    Serial.printf("   %5lu         %.3f      %5lu         %.3f\n",
                  (unsigned long)vb, vb * 2.0f / 1000.0f, (unsigned long)vu, vu * 2.0f / 1000.0f);
    for (int t = 0; t < 10; t++) { if (pollQuit() == 'q') { Serial.println("done."); return; } delay(50); }
  }
}

// [8] RF-W-01/02: scan, then averaged RSSI of a chosen SSID
void modeScan() {
  Serial.println("\n== WiFi scan (RF-W-01) ==");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("No networks found."); return; }
  for (int i = 0; i < n && i < 20; i++)
    Serial.printf("  [%2d] %-28s  %4d dBm  ch%2d\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  String pick = readLine("\nIndex to RSSI-average for RF-W-02 (ENTER to skip): ");
  if (pick.length() == 0) { WiFi.scanDelete(); return; }
  int idx = pick.toInt();
  if (idx < 0 || idx >= n) { Serial.println("bad index"); return; }
  String target = WiFi.SSID(idx);
  WiFi.scanDelete();
  Serial.printf("Averaging RSSI of \"%s\" over 10 scans (~30 s). Keep board still.\n", target.c_str());
  int cnt = 0; long sum = 0; int mn = 0, mx = -127;
  for (int k = 0; k < 10; k++) {
    int m = WiFi.scanNetworks();
    int best = -127;
    for (int i = 0; i < m; i++)
      if (WiFi.SSID(i) == target && WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
    WiFi.scanDelete();
    if (best > -127) {
      cnt++; sum += best;
      if (best < mn || cnt == 1) mn = best;
      if (best > mx) mx = best;
      Serial.printf("  scan %2d: %d dBm\n", k + 1, best);
    } else Serial.printf("  scan %2d: not seen\n", k + 1);
  }
  if (cnt) Serial.printf("RESULT \"%s\": avg %.1f dBm  (min %d / max %d, %d/10 scans)\n",
                         target.c_str(), (float)sum / cnt, mn, mx, cnt);
  else Serial.println("SSID never seen — move closer and retry.");
  Serial.println("Log this value in RF-W-02; repeat on the reference board, same spot/orientation.");
}

// [c] connect & hold: for RF-W-03 timing and PC-side ping (RF-W-04)
void modeConnectHold() {
  Serial.println("\n== Connect & hold (RF-W-03/04) ==");
  uint32_t t0 = millis();
  if (!ensureWiFi()) return;
  Serial.printf("Association+DHCP took %.1f s (RF-W-03 target <10 s)\n", (millis() - t0) / 1000.0f);
  Serial.printf("From your PC run:   ping %s        (q=quit)\n", WiFi.localIP().toString().c_str());
  for (;;) {
    Serial.printf("  holding: RSSI %d dBm  heap %lu\n", WiFi.RSSI(), (unsigned long)ESP.getFreeHeap());
    for (int t = 0; t < 40; t++) { if (pollQuit() == 'q') { Serial.println("done (still connected)."); return; } delay(50); }
  }
}

// [9] continuous TX flood: PWR-06 droop / PWR-09 brownout / RF-W-08
void modeTraffic() {
  Serial.println("\n== Continuous WiFi TX (PWR-06/PWR-09/RF-W-08) ==");
  if (!ensureWiFi()) return;
  Serial.println("Flooding UDP broadcast on port 9999. Scope 3V3 now. q=quit\n");
  WiFiUDP udp;
  uint8_t buf[1200];
  for (int i = 0; i < 1200; i++) buf[i] = i & 0xFF;
  uint32_t pkts = 0, t0 = millis();
  for (;;) {
    udp.beginPacket(WiFi.broadcastIP(), 9999);
    udp.write(buf, sizeof(buf));
    udp.endPacket();
    pkts++;
    if (millis() - t0 >= 1000) {
      Serial.printf("  %lu pkt/s  RSSI %d  heap %lu\n", (unsigned long)pkts, WiFi.RSSI(), (unsigned long)ESP.getFreeHeap());
      pkts = 0; t0 = millis();
      if (pollQuit() == 'q') break;
    }
    delay(1);
  }
  Serial.println("TX flood stopped.");
}

// [h] HTTP throughput (RF-W-05)
void modeThroughput() {
  Serial.println("\n== HTTP throughput (RF-W-05) ==");
  Serial.println("On a LAN PC:  python3 -m http.server 8000   (in a folder with a ~2 MB file)");
  if (!ensureWiFi()) return;
  String url = readLine("URL to fetch (e.g. http://192.168.1.50:8000/test.bin): ");
  if (!url.startsWith("http")) { Serial.println("need a full http:// URL"); return; }
  float best = 0;
  for (int pass = 1; pass <= 5; pass++) {
    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) { Serial.printf("  pass %d: HTTP %d\n", pass, code); http.end(); continue; }
    WiFiClient *s = http.getStreamPtr();
    int len = http.getSize();
    uint32_t got = 0;
    uint8_t buf[1024];
    uint32_t t0 = micros();
    while (http.connected() && (len < 0 || got < (uint32_t)len)) {
      size_t av = s->available();
      if (av) got += s->readBytes(buf, av > sizeof(buf) ? sizeof(buf) : av);
      else delay(1);
      if (got > 8UL * 1024 * 1024) break;
    }
    float secs = (micros() - t0) / 1e6f;
    float mbps = got * 8.0f / secs / 1e6f;
    if (mbps > best) best = mbps;
    Serial.printf("  pass %d: %lu bytes in %.2f s = %.2f Mbit/s\n", pass, (unsigned long)got, secs, mbps);
    http.end();
  }
  Serial.printf("Best of 5: %.2f Mbit/s  (plan sanity floor: 8 Mbit/s)\n", best);
}

// [s] PWR-08 deep sleep
void modeSleep() {
  Serial.println("\n== Deep sleep 60 s (PWR-08) ==");
  Serial.println("Board sleeps for 60 s then reboots into the menu.");
  Serial.println("For the battery sleep-current number: power from J2 through the meter,");
  Serial.println("USB must be UNPLUGGED (pull it during the countdown).");
  for (int i = 8; i > 0; i--) { Serial.printf("  sleeping in %d...\n", i); delay(1000); }
  Serial.println("zzz");
  Serial.flush();
  delay(100);
  esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
  esp_deep_sleep_start();
}

// [b] BLE advertise for an RSSI check (RF-B-01)
// Advertises a named beacon so the board shows up in a BLE scanner app such as
// Nordic nRF Connect; read its RSSI there at a fixed distance (e.g. 1 m).
void modeBLE() {
  if (bleOn) { Serial.println("BLE already advertising (reboot to stop)."); return; }
  String name = "DVT-S3-" + macSuffix();
  BLEDevice::init(name.c_str());
  BLEDevice::createServer();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->setScanResponse(true);
  adv->start();
  bleOn = true;
  Serial.printf("\nBLE advertising as \"%s\".\n", name.c_str());
  Serial.println("Open Nordic nRF Connect (or any BLE scanner), find that name, and");
  Serial.println("log its RSSI at 1 m for RF-B-01. Reboot the board to stop advertising.");
}

// [t] MCU-08 timekeeping
void modeTimeKeeping() {
  Serial.println("\n== Timekeeping sanity (MCU-08) ==");
  Serial.println("Start a phone stopwatch on the word GO. Board logs every 30 s for 10 min.");
  Serial.println("Drift % = (board_s - stopwatch_s) / stopwatch_s * 100.  q=abort\n");
  delay(1500);
  Serial.println(">>> GO <<<");
  uint32_t t0 = millis();
  for (int k = 1; k <= 20; k++) {
    while (millis() - t0 < (uint32_t)k * 30000UL) {
      if (pollQuit() == 'q') { Serial.println("aborted."); return; }
      delay(20);
    }
    uint32_t el = millis() - t0;
    Serial.printf("  board elapsed: %lu ms  (%lu:%02lu)\n", (unsigned long)el,
                  (unsigned long)(el / 60000), (unsigned long)((el / 1000) % 60));
  }
  Serial.println("10:00 board time reached — compare with the stopwatch and compute drift.");
}

// [i] board info (MCU-03 companion: MAC = serial number)
void modeInfo() {
  char up[16]; fmtUptime(up, sizeof(up));
  Serial.println("\n== Board info ==");
  Serial.printf("  FW:            %s v%s  built %s %s\n", FW_NAME, FW_VER, __DATE__, __TIME__);
  Serial.printf("  Chip:          %s rev %d, %d MHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
  Serial.printf("  Flash:         %lu MB\n", (unsigned long)(ESP.getFlashChipSize() / 1024 / 1024));
  Serial.printf("  MAC (=S/N):    %s\n", macStr().c_str());
  Serial.printf("  Reset reason:  %s\n", resetReasonStr());
  Serial.printf("  Uptime:        %s   Heap free: %lu / %lu\n", up,
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getHeapSize());
  Serial.printf("  BOOT presses:  %lu\n", (unsigned long)btnPresses);
}

// [w] store WiFi credentials in NVS
void modeWiFiCreds() {
  String ssid = readLine("SSID (2.4 GHz): ");
  String pass = readLine("Password: ");
  prefs.begin("dvt", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  Serial.println("Stored in NVS. Modes 8/9/c/h will use these.");
}

void printMenu() {
  Serial.println("\n---------------- DVT MENU ----------------");
  Serial.println(" 1  GPIO walking output          (GPIO-01)");
  Serial.println(" 2  GPIO input/pull-up report    (GPIO-02, BTN-01)");
  Serial.println(" 3  Adjacent-bridge test         (GPIO-03)");
  Serial.println(" 4  I2C scan + SHT4x 100k/400k   (I2C-01..06)");
  Serial.println(" 5  OLED pattern cycle           (OLED-02..08)");
  Serial.println(" 6  RGB LED cycle                (RGB-01..04)");
  Serial.println(" 7  ADC dump VBAT/VUSB           (ADC-01..05)");
  Serial.println(" 8  WiFi scan + RSSI average     (RF-W-01/02)");
  Serial.println(" c  Connect & hold (ping target) (RF-W-03/04)");
  Serial.println(" 9  Continuous WiFi TX flood     (PWR-06/09, RF-W-08)");
  Serial.println(" h  HTTP throughput test         (RF-W-05)");
  Serial.println(" s  Deep sleep 60 s              (PWR-08)");
  Serial.println(" b  BLE advertise / RSSI check   (RF-B-01)");
  Serial.println(" t  Timekeeping 10 min           (MCU-08)");
  Serial.println(" i  Board info / MAC serial      (MCU-03)");
  Serial.println(" w  Set WiFi credentials (NVS)");
  Serial.println(" m  Show this menu");
  Serial.println("-------------------------------------------");
}

/* ------------------------------ core ----------------------------- */
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // wait for CDC

  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_VUSB_SENSE, ADC_11db);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);

  // Remap hardware SPI to the OLED pins BEFORE u8g2.begin()
  SPI.begin(PIN_OLED_SCLK, -1, PIN_OLED_MOSI, PIN_OLED_CS);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 14, "DVT " FW_VER);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 30, macStr().c_str());
  u8g2.sendBuffer();

  Serial.printf("\n\n%s v%s | %s | reset: %s\n", FW_NAME, FW_VER, macStr().c_str(), resetReasonStr());
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP)
    Serial.println("(woke from deep sleep — PWR-08 wake OK)");
  printMenu();
}

void loop() {
  // BOOT button edge detect (BTN-01)
  static bool prevBtn = true;
  bool nowBtn = digitalRead(PIN_BOOT_BTN);
  if (prevBtn && !nowBtn) { btnPresses++; Serial.printf("[BTN] BOOT press #%lu\n", (unsigned long)btnPresses); delay(30); }
  prevBtn = nowBtn;

  // idle status heartbeat every 30 s (soak-friendly)
  if (millis() - lastStatus > 30000) {
    lastStatus = millis();
    char up[16]; fmtUptime(up, sizeof(up));
    Serial.printf("[status] up %s  heap %lu\n", up, (unsigned long)ESP.getFreeHeap());
  }

  if (!Serial.available()) { delay(10); return; }
  char c = Serial.read();
  if (c == '\n' || c == '\r' || c == ' ') return;
  switch (c) {
    case '1': modeWalk(); break;
    case '2': modeInputs(); break;
    case '3': modeAdjacent(); break;
    case '4': modeI2C(); break;
    case '5': modeOLED(); break;
    case '6': modeRGB(); break;
    case '7': modeADC(); break;
    case '8': modeScan(); break;
    case '9': modeTraffic(); break;
    case 'c': modeConnectHold(); break;
    case 'h': modeThroughput(); break;
    case 's': modeSleep(); break;
    case 'b': modeBLE(); break;
    case 't': modeTimeKeeping(); break;
    case 'i': modeInfo(); break;
    case 'w': modeWiFiCreds(); break;
    case 'm': printMenu(); break;
    default: Serial.printf("? '%c' — press m for menu\n", c);
  }
}
