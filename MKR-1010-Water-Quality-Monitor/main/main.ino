/*
  This sketch is for the MKR 1010 board monitoring my pond's water quality with the following sensors:
  - Temperature Sensor (DS18B20)
  - Total Dissolved Solids (TDS) Sensor (DFRobot Gravity Analog TDS Sensor)
  - Turbidity Sensor (Gravity Analog Turbidity Sensor)
  - Water Level Ultrasonic Sensor (TD-A02YYMW-V2.0)
  - pH Sensor (Atlas Scientific Lab Grade pH Probe)

  Sensors to be added in the future:
  - Dissolved Oxygen Sensor (Atlas Scientific Lab Grade Dissolved Oxygen Probe)
  - ORP Sensor (Atlas Scientific Lab Grade ORP Probe)
  - Flow Meter (to monitor waterfall flow rate to ensure adequate water circulation)

  The data is captured and relayed to my Central device (Nano 33 IoT board) via Bluetooth Low Energy (BLE).
*/

///////////// Version Control //////////////
#include "version.h"

///////////// Configuration & Helper Files //////////////
#include "config.h"
#include "lcd_display.h" // file created to easily print messages to LCD

///////////// Watchdog Timer Library //////////////
#include <Adafruit_SleepyDog.h>

///////////// BLE Library //////////////
#include <ArduinoBLE.h>

///////////// LCD Library //////////////
#include <LiquidCrystal_I2C.h> // technically do not need since it is included in lcd_display.h

//////////// Sensor Libraries ///////////////
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NewPing.h>
#include "ph_grav_no_eeprom.h"

///////////// LCD Variables //////////////
LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 20 chars and 4 line display
byte degree[8] = { // Custom degree symbol
  0b01110,
	0b01010,
	0b01110,
	0b00000,
	0b00000,
	0b00000,
	0b00000,
	0b00000
};

///////////// Temperature Sensor Variables //////////////
#define ONE_WIRE_BUS 5 // Data wire is plugged into pin #5 on MKR 1010
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
float tempC;

///////////// TD-A02YYMW-V2.0 Ultrasonic Sensor (Water Level) Variables //////////////
#define TRIGGER_PIN  6  // Arduino pin tied to trigger pin on the ultrasonic sensor.
#define ECHO_PIN     7  // Arduino pin tied to echo pin on the ultrasonic sensor.
#define MAX_DISTANCE 100 // Maximum distance (in centimeters). Maximum sensor distance is rated at 400-500cm. Don't need to go more than depth of pond.
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE); // Initialize NewPing constructor.

//////////// Turbidity Sensor Variables //////////////
#define TURBIDITY_SENSOR_PIN A1
float turbidityOffset = 0.46; 

///////////////// TDS Variables //////////////////
#define TDS_SENSOR_PIN A2
#define VREF 3.3    // analog reference voltage(Volt) of the ADC

//////////// Atlas pH Sensor Variables & Setup ////////////
#define PH_SENSOR_PIN A3
Gravity_pH pH = Gravity_pH(PH_SENSOR_PIN);

// BLE configuartion
BLEService sensorDataService(sensorDataServiceUuid); // Custom service for data transfer
BLEFloatCharacteristic temperatureCharacteristic(temperatureCharacteristicUuid, BLERead | BLENotify);
BLEFloatCharacteristic waterLevelCharacteristic(waterLevelCharacteristicUuid, BLERead | BLENotify);
BLEIntCharacteristic turbidityValueCharacteristic(turbidityValueCharacteristicUuid, BLERead | BLENotify);
BLEFloatCharacteristic turbidityVoltageCharacteristic(turbidityVoltageCharacteristicUuid, BLERead | BLENotify);
BLEIntCharacteristic totalDissolvedSolidsCharacteristic(totalDissolvedSolidsCharacteristicUuid, BLERead | BLENotify);
BLEFloatCharacteristic pHCharacteristic(pHCharacteristicUuid, BLERead | BLENotify);

