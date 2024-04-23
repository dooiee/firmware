/*
  This sketch is for the Nano 33 IoT board acting as the Central BLE device in my water monitoring system.

  It will perform the following functions:
  1. Connect to the MKR WiFi 1010 board via TX/RX pins
  2. Connect to the sensor data peripheral device at my pond via BLE 
  3. Read and subscribe to sensor data updates from the peripheral device
  4. Send sensor data to the main board via UART
    - This board will send both realtime values and average values gathered over a 1 minute interval for logging purposes
    - The main board will be responsible for sending the data to my Firebase RTDB via Ethernet & REST APIs

  Potential Feature Additions:
  - Add serial communication for logging purposes when peripheral is connected and disconnected.
  - Add #ifdef statements to remove Serial communication when not needed.
  - Add remote configuration capabilities
  - Add remote firmware update capabilities (OTA)
  - Not entirely necessary but could add Serial communication that is sent to MKR when the peripheral is connected.
    That would allow MKR to only make a server connection when necessary and remove unnecessary server reconnections when not receiving data.
  - Add variables to PondDataService that the MKR sensor collector board could use as configuration and update when a change is made.
  - Integrate BLE capabilities to ESP32-RF-Controller board to ensure that if Firebase connection is not possible the ESP32 can still send out RF commands based on BLE service characteristics.
*/

#include "version.h"
#include "config.h"
#include <ArduinoBLE.h>
#include <ArduinoJson.h>

// #Defines
#define DEBUG (false) // Set to true to enable debug output and fake data generation

// Global variables to store the sensor data to calculate average values over a 1 minute interval
const int MAX_SENSOR_VALUES = 60; // The maximum number of sensor values to store in the arrays

// Arrays for sensor values
float temperatureValues[MAX_SENSOR_VALUES] = {0};
float waterLevelValues[MAX_SENSOR_VALUES] = {0};
int turbidityValues[MAX_SENSOR_VALUES] = {0};
float turbidityVoltageValues[MAX_SENSOR_VALUES] = {0};
int totalDissolvedSolidsValues[MAX_SENSOR_VALUES] = {0};
float pHValues[MAX_SENSOR_VALUES] = {0};

// Global constants for data logging
unsigned long lastDataLogSent = 0;
const unsigned long dataLogInterval = 60000; // 1 minute
const int NUM_SENSORS = 6; // change this if you add/remove sensors

int numValues[NUM_SENSORS] = {0, 0, 0, 0, 0, 0}; // The number of values stored for each sensor
int currentIndex[NUM_SENSORS] = {0, 0, 0, 0, 0, 0}; // The current index for each sensor's array

bool isPeripheralConnected = false;
unsigned long lastConnectionTime = 0; // Store the last connection time in milliseconds
const unsigned long peripheralTimeout = 15000; // 15 seconds
int lastRssi = 0; // Global variable to store the last RSSI reading

void setup() {
    // initialize serial communication
    Serial.begin(115200);
    Serial.println("\nSerial port ready.");
    Serial.print("Software version: v");
    Serial.println(RELEASE_VERSION);

    pinMode(LED_BUILTIN, OUTPUT);

    Serial1.begin(115200);
    // perform handshake connection with MKR 1010 board
    establishSerialConnectionWithMKR();
    Serial.println("Serial1 connected to MKR WiFi 1010!");

    if (DEBUG)
    {
      Serial.println("Debug mode enabled. Skipping BLE initialization.");
      delay(10000); // Delay for 10 seconds to allow the MKR board to bootup
      return;
    }
    
    // initialize BLE communication
    if (!BLE.begin()) {
      Serial.println("Starting BLE failed!");
      while (1);
    }
    Serial.println("Central BLE device started.");

    // start scanning for peripherals
    BLE.scanForName(peripheralName);
    Serial.println("Scanning for peripheral...");
}

