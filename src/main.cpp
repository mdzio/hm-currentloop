/*
    LOOP CURRENT INPUT (4-20mA) FOR HOMEMATIC
    Copyright (C) 2019 MDZ (info@ccu-historian.de)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <Arduino.h>

// *** CONFIGURATION ***

// minimum valid loop current [mA]
// (12.5% underdrive range is included)
const auto LOOP_CURRENT_MIN = 2.0;

// maximum valid loop current [mA]
// (12.5% overdrive range is included)
const auto LOOP_CURRENT_MAX = 22.0;

// minimum send interval [s]
// (do not stress the duty cycle of the HomeMatic transmitter and central to
// much, e.g. use 60 seconds)
const uint32_t SEND_INTERVAL_MIN = 60;

// pause after startup before first measurement [ms]
const uint32_t STARTUP_PAUSE = 4000;

// pause between measurements [ms]
const uint32_t MEASUREMENT_PAUSE = 5000;

// pause between samples [ms]
const uint32_t SAMPLE_PAUSE = 10;

// number of samples for average calculation (noise reduction)
const int NUM_SAMPLES = 8;

// *** HARDWARE CONFIGURATION ***

// analog pin for reading the voltage drop at the shunt resistor 
// (the shunt resistor should have a value of 47 ohm. the measuring range will
// then be from 0 to 23.4 mA.)
const uint8_t LOOP_CURRENT_PIN = A6;

// pin to trigger a send 
// (pin  on the HM-MOD-EM-8Bit)
const uint8_t TRIGGER_SEND_PIN = 2; // PD2

// HomeMatic 8-bit transmitter HM-MOD-EM-8Bit
// (two ports are needed, because pins PD0 (RX) and PD1 (TX) are already used
// for debug messages.)
// HM-MOD-EM-8Bit   | Arduino Nano V3
// --------------------------------
// INH0             |  8 (PB0)
// INH1             |  9 (PB1)
// INH2             | 10 (PB2)
// INH3             | 11 (PB3)
// INH4             | 12 (PB4)
// INH5             |  5 (PD5)
// INH6             |  6 (PD6)
// INH7             |  7 (PD7)
// DUI30            |  2 (PD2)

// shunt resistor [ohm]
constexpr auto SHUNT = 47.0;

// *** CONSTANTS ***

// blink codes
const uint8_t BLINK_TIME_NOT_ELAPSED  = 1;
const uint8_t BLINK_DELTA_NOT_REACHED = 2;
const uint8_t BLINK_NOT_CHANGED       = 3;
const uint8_t BLINK_SEND              = 4;

// baud rate for monitoring [bits/s]
const unsigned long BAUD_RATE = 115200;

// output start of range
const uint8_t OUT_MIN = 0;

// output end of range
const uint8_t OUT_MAX = 254;

// invalid measurement value
const uint8_t OUT_INVALID = 255;

// ADC reference voltage [V]
constexpr auto ADC_REFERENCE = 1.1;

// map current [mA] to ADC value
template<typename Number> constexpr int mapCurrentToADC(Number current) {
  return SHUNT * current / 1000.0 / ADC_REFERENCE * 1024.0;
}

// ADC value for LOOP_CURRENT_MIN
const int ADC_MIN = mapCurrentToADC(LOOP_CURRENT_MIN);

// ADC value for LOOP_CURRENT_MAX
const int ADC_MAX = mapCurrentToADC(LOOP_CURRENT_MAX);

// invalid ADC value
const int ADC_INVALID = 0x8000;

// minimum delta for detecting a changed raw ADC value
const int ADC_DELTA = 3;

// *** GLOBAL VARIABLES ***

// millisecond of the last sending
unsigned long lastSending;

// last ADC value
int lastAdc;

// last out value
uint8_t lastOut;

// *** FUNCIONS ***

// blinks the LED
void blink(uint8_t times) {
  for (; times; times--) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(125);
    digitalWrite(LED_BUILTIN, LOW);
    if (times > 1) {
      delay(125);
    }
  }
}

// read values from ADC and calculate average
// (returns ADC_INVALID, if value is out of range)
int readAverage() {
  // read values
  int avg = 0;
  for (int cnt = NUM_SAMPLES; cnt; cnt--) {
    int adcValue = analogRead(LOOP_CURRENT_PIN);
    Serial.print(F("ADC: "));
    Serial.println(adcValue);
    if (adcValue < ADC_MIN || adcValue > ADC_MAX) {
      Serial.println("OUT OF RANGE");
      return ADC_INVALID;
    }
    avg += adcValue;
    delay(SAMPLE_PAUSE);
  }
  // calculate average
  avg /= NUM_SAMPLES;
  Serial.print(F("AVG: "));
  Serial.println(avg);
  return avg;
}

// map ADC value to output value
uint8_t mapToOut(int adcValue) {
  if (adcValue == ADC_INVALID) {
    return OUT_INVALID;
  }
  auto out = map(adcValue, ADC_MIN, ADC_MAX, OUT_MIN, OUT_MAX);
  Serial.print(F("OUT: ")); 
  Serial.println(out);
  return out;
}

// set output value
void setOut(uint8_t out) {
  // remark: the output is a short time invalid because the two output ports
  // cannot be set at the same time.
  PORTB = (PORTB & 0b11100000) | (out & 0b00011111);
  PORTD = (PORTD & 0b00011111) | (out & 0b11100000);
  
  // trigger
  // (if the trigger is too short, it will not be recognized by the HM module.)
  digitalWrite(TRIGGER_SEND_PIN, HIGH);
  delay(300); 
  digitalWrite(TRIGGER_SEND_PIN, LOW);

  lastSending = millis();
  lastOut = out;
  Serial.println(F("SENT"));
  blink(BLINK_SEND);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  // set ADC reference to 1.1V
  analogReference(INTERNAL);
  // throw away first ADC read
  analogRead(LOOP_CURRENT_PIN);

  // HomeMatic 8-bit transmitter
  // set PB0 - PB4 and PD5 - PD7 to output
  DDRB |= 0b00011111;
  DDRD |= 0b11100000;
  // init send trigger pin
  pinMode(TRIGGER_SEND_PIN, OUTPUT);
  digitalWrite(TRIGGER_SEND_PIN, LOW);

  // start message
  Serial.begin(BAUD_RATE);
  Serial.println(F("*** HM-CURRENTLOOP ***"));
  Serial.print(F("ADC_MIN: "));
  Serial.println(ADC_MIN);
  Serial.print(F("ADC_MAX: "));
  Serial.println(ADC_MAX);

  // read and send first time
  delay(STARTUP_PAUSE);
  Serial.println(F("---"));
  int adc = readAverage();
  lastAdc = adc;
  uint8_t out = mapToOut(adc);
  setOut(out);
}

void loop() {
  // read and send, if send interval elapsed and value changed
  delay(MEASUREMENT_PAUSE);
  Serial.println(F("---"));

  // send interval elapsed?
  if (millis() - lastSending < SEND_INTERVAL_MIN * 1000) {
    Serial.println(F("TIME NOT ELAPSED"));
    blink(BLINK_TIME_NOT_ELAPSED);
    return;
  }

  // read ADC
  int adc = readAverage();

  // reduce noise
  if (abs(adc - lastAdc) < ADC_DELTA) {
    Serial.println(F("DELTA NOT REACHED"));
    blink(BLINK_DELTA_NOT_REACHED);
    return;
  }
  lastAdc = adc;

  // map to out
  uint8_t out = mapToOut(adc);

  // value changed?
  if (out == lastOut) {
    Serial.println(F("NOT CHANGED"));
    blink(BLINK_NOT_CHANGED);
    return;
  }

  // send
  setOut(out);
}
