#include "lcd_display.h"
#include <Arduino.h>

extern LiquidCrystal_I2C lcd;

void lcdPrettyPrint(String dataPrintout) {
    lcdPrettyPrint(dataPrintout, lcd, false, 1500, 50, 250);
}

void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd) {
    lcdPrettyPrint(dataPrintout, lcd, false, 1500, 50, 250);
}

void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay) {
    lcdPrettyPrint(dataPrintout, lcd, clearDisplay, 1500, 50, 250);
}

void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay, int delayBeforeClear) {
    lcdPrettyPrint(dataPrintout, lcd, clearDisplay, delayBeforeClear, 50, 250);
}

void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay, int delayBeforeClear, int delayTime) {
    lcdPrettyPrint(dataPrintout, lcd, clearDisplay, delayBeforeClear, delayTime, 250);
}

/**
 * Prints a string on an LCD display with optional effects and delay times.
 * 
 * @param dataPrintout the string to be printed on the LCD display
 * @param lcd the LiquidCrystal_I2C object representing the LCD display to be used
 * @param clearDisplay whether to clear the LCD display after the message has been printed out (default: false)
 * @param delayBeforeClear the delay time in milliseconds before the LCD display is cleared (default: 1500ms)
 * @param delayTimeBetweenChar the delay time in milliseconds between each character being printed out (default: 50ms)
 * @param delayTimeEndChar the delay time in milliseconds between the last few characters being printed out (default: 250ms)
 */
void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay, int delayBeforeClear, int delayTimeBetweenChar, int delayTimeEndChar) {
    unsigned long previousMillis = 0;
    int currentIndex = 0;
    int stringLength = dataPrintout.length();

    // Iterate over each character in the string and print it out on the LCD display with a delay between characters
    while (currentIndex < stringLength) {
        unsigned long currentMillis = millis();

        if (currentMillis - previousMillis >= delayTimeBetweenChar) {
            lcd.print(dataPrintout.charAt(currentIndex));
            previousMillis = currentMillis;
            currentIndex++;

            // Add a longer delay for the last few characters if the message ends with "..." or "!"
            if (dataPrintout.endsWith("...")) {
                if (currentIndex >= stringLength - 4) {
                    delayTimeBetweenChar = delayTimeEndChar;
                }
            }
            else if (dataPrintout.endsWith("!")) {
                if (currentIndex >= stringLength - 1) {
                    delayTimeBetweenChar = delayTimeEndChar;
                }
            }
            else {
                if (currentIndex >= stringLength - 3) {
                    delayTimeBetweenChar = delayTimeEndChar;
                }
            }
        }
    }

    // Clear the LCD display if the clearDisplay flag is set to true and delay before clearing the display
    if (clearDisplay == true) {
        unsigned long clearMillis = millis();
        while (millis() - clearMillis < delayBeforeClear) {
            // Do nothing for the specified delay time
        }
        lcd.clear();
    }
}