/*
TODO: 
- add debug patches to Firebase to indicate if "PeripheralConnected", "ErrorMessages", etc.
- add feature so server is only connected if Nano is receiving data
- modify server reconnection function to timeout after a certain amount of time so it does not hang
- add way to reset the ESP32 (start FB stream and relay command to Nano who will send command to MKR WiFi 1010)
- maybe add timestamp to Firebase indicating when last bootup was
- maybe add failure method when RTC fails to set
- maybe add ability to only start up Firebase server connection if Nano detects data from peripheral
*/

/*
  This sketch will perform the following functions:
  - Connect to the internet via Ethernet
  - On boot up, connect to an NTP server and set the RTC
  - Connect to a Firebase server via SSL
  - Send a PATCH request to Firebase (for showing runtime)
  - Read data from the Nano 33 IoT via UART (TX/RX pins) 
    (Nano 33 IoT is acting as Bluetooth central device and is reading sensor data from a peripheral MKR 1010)
  - Send data to Firebase via PATCH request (for real-time monitoring)
  - Send a log entry of data values every minute (for long term monitoring)
  - Reconnect to server if disconnected (i.e. perihperal MKR 1010 is turned off/not sending data for some reason)
*/


#include <SPI.h>
#include <EthernetLarge.h>
#include <SSLClient.h>
#include <EthernetUdp.h>
#include <RTCZero.h>
#include "certificates.h"

#include "helpers.h"
#include "version.h"
#include "secrets.h"
#include "on_board_led.h"

#include <ArduinoJson.h>

// #Defines
#define DEBUG (false) // Set to true to enable debug output for SSL

// MKR 1010 ETH shield and board config
byte mac[] = SECRET_ETH_SHIELD_MAC;
IPAddress ip(SECRET_MKR_1010_IP);
IPAddress myDns(SECRET_DNS_GATEWAY);

// Choose the analog pin to get semi-random data from for SSL
// Pick a pin that's not connected or attached to a randomish voltage source
const int rand_pin = A5;

// Initialize the SSL client library
// We input an EthernetClient, our trust anchors, and the analog pin
EthernetClient base_client;
#if DEBUG
SSLClient client(base_client, TAs, (size_t)TAs_NUM, rand_pin, 1, SSLClient::SSL_INFO);
#else
SSLClient client(base_client, TAs, (size_t)TAs_NUM, rand_pin);
#endif
// Variables to measure the speed
unsigned long beginMicros, endMicros;
unsigned long byteCount = 0;
bool printWebData = true;  // set to false for better speed measurement

// Setup for UDP NTP client & RTC
unsigned int localPort = SECRET_LOCAL_PORT;

const char timeServer[] = "pool.ntp.org"; // pool of NTP servers to use
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

// declare an RTC object
RTCZero rtc;

const char firebaseHost[] = SECRET_DATABASE_URL;
// const char firebaseAuth[] = SECRET_DATABASE_SECRET; // Add in auth later

// global paths to upload realtime + log sensor data and debug data to Firebase
const char* firebaseRealtimeDataPath = "/CurrentConditions.json";
const char* firebaseLogSensorDataPath = "/Log/SensorData.json";
const char* firebaseDebugBLEConnectivityDataPath = "/Debug/PeripheralConnected.json";
const char* firebaseDebugErrorMessagesDataPath = "/Debug/ErrorMessage.json";

// flag to check if the program is starting up. If so, don't send data.
bool isStartingUp = true;


