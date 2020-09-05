#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>
#include <SparkFunBME280.h>
#include <paulvha_SCD30.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>


// SETUP -----------------------------------------

// CO2 Thresholds (ppm).
//
// Recommendation from REHVA (Federation of European Heating, Ventilation and Air Conditioning associations, rehva.eu)
// for preventing COVID-19 aerosol spread especially in schools:
// - warn: 800, critical: 1000
// (https://www.rehva.eu/fileadmin/user_upload/REHVA_COVID-19_guidance_document_V3_03082020.pdf)
//
// General air quality recommendation by the German Federal Environmental Agency (2008):
// - warn: 1000, critical: 2000
// (https://www.umweltbundesamt.de/sites/default/files/medien/pdfs/kohlendioxid_2008.pdf)
//
#define CO2_WARN_PPM 1000
#define CO2_CRITICAL_PPM 2000

// LED warning light (always on, green / yellow / red).
#define LED_PIN D3
#define LED_BRIGHTNESS 255

// Buzzer/Speaker.
#define BUZZER_PIN D5
#define SING_INTERVAL_S 10 // Mean sing interval (randomized).

// Servo.
#define SERVO_PIN D6
#define SERVO_POS_UP 0
#define SERVO_POS_DN 60
#define SERVO_MOVE_TIME_MS 500 // Servo will be switched off after this time.

// BME280 pressure sensor (optional).
// Address should be 0x76 or 0x77.
#define BME280_I2C_ADDRESS 0x76

// Update CO2 level every MEASURE_INTERVAL_S seconds.
// Can range from 2 to 1800.
#define MEASURE_INTERVAL_S 2

// WiFi captive portal showing sensor values.
// Set to 0 to disable.
#define WIFI_PORTAL_ENABLED 1
#define WIFI_AP_NAME "CO2narienvogel"

// How long the graph/log in the WiFi portal should go back, in minutes.
#define LOG_MINUTES 60
// Label describing the time axis.
#define TIME_LABEL "1 Stunde"

// -----------------------------------------------


#define GRAPH_W 600
#define GRAPH_H 260
#define LOG_SIZE GRAPH_W

SCD30 scd30;
BME280 bme280;
bool bme280isConnected = false;

uint16_t pressure = 0;
uint16_t co2 = 0;
uint16_t co2log[LOG_SIZE] = {0}; // Ring buffer.
uint32_t co2logPos = 0; // Current buffer start position.
uint16_t co2logDownsample = max(1, ((((LOG_MINUTES) * 60) / MEASURE_INTERVAL_S) / LOG_SIZE));
uint16_t co2avg, co2avgSamples = 0; // Used for downsampling.

unsigned long nextSingTime, now = 0;
unsigned long lastMeasureTime = 0;
bool alarmHasTriggered = false;

Adafruit_NeoPixel led = Adafruit_NeoPixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);
Servo servo;

