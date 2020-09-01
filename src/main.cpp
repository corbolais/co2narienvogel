#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>

#ifdef ESP32
#include <SparkFun_SCD30_Arduino_Library.h>
#include <Tone32.h>
#else

#include <paulvha_SCD30.h>

#endif


// SETUP -----------------------------------------

// LED (always on).
// - Green: all good (CO2 level < 1000 ppm).
// - Yellow: warning, open windows (> 1000 ppm).
// - Red: critical, leave room (> 2000 ppm).
#define LED_PIN 0
#define LED_BRIGHTNESS 37

// Buzzer.
#define BUZZER_PIN 14
#define SING_INTERVAL_S 10 // Mean sing interval (randomized).

// Servo.
#define SERVO_PIN 12
#define SERVO_POS_UP 0
#define SERVO_POS_DN 180
#define SERVO_MOVE_TIME_MS 500 // Servo will be switched off after this time.

// BME280 pressure sensor (optional).
// Address should be 0x76 or 0x77.
#define BME280_I2C_ADDRESS 0x76

// Update CO2 level every MEASURE_INTERVAL_S seconds.
// Can range from 2 to 1800.
#define MEASURE_INTERVAL_S 2

// -----------------------------------------------


SCD30 scd30;
Adafruit_NeoPixel led = Adafruit_NeoPixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_BME280 bme280;
Servo servo;
bool bme280isConnected = false;
sensors_event_t pressureEvent;
uint16_t co2 = 0;
uint16_t pressure = 0;
bool alarmHasTriggered = false;
int sAngulo, sCounter = 0;
unsigned long nextSingTime, now = 0;


void birdUp() {
  servo.attach(SERVO_PIN);
  servo.write(SERVO_POS_UP);
  delay(SERVO_MOVE_TIME_MS);
  servo.detach();
}


void birdDown() {
  servo.attach(SERVO_PIN);
  servo.write(SERVO_POS_DN);
  delay(SERVO_MOVE_TIME_MS);
  servo.detach();
}


void singHighChirp(int intensity, int chirpsNumber) {
  for (int veces = 0; veces <= chirpsNumber; veces++) {
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
  for (int veces = 0; veces <= chirpsNumber; veces++) {
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
  for (int veces = 0; veces < chirpsNumber; veces++) {
    for (int i = 80; i > 0; i--) {
      for (int x = 0; x < intensity; x++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(i);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(i);
      }
    }
  }

  delay(random(800, 1200));
}


/**
 * Play bird sounds.
 */
void sing() {
  for (int i = random(1, 3); i > 0; i--) {
    sAngulo = random(20, 50);
    sCounter = random(2, 6);

    // Makes the sound according to: intensity, varies: normally 5. number of times: how many times tweets, normally 3-6.
    singHighChirp(5, sAngulo / 10);
    delay(random(80, 120));
    singLowChirp(sAngulo * 4, 2);
    delay(random(80, 120));
    singTweet(sCounter, 2);

    // Delay between the closer tweets.
    delay(random(200, 700));
  }
}


void setup() {
  Serial.begin(115200);

  // Initialize pins.
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SERVO_PIN, OUTPUT);

  // Initialize servo.
  birdAlive();

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
  if (bme280.begin(BME280_I2C_ADDRESS, &Wire)) {
    Serial.println("BMP280 pressure sensor detected.");
    bme280isConnected = true;
    bme280.setSampling(Adafruit_BME280::MODE_FORCED,
                       Adafruit_BME280::SAMPLING_X1, // Temperature.
                       Adafruit_BME280::SAMPLING_X16, // Pressure.
                       Adafruit_BME280::SAMPLING_X1, // Humidity.
                       Adafruit_BME280::FILTER_X16);
  }
  else {
    Serial.println("BMP280 pressure sensor not detected. Please check wiring. Continuing without ambient pressure compensation.");
  }
}


void loop() {
  // Read sensors.
  if (bme280isConnected) {
    bme280.takeForcedMeasurement();
    pressure = (uint16_t) (bme280.readPressure() / 100);
    scd30.setAmbientPressure(pressure);
  }
  if (scd30.dataAvailable()) {
    co2 = scd30.getCO2();
  }

  Serial.printf(bme280isConnected ? "%d ppm, %d hPa\r\n" : "%d ppm\r\n", co2, pressure);

  // Update LED.
  if (co2 < 1000) {
    led.setPixelColor(0, 0, 255, 0); // Green.
  }
  else if (co2 < 2000) {
    led.setPixelColor(0, 255, 255, 0); // Yellow.
  }
  else {
    led.setPixelColor(0, 255, 0, 0); // Red.
  }
  led.show();

  // Trigger alarms.
  if (co2 >= 2000) {
    if (!alarmHasTriggered) {
      birdDown();
      alarmHasTriggered = true;
    }
  }
  else if (alarmHasTriggered) {
    birdUp();
    alarmHasTriggered = false;
  }

  // Play sounds.
  now = millis();
  if (co2 < 2000 && nextSingTime < now) {
    sing();
    nextSingTime = now + (random(SING_INTERVAL_S /2, SING_INTERVAL_S * 1.5) * 1000);
  }

  delay(MEASURE_INTERVAL_S * 1000);
}