void setup() {
  // Ethernet.init(pin) to configure the CS pin
  Ethernet.init(5);   // MKR ETH shield

  // initialize serial communication
  Serial.begin(115200);
  Serial.println("\nSerial port connected.");
  Serial.print("Software version: v");
  Serial.println(RELEASE_VERSION);

  Serial1.begin(115200);
  establishSerialConnectionWithNano();

  // initialize the onboard LED
  WiFiDrv::pinMode(LED_RED, OUTPUT);
  WiFiDrv::pinMode(LED_GREEN, OUTPUT);
  WiFiDrv::pinMode(LED_BLUE, OUTPUT);
  setOnBoardLEDColor(255, 255, 255, LED_INTENSITY_HIGH); // white

  // start the Ethernet connection:
  connectToEthernet();
  // Give the Ethernet shield time to initialize
  delay(2000);

  // Initialize and set the RTC to the time from the NTP server
  setRTCFromNTPServer();

  if (!connectToServer()) {
    Serial.println("Connection to server failed.");
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    while (1);
  }

  // time measurement for data transfer rate during server connection
  beginMicros = micros();

  Serial.println("\nChecking for data from Nano33IoT...");
  // flicker LED blue then turn off
  setOnBoardLEDColor(0, 0, 255, LED_INTENSITY_HIGH); // blue
  delay(500);
  setOnBoardLEDColor(0, 0, 255, LED_INTENSITY_HIGH); // blue
  delay(500);
  setOnBoardLEDColor(0, 0, 0, LED_INTENSITY_HIGH); // off
}

void loop() {
  // read any incoming data from the server: (server disconects, etc.)
  readServerResponse();
      
  // if the server's disconnected, stop the client:
  if (!client.connected()) {
    // set on-board LED to yellow
    setOnBoardLEDColor(255, 255, 0, LED_INTENSITY_HIGH); // yellow
    reconnectToServer();
  }

  if (Serial1.available() /* maybe add > 0 */) {
    // Read and process data from the Nano 33 IoT and send to Firebase 
    processSensorDataFromNano();
  }
}


void establishSerialConnectionWithNano() {
  unsigned long lastAttemptTime = 0;
  const unsigned long attemptInterval = 1000; // half second

  Serial.println("Establishing handshake with Nano 33 IoT...");
  // keep sending connection message to Nano until we receive a response
  while (true) {
    // Check if debug command is received
    if (Serial.available()) {
      String command = Serial.readStringUntil('\n');
      command.trim();
      if (command == "DEBUG") {
        Serial.println("Debug mode enabled, skipping connection with Nano.");
        return;
      }
    }

    // if enough time has passed since the last attempt
    if (millis() - lastAttemptTime >= attemptInterval) {
      Serial.println("Sending READY_TO_CONNECT to Nano... ");
      // save the current time
      lastAttemptTime = millis();
      // send connection message to Nano
      Serial1.println("READY_TO_CONNECT");
    }

    // wait for acknowledgment from Nano
    if (Serial1.available()) {
      String received = Serial1.readStringUntil('\n');
      received.trim(); // remove any leading/trailing white space or special characters
      Serial.println("\tReceived: " + received); // Print any received message
      if (received == "NANO_CONNECTED") {
        Serial.println("Serial connection with Nano established!");
        return;
      }
    }
  }
}