AsyncWebServer server(80);
IPAddress apIP(10, 0, 0, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;


/**
 * Moves the servo to given position.
 *
 * @param position degrees
 * @param moveTime ms
 */
void moveServo(int position, uint moveTime = SERVO_MOVE_TIME_MS) {
  servo.attach(SERVO_PIN);
  servo.write(position);
  delay(moveTime);
  servo.detach();
}


/**
 * Triggered once when the CO2 level goes critical.
 */
void alarmOnce() {
  moveServo(SERVO_POS_DN);
  siren(5);
}


/**
 * Triggered once when the CO2 level becomes yellow again.
 */
void alarmOnceDone() {
  moveServo(SERVO_POS_UP);
}


/**
 * Triggered continuously when the CO2 level is critical.
 */
void alarmContinuous() {

}


void singHighChirp(int intensity, int chirpsNumber) {
  for (int times = 0; times <= chirpsNumber; times++) {
    for (int i = 100; i > 0; i--) {
      for (int x = 0; x < intensity; x++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(i);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(i);
      }
    }
  }
}

void singLowChirp(int intensity, int chirpsNumber) {
  for (int times = 0; times <= chirpsNumber; times++) {
    for (int i = 0; i < 200; i++) {
      digitalWrite(BUZZER_PIN, HIGH);
      delayMicroseconds(i);
      digitalWrite(BUZZER_PIN, LOW);
      delayMicroseconds(i);
    }
    for (int i = 90; i > 80; i--) {
      for (int x = 0; x < 5; x++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(i);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(i);
      }
    }
  }
}

void singTweet(int intensity, int chirpsNumber) {
  // Normal chirpsNumber 3, normal intensity 5
  for (int times = 0; times < chirpsNumber; times++) {
    for (int i = 80; i > 0; i--) {
      for (int x = 0; x < intensity; x++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(i);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(i);
      }
    }
  }
}


/**
 * Play bird sounds.
 */
void sing() {
  int seq;
  for (int i = random(2, 6); i > 0; i--) {
    seq = random(0, 2);
    if (seq == 0) {
      singHighChirp(5, random(20, 50) / 10);
    }
    if (seq == 1) {
      singLowChirp(random(20, 50) * 4, 2);
    }
    delay(random(80, 120));
  }
  if (seq == 1 && random(0, 4) >= 1) {
    singTweet(random(2, 6), 3);
  }
}


/**
 * Play a siren sound.
 */
void siren(uint times) {
  for (int i = 0; i < times; i++) {
    // Whoop up
    for (int hz = 440; hz < 1000; hz += 25) {
      tone(BUZZER_PIN, hz, 50);
      delay(5);
    }
    // Whoop down
    for (int hz = 1000; hz > 440; hz -= 25) {
      tone(BUZZER_PIN, hz, 50);
      delay(5);
    }
  }
}


/**
 * Handle requests for the captive portal.
 */
void handleCaptivePortal(AsyncWebServerRequest *request) {
  Serial.println("handleCaptivePortal");
  AsyncResponseStream *response = request->beginResponseStream("text/html");

  response->print("<!DOCTYPE html><html><head>");
  response->print("<title>CO2narienvogel</title>");
  response->print(R"(<meta content="width=device-width,initial-scale=1" name="viewport">)");
  response->printf(R"(<meta http-equiv="refresh" content="%d">)", max(MEASURE_INTERVAL_S, 10));
  response->print(R"(<style type="text/css">* { font-family:sans-serif }</style>)");
  response->print("</head><body>");

  // Current measurement.
  response->printf(R"(<h1><span style="color:%s">&#9679;</span> %d ppm CO<sub>2</sub></h1>)",
                   co2 > CO2_CRITICAL_PPM ? "red" : co2 > CO2_WARN_PPM ? "yellow" : "green", co2);

  // Generate SVG graph.
  uint16_t maxVal = CO2_CRITICAL_PPM + (CO2_CRITICAL_PPM - CO2_WARN_PPM);
  for (uint16_t val : co2log) {
    if (val > maxVal) {
      maxVal = val;
    }
  }
  uint w = GRAPH_W, h = GRAPH_H, x, y;
  uint16_t val;
  response->printf(R"(<svg width="100%%" height="100%%" viewBox="0 0 %d %d">)", w, h);
  // Background.
  response->printf(R"(<rect style="fill:#FFC1B0; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
                   0, 0, w, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h));
  response->printf(R"(<rect style="fill:#FFFCB3; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
                   0, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h), w, (int) map(CO2_WARN_PPM, 0, maxVal, 0, h));
  response->printf(R"(<rect style="fill:#AFF49D; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
                   0, (int) map(maxVal - CO2_WARN_PPM, 0, maxVal, 0, h), w, (int) map(CO2_WARN_PPM, 0, maxVal, 0, h));
  // Threshold values.
  response->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">> %d ppm</text>)",
                   4, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h) - 6, CO2_CRITICAL_PPM);
  response->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">< %d ppm</text>)",
                   4, (int) map(maxVal - CO2_WARN_PPM, 0, maxVal, 0, h) + 12, CO2_WARN_PPM);
  // Plot line.
  response->print(R"(<path style="fill:none; stroke:black; stroke-width:2px; stroke-linejoin:round" d=")");
  for (uint32_t i = 0; i < LOG_SIZE; i += (LOG_SIZE / w)) {
    val = co2log[(co2logPos + i) % LOG_SIZE];
    x = (int) map(i, 0, LOG_SIZE, 0, w + (w / LOG_SIZE));
    y = h - (int) map(val, 0, maxVal, 0, h);
    response->printf("%s%d,%d", i == 0 ? "M" : "L", x, y);
  }
  response->print(R"("/>)");
  response->print("</svg>");

  // Labels.
  response->printf("<p>%s</p>", TIME_LABEL);

  response->print("</body></html>");
  request->send(response);
}


