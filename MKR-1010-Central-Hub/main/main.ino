/*
TODO: 
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

#include <ArduinoJson.h>
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

// #Defines
#define DEBUG (false) // Set to true to enable debug output for SSL and startup serial messages

#define SERVER_PORT 80

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
SSLClient firebaseClient(base_client, TAs, (size_t)TAs_NUM, rand_pin, 1, SSLClient::SSL_DUMP);
#else
SSLClient firebaseClient(base_client, TAs, (size_t)TAs_NUM, rand_pin);
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

// Initialize the Ethernet server object to listen for incoming connections (from the iOS app)
EthernetServer server(SERVER_PORT);

// declare an RTC object
RTCZero rtc;

const char firebaseHost[] = SECRET_DATABASE_URL;
// const char firebaseAuth[] = SECRET_DATABASE_SECRET; // Add in auth later

// global paths to upload realtime + log sensor data and debug data to Firebase
const char* firebaseRealtimeDataPath = "/CurrentConditions.json";
const char* firebaseLogSensorDataPath = "/Log/SensorData.json";
const char* firebaseDebugLogSensorDataPath = "/Debug/Log/SensorData.json";
const char* firebaseDebugBLEConnectivityDataPath = "/Debug/PeripheralConnected.json";
const char* firebaseDebugErrorMessagesDataPath = "/Debug/ErrorMessage.json";

bool recentlyDisconnected = false; // Tracks if we've recently disconnected
static unsigned long lastDisconnectTime = 0;  // Timestamp of the last disconnect
const unsigned long disconnectDelay = 15000;  // Delay before trying to reconnect, in milliseconds

bool socketsInUse[MAX_SOCK_NUM] = {false}; // MAX_SOCK_NUM is defined in EthernetLarge.h

extern "C" char* sbrk(int incr);
int freeRam();
void display_freeram();

void setup() {
  Ethernet.init(5);   // MKR ETH shield

  // initialize serial communication
  if (DEBUG) {
    Serial.begin(115200);
    while (!Serial) {
      ; // Wait for the serial port connection, for debugging purposes.
    }
    Serial.println(F("\nSerial port connected."));
    Serial.println("Software version: v" + String(RELEASE_VERSION) + "_debug");
  } else {
    Serial.begin(115200);
    Serial.println(F("\nSerial port ready."));
    Serial.print(F("Software version: v"));
    Serial.println(RELEASE_VERSION);
  }
  
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

  // Start the server
  server.begin();
  Serial.print(F("Server is at "));
  Serial.println(Ethernet.localIP());

  if (DEBUG) {
  display_freeram();  // Display free RAM before attempting to connect
  }

  if (!connectToServer()) {
    Serial.println(F("Connection to server failed."));
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    while (1);
  }

  if (DEBUG) {
    display_freeram();  // Display free RAM after attempting to connect
  }

  // time measurement for data transfer rate during server connection
  beginMicros = micros();

  Serial.println(F("Checking for data from Nano33IoT..."));
  setOnBoardLEDColor(0, 0, 255, LED_INTENSITY_HIGH); // blue
}

void loop() {
  // Check for incoming client requests and handle them
  EthernetClient localClient = server.available();
  if (localClient) {
    uint8_t socketNum = localClient.getSocketNumber();
    if (!socketsInUse[socketNum]) {
        socketsInUse[socketNum] = true; // Mark this socket as in use
        Serial.println("New Client detected.");
        Serial.print("Socket number: ");
        Serial.println(socketNum);
        handleClientRequests(localClient);
    } else {
        Serial.print("Ongoing operation detected on socket ");
        Serial.println(socketNum);
    }
  }

  // read any incoming data from the server: (server disconects, etc.)
  if (firebaseClient.connected()) {
    readServerResponse();
  } else {
    if (handleDisconnection()) {
      lastDisconnectTime = millis();  // Update the last disconnect time
      flushSerial1(); // Flush any incoming serial data immediately after disconnection
      return; // Exit the loop to avoid further processing if disconnected
    }
  }

  if (Serial1.available() > 0) {
    // Ensure a delay has passed before reconnecting
    if (millis() - lastDisconnectTime > disconnectDelay) {
      // Read and process data from the Nano 33 IoT and route correctly
      processSensorDataFromNano(localClient);
    } else {
      flushSerial1(); // disregard incoming nano data during delay period
      // lets keep checking if our firebase connection is closed successfully
      if (firebaseClient.m_soft_connected(__func__)) {
        Serial.println(F("Soft check failed: SSL connection was not closed properly."));
      } else {
        // Serial.println(F("SSL connection closed properly."));
      }
    }
  }
}

int freeRam() {
  char top;
  return &top - reinterpret_cast<char*>(sbrk(0));
}

void display_freeram(){
  Serial.print(F("- SRAM left: "));
  Serial.println(freeRam());
  Serial.println((int)sbrk(0));
}

void flushSerial1() {
  while (Serial1.available() > 0) {
      Serial1.read(); // Read and discard the incoming byte
  }
}

bool handleDisconnection() {
  if (!firebaseClient.connected() && !recentlyDisconnected) {
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // Red
    disconnectFromServer();
    recentlyDisconnected = true;
    return true;  // Indicate that a disconnection was handled
  }
  return false;  // No disconnection was handled
}

bool readRequestPath(EthernetClient& client, String& requestPath, String& queryString) {
  bool currentLineIsBlank = true;
  String currentLine = "";
  bool requestLineParsed = false;
  bool isHeaderSection = false;

  while (client.available()) {
      char c = client.read();
      Serial.write(c); // Echo to serial monitor for debugging

      if (c == '\n' || c == '\r') {
          if (currentLineIsBlank && isHeaderSection) {
              // End of headers detected after an empty line, indicating that headers are fully parsed.
              break; 
          }
          if (!requestLineParsed && currentLine.length() > 0) {
              // Parsing the request line
              int firstSpace = currentLine.indexOf(' ');
              int secondSpace = currentLine.indexOf(' ', firstSpace + 1);
              if (firstSpace != -1 && secondSpace != -1) {
                  requestPath = currentLine.substring(firstSpace + 1, secondSpace);
                  int questionMark = requestPath.indexOf('?');
                  if (questionMark != -1) {
                      queryString = requestPath.substring(questionMark + 1);
                      requestPath = requestPath.substring(0, questionMark);
                  }
                  Serial.println("\nRequest Path: '" + requestPath + "', Query String: '" + queryString + "'");
                  requestLineParsed = true;
                  isHeaderSection = true; // Start expecting headers next
              }
          } else if (isHeaderSection) {
              // Processing and ignoring headers
              if (currentLine.length() > 0) {
                  Serial.println("Processing Header: " + currentLine);
              }
          }
          currentLine = ""; // Reset current line
          currentLineIsBlank = true;
      } else {
          currentLine += c;
          currentLineIsBlank = false;
      }
  }
  return requestLineParsed;
}

void handleClientRequests(EthernetClient& client) {
  uint8_t socketNum = client.getSocketNumber();

  String requestPath, queryString;
  if (readRequestPath(client, requestPath, queryString)) {
      if (requestPath == "/status") {
          Serial.println("Responding with LED status...");
          respondWithLEDStatus(client);
          client.stop();  // It's safe to close connection here
          markSocketAsFree(socketNum); // Mark the socket as free after the client is stopped
      } else if (requestPath == "/status/nano") {
          Serial.println("Requesting Nano status...");
          requestNanoStatus();
          // Do not close the client connection here, wait for the response from the Nano
          // Make sure to close the connection after the Nano response is processed and sent
      } else if (requestPath == "/calibrate/ph") {
          Serial.println("Calibrating pH...");
          handlePhCalibration(client, queryString);
          client.stop();  // Close connection if response is immediate
          // Serial.println("Client Disconnected.");
          markSocketAsFree(socketNum);
      } else {
          // Respond with a 404 Not Found error for unrecognized paths
          Serial.println("Responding with 404 Not Found...");
          client.println("HTTP/1.1 404 Not Found");
          client.println("Content-Type: text/plain");
          client.println("Connection: close");
          client.println();
          client.println("Error: Not Found");
          client.stop();  // Close the client connection here as well
          // Serial.println("Client Disconnected.");
          markSocketAsFree(socketNum);
      }
  } else {
      // If path could not be read, also return a 404 error
      Serial.println("Bad Request or Incomplete Request...");
      client.println("HTTP/1.1 400 Bad Request");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Error: Not Found");
      client.stop();  // Close the client connection
      markSocketAsFree(socketNum);
  }
}

void markSocketAsFree(uint8_t socketNum) {
  // Ensure the socket number is valid before marking it free (should always be valid)
  if (socketNum < MAX_SOCK_NUM) {
      socketsInUse[socketNum] = false; // Reset the flag indicating this socket is no longer in use
      Serial.println("Socket " + String(socketNum) + " marked as free.");
  } else {
      Serial.print("Error: Invalid socket number " + String(socketNum) + " provided.");
  }
}

void handlePhCalibration(EthernetClient& localClient, String queryString) {
    // Extract calibration values from the query string
    String lowCal = getValue(queryString, "low_cal=");
    String midCal = getValue(queryString, "mid_cal=");
    String highCal = getValue(queryString, "high_cal=");

    if (midCal.length() > 0 && lowCal.length() > 0 && highCal.length() > 0) {
        String calibrationCommand = "CALIBRATE_PH " + lowCal + "," + midCal + "," + highCal;
        Serial1.println(calibrationCommand);

        // Wait for a response or timeout
        String response = "";
        long timeout = millis() + 5000;  // 5-second timeout
        while (millis() < timeout && response.length() == 0) {
            if (Serial1.available() > 0) {
                char c = Serial1.read();
                if (c == '\n') break;  // End of message
                response += c;
            }
        }

        if (response.length() > 0 && response == "CALIBRATION_SUCCESS") {
            localClient.println("HTTP/1.1 200 OK");
            localClient.println("Content-Type: text/plain");
            localClient.println("Connection: close");
            localClient.println();
            localClient.println("Calibration successful: " + response);
        } else if (response == "PENDING_OPERATION") {
            localClient.println("HTTP/1.1 202 Accepted");
            localClient.println("Content-Type: text/plain");
            localClient.println("Connection: close");
            localClient.println();
            localClient.println("Calibration pending until peripheral connection.");
        } else {
            localClient.println("HTTP/1.1 504 Gateway Timeout");
            localClient.println("Content-Type: text/plain");
            localClient.println("Connection: close");
            localClient.println();
            localClient.println("Calibration failed or no response");
        }
    } else {
        localClient.println("HTTP/1.1 400 Bad Request");
        localClient.println("Content-Type: text/plain");
        localClient.println("Connection: close");
        localClient.println();
        localClient.println("Missing one or more calibration parameters");
    }
}

String getValue(String data, String key) {
    int found = data.indexOf(key);
    if (found == -1) return "";

    int startIndex = found + key.length();
    int endIndex = data.indexOf('&', startIndex);
    if (endIndex == -1) endIndex = data.length();

    return data.substring(startIndex, endIndex);
}

void establishSerialConnectionWithNano() {
  unsigned long lastAttemptTime = 0;
  const unsigned long attemptInterval = 1000; // half second

  Serial.println(F("Establishing handshake with Nano 33 IoT..."));
  // keep sending connection message to Nano until we receive a response
  while (true) {
    // Check if debug command is received
    if (Serial.available()) {
      String command = Serial.readStringUntil('\n');
      command.trim();
      if (command == "DEBUG") {
        Serial.println(F("Debug mode enabled, skipping connection with Nano."));
        return;
      }
    }

    // if enough time has passed since the last attempt
    if (millis() - lastAttemptTime >= attemptInterval) {
      Serial.println(F("Sending READY_TO_CONNECT to Nano... "));
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
        Serial.println(F("Serial connection with Nano established!"));
        return;
      }
    }
  }
}

void connectToEthernet() {
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("Failed to configure Ethernet using DHCP"));
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println(F("Ethernet shield was not found.  Sorry, can't run without hardware. :("));
      setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
      while (true) {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println(F("Ethernet cable is not connected."));
    }
    // try to configure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, myDns);
  } else {
    Serial.print(F("Ethernet connected. DCHP assigned IP:"));
    Serial.println(Ethernet.localIP());
  }
}

void sendNTPpacket(const char * address) {
  // send an NTP request to the time server at the given address
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
  Serial.println(F("Beginning UDP..."));
  Udp.begin(localPort);
  Serial.print(F("Local port: "));
  Serial.println(Udp.localPort());
  setOnBoardLEDColor(255, 255, 0, LED_INTENSITY_HIGH); // yellow

  // Number of attempts to get NTP packet
  const int maxAttempts = 3;
  bool packetReceived = false;

  for(int attempt = 0; attempt < maxAttempts && !packetReceived; attempt++) {
    int waitTime = attempt == 0 ? 0 : 4000; // 5000ms initially, 4000ms after the first attempt
    delay(waitTime);
    // send an NTP packet to a time server
    sendNTPpacket(timeServer);

    // wait to see if a reply is available (must be > 4 seconds to resist a denial of service)
    delay(1000);

    if (Udp.parsePacket()) {
      Serial.println(F("Received NTP packet!"));
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
      Serial.print(F("Unix time = "));
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
      Serial.print(F("Attempt "));
      Serial.print(attempt + 1);
      Serial.println(F(" to receive NTP packet failed."));
      setOnBoardLEDColor(255, 0, 255, LED_INTENSITY_HIGH); // purple
    }
  }

  // Close the UDP socket whether NTP sync was successful or not
  Udp.stop();
  if (!packetReceived) {
    Serial.println(F("No NTP packet received after maximum attempts. Fetching server timestamp from Firebase."));
    setOnBoardLEDColor(255, 165, 0, LED_INTENSITY_HIGH); // orange

    // Connect to the Firebase server
    if (!connectToServer()) {
      Serial.println(F("Connection to Firebase server failed."));
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
  while (firebaseClient.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      firebaseClient.stop();
      return;
    }
  }

  // Read all the lines of the reply from server
  String response = "";
  while (firebaseClient.available()) {
    response += firebaseClient.readStringUntil('\n');
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
  Serial.print(F("Connecting to "));
  Serial.print(firebaseHost);
  Serial.println(F(" ..."));

  // if you get a connection, report back via serial:
  auto start = millis();
  // specify the server and port, 443 is the standard port for HTTPS
  if (firebaseClient.connect(firebaseHost, 443)) {
    auto time = millis() - start;
    Serial.print(F("Took: "));
    Serial.print(time);
    Serial.println(F("ms"));
    setOnBoardLEDColor(0, 255, 0, LED_INTENSITY_HIGH); // green
    return true;
  } else {
    // if you didn't get a connection to the server:
    Serial.println(F("Connection failed"));
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    return false;
  }
}

void reconnectToServer() {
  // Check if we are actually closed before trying
  if (firebaseClient.m_soft_connected(__func__)) {
    Serial.println(F("Soft check failed: SSL connection was not closed properly."));
  }

  Serial.println(F("\nReconnecting to server..."));
  setOnBoardLEDColor(255, 255, 0, LED_INTENSITY_HIGH); // yellow

  unsigned long retryDelay = 2000; // Start with a 3-second delay
  const unsigned long maxRetryDelay = 60000; // Maximum delay of 60 seconds
  bool connected = false;
  int attempt = 0;

  while (!connected && attempt < 5) { // Limit the number of attempts
    auto start = millis();
    int result = firebaseClient.connect(firebaseHost, 443);
    display_freeram();  // Display free RAM after attempting to reconnect

    if (result) {
      // Handle connection success
      auto time = millis() - start;
      Serial.println(F("Reconnected to server!"));
      Serial.print(F("Took: "));
      Serial.print(time);
      Serial.println(F("ms"));
      // Flicker onboard LED green
      setOnBoardLEDColor(0, 255, 0, LED_INTENSITY_HIGH); // green
      connected = true;
      Serial.println(F("Connected."));
      // Serial.println("Connected on attempt " + String(attempt + 1) + ".");
    } else {
      // Handle connection failure
      Serial.println(F("Reconnection failed. Attempting again..."));
      setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red

      // Log the specific SSL error if possible
      if (firebaseClient.getWriteError() == SSLClient::SSL_CLIENT_CONNECT_FAIL) {
        Serial.println(F("SSL Connection failed. Check internet connection and SSL settings."));
      } else if (firebaseClient.getWriteError() != 0) {
        Serial.print(F("SSL Error Code: "));
        Serial.println(firebaseClient.getWriteError());
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
    Serial.println(F("Failed to reconnect after multiple attempts."));
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    delay(500);
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    delay(500);
    setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red
    delay(5000);
    setOnBoardLEDColor(0, 0, 0, LED_INTENSITY_HIGH); // off

    // Perform a system reset
    NVIC_SystemReset();
  } else {
    display_freeram();  // Display free RAM after successful connection
  }
}

void processSensorDataFromNano(EthernetClient& localClient) {
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
    // Serial.print(F("\ndeserializeJson() failed: "));
    // Serial.println(error.c_str());
    return;
  }
  
  // Print the received JSON object in a human-readable format
  Serial.println(F("Data received from Nano 33 IoT!"));
  Serial.println(F("Received JSON payload:"));
  serializeJsonPretty(jsonPayload, Serial);
  Serial.println();

  Serial.print(F("JSON Object Size: "));
  Serial.println(measureJson(jsonPayload));

  // Read the value at the "type" key to route data correctly
  const char* updateType = jsonPayload["type"];
  jsonPayload.remove("type"); // Remove the "type" key from the JSON payload

  // Determine the correct data path based on the "type" key value
  if (strcmp(updateType, "status") == 0) {
      respondWithStatus(localClient, jsonPayload);
  } else if (strcmp(updateType, "realtime") == 0 || strcmp(updateType, "realtime-debug") == 0) {
    Serial.println(F("Hi!"));
    // if the server's disconnected, stop the client:
    if (!firebaseClient.connected()) {
      display_freeram();  // Display free RAM before attempting to reconnect
      reconnectToServer();
      display_freeram();  // Display free RAM after attempting to reconnect
      Serial.println(F("Reconnected to server. Sending sensor data..."));
      recentlyDisconnected = false; // Reset flag on successful reconnection
      return;
    }
    Serial.println(F("Hello!"));
    // freeRam before sending data
    display_freeram();
    sendJsonPatchRequest(firebaseRealtimeDataPath, jsonPayload);
    // freeRam after sending data
    display_freeram();
    Serial.println(F("Sent realtime data to Firebase."));
  } else if (strcmp(updateType, "log") == 0 || strcmp(updateType, "log-debug") == 0) {
    const char* firebasePath = (strcmp(updateType, "log") == 0) ? firebaseLogSensorDataPath : firebaseDebugLogSensorDataPath;
    if (!firebaseClient.connected()) {
      reconnectToServer();
      Serial.println(F("Reconnected to server. Sending log data..."));
      recentlyDisconnected = false; // Reset flag on successful reconnection
    }
    handleLogType(firebasePath, jsonPayload);
  } else {
    Serial.println(F("Invalid update type. Ignoring data."));
    return;
  }
}

void respondWithStatus(EthernetClient& client, const JsonDocument& jsonPayload) {
  uint8_t socketNum = client.getSocketNumber();

  if (client.connected()) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    serializeJson(jsonPayload, client);
    client.stop();  // Close the client connection
    Serial.println("Sent status response: ");
    serializeJsonPretty(jsonPayload, Serial);
    Serial.println();
    markSocketAsFree(socketNum);
  }
}

void handleLogType(const char* path, const JsonDocument& jsonPayload) {
    StaticJsonDocument<256> jsonToSend;
    unsigned long epoch = rtc.getEpoch();
    jsonToSend[String(epoch)] = jsonPayload;

    JsonObject timestampObj = jsonToSend[String(epoch)].createNestedObject("timestamp");
    timestampObj[".sv"] = "timestamp";

    sendJsonPatchRequest(path, jsonToSend);
}

void readServerResponse() {
  // check again if we are actually connected here
  if (!firebaseClient.m_soft_connected(__func__)) {
    Serial.println(F("Soft check failed: SSL connection was not closed properly."));
  } else {
    // Read all the lines of the reply from server and print them to Serial
    size_t byteCount = 0;
    while (firebaseClient.available()) {
      size_t len = firebaseClient.available();
      byte buffer[80];
      if (len > 80) len = 80;
      firebaseClient.read(buffer, len);
      if (printWebData) {
        Serial.write(buffer, len);
      }
      byteCount += len;
    }
    // Optional: Add a delay to ensure the request has completed
    delay(100);

    // int len = firebaseClient.available();
    // if (len > 0) {
    //   byte buffer[80];
    //   if (len > 80) len = 80;
    //   firebaseClient.read(buffer, len);
    //   if (printWebData) {
    //     Serial.write(buffer, len); // show in the serial monitor (slows some boards)
    //   }
    //   byteCount = byteCount + len;
    // }
    // // Optional: Add a delay to ensure the request has completed
    // delay(100);
  }
}

void processServerResponse() {
  // // Wait for the server to respond
  // unsigned long timeout = millis();
  // while (firebaseClient.available() == 0) {
  //   if (millis() - timeout > 5000) {
  //     Serial.println(">>> Client Timeout !");
  //     firebaseClient.stop();
  //     return;
  //   }
  // }
  // Read all the lines of the reply from server and print them to Serial
  size_t byteCount = 0;
  while (firebaseClient.available()) {
    size_t len = firebaseClient.available();
    byte buffer[80];
    if (len > 80) len = 80;
    firebaseClient.read(buffer, len);
    if (printWebData) {
      Serial.write(buffer, len);
    }
    byteCount += len;
  }
  // Optional: Add a delay to ensure the request has completed
  delay(100);
}

void sendJsonPatchRequest(const char* path, const StaticJsonDocument<256>& jsonPayload) {
  Serial.println(F("Serializing JSON payload..."));
  Serial.print(F("Free RAM before serialization: "));
  Serial.println(freeRam());

  Serial.println(jsonPayload.capacity());
  Serial.println(jsonPayload.memoryUsage());
  Serial.print(F("jsonPayload.overflowed?: "));
  Serial.println(jsonPayload.overflowed());

  Serial.println(F("Sending JSON patch request..."));

  if (firebaseClient.available() != 0) {
    Serial.println(F("Firebase client not available."));
    return;
  }

  // Calculate content length by serializing to a temporary buffer or using measureJson
  size_t contentLength = measureJson(jsonPayload);

  firebaseClient.print(F("PATCH "));
  firebaseClient.print(path);
  // firebaseClient.print(F("?auth="));
  // firebaseClient.print(firebaseAuth);
  firebaseClient.println(F(" HTTP/1.1"));
  firebaseClient.println(F("User-Agent: SSLClientOverEthernet"));
  firebaseClient.print(F("Host: "));
  firebaseClient.println(firebaseHost);
  firebaseClient.println(F("Content-Type: application/json"));
  firebaseClient.print(F("Content-Length: "));
  firebaseClient.println(contentLength);
  firebaseClient.println(F("Connection: keep-alive"));
  firebaseClient.println();
  // Serialize JSON directly to the client, effectively sending the payload
  serializeJson(jsonPayload, firebaseClient);
  Serial.println(F("Sent JSON patch request directly using serializeJson."));

  Serial.print(F("Free RAM after serialization: "));
  Serial.println(freeRam());
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

  firebaseClient.print("PATCH ");
  firebaseClient.print(path);
  // firebaseClient.print("?auth=");
  // firebaseClient.print(firebaseAuth);
  firebaseClient.println(" HTTP/1.1");
  firebaseClient.println("User-Agent: SSLClientOverEthernet");
  firebaseClient.print("Host: ");
  firebaseClient.println(firebaseHost);
  firebaseClient.println("Content-Type: application/json");
  firebaseClient.print("Content-Length: ");
  firebaseClient.println(strlen(payload.c_str()));
  firebaseClient.println("Connection: keep-alive");
  firebaseClient.println();
  firebaseClient.println(payload);

  // Process the response
  // processServerResponse();
}

void sendGetRequest(const char* path) {
  firebaseClient.print("GET ");
  firebaseClient.print(path);
  // firebaseClient.print("?auth=");
  // firebaseClient.print(firebaseAuth);
  firebaseClient.println(" HTTP/1.1");
  firebaseClient.println("User-Agent: SSLClientOverEthernet");
  firebaseClient.print("Host: ");
  firebaseClient.println(firebaseHost);
  firebaseClient.println("Connection: keep-alive");
  firebaseClient.println();

  // process the response
  // processServerResponse();
}

// Overloaded sendGetRequest function to handle and return the response to pass elsewhere
void sendGetRequest(const char* path, void (*processResponse)()) {
  firebaseClient.print("GET ");
  firebaseClient.print(path);
  // firebaseClient.print("?auth=");
  // firebaseClient.print(firebaseAuth);
  firebaseClient.println(" HTTP/1.1");
  firebaseClient.println("User-Agent: SSLClientOverEthernet");
  firebaseClient.print("Host: ");
  firebaseClient.println(firebaseHost);
  firebaseClient.println("Connection: keep-alive");
  firebaseClient.println();

  // process the response
  processResponse();
}

void sendPutRequest(const char* path, const char* data) {
  firebaseClient.print("PUT ");
  firebaseClient.print(path);
  // firebaseClient.print("?auth=");
  // firebaseClient.print(firebaseAuth);
  firebaseClient.println(" HTTP/1.1");
  firebaseClient.println("User-Agent: SSLClientOverEthernet");
  firebaseClient.print("Host: ");
  firebaseClient.println(firebaseHost);
  firebaseClient.println("Content-Type: application/json");
  firebaseClient.print("Content-Length: ");
  firebaseClient.println(strlen(data));
  firebaseClient.println("Connection: keep-alive");
  firebaseClient.println();
  firebaseClient.println(data);

  // process the response
  // processServerResponse();
}

void respondWithLEDStatus(EthernetClient& localClient) {
    int red, green, blue, intensity;
    getOnBoardLEDColor(&red, &green, &blue, &intensity); // Fetch the current LED status

    // Construct JSON string
    String jsonResponse = "{\"red\":" + String(red) + ",\"green\":" + String(green) + ",\"blue\":" + String(blue) + ",\"intensity\":" + String(intensity) + "}";
    Serial.println("Sending LED status response: " + jsonResponse);

    // Send HTTP headers
    localClient.println("HTTP/1.1 200 OK");
    localClient.println("Content-Type: application/json"); // Indicate the response type is JSON
    localClient.println("Connection: close");  // The connection will be closed after completion of the response
    localClient.println();  // End of HTTP headers

    // Send the JSON response
    localClient.println(jsonResponse);
}

void requestNanoStatus() {
    // Log to the serial that a status command is being sent to the Nano
    Serial.println("Sending STATUS command to Nano...");
    // Send the STATUS command to the Nano 33 IoT
    Serial1.println("STATUS");
}

void disconnectFromServer() {
  display_freeram();  // Display free RAM before attempting to connect
  endMicros = micros();
  Serial.println();
  Serial.println(F("Server disconnected. Stopping client."));
  firebaseClient.stop();
  Serial.print(F("Received "));
  Serial.print(byteCount);
  Serial.print(F(" bytes in "));
  float seconds = (float)(endMicros - beginMicros) / 1000000.0;
  Serial.print(seconds, 4);
  float rate = (float)byteCount / seconds / 1000.0;
  Serial.print(F(", rate = "));
  Serial.print(rate);
  Serial.print(F(" kbytes/second"));
  Serial.println();

  setOnBoardLEDColor(255, 0, 0, LED_INTENSITY_HIGH); // red

  // Check if we actually stopped the client and the SSL connection was closed gracefully
  if (firebaseClient.m_soft_connected(__func__)) {
    Serial.println(F("Soft check failed: SSL connection was not closed properly."));
  } else {
    Serial.println(F("SSL connection closed properly."));
  }
}
