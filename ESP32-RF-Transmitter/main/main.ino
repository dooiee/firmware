/*
TODO: 
- remove Serial while loop
- add way to remotely reset the ESP32 (perhaps wired through MKR 1010)
- reupload to ESP32
*/

/*
  This sketch will perform the following functions:
    1. Connect to the WiFi network
    2. Use the Firebase-ESP32 library to establish a connection to the Firebase database
    3. Establish a data stream to monitor changes to the RF color code, brightness, or power state which will all be within the UnderwaterLEDs node.
    4. When change is detected, the correct RF code will be sent to the RF transmitter to change the color, brightness, or power state of the UnderwaterLEDs.
        The database will also set a timestamp value to indicate when the change was made.
    5. Wi-Fi and Firebase connections will be re-established if they are lost.
*/
#include "version.h"
#include "secrets.h"
#include "config.h"
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <RCSwitch.h>

// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>

// Define base path to check for configuration values
#define FB_STREAM_PATH_CONFIG "Config/ESP32"

// Define paths with possible configuration values
#define FB_STREAM_CONFIG_PATH "/streamPath"
#define FB_RF_CODE_CONFIG_PATH "/rfCodePath"
#define FB_BRIGHTNESS_CONFIG_PATH "/brightnessPath"
#define FB_POWER_STATE_CONFIG_PATH "/powerStatePath"

// Define global String variables (w/ defaults) for stream path configuration
String FB_BASE_STREAM_PATH = "test/Control/UnderwaterLEDs"; // default value if none is set.
String FB_STREAM_RF_CODE_PATH = "/decimalCode"; // the value of this key will be an int (i.e. 1776964)
String FB_STREAM_BRIGHTNESS_PATH = "/brightnessLevel"; // the value of this key will be a value 1-5 and will increment until 5 is hit and then reset to 1.
String FB_STREAM_POWER_STATE_PATH = "/powerState"; // the value of this key will be a string (i.e. "111111111111111100000001")

// Define Firebase Data object
FirebaseData stream;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Define the RCSwitch object and the pin to which the RF transmitter is connected
RCSwitch rfTransmitter = RCSwitch();
const int RF_TRANSMITTER_PIN = 16;

// flag to check if the program is starting up. If so, don't send data.
bool isStartingUp = true;

