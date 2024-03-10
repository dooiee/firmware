#include "on_board_led.h"

/**
* Set the RGB color and brightness of the on-board LED of the MKR WiFi 1010
* @param red The red intensity of the LED (0-255)
* @param green The green intensity of the LED (0-255)
* @param blue The blue intensity of the LED (0-255)
* @param intensity The brightness of the LED (0-255)
*/
void setOnBoardLEDColor(int red, int green, int blue, int intensity) {
  // Ensure the input values are within the valid range
  red = constrain(red, LED_INTENSITY_MIN, LED_INTENSITY_MAX);
  green = constrain(green, LED_INTENSITY_MIN, LED_INTENSITY_MAX);
  blue = constrain(blue, LED_INTENSITY_MIN, LED_INTENSITY_MAX);
  intensity = constrain(intensity, LED_INTENSITY_MIN, LED_INTENSITY_MAX);

  // Set the LED color and brightness
  WiFiDrv::analogWrite(LED_RED, red * intensity / LED_INTENSITY_MAX);
  WiFiDrv::analogWrite(LED_GREEN, green * intensity / LED_INTENSITY_MAX);
  WiFiDrv::analogWrite(LED_BLUE, blue * intensity / LED_INTENSITY_MAX);
}

/**
* Fade the on-board LED of the MKR WiFi 1010 to show user an operation is in progress
* red, green, blue, and intensity params are same as setLedColor()
* @param red The red intensity of the LED (0-255)
* @param green The green intensity of the LED (0-255)
* @param blue The blue intensity of the LED (0-255)
* @param intensity The brightness of the LED (0-255)
* @param duration The duration of the fade in milliseconds
*/
void fadeOnBoardLedColor(int red, int green, int blue, int intensity, int fadeDuration) {
  // Ensure the input values are within the valid range
  red = constrain(red, LED_INTENSITY_MIN, LED_INTENSITY_MAX);
  green = constrain(green, LED_INTENSITY_MIN, LED_INTENSITY_MAX);
  blue = constrain(blue, LED_INTENSITY_MIN, LED_INTENSITY_MAX);
  intensity = constrain(intensity, LED_INTENSITY_MIN, LED_INTENSITY_MAX);

  // Determine the number of steps for the fade effect
  int numSteps = fadeDuration / 10;
  if (numSteps < 1) {
    numSteps = 1;
  }

  // Calculate the step size for each color component
  float stepR = ((float)(red - LED_INTENSITY_MIN) / numSteps);
  float stepG = ((float)(green - LED_INTENSITY_MIN) / numSteps);
  float stepB = ((float)(blue - LED_INTENSITY_MIN) / numSteps);
  float stepI = ((float)(intensity - LED_INTENSITY_MIN) / numSteps);

  // Fade the LED color in
  for (int i = 0; i < numSteps; i++) {
    setOnBoardLEDColor((int)(i * stepR), (int)(i * stepG), (int)(i * stepB), (int)(i * stepI));
    delay(10);
  }

  // Fade the LED color out
  for (int i = numSteps; i > 0; i--) {
    setOnBoardLEDColor((int)((i - 1) * stepR), (int)((i - 1) * stepG), (int)((i - 1) * stepB), (int)((i - 1) * stepI));
    delay(10);
  }

  // Set the final LED color and intensity
  setOnBoardLEDColor(red, green, blue, intensity);
}

/**
* Set the RGB color and brightness of the on-board LED of the MKR WiFi 1010 based on a color code
* set in the config.h file
* @param colorCode The color code to set the LED to (see config.h)
* @param intensity The brightness of the LED (0-255)
*/
void setLedColorForCode(const int* colorCode, int intensity) {
    intensity = constrain(intensity, LED_INTENSITY_MIN, LED_INTENSITY_MAX);

    WiFiDrv::analogWrite(LED_RED, colorCode[0] * intensity / LED_INTENSITY_MAX);
    WiFiDrv::analogWrite(LED_GREEN, colorCode[1] * intensity / LED_INTENSITY_MAX);
    WiFiDrv::analogWrite(LED_BLUE, colorCode[2] * intensity / LED_INTENSITY_MAX);
}

/**
* Fade the on-board LED of the MKR WiFi 1010 to show user an operation is in progress
* colorCode and intensity params are same as setLedColorForCode()
* @param colorCode The color code to set the LED to (see config.h)
* @param intensity The brightness of the LED (0-255)
* @param duration The duration of the fade in milliseconds
*/
void fadeOnBoardLedColorForCode(const int* colorCode, int intensity, int fadeDuration) {
    // Ensure the input values are within the valid range
    intensity = constrain(intensity, LED_INTENSITY_MIN, LED_INTENSITY_MAX);

    // Determine the number of steps for the fade effect
    int numSteps = fadeDuration / 10;
    if (numSteps < 1) {
        numSteps = 1;
    }

    // Calculate the step size for each color component
    float stepR = ((float)(colorCode[0] - LED_INTENSITY_MIN) / numSteps);
    float stepG = ((float)(colorCode[1] - LED_INTENSITY_MIN) / numSteps);
    float stepB = ((float)(colorCode[2] - LED_INTENSITY_MIN) / numSteps);
    float stepI = ((float)(intensity - LED_INTENSITY_MIN) / numSteps);

    // Fade the LED color in
    for (int i = 0; i < numSteps; i++) {
        setLedColorForCode(colorCode, (int)(i * stepI));
        delay(10);
    }

    // Fade the LED color out
    for (int i = numSteps; i > 0; i--) {
        setLedColorForCode(colorCode, (int)((i - 1) * stepI));
        delay(10);
    }

    // Set the final LED color and intensity
    setLedColorForCode(colorCode, intensity);
}
   