void connectToEthernet() {
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
      while (true) {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    // try to configure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, myDns);
  } else {
    Serial.print("Ethernet connected. DCHP assigned IP:");
    Serial.println(Ethernet.localIP());
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(const char * address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void setRTCFromNTPServer() {
  // Initialize RTC
  rtc.begin();

  // get a random server from the pool
  Serial.println("Beginning UDP...");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
  setOnBoardLEDColor(255, 255, 0, LED_INTENSITY_HIGH); // yellow

  // Number of attempts to get NTP packet
  const int maxAttempts = 3;
  bool packetReceived = false;

  for(int attempt = 0; attempt < maxAttempts && !packetReceived; attempt++) {
    // send an NTP packet to a time server
    sendNTPpacket(timeServer); 

    // wait to see if a reply is available (must be > 4 seconds to resist a denial of service)
    delay(5000);

    if (Udp.parsePacket()) {
      Serial.println("Received NTP packet!");
      packetReceived = true;

      // We've received a packet, read the data from it
      Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

      // the timestamp starts at byte 40 of the received packet and is four bytes,
      // or two words, long. First, extract the two words:
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      // combine the four bytes (two words) into a long integer
      // this is NTP time (seconds since Jan 1 1900):
      unsigned long secsSince1900 = highWord << 16 | lowWord;

      // now convert NTP time into everyday time:
      Serial.print("Unix time = ");
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      // subtract seventy years:
      unsigned long epoch = secsSince1900 - seventyYears;
      // print Unix time:
      Serial.println(epoch);

      // Set the RTC to the time from the NTP server
      rtc.setEpoch(epoch);

      // Set the onboard LED to cyan to indicate success
      setOnBoardLEDColor(0, 255, 255, LED_INTENSITY_HIGH); // cyan
    } 
    else {
      Serial.print("Attempt ");
      Serial.print(attempt + 1);
      Serial.println(" to receive NTP packet failed.");
      setOnBoardLEDColor(255, 0, 255, LED_INTENSITY_HIGH); // purple
    }
  }

  if (!packetReceived) {
    Serial.println("No NTP packet received after maximum attempts. Fetching server timestamp from Firebase.");
    setOnBoardLEDColor(255, 165, 0, LED_INTENSITY_HIGH); // orange

    // Connect to the Firebase server
    if (!connectToServer()) {
      Serial.println("Connection to Firebase server failed.");
      setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
      while (1); // halt execution if connection fails
    }
    
    // Write a new entry to Firebase with the server's timestamp
    StaticJsonDocument<256> jsonPayload;
    jsonPayload["timestamp"][".sv"] = "timestamp";
    sendJsonPatchRequest("/timestamp.json", jsonPayload);

    // Read the timestamp back from Firebase
    sendGetRequest("/timestamp.json", processFirebaseTimestampResponse);
  }
}

void processFirebaseTimestampResponse() {
  // Wait for the server to respond
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }

  // Read all the lines of the reply from server
  String response = "";
  while (client.available()) {
    response += client.readStringUntil('\n');
  }

  // Optional: Add a delay to ensure the request has completed
  delay(100);

  // Parse the response
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  // Get the timestamp
  unsigned long timestamp = doc["timestamp"].as<unsigned long>();
  // Convert the timestamp from milliseconds to seconds
  unsigned long epoch = timestamp / 1000;

  // Set the RTC
  rtc.setEpoch(epoch);
}

bool connectToServer() {
  // connect to the server
  Serial.print("Connecting to ");
  Serial.print(firebaseHost);
  Serial.println(" ...");

  // if you get a connection, report back via serial:
  auto start = millis();
  // specify the server and port, 443 is the standard port for HTTPS
  if (client.connect(firebaseHost, 443)) {
    auto time = millis() - start;
    Serial.print("Took: ");
    Serial.print(time);
    Serial.println("ms");
    setOnBoardLEDColor(0, 255, 0, LED_INTENSITY_HIGH); // green
    return true;
  } else {
    // if you didn't get a connection to the server:
    Serial.println("Connection failed");
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    return false;
  }
}

// bool connectToServer() {
//   Serial.print("Connecting to ");
//   Serial.println(firebaseHost);

//   unsigned long retryDelay = 500; // Start with a half-second delay
//   const unsigned long maxRetryDelay = 60000; // Maximum delay of 60 seconds
//   bool isConnected = false;
//   int attemptCount = 0;
//   const int maxAttempts = 5; // Maximum number of connection attempts

//   while (!isConnected && attemptCount < maxAttempts) {
//     auto start = millis();
//     // Attempt to connect
//     if (client.connect(firebaseHost, 443)) {
//       // If connection is successful
//       auto time = millis() - start;
//       Serial.print("Connected! Took: ");
//       Serial.print(time);
//       Serial.println("ms");
//       setOnBoardLEDColor(0, 255, 0, LED_INTENSITY_HIGH); // Set LED to green
//       isConnected = true; // Exit loop on successful connection
//     } else {
//       // If connection fails
//       Serial.print("Connection failed. Attempt ");
//       Serial.print(attemptCount + 1);
//       Serial.println(" of 5.");
//       setOnBoardLEDColor(255, 255, 0, LED_INTENSITY_HIGH); // Set LED to yellow to indicate retry

//       delay(retryDelay); // Wait before retrying
//       retryDelay *= 2; // Exponential backoff for the next retry attempt
//       if (retryDelay > maxRetryDelay) {
//         retryDelay = maxRetryDelay; // Cap the retry delay
//       }
//     }
//     attemptCount++;
//   }

//   if (!isConnected) {
//     // If unable to connect after all attempts
//     Serial.println("Unable to connect after multiple attempts.");
//     setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // Set LED to red to indicate failure
//     return false;
//   }

//   return true;
// }


// void reconnectToServer() {
//   Serial.println("\nReconnecting to server...");
//   // if you get a connection, report back via serial:
//   auto start = millis();
//   if (client.connect(firebaseHost, 443)) {
//     // Handle connection success
//     Serial.println("Reconnected to server!");
//     auto time = millis() - start;
//     Serial.print("Took: ");
//     Serial.print(time);
//     Serial.println("ms");
//     // flicker onboard LED green 
//     setOnBoardLEDColor(0, 255, 0, LED_INTENSITY_HIGH); // green
//     delay(1000);
//     setOnBoardLEDColor(0, 0, 0, LED_INTENSITY_HIGH); // off    
//   } else {
//     // Handle connection failure
//     Serial.println("Reconnection failed.");
//     setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
//     // Perform some error handling or retry logic
//   }
// }

void reconnectToServer() {
  Serial.println("\nReconnecting to server...");

  unsigned long retryDelay = 500; // Start with a half second delay
  const unsigned long maxRetryDelay = 60000; // Maximum delay of 60 seconds
  bool connected = false;
  int attempt = 0;

  while (!connected && attempt < 5) { // Limit the number of attempts
    auto start = millis();
    int result = client.connect(firebaseHost, 443);

    if (result) {
      // Handle connection success
      auto time = millis() - start;
      Serial.println("Reconnected to server!");
      Serial.print("Took: ");
      Serial.print(time);
      Serial.println("ms");
      // Flicker onboard LED green
      setOnBoardLEDColor(0, 255, 0, LED_INTENSITY_HIGH); // green
      delay(1000);
      setOnBoardLEDColor(0, 255, 0, LED_INTENSITY_HIGH); // green
      delay(1000);
      setOnBoardLEDColor(0, 0, 0, LED_INTENSITY_HIGH); // off
      connected = true;
    } else {
      // Handle connection failure
      Serial.println("Reconnection failed. Attempting again...");
      setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red

      // Log the specific SSL error if possible
      if (client.getWriteError() == SSLClient::SSL_CLIENT_CONNECT_FAIL) {
        Serial.println("SSL Connection failed. Check internet connection and SSL settings.");
      } else if (client.getWriteError() != 0) {
        Serial.print("SSL Error Code: ");
        Serial.println(client.getWriteError());
      }

      delay(retryDelay);
      retryDelay *= 2; // Exponential backoff
      if (retryDelay > maxRetryDelay) {
        retryDelay = maxRetryDelay;
      }
    }
    attempt++;
  }

  if (!connected) {
    Serial.println("Failed to reconnect after multiple attempts.");
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    delay(500);
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    delay(500);
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    delay(5000);
    setOnBoardLEDColor(0, 0, 0, LED_INTENSITY_HIGH); // off

    // Perform a system reset
    NVIC_SystemReset();
  }
}


// TODO: Perhaps can add other use cases here and simply read updateType to properly handle dataPath and jsonPayload
void processSensorDataFromNano() {
  StaticJsonDocument<256> jsonPayload;
  DeserializationError error = deserializeJson(jsonPayload, Serial1);
  if (error.c_str() == "InvalidInput") {
    // ignore "InvalidInput" case (likely due to Nano not connected which only occurs in testing)
    //// documentation: https://arduinojson.org/v6/api/misc/deserializationerror/
    return;
  }
  else if (error) {
    // this oftentimes occurs when the input is empty (inbetween sensor readings)
    // use this case to catch that and not send unnecessary data to Firebase
    Serial.print(F("\ndeserializeJson() failed: "));
    Serial.println(error.c_str());
    // TODO: write to Firebase ErrorMessage path that there was an error to get a better sense of what is going on.
    return;
  }
  
  // Print the received JSON object in a human-readable format
  Serial.println("\nData received from Nano 33 IoT!");
  Serial.println("Received JSON payload:");
  serializeJsonPretty(jsonPayload, Serial);
  Serial.println();

  // Read the value at the "type" key to determine if the data is a realtime update or a log entry
  const char* updateType = jsonPayload["type"];

  // Remove the "type" key from the jsonPayload
  jsonPayload.remove("type");

  // Determine the correct data path based on the "type" key value
  const char* dataPath = nullptr;
  if (strcmp(updateType, "realtime") == 0) {
    dataPath = firebaseRealtimeDataPath;
  } else if (strcmp(updateType, "log") == 0) {
    dataPath = firebaseLogSensorDataPath;

    // Create a new JSON object to store the timestamp and original JSON payload
    StaticJsonDocument<256> jsonToSend;

    // Add the Unix epoch timestamp as a key
    unsigned long epoch = rtc.getEpoch();
    jsonToSend[String(epoch)] = jsonPayload;

    // Create an intermediate JsonObject to store the ".sv" and "timestamp" values
    JsonObject timestampObj = jsonToSend[String(epoch)].createNestedObject("timestamp");
    timestampObj[".sv"] = "timestamp";

    // Update jsonPayload with the jsonToSend content
    jsonPayload = jsonToSend;
    /* jsonPayload will look like this:
    * { "1623686400" : {
    *    "timestamp" : {
    *     ".sv" : "timestamp"
    *   },
    *   "temperature" : 54.00,
    *   "totalDissolvedSolids" : 220,
    *   "turbidity" : 1000,
    *   "turbidityVoltage" : 4.05,
    *   "waterLevel" : 6.30,
    *   "pH" : 6.80
    *   }
    * }
    */
  } else if (strcmp(updateType, "debug") == 0) {
    dataPath = firebaseDebugBLEConnectivityDataPath;
    // send patch to Firebase data path Debug/PeripheralConnected
    /* jsonPayload will look like this:
    * { "PeripheralConnected" : {
    *    "value" : "true",
    *    "timestamp" : {
    *     ".sv" : "timestamp"
    *    }
    *  }
    * }
    */
  }
  else {
    Serial.println("Invalid update type. Data not sent to Firebase.");
    return;
  }

  // Send the data (jsonPayload) to Firebase dataPath
  sendJsonPatchRequest(dataPath, jsonPayload);
}

void readServerResponse() {
  int len = client.available();
  if (len > 0) {
    byte buffer[80];
    if (len > 80) len = 80;
    client.read(buffer, len);
    if (printWebData) {
      Serial.write(buffer, len); // show in the serial monitor (slows some boards)
    }
    byteCount = byteCount + len;
  }
}

void processServerResponse() {
  // Wait for the server to respond
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  // Read all the lines of the reply from server and print them to Serial
  size_t byteCount = 0;
  while (client.available()) {
    size_t len = client.available();
    byte buffer[80];
    if (len > 80) len = 80;
    client.read(buffer, len);
    if (printWebData) {
      Serial.write(buffer, len);
    }
    byteCount += len;
  }
  // Optional: Add a delay to ensure the request has completed
  delay(100);
}

void sendJsonPatchRequest(const char* path, const StaticJsonDocument<256>& jsonPayload) {
  String payload;
  serializeJson(jsonPayload, payload);

  client.print("PATCH ");
  client.print(path);
  // client.print("?auth=");
  // client.print(firebaseAuth);
  client.println(" HTTP/1.1");
  client.println("User-Agent: SSLClientOverEthernet");
  client.print("Host: ");
  client.println(firebaseHost);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println("Connection: keep-alive");
  client.println();
  client.println(payload);

  // serializeJson(jsonPayload, client); // can possibly use this instead of the above code

  // Process the response
  processServerResponse();
}

/// Checks if the data is a number or a string and creates corresponding JSON payload syntax
bool isNumber(const String &data) {
  char *endptr;
  float val = strtod(data.c_str(), &endptr);
  return endptr != data.c_str() && *endptr == '\0';
}

void sendPatchRequest(const char* path, const char* key, const String& data) {
  // Check if the data is a number or a string
  bool isString = !isNumber(data);

  /// Construct the JSON payload
  String payload = "{\"";
  payload += key;
  payload += "\": ";
  // Add double quotes if the data type is a string
  if (isString) {
    payload += "\"";
  }
  // Add data
  payload += data;
  // Add closing double quotes if the data type is a string
  if (isString) {
    payload += "\"";
  }
  // Close the JSON object
  payload += "}";

  client.print("PATCH ");
  client.print(path);
  // client.print("?auth=");
  // client.print(firebaseAuth);
  client.println(" HTTP/1.1");
  client.println("User-Agent: SSLClientOverEthernet");
  client.print("Host: ");
  client.println(firebaseHost);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(payload.c_str()));
  client.println("Connection: keep-alive");
  client.println();
  client.println(payload);

  // Process the response
  processServerResponse();
}

void sendGetRequest(const char* path) {
  client.print("GET ");
  client.print(path);
  // client.print("?auth=");
  // client.print(firebaseAuth);
  client.println(" HTTP/1.1");
  client.println("User-Agent: SSLClientOverEthernet");
  client.print("Host: ");
  client.println(firebaseHost);
  client.println("Connection: keep-alive");
  client.println();

  // process the response
  processServerResponse();
}

// Overloaded sendGetRequest function to handle and return the response to pass elsewhere
void sendGetRequest(const char* path, void (*processResponse)()) {
  client.print("GET ");
  client.print(path);
  // client.print("?auth=");
  // client.print(firebaseAuth);
  client.println(" HTTP/1.1");
  client.println("User-Agent: SSLClientOverEthernet");
  client.print("Host: ");
  client.println(firebaseHost);
  client.println("Connection: keep-alive");
  client.println();

  // process the response
  processResponse();
}

void sendPutRequest(const char* path, const char* data) {
  client.print("PUT ");
  client.print(path);
  // client.print("?auth=");
  // client.print(firebaseAuth);
  client.println(" HTTP/1.1");
  client.println("User-Agent: SSLClientOverEthernet");
  client.print("Host: ");
  client.println(firebaseHost);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(data));
  client.println("Connection: keep-alive");
  client.println();
  client.println(data);

  // process the response
  processServerResponse();
}

void disconnectFromServer() {
  endMicros = micros();
  Serial.println();
  Serial.println("Server disconnected. Stopping client.");
  client.stop();
  Serial.print("Received ");
  Serial.print(byteCount);
  Serial.print(" bytes in ");
  float seconds = (float)(endMicros - beginMicros) / 1000000.0;
  Serial.print(seconds, 4);
  float rate = (float)byteCount / seconds / 1000.0;
  Serial.print(", rate = ");
  Serial.print(rate);
  Serial.print(" kbytes/second");
  Serial.println();

  // do nothing forevermore (for now):
  while (true) {
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH);
    delay(1);
  }
}