// Global sensor array variables that will be used to average out values
// Arrays will be of size 5 and newest values in will overwrite the oldest values
const int ARRAY_SIZE = 5; // modify this to change the size of the array for averaging sensor values
float temperatureValues[ARRAY_SIZE] = {0};
int totalDissolvedSolidsValues[ARRAY_SIZE] = {0};
int turbidityValues[ARRAY_SIZE] = {0};
float turbidityVoltageValues[ARRAY_SIZE] = {0};
float waterLevelValues[ARRAY_SIZE] = {0};
float pHValues[ARRAY_SIZE] = {0};
int arrayIndex = 0;

// Global constants
const float ANALOG_TO_VOLTAGE = VREF / 4095.0; // 12-bit ADC
const unsigned long DATA_CAPTURE_INTERVAL = 500; // half second
const unsigned long BLE_UPDATE_INTERVAL = 3000; // 3 seconds
unsigned long lastDataCapture = 0;
unsigned long lastBLEUpdate = 0;
int watchdogTimeoutInterval = 8000; // 8 second timeout interval
bool initialValue = true; // flag to not print out sensor values on LCD on first run

float convertAnalogToVoltage(int analogValue) {
  return analogValue * ANALOG_TO_VOLTAGE;
}

// Function to calculate average of an array
template <typename T>
float average(T arr[], int size) {
  float sum = 0.0;
  int count = 0;
  for (int i = 0; i < size; i++) {
    if (arr[i] != 0) {
      sum += arr[i];
      count++;
    }
  }
  return count == 0 ? 0 : sum / count;
}

// Function to calculate the integer average of an array. 
// Done for readability & integer characteristics in the updateBLECharacteristics function
template <typename T>
int intAverage(T arr[], int size) {
  int sum = 0;
  int count = 0;
  for (int i = 0; i < size; i++) {
    if (arr[i] != 0) {
      sum += static_cast<int>(arr[i]);
      count++;
    }
  }
  return count == 0 ? 0 : round(static_cast<float>(sum) / count);
}

// Function to append value to array if it's within 5% of average
template <typename T>
void appendValueToAverageArray(T arr[], int size, T value) {
  float avg = average(arr, size);
  if (avg == 0 || (value >= 0.95 * avg && value <= 1.05 * avg)) {
    arr[arrayIndex] = value;
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("\nSerial port connected.");
  Serial.print("Software version: v");
  Serial.println(RELEASE_VERSION);

  //////////// Setting up the LCD ////////////
  lcd.init(); // initialize the lcd
  lcd.backlight(); // turn on backlight
  lcd.createChar(0, degree); // create custom degree symbol
  lcd.setCursor(3, 0); // set cursor to first column, second row
  lcd.blink(); // turn on blinking cursor
  lcdPrettyPrint("Booting up...", lcd); // helper function from lcd_display.h for readability
  lcd.setCursor(2, 1);
  lcdPrettyPrint("Software v" + String(RELEASE_VERSION), lcd);

  // Initialize the watchdog with an 8 second timeout
  Watchdog.enable(watchdogTimeoutInterval);

  //////////// Setting up the sensors and pins ////////////
  // Ultrasonic and pH do not need pinMode setup. Pins are set up in the NewPing and Atlas pH libraries
  analogReadResolution(12); // change to 12 bits for compatibility with MKR 1010 
  ds18b20.begin(); // begin the temperature sensor
  pinMode(TDS_SENSOR_PIN, INPUT);
  pinMode(TURBIDITY_SENSOR_PIN, INPUT);
  
  //////////// Setting up the Bluetooth service ////////////
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    lcd.setCursor(5, 2);
    lcdPrettyPrint("BLE failed!", lcd); 
    while (1);
  }

  BLE.setLocalName(peripheralName);
  BLE.setAdvertisedService(sensorDataService);

  sensorDataService.addCharacteristic(temperatureCharacteristic);
  sensorDataService.addCharacteristic(totalDissolvedSolidsCharacteristic);
  sensorDataService.addCharacteristic(turbidityValueCharacteristic);
  sensorDataService.addCharacteristic(turbidityVoltageCharacteristic);
  sensorDataService.addCharacteristic(waterLevelCharacteristic);
  sensorDataService.addCharacteristic(pHCharacteristic);

  BLE.addService(sensorDataService);

  // Read sensor values to obtain initial values so not sending 0
  readSensorValues();
  // set initalValue flag to false to print out future values to LCD
  initialValue = false;

  BLE.advertise();

  lcd.setCursor(1, 3);
  lcdPrettyPrint("Boot up complete!", lcd, true, 2000); // clearing LCD after 2 seconds
  lcd.noBlink(); // Turn off blinking cursor before starting loop

  // Kick the watchdog
  Watchdog.reset();
}