void setup() {
  Serial.begin(115200);
  Serial.println("----------------------------");
  Serial.println("Say hello to Co2narienvogel!");
  // Initialize pins.
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SERVO_PIN, OUTPUT);

  // Initialize LED.
  led.begin();
  led.setBrightness(LED_BRIGHTNESS);
  led.setPixelColor(0, 0, 0, 0);
  led.show();

  // Initialize SCD30 sensor.
  Wire.begin();
  if (scd30.begin(Wire)) {
    Serial.println("SCD30 CO2 sensor detected.");
  }
  else {
    Serial.println("SCD30 CO2 sensor not detected. Please check wiring. Freezing.");
    delay(UINT32_MAX);
  }
  scd30.setMeasurementInterval(MEASURE_INTERVAL_S);

  // Initialize BME280 sensor.
  bme280.setI2CAddress(BME280_I2C_ADDRESS);
  if (bme280.beginI2C(Wire)) {
    Serial.println("BMP280 pressure sensor detected.");
    bme280isConnected = true;
    // Settings.
    bme280.setFilter(4);
    bme280.setStandbyTime(0);
    bme280.setTempOverSample(1);
    bme280.setPressureOverSample(16);
    bme280.setHumidityOverSample(1);
    bme280.setMode(MODE_NORMAL);
  }
  else {
    Serial.println("BMP280 pressure sensor not detected. Continuing without ambient pressure compensation.");
  }

  // Initialize servo.
  moveServo(SERVO_POS_UP);

  // Initialize WiFi, DNS and web server.
  if (WIFI_PORTAL_ENABLED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netMsk);
    WiFi.softAP(WIFI_AP_NAME);
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);
    server.on("/", HTTP_GET, handleCaptivePortal);
    server.onNotFound(handleCaptivePortal);
    server.begin();
  }
}


void loop() {
  // Tasks that need to run continuously.
  if (WIFI_PORTAL_ENABLED) {
    dnsServer.processNextRequest();
  }

  // Early exit.
  if ((millis() - lastMeasureTime) < (MEASURE_INTERVAL_S * 1000)) {
    return;
  }

  // Read sensors.
  if (bme280isConnected) {
    pressure = (uint16_t)(bme280.readFloatPressure() / 100);
    scd30.setAmbientPressure(pressure);
  }
  if (scd30.dataAvailable()) {
    co2 = scd30.getCO2();
  }

  // Average (downsample) and log CO2 values for the graph.
  co2avg = ((co2avgSamples * co2avg) + co2) / (co2avgSamples + 1);
  co2avgSamples++;
  if (co2avgSamples >= co2logDownsample) {
    co2log[co2logPos] = co2avg;
    co2logPos++;
    co2logPos %= LOG_SIZE;
    co2avg = co2avgSamples = 0;
  }

  // Print all sensor values.
  Serial.printf(
    "[SCD30]  temp: %.2f°C, humid: %.2f%%, CO2: %dppm\r\n",
    scd30.getTemperature(), scd30.getHumidity(), co2
  );
  if (bme280isConnected) {
    Serial.printf(
      "[BME280] temp: %.2f°C, humid: %.2f%%, press: %dhPa\r\n",
      bme280.readTempC(), bme280.readFloatHumidity(), pressure
    );
  }
  Serial.println("-----------------------------------------------------");

  // Update LED.
  if (co2 < CO2_WARN_PPM) {
    led.setPixelColor(0, 0, 255, 0); // Green.
  }
  else if (co2 < CO2_CRITICAL_PPM) {
    led.setPixelColor(0, 255, 200, 0); // Yellow.
  }
  else {
    led.setPixelColor(0, 255, 0, 0); // Red.
  }
  led.show();

  // Trigger alarms.
  if (co2 >= CO2_CRITICAL_PPM) {
    alarmContinuous();
    if (!alarmHasTriggered) {
      alarmOnce();
      alarmHasTriggered = true;
    }
  }
  else if (alarmHasTriggered) {
    alarmOnceDone();
    alarmHasTriggered = false;
  }

  // Play sounds.
  now = millis();
  if (co2 < CO2_WARN_PPM && nextSingTime < now) {
    sing();
    nextSingTime = now + (random(SING_INTERVAL_S / 2, SING_INTERVAL_S * 1.5) * 1000);
  }

  lastMeasureTime = millis();
}
