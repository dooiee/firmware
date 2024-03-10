#ifndef ON_BOARD_LED_H
#define ON_BOARD_LED_H

#include "config_codes.h"
#include <WiFiNINA.h>
#include <utility/wifi_drv.h>

// Define the RGB LED pin numbers
#define LED_RED    25
#define LED_GREEN  26
#define LED_BLUE   27

// Define the LED intensity range
#define LED_INTENSITY_MIN 0
#define LED_INTENSITY_DIM 16
#define LED_INTENSITY_LOW 64
#define LED_INTENSITY_MEDIUM 128
#define LED_INTENSITY_HIGH 192
#define LED_INTENSITY_BRIGHT 224
#define LED_INTENSITY_MAX 255

// Function to set the RGB color and brightness of the LED
void setOnBoardLEDColor(int red, int green, int blue, int intensity);
void fadeOnBoardLedColor(int red, int green, int blue, int intensity, int fadeDuration);
void setLedColorForCode(const int* colorCode, int intensity);
void fadeOnBoardLedColorForCode(const int* colorCode, int intensity, int fadeDuration);

#endif // ON_BOARD_LED_H