
#if ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include "ph_grav_no_eeprom.h"

Gravity_pH::Gravity_pH(uint8_t pin){
	this->pin = pin;
}

bool Gravity_pH::begin(){
	return false;
}

float Gravity_pH::read_voltage() {
	float voltage_mV = 0;
	for (int i = 0; i < volt_avg_len; ++i) {
    #if defined(ESP32)
    //ESP32 has significant nonlinearity in its ADC, we will attempt to compensate 
    //but you're on your own to some extent
    //this compensation is only for the ESP32
    //https://github.com/espressif/arduino-esp32/issues/92
      voltage_mV += analogRead(this->pin) / 4095.0 * 3300.0 + 130;
    #elif defined(ARDUINO_SAMD_NANO_33_IOT)
      analogReadResolution(12);
      voltage_mV += analogRead(this->pin) / 4095.0 * 3300.0;
    #elif defined(ARDUINO_AVR_UNO)
      // UNO has only 10-bit ADC
      voltage_mV += analogRead(this->pin) / 1024.0 * 5000.0;
    #elif defined(ARDUINO_SAMD_MKRWIFI1010)
      analogReadResolution(12);
      voltage_mV += analogRead(this->pin) / 4095.0 * 3300.0;
    #else
      // Default case if board not recognized
      analogReadResolution(10);
      voltage_mV += analogRead(this->pin) / 1024.0 * 5000.0;
    #endif
	}
	voltage_mV /= volt_avg_len;
	return voltage_mV;
}

float Gravity_pH::read_ph(float voltage_mV) {
  if (voltage_mV > pH.mid_cal) { //high voltage = low ph
    return this->pH.mid_solution_ph - (this->pH.mid_solution_ph - this->pH.low_solution_ph) / (this->pH.low_cal - this->pH.mid_cal) * (voltage_mV - this->pH.mid_cal);
  } else {
    return this->pH.mid_solution_ph - (this->pH.high_solution_ph - this->pH.mid_solution_ph) / (this->pH.mid_cal - this->pH.high_cal) * (voltage_mV - this->pH.mid_cal);
  }
}

float Gravity_pH::read_ph() {
  return(read_ph(read_voltage()));
}

float Gravity_pH::calc_ph_from_formula(float voltage_mV) {
  return (-5.6548 * voltage_mV / 1000) + 15.509;
}

float Gravity_pH::calc_ph_from_formula() {
  return calc_ph_from_formula(read_voltage());
}

void Gravity_pH::cal_mid(float voltage_mV) {
  this->pH.mid_cal = voltage_mV;
}

void Gravity_pH::cal_mid() {
  cal_mid(read_voltage());
}

void Gravity_pH::cal_low(float voltage_mV) {
  this->pH.low_cal = voltage_mV;
}

void Gravity_pH::cal_low() {
  cal_low(read_voltage());
}

void Gravity_pH::cal_high(float voltage_mV) {
  this->pH.high_cal = voltage_mV;
}

void Gravity_pH::cal_high() {
  cal_high(read_voltage());
}

void Gravity_pH::cal_clear() {
  // expected calibration mV values for pH 4.00, 6.86, and 9.18 solutions
  this->pH.mid_cal = 1529; // also changing from 1500 to 1529 to match pH solution which is 6.86 instead of 7.00
  this->pH.low_cal = 2033; // changing from 2030 to 2033 to match pH solution which is 4.01 instead of 4.00
  this->pH.high_cal = 1119; // changing from 975 to match pH solution which is 9.18 instead of 10
  // used formula pH = (-5.6548 * voltage) + 15.509 then solved for voltage
}