void loop() {
  // read sensor values every data capture interval (1/2 second) and append to array for averaging
  // and update BLE characteristics every BLE update interval (3 seconds)

  if (millis() - lastDataCapture >= DATA_CAPTURE_INTERVAL) {
    lastDataCapture = millis();

    // read sensor values and update average arrays
    readSensorValues();
  }
  
  BLEDevice central = BLE.central();

  if (central) {
    Serial.print("Connected to central: ");
    Serial.println(central.address());

    while (central.connected()) {
      // read sensor values every data capture interval (1/2 second)
      if (millis() - lastDataCapture >= DATA_CAPTURE_INTERVAL) {
        lastDataCapture = millis();

        // read sensor values and update average arrays
        readSensorValues();
        // Serial.println("Sensor values read.");
 
        if (millis() - lastBLEUpdate >= BLE_UPDATE_INTERVAL) {
          lastBLEUpdate = millis();

          // update the BLE characteristics with the average values
          updateBLECharacteristics();
          // Serial.println("BLE characteristics updated.");
        }
      }
      // Kick the watchdog to reset the timer
      Watchdog.reset();
    }
    Serial.print("Disconnected from central: ");
    Serial.println(central.address());
  }

  // Kick the watchdog to reset the timer
  Watchdog.reset();
}

float readTemperature() {
  // Read the temperature from the sensor
  ds18b20.requestTemperatures();
  tempC = ds18b20.getTempCByIndex(0); // Update global tempC variable for TDS calculation
  float tempF = ds18b20.getTempFByIndex(0);

  // update LCD if not sending initial values
  if (!initialValue) {
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(tempF, 1);
    lcd.write(byte(0));
  }

  return tempF;
}

int readTotalDissolvedSolids() {
  float tdsVoltage = convertAnalogToVoltage(analogRead(TDS_SENSOR_PIN));
  float compensationCoefficient = 1.0 + 0.0191 * (tempC - 25.0);
  float compensationVoltage = tdsVoltage / compensationCoefficient;
  float tdsValue = (133.42 * pow(compensationVoltage, 3) - 255.86 * pow(compensationVoltage, 2) + 857.39 * compensationVoltage) * 0.5; 
  
  // update LCD
  if (!initialValue) {
    lcd.setCursor(0, 3);
    lcd.print("TDS Value: ");
    lcd.print(tdsValue, 0);
    if (tdsValue < 1000) {
      lcd.print(" ppm  ");
    }
    else {
      lcd.print(" ppm ");
    }
  }

  return round(tdsValue);
}

