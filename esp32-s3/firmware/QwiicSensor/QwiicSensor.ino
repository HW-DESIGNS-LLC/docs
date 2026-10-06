/*
 * Crescent City - Qwiic sensor in 5 minutes
 * ============================================================================
 * Plug a Qwiic / STEMMA QT temperature & humidity sensor (SHT40, SHT41 or
 * SHT45, I2C address 0x44) into the side connector and watch live readings and
 * a scrolling chart on the built-in OLED. No soldering and no Wi-Fi needed.
 *
 *   - Top of the screen: temperature and humidity, updated every second.
 *   - Bottom: a chart of the last 2 minutes (one point per second).
 *   - BOOT button: switch the chart between temperature and humidity.
 *   - Unplug or re-plug the sensor at any time; the sketch finds it again.
 *   - RGB LED: dim green while readings are good, dim red with no sensor.
 *   - Serial Monitor / Serial Plotter (115200 baud): one line per reading.
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

// ---- Board pins (Crescent City V1.0 and V2.0) ----
#define PIN_OLED_SCLK  12
#define PIN_OLED_MOSI  11
#define PIN_OLED_CS    10
#define PIN_OLED_DC     9
#define PIN_OLED_RST   14
#define PIN_I2C_SDA     8   // Qwiic SDA
#define PIN_I2C_SCL    18   // Qwiic SCL
#define PIN_RGB        48   // addressable RGB LED
#define PIN_BUTTON      0   // BOOT button (active low)

// ---- Settings ----
#define USE_FAHRENHEIT  false   // true: show temperature in degrees F
#define SAMPLE_MS       1000    // one reading per second
#define RETRY_MS        2000    // look for a sensor this often when none is found
#define LED_LEVEL       8       // RGB LED brightness (0-255)

U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
    U8G2_R0, PIN_OLED_SCLK, PIN_OLED_MOSI, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RST);
Adafruit_SHT4x sht4;

// Chart history: one sample per second, oldest first when read back.
const int N = 120;
float histT[N], histH[N];
int count = 0, head = 0;

bool sensorOK = false;
bool chartHumidity = false;
float lastT = NAN, lastH = NAN;

// ==================== Sensor ====================
// [SWAP] To use a different temperature/humidity sensor, change only these two.
static bool sensorInit() {
  if (!sht4.begin(&Wire)) return false;
  sht4.setPrecision(SHT4X_MED_PRECISION);
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

static float toDisplayTemp(float tC) { return USE_FAHRENHEIT ? tC * 9.0f / 5.0f + 32.0f : tC; }

static void addSample(float t, float h) {
  histT[head] = t;
  histH[head] = h;
  head = (head + 1) % N;
  if (count < N) count++;
}

// ==================== Screens ====================
static void drawNoSensor() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  const char *l1 = "No sensor";
  u8g2.drawStr((128 - u8g2.getStrWidth(l1)) / 2, 18, l1);
  u8g2.setFont(u8g2_font_6x12_tr);
  const char *l2 = "Plug a Qwiic temp /";
  const char *l3 = "humidity sensor into";
  const char *l4 = "the side connector";
  u8g2.drawStr((128 - u8g2.getStrWidth(l2)) / 2, 36, l2);
  u8g2.drawStr((128 - u8g2.getStrWidth(l3)) / 2, 48, l3);
  u8g2.drawStr((128 - u8g2.getStrWidth(l4)) / 2, 60, l4);
  u8g2.sendBuffer();
}

static void drawChart(const float *hist, float minSpan, int x0, int y0, int w, int h) {
  if (count < 2) return;
  // Find the range of the stored samples.
  float lo = 1e9, hi = -1e9;
  for (int i = 0; i < count; i++) {
    float v = hist[(head - count + i + N) % N];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  if (hi - lo < minSpan) {               // keep small changes from looking huge
    float mid = (hi + lo) / 2;
    lo = mid - minSpan / 2;
    hi = mid + minSpan / 2;
  }
  // Frame and min/max labels.
  u8g2.drawFrame(x0, y0, w, h);
  u8g2.setFont(u8g2_font_4x6_tr);
  char buf[12];
  snprintf(buf, sizeof(buf), "%.1f", hi);
  u8g2.drawStr(x0 + 2, y0 + 7, buf);
  snprintf(buf, sizeof(buf), "%.1f", lo);
  u8g2.drawStr(x0 + 2, y0 + h - 2, buf);
  // Plot: newest sample at the right edge.
  int prevX = -1, prevY = -1;
  for (int i = 0; i < count; i++) {
    float v = hist[(head - count + i + N) % N];
    int x = x0 + w - 2 - (count - 1 - i) * (w - 3) / (N - 1);
    int y = y0 + h - 2 - (int)((v - lo) / (hi - lo) * (h - 4) + 0.5f);
    if (prevX >= 0) u8g2.drawLine(prevX, prevY, x, y);
    prevX = x;
    prevY = y;
  }
}

static void drawReadings() {
  char buf[16];
  u8g2.clearBuffer();

  // Temperature (left) and humidity (right).
  u8g2.setFont(u8g2_font_logisoso16_tf);
  snprintf(buf, sizeof(buf), "%.1f", toDisplayTemp(lastT));
  u8g2.drawStr(0, 18, buf);
  int tx = u8g2.getStrWidth(buf) + 2;
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawUTF8(tx, 9, USE_FAHRENHEIT ? "\xC2\xB0" "F" : "\xC2\xB0" "C");

  u8g2.setFont(u8g2_font_logisoso16_tf);
  snprintf(buf, sizeof(buf), "%.0f%%", lastH);
  u8g2.drawStr(128 - u8g2.getStrWidth(buf), 18, buf);

  // Chart label and chart.
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 28, chartHumidity ? "Humidity, last 2 min" : "Temp, last 2 min");
  u8g2.drawStr(108, 28, "BOOT");
  if (chartHumidity) {
    drawChart(histH, 5.0f, 0, 31, 128, 33);
  } else {
    static float histDisp[N];
    for (int i = 0; i < N; i++) histDisp[i] = toDisplayTemp(histT[i]);
    drawChart(histDisp, USE_FAHRENHEIT ? 2.0f : 1.0f, 0, 31, 128, 33);
  }
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

// ==================== Setup / loop ====================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  neopixelWrite(PIN_RGB, 0, 0, 0);

  u8g2.begin();
  u8g2.enableUTF8Print();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  sensorOK = sensorInit();
  if (!sensorOK) drawNoSensor();
  Serial.println(sensorOK ? "Sensor found" : "No sensor found - plug one into the Qwiic connector");
}

void loop() {
  static uint32_t tSample = 0, tRetry = 0;

  if (buttonPressed()) {
    chartHumidity = !chartHumidity;
    if (sensorOK && !isnan(lastT)) drawReadings();
  }

  if (!sensorOK) {
    neopixelWrite(PIN_RGB, LED_LEVEL, 0, 0);            // dim red: no sensor
    if (millis() - tRetry >= RETRY_MS) {
      tRetry = millis();
      sensorOK = sensorInit();                          // Qwiic is hot-pluggable
      if (sensorOK) {
        Serial.println("Sensor found");
        tSample = 0;
      } else {
        drawNoSensor();
      }
    }
    delay(10);
    return;
  }

  if (tSample == 0 || millis() - tSample >= SAMPLE_MS) {
    tSample = millis();
    float t, h;
    if (sensorRead(t, h)) {
      lastT = t;
      lastH = h;
      addSample(t, h);
      neopixelWrite(PIN_RGB, 0, LED_LEVEL, 0);          // dim green: reading OK
      drawReadings();
      // "Name:value" pairs work in the Arduino Serial Plotter.
      Serial.printf("Temperature:%.2f,Humidity:%.1f\n", toDisplayTemp(t), h);
    } else {
      Serial.println("Sensor read failed - was it unplugged?");
      sensorOK = false;
      tRetry = 0;
    }
  }
  delay(10);
}