void loop() {
  if (DEBUG) {
    // Simulate sensor data read and send it every few seconds
    static unsigned long lastFakeDataTime = 0;
    if (millis() - lastFakeDataTime > 3000) {
      generateAndAppendFakeSensorData();
      lastFakeDataTime = millis();
      isPeripheralConnected = true;
      lastConnectionTime = millis();
      lastRssi = generateRandomValue(-100, -50);
    }
  }
  else {
    BLEDevice peripheral = BLE.available();

    if (peripheral) {
      if (peripheral.localName() == peripheralName) {
        // stop scanning
        BLE.stopScan();
        Serial.print("Found: ");
        Serial.print(peripheral.address());
        Serial.print(" '");
        Serial.print(peripheral.localName());
        Serial.print("' ");
        Serial.print(peripheral.advertisedServiceUuid());
        Serial.println();

        // Update connection status
        isPeripheralConnected = true;
        lastConnectionTime = millis();

        // Update the RSSI value
        lastRssi = peripheral.rssi();
        Serial.print("RSSI: ");
        Serial.println(lastRssi);

        // read peripheral data and send it to the main board
        streamPeripheralData(peripheral);
        
        // resume scanning
        BLE.scanForName(peripheralName);
      }
    } else {
      // Check if the peripheral is still connected
      if (isPeripheralConnected && millis() - lastConnectionTime >= peripheralTimeout) {
        Serial.println("Peripheral disconnected.");
        isPeripheralConnected = false;
      }
    }
  }

  // Handle incoming commands from the MKR central hub
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim(); // Remove any whitespace or newline characters

    // Handling various commands
    if (command == "READY_TO_CONNECT") {
      Serial1.println("NANO_CONNECTED");
      Serial.println("Connection with MKR established.");
    } else if (command == "STATUS") {
      sendStatus();
    } else if (command == "RECONNECT") {
      // Reconnect to the peripheral device
      BLE.scanForName(peripheralName);
      Serial.println("Scanning for peripheral...");
    } else if (command.startsWith("CALIBRATE_PH")) {
      // Parse calibration values from the command
      int firstComma = command.indexOf(',');
      int secondComma = command.indexOf(',', firstComma + 1);

      if (firstComma != -1 && secondComma != -1) {
        String lowCal = command.substring(13, firstComma); // start after "CALIBRATE_PH "
        String midCal = command.substring(firstComma + 1, secondComma);
        String highCal = command.substring(secondComma + 1);

        // Now you can convert these String values to floats if needed:
        float lowCalValue = lowCal.toFloat();
        float midCalValue = midCal.toFloat();
        float highCalValue = highCal.toFloat();

        // Assuming you are connected to the peripheral
        updatePhCalibrationCharacteristic(lowCalValue, midCalValue, highCalValue);
      } else {
        Serial.println("Invalid calibration command format.");
        Serial1.println("ERROR: Invalid calibration command format");
      }
    }
    else {
      Serial.print("Unknown command received: ");
      Serial.println(command);
      
      // Send an error message back to the MKR board
      Serial1.println("ERROR: Unknown command");
    }
  }
}

void establishSerialConnectionWithMKR() {
  // wait for connection message from MKR
  while (true) {
    // Check if debug command is received
    if (Serial.available()) {
      String command = Serial.readStringUntil('\n');
      command.trim();
      if (command == "DEBUG") {
        Serial.println("Debug mode enabled, skipping connection with MKR.");
        return;
      }
    }

    if (Serial1.available()) {
      String received = Serial1.readStringUntil('\n');
      received.trim(); // remove any leading/trailing white space or special characters
      Serial.println("Received: " + received); // Print any received message
      if (received == "READY_TO_CONNECT") {
        // send connection message to MKR
        Serial1.println("NANO_CONNECTED");
        Serial.println("Sent NANO_CONNECTED to MKR");
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        break;
      }
    }
  }
}

