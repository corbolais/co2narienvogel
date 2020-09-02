# CO<sub>2</sub>narienvogel

The project of the Netzbasteln radio-show #148 is a canary bird that measures CO<sub>2</sub> concentration in room air and warns if levels become critical for Covid-19 infection risk. Just like a canary bird warned miners when "[Evil Weather](https://de.wikipedia.org/wiki/B%C3%B6se_Wetter)" occured.

## Threshold values
| LED color                 |CO<sub>2</sub> concentration |
|:--------------------------|:----------------------------|
| Green ("all good")        | < 1000 ppm                  |
| Yellow ("open windows")   | 1000 – 2000 ppm             |
| Red ("leave room")        | \> 2000 ppm                 |

Based on [coro2sens](https://github.com/kmetz/coro2sens) and [ideas from Umwelt-Campus Birkenfeld](https://www.umwelt-campus.de/forschung/projekte/iot-werkstatt/ideen-zur-corona-krise), which are based on 2008 [research by the German Federal Environmental Agency](https://www.umweltbundesamt.de/sites/default/files/medien/pdfs/kohlendioxid_2008.pdf).


## Material
1. Any ESP32 or ESP8266 board (like a WEMOS D32 or WEMOS D1 Mini).
1. ([Sensirion SCD30](https://www.sensirion.com/de/umweltsensoren/kohlendioxidsensor/kohlendioxidsensoren-co2/)) carbon dioxide sensor module.
1. 1 NeoPixel compatible RGB LED (WS2812B). 
1. Optional: BME280 I<sup>2</sup>C pressure sensor module, improves accuracy.   
1. 3V piezo buzzer or simple speaker.
1. SG90 5V servo motor.
1. A bird :)


### Wiring

| ESP pin      | goes to                                               |
|:-------------|:------------------------------------------------------|
| 3V3          | SCD30 VIN, BME280 VIN                                 |
| 5V           | LED +5V, if you dare: Servo (+) (pulls about 70ma)     |
| GND          | SCD30 GND, BME280 GND, LED GND, Buzzer (-), Servo (-) |
| SCL / D1     | SCD30 SCL, BME280 SCL                                 |
| SDA / D2     | SCD30 SDA, BME280 SDA                                 |
| GPIO 0 / D3  | LED DIN                                               |
| GPIO 14 / D5 | Buzzer (+)                                            |
| GPIO 12 / D6 | Servo PWM (often brown)                               |

### Flash using the Arduino IDE
- Rename `src/main.cpp` to `co2narienvogel.ino` and place it in a folder named `co2narienvogel`.
- Open `co2narienvogel.ino` in the Arduino IDE.
- Install all libraries mentioned in `platformio.ini` (the `lib_deps` section) using the library manager.
- Upload (hope it works).
