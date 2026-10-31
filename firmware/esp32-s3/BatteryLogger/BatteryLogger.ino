/*
 * Crescent City - Battery-powered sensor logger
 * ============================================================================
 * Logs a Qwiic / STEMMA QT temperature & humidity sensor (SHT40, SHT41 or
 * SHT45, I2C address 0x44) on battery power, sleeping between readings.
 *
 * ON BATTERY
 *   - Wakes every LOG_INTERVAL_MIN minutes, takes one reading, stores it in
 *     memory that survives deep sleep (the last 24 hours at 15 minutes), and
 *     goes straight back to sleep. Wi-Fi is never switched on.
 *   - Press BOOT to see the screen: the current reading, the battery, and a
 *     chart of the log. Press BOOT again while the screen is on to switch the
 *     chart between temperature and humidity. The screen turns off by itself.
 *   - Low battery: below VBAT_CUTOFF_V the board stops logging and sleeps
 *     until USB is plugged in, so the cell is never run flat.
 *
 * ON USB POWER
 *   - The board stays awake with the screen on, keeps logging on the same
 *     schedule, and prints each reading on the Serial Monitor (115200 baud).
 *     Type "d" and press Enter to print the whole log as CSV.
 *   - Plugging in USB wakes a sleeping board, and because it stays awake on
 *     USB, uploading a new sketch works as usual.
 *
 * The log is kept through deep sleep and lost when the battery is removed or
 * the board is reset.
 *
 * BUILD (Arduino IDE)
 *   Board: "ESP32S3 Dev Module", USB CDC On Boot: "Enabled", Flash Size: 8MB.
 *   Core: esp32 by Espressif Systems 3.3.6.
 *
 * LIBRARIES (Library Manager)
 *   U8g2, Adafruit SHT4x (also installs Adafruit BusIO and Adafruit Unified
 *   Sensor). The RGB LED uses the core's built-in neopixelWrite().
 *
 * Version 2026-10-06. MIT license - see ../LICENSE.
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_SHT4x.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <sys/time.h>

// ---- Board pins (Crescent City V1.0 and V2.0) ----
#define PIN_OLED_SCLK   12
#define PIN_OLED_MOSI   11
#define PIN_OLED_CS     10
#define PIN_OLED_DC      9
#define PIN_OLED_RST    14
#define PIN_I2C_SDA      8   // Qwiic SDA
#define PIN_I2C_SCL     18   // Qwiic SCL
#define PIN_RGB         48   // addressable RGB LED
#define PIN_BUTTON       0   // BOOT button (active low, RTC GPIO: can wake the board)
#define PIN_VBAT_SENSE   4   // battery voltage / 2
#define PIN_VBUS_SENSE   2   // USB voltage / 2 (RTC GPIO: can wake the board)

// ---- Settings ----
#define LOG_INTERVAL_MIN  15       // minutes between logged readings
#define LOG_LEN           96       // readings kept: 96 x 15 min = 24 hours
#define SCREEN_MS         8000     // screen stays on this long after BOOT on battery
#define USE_FAHRENHEIT    false    // true: show temperature in degrees F

// ---- Battery (voltage at the battery connector) ----
#define VBAT_DIV          2.0f     // 100k / 100k dividers
#define VBUS_DIV          2.0f
#define VBAT_CAL_OFFSET   0.070f   // V; the ADC reads about 70 mV low (measured)
#define VBAT_CUTOFF_V     3.30f    // below this on battery: stop and sleep until USB
#define VBAT_RESUME_V     3.50f    // after a cutoff, resume logging only above this

U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
    U8G2_R0, PIN_OLED_SCLK, PIN_OLED_MOSI, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RST);
Adafruit_SHT4x sht4;

// ---- Log, kept in RTC memory through deep sleep ----
RTC_DATA_ATTR int16_t  logT[LOG_LEN];      // temperature x 100, degrees C
RTC_DATA_ATTR uint8_t  logH[LOG_LEN];      // relative humidity, %
RTC_DATA_ATTR uint16_t logCount = 0;
RTC_DATA_ATTR uint16_t logHead  = 0;
RTC_DATA_ATTR int64_t  lastLogS = 0;       // when the last reading was logged (s)
RTC_DATA_ATTR bool     haveLog  = false;
RTC_DATA_ATTR bool     battLow  = false;

bool displayOn     = false;
bool sensorOK      = false;
bool chartHumidity = false;
float liveT = NAN, liveH = NAN;

// ==================== Time (keeps counting through deep sleep) ====================
static int64_t nowS() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec;
}
static int64_t secondsToNextLog() {
  if (!haveLog) return 0;
  int64_t left = (int64_t)LOG_INTERVAL_MIN * 60 - (nowS() - lastLogS);
  return left < 0 ? 0 : left;
}

// ==================== Sensor ====================
// [SWAP] To use a different temperature/humidity sensor, change only these two.
static bool sensorInit() {
  if (!sht4.begin(&Wire)) return false;
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  return true;
}
static bool sensorRead(float &tC, float &rh) {
  sensors_event_t h, t;
  if (!sht4.getEvent(&h, &t)) return false;
  tC = t.temperature;
  rh = h.relative_humidity;
  return true;
}
static bool readLive() {
  if (!sensorOK) sensorOK = sensorInit();
  if (sensorOK && sensorRead(liveT, liveH)) return true;
  sensorOK = false;
  liveT = liveH = NAN;
  return false;
}

static float toDisplayTemp(float tC) { return USE_FAHRENHEIT ? tC * 9.0f / 5.0f + 32.0f : tC; }

static void logReading() {
  lastLogS = nowS();            // keep the schedule even if this reading fails
  haveLog = true;
  if (!readLive()) {
    Serial.println("Log: no sensor on the Qwiic connector - reading skipped");
    return;
  }
  logT[logHead] = (int16_t)lroundf(liveT * 100.0f);
  logH[logHead] = (uint8_t)constrain(lroundf(liveH), 0, 100);
  logHead = (logHead + 1) % LOG_LEN;
  if (logCount < LOG_LEN) logCount++;
  Serial.printf("Log #%u: %.2f C, %.0f %%RH\n", logCount, liveT, liveH);
}

static void dumpLogCsv() {
  Serial.println("index,minutes_ago,temperature_C,humidity_pct");
  int64_t sinceLast = haveLog ? (nowS() - lastLogS) / 60 : 0;
  for (int i = 0; i < logCount; i++) {
    int k = (logHead - logCount + i + LOG_LEN) % LOG_LEN;
    long minutesAgo = (long)((logCount - 1 - i) * LOG_INTERVAL_MIN + sinceLast);
    Serial.printf("%d,%ld,%.2f,%u\n", i + 1, minutesAgo, logT[k] / 100.0f, logH[k]);
  }
}

// ==================== Power ====================
static float readVbat() {
  const int n = 16;
  uint32_t acc = 0;
  for (int i = 0; i < n; i++) { acc += analogReadMilliVolts(PIN_VBAT_SENSE); delayMicroseconds(200); }
  return (acc / (float)n) / 1000.0f * VBAT_DIV + VBAT_CAL_OFFSET;
}
static bool usbPresent() {
  return analogReadMilliVolts(PIN_VBUS_SENSE) / 1000.0f * VBUS_DIV > 4.0f;
}
static int lipoPct(float v) {
  static const float P[][2] = {{4.20, 100}, {4.13, 90}, {4.06, 80}, {3.98, 70}, {3.92, 60},
                               {3.87, 50}, {3.82, 40}, {3.79, 30}, {3.75, 20}, {3.70, 10},
                               {3.60, 5},  {3.40, 0}};
  const int n = sizeof(P) / sizeof(P[0]);
  if (v >= P[0][0]) return 100;
  for (int i = 0; i < n - 1; i++) {
    if (v <= P[i][0] && v >= P[i + 1][0])
      return (int)(P[i + 1][1] + (v - P[i + 1][0]) * (P[i][1] - P[i + 1][1]) / (P[i][0] - P[i + 1][0]) + 0.5f);
  }
  return 0;
}

// ==================== Screen ====================
static void displayBegin() {
  if (displayOn) return;
  u8g2.begin();
  u8g2.enableUTF8Print();
  displayOn = true;
}

static void drawLogChart(int x0, int y0, int w, int h) {
  u8g2.drawFrame(x0, y0, w, h);
  u8g2.setFont(u8g2_font_4x6_tr);
  if (logCount < 2) {
    u8g2.drawStr(x0 + 4, y0 + h / 2 + 3, "Chart starts after 2 readings");
    return;
  }
  float lo = 1e9, hi = -1e9;
  for (int i = 0; i < logCount; i++) {
    int k = (logHead - logCount + i + LOG_LEN) % LOG_LEN;
    float v = chartHumidity ? logH[k] : toDisplayTemp(logT[k] / 100.0f);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  float minSpan = chartHumidity ? 5.0f : (USE_FAHRENHEIT ? 2.0f : 1.0f);
  if (hi - lo < minSpan) { float m = (hi + lo) / 2; lo = m - minSpan / 2; hi = m + minSpan / 2; }
  char buf[12];
  snprintf(buf, sizeof(buf), "%.1f", hi);
  u8g2.drawStr(x0 + 2, y0 + 7, buf);
  snprintf(buf, sizeof(buf), "%.1f", lo);
  u8g2.drawStr(x0 + 2, y0 + h - 2, buf);
  int px = -1, py = -1;
  for (int i = 0; i < logCount; i++) {
    int k = (logHead - logCount + i + LOG_LEN) % LOG_LEN;
    float v = chartHumidity ? logH[k] : toDisplayTemp(logT[k] / 100.0f);
    int x = x0 + w - 2 - (logCount - 1 - i) * (w - 3) / (LOG_LEN - 1);
    int y = y0 + h - 2 - (int)((v - lo) / (hi - lo) * (h - 4) + 0.5f);
    if (px >= 0) u8g2.drawLine(px, py, x, y);
    px = x;
    py = y;
  }
}

static void drawScreen(bool usb, float vbat) {
  char buf[32];
  displayBegin();
  u8g2.clearBuffer();

  // Live reading.
  if (!isnan(liveT)) {
    u8g2.setFont(u8g2_font_logisoso16_tf);
    snprintf(buf, sizeof(buf), "%.1f", toDisplayTemp(liveT));
    u8g2.drawStr(0, 17, buf);
    int tx = u8g2.getStrWidth(buf) + 2;
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawUTF8(tx, 8, USE_FAHRENHEIT ? "\xC2\xB0" "F" : "\xC2\xB0" "C");
    u8g2.setFont(u8g2_font_logisoso16_tf);
    snprintf(buf, sizeof(buf), "%.0f%%", liveH);
    u8g2.drawStr(128 - u8g2.getStrWidth(buf), 17, buf);
  } else {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, "No sensor - plug a");
    u8g2.drawStr(0, 21, "Qwiic temp/RH sensor");
  }

  // Status line: power and log.
  u8g2.setFont(u8g2_font_5x7_tr);
  if (usb) snprintf(buf, sizeof(buf), "USB power");
  else     snprintf(buf, sizeof(buf), "Batt %.2fV %d%%", vbat, lipoPct(vbat));
  u8g2.drawStr(0, 28, buf);
  snprintf(buf, sizeof(buf), "%s %uh log", chartHumidity ? "RH" : "T",
           (unsigned)((logCount ? logCount - 1 : 0) * LOG_INTERVAL_MIN / 60));
  u8g2.drawStr(128 - u8g2.getStrWidth(buf), 28, buf);

  drawLogChart(0, 31, 128, 33);
  u8g2.sendBuffer();
}

static void drawLowBattery(float vbat) {
  displayBegin();
  u8g2.clearBuffer();
  u8g2.drawFrame(47, 2, 30, 14);
  u8g2.drawBox(77, 6, 3, 6);
  u8g2.setFont(u8g2_font_helvB10_tr);
  const char *l1 = "Battery low";
  u8g2.drawStr((128 - u8g2.getStrWidth(l1)) / 2, 34, l1);
  u8g2.setFont(u8g2_font_6x12_tr);
  const char *l2 = "charge me";
  u8g2.drawStr((128 - u8g2.getStrWidth(l2)) / 2, 50, l2);
  char vs[12];
  snprintf(vs, sizeof(vs), "%.2f V", vbat);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr((128 - u8g2.getStrWidth(vs)) / 2, 63, vs);
  u8g2.sendBuffer();
}

// ==================== Button ====================
static bool buttonPressed() {           // true once per press, debounced
  static bool last = HIGH;
  static uint32_t tChange = 0;
  bool now = digitalRead(PIN_BUTTON);
  if (now != last && millis() - tChange > 40) {
    tChange = millis();
    last = now;
    if (now == LOW) return true;
  }
  return false;
}

// ==================== Deep sleep ====================
// Pins that could switch a load on during sleep are held: RGB data low, OLED
// chip-select and reset high (the panel stays asleep and ignores the bus).
static void releaseSleepHolds() {
  gpio_hold_dis((gpio_num_t)PIN_RGB);
  gpio_hold_dis((gpio_num_t)PIN_OLED_CS);
  gpio_hold_dis((gpio_num_t)PIN_OLED_RST);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  gpio_deep_sleep_hold_dis();
#endif
}
static void holdPinsForSleep() {
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

// seconds = 0: no timer (low battery). BOOT and plugging in USB always wake it.
static void sleepNow(int64_t seconds) {
  neopixelWrite(PIN_RGB, 0, 0, 0);
  if (displayOn) u8g2.setPowerSave(1);                  // OLED off
  while (digitalRead(PIN_BUTTON) == LOW) delay(10);     // don't wake again at once
  holdPinsForSleep();

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);          // BOOT pressed
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  if (seconds > 0) esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_VBUS_SENSE, ESP_EXT1_WAKEUP_ANY_HIGH);  // USB plugged in

  Serial.printf("Sleeping %s\n", seconds > 0 ? "until the next reading" : "until USB is plugged in");
  Serial.flush();
  esp_deep_sleep_start();                               // does not return
}

static void sleepUntilNextReading() {
  int64_t s = secondsToNextLog();
  sleepNow(s < 5 ? 5 : s);
}

// ==================== Setup / loop ====================
void setup() {
  releaseSleepHolds();
  rtc_gpio_deinit(GPIO_NUM_0);                          // BOOT back to a normal GPIO after wake
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool coldBoot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);   // power-on or RESET
  bool userWake = (cause == ESP_SLEEP_WAKEUP_EXT0);        // BOOT pressed

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_VBUS_SENSE, ADC_11db);
  bool usb = usbPresent();
  if (!usb) setCpuFrequencyMhz(80);                     // less current while awake on battery

  Serial.begin(115200);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  neopixelWrite(PIN_RGB, 0, 0, 0);

  if (coldBoot) {                                       // new log after power-on or RESET
    logCount = 0; logHead = 0; haveLog = false; battLow = false;
  }

  float vbat = readVbat();

  // Low battery: never run the cell flat. Resume only once it is charged.
  if (!usb && (battLow ? vbat < VBAT_RESUME_V : vbat < VBAT_CUTOFF_V)) {
    battLow = true;
    Serial.printf("Battery low (%.2f V) - logging stopped until USB is plugged in\n", vbat);
    if (coldBoot || userWake) { drawLowBattery(vbat); delay(3000); }
    sleepNow(0);
  }
  battLow = false;

  if (!haveLog || secondsToNextLog() <= 5) logReading();   // a reading is due

  if (usb) return;                                     // stay awake: see loop()

  if (coldBoot || userWake) {                           // show the screen, then sleep
    readLive();
    drawScreen(false, vbat);
    uint32_t t0 = millis();
    while (millis() - t0 < SCREEN_MS) {
      if (buttonPressed()) {
        chartHumidity = !chartHumidity;
        drawScreen(false, vbat);
        t0 = millis();                                  // keep the screen on a bit longer
      }
      delay(10);
    }
  }
  sleepUntilNextReading();
}

void loop() {                                           // only runs on USB power
  static uint32_t tDraw = 0;

  if (!usbPresent()) {                                  // USB unplugged: battery mode
    Serial.println("USB unplugged - switching to battery logging");
    sleepUntilNextReading();
  }

  if (secondsToNextLog() == 0) logReading();

  if (buttonPressed()) {
    chartHumidity = !chartHumidity;
    tDraw = 0;
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'd' || c == 'D') dumpLogCsv();
  }

  if (tDraw == 0 || millis() - tDraw >= 2000) {
    tDraw = millis();
    readLive();
    drawScreen(true, readVbat());
    if (sensorOK) neopixelWrite(PIN_RGB, 0, 6, 0);       // dim green: sensor OK
    else          neopixelWrite(PIN_RGB, 6, 0, 0);       // dim red: no sensor
  }
  delay(10);
}