void sendStatus() {
  // Create the status JSON payload
  StaticJsonDocument<256> jsonPayload;
  jsonPayload["type"] = "status";  // type field so MKR can route properly
  jsonPayload["connected"] = isPeripheralConnected;
  if (isPeripheralConnected) {
    jsonPayload["rssi"] = lastRssi;
  } else {
    // Calculate the time since the last connection in seconds
    unsigned long timeSinceLastConnection = (millis() - lastConnectionTime) / 1000;
    jsonPayload["timeSinceLastConnection"] = timeSinceLastConnection;
  }

  serializeJson(jsonPayload, Serial1);
  Serial1.println();
}

void updatePhCalibrationCharacteristic(float lowCal, float midCal, float highCal) {
  // Assuming BLE peripheral is already connected and characteristic discovered
  BLEDevice peripheral; // You should have this from your connection logic
  BLECharacteristic pHCalibrationCharacteristic = peripheral.characteristic(pHCalibrationCharacteristicUuid);

  if (pHCalibrationCharacteristic) {
    Serial.println("Found pH Calibration Characteristic. Updating values...");
    
    // Prepare the data packet - ensure it matches the expected format on the peripheral
    char calibData[20]; // Adjust size based on your specific needs
    snprintf(calibData, sizeof(calibData), "%f,%f,%f", lowCal, midCal, highCal);
    
    // Write new calibration data to the characteristic
    if (pHCalibrationCharacteristic.writeValue(calibData)) {
      Serial.println("Calibration values updated successfully.");
      Serial1.println("CALIBRATION_SUCCESS");
    } else {
      Serial.println("Failed to update calibration values.");
      Serial1.println("ERROR: Calibration update failed");
    }
  } else {
    Serial.println("pH Calibration Characteristic not found.");
    Serial1.println("ERROR: pH Characteristic not found");
  }
}

template <typename T>
void appendSensorValue(int sensorIndex, T value) {
  switch (sensorIndex) {
    case 0: temperatureValues[currentIndex[sensorIndex]] = value; break;
    case 1: waterLevelValues[currentIndex[sensorIndex]] = value; break;
    case 2: turbidityValues[currentIndex[sensorIndex]] = value; break;
    case 3: turbidityVoltageValues[currentIndex[sensorIndex]] = value; break;
    case 4: totalDissolvedSolidsValues[currentIndex[sensorIndex]] = value; break;
    case 5: pHValues[currentIndex[sensorIndex]] = value; break;
  }
  numValues[sensorIndex]++;
  currentIndex[sensorIndex] = (currentIndex[sensorIndex] + 1) % MAX_SENSOR_VALUES;
}

void transmitDataToMkrBoard(StaticJsonDocument<256>& jsonPayload) {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);

  // Print the JSON object in a human-readable format
  Serial.println("JSON payload to send:");
  serializeJsonPretty(jsonPayload, Serial);
  Serial.println();

  serializeJson(jsonPayload, Serial1);
  Serial1.println();
}

template <typename T>
float average(const T arr[], int n) {
  float sum = 0;
  for (int i = 0; i < n; i++) {
    sum += arr[i];
  }
  return sum / n;
}

void sendLogUpdate() {
  // Create the log update JSON payload
  StaticJsonDocument<256> jsonPayload;

  // Set the log type
  jsonPayload["type"] = DEBUG ? "log-debug" : "log";

  // Calculate the averages
  jsonPayload["temperature"] = average(temperatureValues, numValues[0]);
  jsonPayload["waterLevel"] = average(waterLevelValues, numValues[1]);
  jsonPayload["turbidity"] = average(turbidityValues, numValues[2]);
  jsonPayload["turbidityVoltage"] = average(turbidityVoltageValues, numValues[3]);
  jsonPayload["totalDissolvedSolids"] = average(totalDissolvedSolidsValues, numValues[4]);
  jsonPayload["pH"] = average(pHValues, numValues[5]);

  // Transmit the JSON payload to the MKR board
  Serial.println("Transmitting LOG data to main board...");
  transmitDataToMkrBoard(jsonPayload);

  // Reset the sensor value arrays and counters
  memset(temperatureValues, 0, sizeof(temperatureValues));
  memset(waterLevelValues, 0, sizeof(waterLevelValues));
  memset(turbidityValues, 0, sizeof(turbidityValues));
  memset(turbidityVoltageValues, 0, sizeof(turbidityVoltageValues));
  memset(totalDissolvedSolidsValues, 0, sizeof(totalDissolvedSolidsValues));
  memset(pHValues, 0, sizeof(pHValues));
  memset(numValues, 0, sizeof(numValues));
  memset(currentIndex, 0, sizeof(currentIndex));
}

