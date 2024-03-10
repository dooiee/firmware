#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <LiquidCrystal_I2C.h>

extern LiquidCrystal_I2C lcd;

void lcdPrettyPrint(String dataPrintout);
void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd);
void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay);
void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay, int delayBeforeClear);
void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay, int delayBeforeClear, int delayTimeBetweenChar);
void lcdPrettyPrint(String dataPrintout, LiquidCrystal_I2C &lcd, bool clearDisplay, int delayBeforeClear, int delayTimeBetweenChar, int delayTimeEndChar);

#endif // LCD_DISPLAY_H