int readTurbidityValue() {
  // turbidity is the measure of cloudiness in the water. Range is 0 to 3000 NTU
  float turbidityVoltage = convertAnalogToVoltage(analogRead(TURBIDITY_SENSOR_PIN)) - turbidityOffset;
  
  // update LCD
  if (!initialValue) {
    lcd.setCursor(0, 2);
    lcd.print("Turb: ");
    lcd.print(turbidityVoltage, 2);
    lcd.print("V ");
  }
  // Convert the voltage to NTU (valid range is 2.5 to 4.21V)
  if (turbidityVoltage <= 2.5) {
    if (!initialValue) {
      lcd.print(3000);
      lcd.print(" NTU"); 
    }
    return 3000;
  }
  else if (turbidityVoltage > 4.21) {
    if (!initialValue) {
      lcd.print(0);
      lcd.print(" NTU   "); // padding spcaes added to overwrite unused spaces from previous print
    }
    return 0;
  }
  else {
    // for 3.3V output this is the quadratic equation: y = -2572.2x² + 8700.5x - 4352.9
    //// source: https://forum.arduino.cc/t/getting-ntu-from-turbidity-sensor-on-3-3v/658067/14
    float ntuValue = round(-1120.4 * sq(turbidityVoltage) + 5742.3 * turbidityVoltage - 4352.9);
    if (!initialValue) {
      lcd.print(ntuValue);
      if (ntuValue >= 1000) {
        lcd.print(" NTU");
      }
      else if (ntuValue >= 100 || ntuValue < -9) {
        lcd.print(" NTU ");
      }
      else {
        lcd.print(" NTU  "); // padding spaces
      }
    }
    return ntuValue;
  }
}

float readTurbidityVoltage() {
  return convertAnalogToVoltage(analogRead(TURBIDITY_SENSOR_PIN)) - turbidityOffset;
}

float readWaterLevel() {
  // Read the distance from the A02YYMW sensor
  float distance = sonar.ping_in();

  // Update LCD
  if (!initialValue) {
    lcd.setCursor(0, 1);
    lcd.print("Pond Level: ");
    if (distance <= MAX_DISTANCE) {
      lcd.print(distance);
      lcd.print(" in");
    }
    else {
      lcd.print("N/A ");
    }
  }

  return distance;
}

float readPH() {
  // Read the pH from the Atlas Scientific pH sensor. Refer to ph_grav_no_eeprom.h for more info 
  float pH_value = pH.read_ph();

  // Update LCD
  if (!initialValue) {
    lcd.setCursor(12, 0);
    lcd.print("pH: ");
    if (pH_value < 10.0) {
      lcd.print(pH_value, 2);
    }
    else {
      lcd.print(pH_value, 1);
    }
  }

  return pH_value;
}

void readSensorValues() {
  // Read and Append sensor values directly to their associated storage arrays if within 5% of average
  appendValueToAverageArray(temperatureValues, ARRAY_SIZE, readTemperature());
  appendValueToAverageArray(totalDissolvedSolidsValues, ARRAY_SIZE, readTotalDissolvedSolids());
  appendValueToAverageArray(turbidityValues, ARRAY_SIZE, readTurbidityValue());
  appendValueToAverageArray(turbidityVoltageValues, ARRAY_SIZE, readTurbidityVoltage());
  appendValueToAverageArray(waterLevelValues, ARRAY_SIZE, readWaterLevel());
  appendValueToAverageArray(pHValues, ARRAY_SIZE, readPH());

  // increment the array index 
  arrayIndex = (arrayIndex + 1) % ARRAY_SIZE;
}

void updateBLECharacteristics() {
  // Calculate the average of the sensor arrays and write the values to the BLE characteristics
  // Note some of the characteristics are int types so those averages are rounded to nearest integer
  temperatureCharacteristic.writeValue(average(temperatureValues, ARRAY_SIZE));
  totalDissolvedSolidsCharacteristic.writeValue(intAverage(totalDissolvedSolidsValues, ARRAY_SIZE));
  turbidityValueCharacteristic.writeValue(intAverage(turbidityValues, ARRAY_SIZE));
  turbidityVoltageCharacteristic.writeValue(average(turbidityVoltageValues, ARRAY_SIZE));
  waterLevelCharacteristic.writeValue(average(waterLevelValues, ARRAY_SIZE));
  pHCharacteristic.writeValue(average(pHValues, ARRAY_SIZE));
}