bool streamPeripheralData(BLEDevice peripheral) {
  if (!peripheral.connect()) {
    return false;
  }
  Serial.println("Connected to peripheral!");

  if (!peripheral.discoverAttributes()){
    Serial.println("Failed to discover peripheral attributes.");
    peripheral.disconnect();
    return false;
  }
  Serial.println("Discovered peripheral attributes!");

  BLECharacteristic temperatureCharacteristic = peripheral.characteristic(temperatureCharacteristicUuid);
  BLECharacteristic waterLevelCharacteristic = peripheral.characteristic(waterLevelCharacteristicUuid);
  BLECharacteristic totalDissolvedSolidsCharacteristic = peripheral.characteristic(totalDissolvedSolidsCharacteristicUuid);
  BLECharacteristic turbidityValueCharacteristic = peripheral.characteristic(turbidityValueCharacteristicUuid);
  BLECharacteristic turbidityVoltageCharacteristic = peripheral.characteristic(turbidityVoltageCharacteristicUuid);
  BLECharacteristic pHCharacteristic = peripheral.characteristic(pHCharacteristicUuid);

  if (!temperatureCharacteristic || !totalDissolvedSolidsCharacteristic || !turbidityValueCharacteristic || !turbidityVoltageCharacteristic || !waterLevelCharacteristic || !pHCharacteristic) {
    Serial.println("Failed to find characteristics.");
    peripheral.disconnect();
    return false;
  }

  if (!temperatureCharacteristic.canSubscribe() || !totalDissolvedSolidsCharacteristic.canSubscribe() || !turbidityValueCharacteristic.canSubscribe() || !turbidityVoltageCharacteristic.canSubscribe() || !waterLevelCharacteristic.canSubscribe() || !pHCharacteristic.canSubscribe()) {
    Serial.println("Failed to subscribe to characteristics.");
    peripheral.disconnect();
    return false;
  }

  if (!temperatureCharacteristic.subscribe() || !totalDissolvedSolidsCharacteristic.subscribe() || !turbidityValueCharacteristic.subscribe() || !turbidityVoltageCharacteristic.subscribe() || !waterLevelCharacteristic.subscribe() || !pHCharacteristic.subscribe()) {
    Serial.println("Failed to subscribe to characteristics.");
    peripheral.disconnect();
    return false;
  }

  BLECharacteristic* sensorCharacteristics[NUM_SENSORS] = {&temperatureCharacteristic, &waterLevelCharacteristic, &turbidityValueCharacteristic, &turbidityVoltageCharacteristic, &totalDissolvedSolidsCharacteristic, &pHCharacteristic};

  Serial.println("Reading data from peripheral...");
  while (peripheral.connected()) {
    StaticJsonDocument<256> jsonPayload;
    bool dataUpdated = false;

    for (int i = 0; i < NUM_SENSORS; i++) {
      if (!dataUpdated) {
        // add data type to JSON payload for MKR to parse
        jsonPayload["type"] = "realtime";
      }

      if (sensorCharacteristics[i]->valueUpdated()) {
        dataUpdated = true;
        const char* sensorKey;
        switch (i) {
          case 0: sensorKey = "temperature"; break;
          case 1: sensorKey = "waterLevel"; break;
          case 2: sensorKey = "turbidity"; break;
          case 3: sensorKey = "turbidityVoltage"; break;
          case 4: sensorKey = "totalDissolvedSolids"; break;
          case 5: sensorKey = "pH"; break;
        }

        if (i == 0 || i == 1 || i == 3 || i == 5) { // for sensor 1, 2, 4, and 6
          float sensorValue;
          sensorCharacteristics[i]->readValue(&sensorValue, sizeof(sensorValue));
          jsonPayload[sensorKey] = sensorValue;
          appendSensorValue(i, sensorValue);
        } else { // for sensor 3 and 5
          int sensorValue;
          sensorCharacteristics[i]->readValue(&sensorValue, sizeof(sensorValue));
          jsonPayload[sensorKey] = sensorValue;
          appendSensorValue(i, sensorValue);
        }
      }
    }

    if (dataUpdated) {
      // Transmit the JSON payload to the MKR board
      Serial.println("Transmitting REALTIME data to main board...");
      transmitDataToMkrBoard(jsonPayload);
    }

    // Check if it's time to send a data log update to the main board
    unsigned long currentTime = millis();
    if (currentTime - lastDataLogSent >= dataLogInterval) {
      sendLogUpdate();
      // Update the lastDataLogSent variable
      lastDataLogSent = currentTime;
    }
  }

  peripheral.disconnect();
  Serial.println("Peripheral disconnected.");
  return true;
}

