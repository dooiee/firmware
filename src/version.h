#ifndef VERSION_H
#define VERSION_H

// this file has been symlinked to each main folder in the firmware directory to allow for easy versioning of the software
// ran the commands 'cd /path/to/{REPLACE-WITH-SPECIFIC-BOARD}/main' followed by `ln -s ../../src/version.h version.h` to create the symlinks

// Variable to store the release version of the software running on the MCUs
#define RELEASE_VERSION "2.0.5" 

#endif // VERSION_H