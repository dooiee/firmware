#ifndef CONFIG_CODES_H
#define CONFIG_CODES_H

// Define my error codes that will be used to perform error handling
// as well as change the color of the on-board LED for visual feedback
// define my on-board LED colors
#define COLOR_RED {255, 0, 0}
#define COLOR_ORANGE {255, 200, 0}
#define COLOR_YELLOW {255, 255, 0}
#define COLOR_GREEN {0, 255, 0}
#define COLOR_CYAN {0, 255, 255}
#define COLOR_BLUE {0, 0, 255}
#define COLOR_PURPLE {255, 0, 255}
#define COLOR_WHITE {255, 255, 255}
#define COLOR_OFF {0, 0, 0}

// for testing purposes
const int RED[] = {255, 0, 0};
const int ORANGE[] = {255, 200, 0};
const int YELLOW[] = {255, 255, 0};
const int GREEN[] = {0, 255, 0};
const int CYAN[] = {0, 255, 255};
const int BLUE[] = {0, 0, 255};
const int PURPLE[] = {255, 0, 255};
const int WHITE[] = {255, 255, 255};
const int OFF[] = {0, 0, 0};

// also add a few more error codes for the on-board LED
const int WIFI_DISCONNECTED[] = {255, 0, 0}; // red
const int WIFI_CONNECTING[] = {255, 200, 0}; // orange fade
const int WIFI_CONNECTED[] = {255, 200, 0}; // orange
const int WIFI_NOT_CONNECTED[] = {255, 0, 0}; // red

const int FIREBASE_NOT_CONNECTED[] = {255, 0, 0}; // red
const int FIREBASE_DISCONNECTED[] = {255, 0, 0}; // red
const int FIREBASE_CONNECTING[] = {255, 200, 0}; // orange fade
const int FIREBASE_CONNECTED[] = {255, 255, 0}; // yellow
const int FIREBASE_UPLOAD_SUCCESS[] = {0, 255, 0}; // green

const int WIFI_OR_FIREBASE_DISCONNECTED[] = {255, 0, 0}; // red
const int WIFI_OR_FIREBASE_CONNECTING[] = {255, 255, 0}; // yellow fade
const int WIFI_AND_FIREBASE_CONNECTED[] = {0, 255, 0}; // green
const int ETHERNET_AND_FIREBASE_CONNECTED[] = {0, 255, 0}; // green

const int ESP32_DISCONNECTED[] = {0, 255, 255}; // cyan
const int CONNECTING_TO_ESP32[] = {0, 0, 255}; // blue fade
const int CONNECTED_TO_ESP32[] = {255, 0, 255}; // purple

const int BLE_FAILED[] = {255, 0, 0}; // red
const int BLUETOOTH_CONNECTION_FAILED[] = {255, 0, 0}; // red
const int BLUETOOTH_DISCONNECTED[] = {255, 255, 0}; // yellow
const int BLUETOOTH_SERVICE_STARTED[] = {255, 255, 255}; // white
const int BLUETOOTH_SERVICE_STOPPED[] = {255, 255, 0}; // yellow
const int BLUETOOTH_CONNECTED[] = {0, 255, 255}; // cyan
const int BLUETOOTH_CONNECTION_MADE[] = {0, 0, 255}; // blue

const int ETHERNET_CONNECTED[] = COLOR_GREEN;

#endif // CONFIG_CODES_H