template <typename T>
T generateRandomValue(T lowerBound, T upperBound) {
    T range = upperBound - lowerBound;
    T randomValue = static_cast<T>(rand()) / static_cast<T>(RAND_MAX);
    return lowerBound + randomValue * range;
}

void generateAndAppendFakeSensorData() {
    StaticJsonDocument<256> jsonPayload;
    jsonPayload["type"] = "realtime-debug";

    for (int i = 0; i < NUM_SENSORS; i++) {
      switch (i) {
        case 0: // Temperature
            temperatureValues[currentIndex[i]] = generateRandomValue<float>(45.0, 55.0);
            jsonPayload["temperature"] = temperatureValues[currentIndex[i]];
            break;
        case 1: // Water Level
            waterLevelValues[currentIndex[i]] = generateRandomValue<float>(0.0, 12.0);
            jsonPayload["waterLevel"] = waterLevelValues[currentIndex[i]];
            break;
        case 2: // Turbidity
            turbidityValues[currentIndex[i]] = generateRandomValue<int>(0, 3000);
            jsonPayload["turbidity"] = turbidityValues[currentIndex[i]];
            break;
        case 3: // Turbidity Voltage
            turbidityVoltageValues[currentIndex[i]] = generateRandomValue<float>(0.0, 3.3);
            jsonPayload["turbidityVoltage"] = turbidityVoltageValues[currentIndex[i]];
            break;
        case 4: // Total Dissolved Solids
            totalDissolvedSolidsValues[currentIndex[i]] = generateRandomValue<int>(50, 300);
            jsonPayload["totalDissolvedSolids"] = totalDissolvedSolidsValues[currentIndex[i]];
            break;
        case 5: // pH
            pHValues[currentIndex[i]] = generateRandomValue<float>(6.0, 8.0);
            jsonPayload["pH"] = pHValues[currentIndex[i]];
            break;
      }
      numValues[i]++;  // Ensure we increment the count of values collected for averaging
      currentIndex[i] = (currentIndex[i] + 1) % MAX_SENSOR_VALUES;
    }

    // Transmit the JSON payload to the MKR board
    Serial.println("Transmitting REALTIME DEBUG data to main board...");
    transmitDataToMkrBoard(jsonPayload);

    // Check if it's time to send a data log update to the main board
    unsigned long currentTime = millis();
    if (currentTime - lastDataLogSent >= dataLogInterval) {
      sendLogUpdate();
      // Update the lastDataLogSent variable
      lastDataLogSent = currentTime;
    }
}
