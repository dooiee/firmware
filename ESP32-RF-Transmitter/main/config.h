#ifndef CONFIG_H
#define CONFIG_H

///// Below is all transmitted codes to control the underwater LEDs (Decimal and Binary).
const int rfDecimalCodeBrightness = 334849;
const int rfDecimalCodeSleepTimer = 334850;
const int rfDecimalCodePower = 334851;
const int rfDecimalCode4H = 334852;
const int rfDecimalCode8H = 334853;
const int rfDecimalCode12H = 334854;
const int rfDecimalCodeFlash = 334855;
const int rfDecimalCodeWhite = 334856;
const int rfDecimalCodeFade = 334857;
const int rfDecimalCodeRed = 334858;
const int rfDecimalCodeGreen = 334859;
const int rfDecimalCodeBlue = 334860;
const int rfDecimalCodeOrange = 334861;
const int rfDecimalCodeSeaGreen = 334862;
const int rfDecimalCodeTeal = 334863;
const int rfDecimalCodeOrangeYellow = 334864;
const int rfDecimalCodeCyan = 334865;
const int rfDecimalCodeIndigo = 334866;
const int rfDecimalCodeYellow = 334867;
const int rfDecimalCodeAzure = 334868;
const int rfDecimalCodeMagenta = 334869;

const char rfBinaryCodeBrightness[] = "000001010001110000000001";
const char rfBinaryCodeSleepTimer[] = "000001010001110000000010";
const char rfBinaryCodePower[] = "000001010001110000000011";
const char rfBinaryCode4H[] = "000001010001110000000100";
const char rfBinaryCode8H[] = "000001010001110000000101";
const char rfBinaryCode12H[] = "000001010001110000000110";
const char rfBinaryCodeFlash[] = "000001010001110000000111";
const char rfBinaryCodeWhite[] = "000001010001110000001000";
const char rfBinaryCodeFade[] = "000001010001110000001001";
const char rfBinaryCodeRed[] = "000001010001110000001010";
const char rfBinaryCodeGreen[] = "000001010001110000001011";
const char rfBinaryCodeBlue[] = "000001010001110000001100";
const char rfBinaryCodeOrange[] = "000001010001110000001101";
const char rfBinaryCodeSeaGreen[] = "000001010001110000001110";
const char rfBinaryCodeTeal[] = "000001010001110000001111";
const char rfBinaryCodeOrangeYellow[] = "000001010001110000010000";
const char rfBinaryCodeCyan[] = "000001010001110000010001";
const char rfBinaryCodeIndigo[] = "000001010001110000010010";
const char rfBinaryCodeYellow[] = "000001010001110000010011";
const char rfBinaryCodeAzure[] = "000001010001110000010100";
const char rfBinaryCodeMagenta[] = "000001010001110000010101";

#endif // CONFIG_H