void setup() {
  // initialize serial communication
  Serial.begin(115200);
  while (!Serial) { // for testing purposes
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("\nSerial port connected.");

  Serial.printf("Software version: v%s\n", RELEASE_VERSION);
  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  // Connect to WiFi network
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Configure RF transmitter
  rfTransmitter.enableTransmit(RF_TRANSMITTER_PIN);
  rfTransmitter.setRepeatTransmit(10);
  rfTransmitter.setPulseLength(390);

  // Or use legacy authenticate method
  config.database_url = DATABASE_URL;
  config.signer.test_mode = true;
//   config.api_key = DATABASE_API_KEY;
  config.signer.tokens.legacy_token = DATABASE_API_KEY;
  
  Serial.printf("Begin Firebase with config and auth...\n");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.printf("Firebase.begin completed\n");

  // Get the stream configuration from the database
  getStreamPathConfig();

  // begin Firebase RTDB stream
  if (!Firebase.beginStream(stream, FB_BASE_STREAM_PATH))
  {
    Serial.println("Can't begin stream connection...");
    Serial.printf("REASON: %s\n", stream.errorReason().c_str());
  }
}

void loop() {
  while (millis() < 4147200000) { // 48 days so it resets number before it overloads.
    if (!Firebase.ready()) {
        return;
    }

    if (!Firebase.readStream(stream)) {
      Serial.println("Can't read stream data...");
      Serial.printf("REASON: %s\n", stream.errorReason().c_str());
    }

    if (stream.streamTimeout()) {
      Serial.println("stream timed out, resuming...\n");

      if (!stream.httpConnected()) {
        Serial.printf("error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
      }
    }

    if (stream.streamAvailable()) {
      Serial.printf("stream path, %s\nevent path, %s\ndata type, %s\nevent type, %s\n\n",
                stream.streamPath().c_str(),
                stream.dataPath().c_str(),
                stream.dataType().c_str(),
                stream.eventType().c_str());
      printResult(stream); // see addons/RTDBHelper.h
      Serial.println();

      if (!isStartingUp) {
        if (stream.dataPath() == FB_STREAM_RF_CODE_PATH) {
          // Send the value via the RF transmitter
          rfTransmitter.send(stream.intData(), 25); // 25 is the number of bits to be transmitted; adjust if needed 24 not enough for 1776964 so adjusting to 25
          Serial.printf("Sent stream value: %d\n", stream.intData());
        }
        else if (stream.dataPath() == FB_STREAM_BRIGHTNESS_PATH /* special case for repeated command*/ ) {
          // get Code(Decimal) value from database
          Firebase.getInt(fbdo, FB_BASE_STREAM_PATH + FB_STREAM_RF_CODE_PATH);
          Serial.printf("Color code value to send: %d\n", fbdo.intData());

          // Send the value via the RF transmitter
          rfTransmitter.send(fbdo.intData(), 25); // 25 is the number of bits to be transmitted; adjust if needed
        }
        else if (stream.dataPath() == FB_STREAM_POWER_STATE_PATH) {
          // sending power state binary rather than decimal
          rfTransmitter.send(stream.stringData().c_str());
          Serial.printf("Sent stream value: %s\n", stream.stringData().c_str());
        }
      }
      else {
        isStartingUp = false;
        Serial.printf("Started up, not sending stream value.\n");
      }

      Serial.printf("Received stream payload size: %d (Max. %d)\n\n", stream.payloadLength(), stream.maxPayloadLength());
    }
  }
}

/// Function to get the stream path from Firebase.
/// Used to change the stream path from the app without having to recompile the code.
void getStreamPathConfig() {
  // Check streamPath
  if (Firebase.getString(fbdo, String(FB_STREAM_PATH_CONFIG) + String(FB_STREAM_CONFIG_PATH))) {
    Serial.println("Stream path configuration obtained!");
    FB_BASE_STREAM_PATH = fbdo.stringData();
  } else {
    Serial.println("Can't obtain stream path configuration. Using default value.");
    Serial.printf("REASON: %s\n", fbdo.errorReason().c_str());
  }

  // Check rfCodePath
  if (Firebase.getString(fbdo, String(FB_STREAM_PATH_CONFIG) + String(FB_RF_CODE_CONFIG_PATH))) {
    Serial.println("RF code path configuration obtained!");
    FB_STREAM_RF_CODE_PATH = fbdo.stringData();
  } else {
    Serial.println("Can't obtain RF code path configuration. Using default value.");
    Serial.printf("REASON: %s\n", fbdo.errorReason().c_str());
  }

  // Check brightnessPath
  if (Firebase.getString(fbdo, String(FB_STREAM_PATH_CONFIG) + String(FB_BRIGHTNESS_CONFIG_PATH))) {
    Serial.println("Brightness path configuration obtained!");
    FB_STREAM_BRIGHTNESS_PATH = fbdo.stringData();
  } else {
    Serial.println("Can't obtain brightness path configuration. Using default value.");
    Serial.printf("REASON: %s\n", fbdo.errorReason().c_str());
  }

  // Check powerStatePath
  if (Firebase.getString(fbdo, String(FB_STREAM_PATH_CONFIG) + String(FB_POWER_STATE_CONFIG_PATH))) {
    Serial.println("Power state path configuration obtained!");
    FB_STREAM_POWER_STATE_PATH = fbdo.stringData();
  } else {
    Serial.println("Can't obtain power state path configuration. Using default value.");
    Serial.printf("REASON: %s\n", fbdo.errorReason().c_str());
  }
}
