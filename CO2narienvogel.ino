#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>
#include <SparkFunBME280.h>
#include <paulvha_SCD30.h>


// SETUP -----------------------------------------

// LED (always on).
// - Green: all good (CO2 level < 1000 ppm).
// - Yellow: warning, open windows (> 1000 ppm).
// - Red: critical, leave room (> 2000 ppm).
#define LED_PIN D3
#define LED_BRIGHTNESS 255

// Buzzer.
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

// -----------------------------------------------


SCD30 scd30;
Adafruit_NeoPixel led = Adafruit_NeoPixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);
BME280 bme280;
Servo servo;
bool bme280isConnected = false;
uint16_t co2 = 0;
uint16_t pressure = 0;
bool alarmHasTriggered = false;
unsigned long nextSingTime, now = 0;


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
  alarmSound(5);
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

  delay(random(800, 1200));
}


/**
 * Play bird sounds.
 */
void sing() {
  for (int i = random(2, 6); i > 0; i--) {
    int seq = random(0, 2);
    if (seq == 0) {
      singHighChirp(5, random(20, 50) / 10);
    }
    if (seq == 1) {
      singLowChirp(random(20, 50) * 4, 2);
    }

    if (seq == 2) {
      singTweet(random(2, 6), 2);
    }
    delay(random(80, 120));
  }
}


void alarmSound(uint times) {
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
}


void loop() {
  // Read sensors.
  if (bme280isConnected) {
    pressure = (uint16_t)(bme280.readFloatPressure() / 100);
    scd30.setAmbientPressure(pressure);
  }
  if (scd30.dataAvailable()) {
    co2 = scd30.getCO2();
  }

  // Log all sensor values.
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
  if (co2 < 2000 && nextSingTime < now) {
    sing();
    nextSingTime = now + (random(SING_INTERVAL_S / 2, SING_INTERVAL_S * 1.5) * 1000);
  }

  delay(MEASURE_INTERVAL_S * 1000);
}
