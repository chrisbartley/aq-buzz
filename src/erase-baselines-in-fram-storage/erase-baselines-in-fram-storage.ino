/***********************************************************************************************************************
 erase-baselines-in-fram-storage

 Chris Bartley <chris@chrisbartley.com>
 2020-09-02
***********************************************************************************************************************/

#include <Wire.h>
#include "Adafruit_FRAM_I2C.h"

//----------------------------------------------------------------------------------------------------------------------
// This stuff should go into a .h file, but I don't have time to grok Arduino's crazy build process to figure out a
// clean, quick, and proper way to do that with an .ino file.  So, for now, there's this ugly duplication...
//
// We don't want to read baselines from storage if the baselines have never been persisted. We'll store the value
// PERSISTENCE_DATA_EXISTS_FLAG at the PERSISTENCE_DATA_EXISTS_FLAG_BYTE_INDEX to flag that data has been persisted.
// This is my hacky way of doing a "file" existence check.
const uint8_t PERSISTENCE_DATA_EXISTS_FLAG = 42;
const unsigned int PERSISTENCE_DATA_EXISTS_FLAG_BYTE_INDEX = 0;

// Byte index of where persisted data starts
const unsigned int PERSISTENCE_START_BYTE_INDEX = 1;

typedef struct {
   bool isValid;
   time_t unixTimeSecs;
   uint16_t eco2;
   uint16_t tvoc;
} BASELINES;
//----------------------------------------------------------------------------------------------------------------------

Adafruit_FRAM_I2C fram = Adafruit_FRAM_I2C();      // FRAM storage

void setup() {
   initSerial();

   Serial.println("===============================");
   Serial.println("Erase Baselines in FRAM Storage");
   Serial.println("===============================\n");

   // initialize the storage
   Serial.println("Checking for FRAM storage...");
   if (fram.begin()) {
      Serial.println("FRAM storage found!");

      // create an all-zero byte array the size of the BASELINES struct
      char data[sizeof(BASELINES)] = {};

      // write data to FRAM
      for (uint16_t i = 0; i < sizeof(BASELINES); i++) {
         fram.write8(PERSISTENCE_START_BYTE_INDEX + i, data[i]);
      }

      // clear the flag in FRAM signifying that baselines have been persisted
      fram.write8(PERSISTENCE_DATA_EXISTS_FLAG_BYTE_INDEX, 0);

      Serial.println("Baselines erased successfully!");
   } else {
      Serial.println("FRAM storage not found, or could not be started!");
   }
}

void loop() {
}

//======================================================================================================================
// Serial
//======================================================================================================================

void initSerial() {
   Serial.begin(115200);

   // Check every 100ms for serial to be ready, timing out after 3 seconds
   int serialWaitCount = 0;
   while (!Serial && serialWaitCount < 30) {
      serialWaitCount++;
      delay(100);
   }
}