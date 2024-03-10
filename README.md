## Building / Flashing the Code on Individual MCUs

Follow these steps to run the code on each individual MCU:

### Configuring the `arduino.json` File

1. Open the `arduino.json` file in your project directory.
2. Locate the section for the specific MCU you want to run the code on.
3. Set the necessary values in the `arduino.json` file according to the requirements of your MCU. This may include specifying the board type, baud rate, and other configuration options.
4. Save the `arduino.json` file.

### Choosing the Correct Board and Configuration

1. Open the `c_cpp_properties.json` file in your project directory.
2. Look for the configuration that corresponds to the board you want to flash the code onto.
3. Ensure that the necessary compiler and linker settings are correctly configured for your MCU.

> **Note**:
> This may be able to be done by clicking on the "Show Board Config" option on the bottom bar of the VSCode window.

### Flashing the Code onto the MCU

1. Open the Arduino IDE (VSCode Arduino Extension has been experiencing some flashing issues).
2. Connect the MCU to your computer using a USB cable.
3. Select the correct board type from the "Tools" menu in the Arduino IDE.
4. Choose the correct serial port for your MCU from the "Tools" menu.
5. Click on the "Upload" button in the Arduino IDE to flash the code onto the MCU.
