/***********************************************************************************************************************
 feather-express-aq

 Chris Bartley <chris@chrisbartley.com>
 2020-09-02

 Copyright (c) 2020 Chris Bartley. Licensed under the MIT license. See LICENSE file.
***********************************************************************************************************************/

#include <Adafruit_BME680.h>
#include <Adafruit_SGP30.h>
#include <Wire.h>
#include <RTClib.h>
#include <neosensory_bluefruit.h>
#include <CircularBuffer.h>
#include <LinearRegression.h>

//----------------------------------------------------------------------------------------------------------------------
// A lot of this stuff should go into a .h file, but I don't have time to grok Arduino's crazy build process to figure
// out a clean, quick, and proper way to do that with an .ino file.  So, for now, there's this ugly duplication...
//----------------------------------------------------------------------------------------------------------------------

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

typedef struct {
   double timeSecs;
   double value;
} DATA_SAMPLE;

const unsigned long DATA_SAMPLE_INTERVAL_MILLIS = 500;     // take a sample every half second
const unsigned int SECONDS_IN_ONE_HOUR = 60 * 60;
const unsigned int SECONDS_IN_TWELVE_HOURS = 12 * SECONDS_IN_ONE_HOUR;
const unsigned int SECONDS_IN_ONE_DAY = 24 * SECONDS_IN_ONE_HOUR;
const unsigned int SECONDS_IN_ONE_WEEK = 7 * SECONDS_IN_ONE_DAY;

const bool BASELINES_DEBUG_MODE = false;
const bool BUZZ_DEBUG_MODE = false;
const float MAX_TVOC_PPB = 200.0;

const unsigned int NUM_SAMPLES_IN_RUNNING_AVERAGE = 10;
const unsigned int NUM_SLOPE_VIBRATION_THRESHOLDS = 7;

Adafruit_BME680 bme680;                            // temperature and humidity
Adafruit_SGP30 sgp30;                              // tVOC and eCO2
RTC_PCF8523 rtc;                                   // real-time clock

NeosensoryBluefruit NeoBluefruit;

bool isOk = true;
bool hasRtc = false;
bool hasStorage = false;
bool isBaseliningEnabled = false;
BASELINES baselines;

CircularBuffer <DATA_SAMPLE, NUM_SAMPLES_IN_RUNNING_AVERAGE> dataSamples;
LinearRegression linearRegression = LinearRegression();
double slopeAndIntercept[2];
float slopeVibrationPatternThresholds[NUM_SLOPE_VIBRATION_THRESHOLDS];

unsigned long previousMillis = 0;   // used for calculating when to take the next data sample

void setup() {
   initSerial();

   Serial.println("==================");
   Serial.println("Feather Express AQ");
   Serial.println("==================\n");

   // initialize the real time clock
   initRtc();

   // initialize the storage
   initStorage();

   // initialize sensors
   initBme680();

   // we can only do baselining if we have an RTC and somewhere to write/read the baselines to/from.
   isBaseliningEnabled = hasRtc && hasStorage;
   Serial.printf("SGP30 baselining enabled = %d\n", isBaseliningEnabled);

   // Initialize the SGP30 and then try to restore its baselines from storage
   initSgp30();
   restoreBaselines();

   // compute thresholds for slope vibration patterns
   float radiansIncrement = (HALF_PI / (NUM_SLOPE_VIBRATION_THRESHOLDS + 1));
   for (int i = 0; i < NUM_SLOPE_VIBRATION_THRESHOLDS; i++) {
      slopeVibrationPatternThresholds[i] = tan(radiansIncrement * (i + 1));
   }

   // Setup BLE communication with the Buzz
   NeoBluefruit.begin();
   NeoBluefruit.setConnectedCallback(onConnectedToBuzz);
   NeoBluefruit.setDisconnectedCallback(onDisconnectedFromBuzz);
   NeoBluefruit.setReadNotifyCallback(onReadFromBuzz);
   NeoBluefruit.startScan();
}

void loop() {
   if (isOk) {
      // Get the current "time" in millis.  Note that the docs say that millis() will wrap after ~50 days, so elapsed
      // millis *can* be negative, but I'm OK dropping a sample once every 50 days :-)
      unsigned long currentMillis = millis();

      // see whether it's time to take a sample
      if (currentMillis - previousMillis >= DATA_SAMPLE_INTERVAL_MILLIS) {
         previousMillis = currentMillis;

         if (bme680.performReading()) {
            float humidity = bme680.humidity;
            float temperature = bme680.temperature;

            // do humidity compensation before reading the SGP30
            sgp30.setHumidity(getAbsoluteHumidity(temperature, humidity));

            // attempt to read the SGP30
            if (sgp30.IAQmeasure()) {
               // refresh SGP30 baselines
               refreshBaselines();

               // add this new sample to the circular buffer
               dataSamples.push(DATA_SAMPLE{(double) currentMillis / 1000.0, (double) (sgp30.TVOC)});

               // reset the linear regression
               linearRegression.reset();

               // Calculate the moving average and update the linear regression.  I know the averaging can be done more
               // efficiently, but we need to iterate over all the items anyway to compute the linear regression, so this
               // is fine, whatever.
               unsigned long sum = 0;
               using index_t = decltype(dataSamples)::index_t; // ensures we're using the right type for the index variable
               for (index_t i = 0; i < dataSamples.size(); i++) {
                  sum += dataSamples[i].value;
                  linearRegression.learn(dataSamples[i].timeSecs, dataSamples[i].value);
               }

               // compute the average
               float avgVoc = sum / (float) dataSamples.size();

               // get the slope and Y intercept from the linear regression
               linearRegression.parameters(slopeAndIntercept);

               // print in a format that makes the Serial Plotter happy (see
               // https://diyrobocars.com/2020/05/04/arduino-serial-plotter-the-missing-manual/)
               Serial.print("VOC:");
               Serial.print(sgp30.TVOC);
               Serial.print(",");
               Serial.print("Avg:");
               Serial.print(avgVoc);
               Serial.print(",");
               Serial.print("Slope:");
               Serial.printf("%5.5f", slopeAndIntercept[0]);
               Serial.println();

               // vibrate the buzz, if connected
               if (NeoBluefruit.isConnected() & NeoBluefruit.isAuthorized()) {
                  setVibration(avgVoc, slopeAndIntercept[0]);
               }
            } else {
               Serial.println("Failed to read SGP30");
               delay(250);
            }
         } else {
            Serial.println("Failed to read BME680");
            delay(250);
         }
      }
   } else {
      Serial.println("One or more sensors could not be initialized.");
      delay(1000);
   }
}

//======================================================================================================================
// Buzz
//======================================================================================================================
// Nothing super exciting here...vibrations "lean" to one side or the other (or middle) depending on slope.  I fiddled
// a few various ramping up ideas, but I thought the pulsing was annoying and not super helpful.
void setVibration(float tvoc, float slope) {
   float intensity = (min(tvoc, MAX_TVOC_PPB) / MAX_TVOC_PPB);
   float intensities[NeoBluefruit.num_motors()];
   if (slope > 0.414) {
      intensities[0] = 0;
      intensities[1] = 0;
      intensities[2] = intensity / 2;
      intensities[3] = intensity;
   } else if (slope < -0.414) {
      intensities[0] = intensity;
      intensities[1] = intensity / 2;
      intensities[2] = 0;
      intensities[3] = 0;
   } else {
      intensities[0] = 0;
      intensities[1] = intensity;
      intensities[2] = intensity;
      intensities[3] = 0;
   }
   NeoBluefruit.vibrateMotors(intensities);
}

void onConnectedToBuzz(bool success) {
   if (!success) {
      Serial.println("Attempted connection to Buzz but failed.");
      return;
   }
   Serial.println("Connected to Buzz!");
   NeoBluefruit.authorizeDeveloper();
   NeoBluefruit.acceptTermsAndConditions();
   NeoBluefruit.stopAlgorithm();
}

void onDisconnectedFromBuzz(uint16_t conn_handle, uint8_t reason) {
   Serial.println("\nDisconnected from Buzz.");
}

void onReadFromBuzz(BLEClientCharacteristic *chr, uint8_t *data, uint16_t len) {
   if (!BUZZ_DEBUG_MODE) return;
   for (int i = 0; i < len; i++) {
      Serial.write(data[i]);
   }
}

//======================================================================================================================
// BME680 (Temperature and humidity)
//======================================================================================================================
void initBme680() {
   if (!bme680.begin()) {
      Serial.println("BME680 sensor not found, or could not be started!");
      isOk = false;
      return;
   }

   // Set up oversampling and filter initialization (taken from Adafruit example)
   bme680.setTemperatureOversampling(BME680_OS_8X);
   bme680.setHumidityOversampling(BME680_OS_2X);
   // bme680.setPressureOversampling(BME680_OS_4X);
   bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
   // bme680.setGasHeater(320, 150); // 320*C for 150 ms

   Serial.println("BME680 ready!");
}

//======================================================================================================================
// SGP30 (tVOC/eCO2)
//======================================================================================================================
void initSgp30() {
   if (!sgp30.begin()) {
      Serial.println("SGP30 sensor not found, or could not be started!");
      isOk = false;
      return;
   }

   Serial.println("SGP30 ready!");
}

bool persistBaselines() {
   if (isBaseliningEnabled) {

      Serial.println("Reading baselines from sensor...");
      uint16_t eCO2Baseline, tVOCBaseline;
      if (sgp30.getIAQBaseline(&eCO2Baseline, &tVOCBaseline)) {
         Serial.printf("Baselines successfully read from sensor, now persisting them (eco2=%d, tvoc=%d)...\n",
                       eCO2Baseline, tVOCBaseline);

         baselines.isValid = true;
         baselines.unixTimeSecs = rtc.now().unixtime();
         baselines.eco2 = eCO2Baseline;
         baselines.tvoc = tVOCBaseline;

         writeBaselinesToStorage();
         return true;
      } else {
         Serial.println("Failed to read baselines from sensor!");
      }
   } else {
      Serial.println("Not persisting baselines since baselining is disabled.");
   }

   return false;
}

void refreshBaselines() {
   if (isBaseliningEnabled) {
      unsigned long elapsedSecondsSinceLastPersistence = rtc.now().unixtime() - baselines.unixTimeSecs;
      if (BASELINES_DEBUG_MODE) {
         Serial.printf("Refreshing baselines (seconds since last persistence=%d)\n",
                       elapsedSecondsSinceLastPersistence);
      }
      if (baselines.isValid) {
         // After the baselines are valid, then update the stored values hourly (as recommended by Sensirion)
         if (elapsedSecondsSinceLastPersistence >= SECONDS_IN_ONE_HOUR) {
            if (persistBaselines()) {
               Serial.println("Baselines valid, and older than 1 hour, so I persisted them.");
            } else {
               Serial.println("Baselines valid, and older than 1 hour, but persistence to storage failed.");
            }
         } else {
            if (BASELINES_DEBUG_MODE) {
               Serial.println("Baselines valid, but younger than 1 hour, so nothing to do.");
            }
         }
      } else {
         if (BASELINES_DEBUG_MODE) {
            Serial.println("Baselines not valid, so checking whether it has been more than 12 hours");
         }
         // Restarting the sensor without restoring a baseline will result in the sensor trying to determine a new
         // baseline. The baseline calibration algorithm will be performed on the SGP30 for 12hrs.
         if (elapsedSecondsSinceLastPersistence >= SECONDS_IN_TWELVE_HOURS) {
            if (persistBaselines()) {
               Serial.println("Baseline calibration complete, baselines persisted!");
            } else {
               Serial.println("Baseline calibration complete, but persistence to storage failed.");
            }
         } else {
            if (BASELINES_DEBUG_MODE) {
               Serial.println("Baselines calibration still pending, so nothing to do.");
            }
         }
      }
   } else {
      if (BASELINES_DEBUG_MODE) {
         Serial.println("Not refreshing baselines since baselining is disabled.");
      }
   }
}

void restoreBaselines() {
   if (isBaseliningEnabled) {
      readBaselinesFromStorage();
      bool isYoungerThanOneWeek = (rtc.now().unixtime() - baselines.unixTimeSecs) <= SECONDS_IN_ONE_WEEK;

      // see whether the baselines are valid and not too old
      if (baselines.isValid && isYoungerThanOneWeek) {
         // copy the baselines to the sensor
         if (sgp30.setIAQBaseline(baselines.eco2, baselines.tvoc)) {
            printBaselines("Restored baselines by reading from storage and setting in SGP30:");
         } else {
            Serial.println("Read baselines from storage, but failed to set in the SGP30.");
         }
      } else {
         // we're still in the 12-hour, early operation phase
         baselines.isValid = false;
         baselines.unixTimeSecs = rtc.now().unixtime();
         Serial.println("Baseline calibration pending.");
      }
   } else {
      Serial.println("Not restoring baselines since baselining is disabled.");
   }
}

// struct deserialization based on https://stackoverflow.com/a/13775983/703200
void readBaselinesFromStorage() {
   if (isBaseliningEnabled) {
      if (doBaselinesExist()) {
         // TODO
         Serial.println("Reading baselines from storage is not yet supported");
      } else {
         baselines.isValid = false;
         baselines.unixTimeSecs = rtc.now().unixtime();
         baselines.eco2 = 0;
         baselines.tvoc = 0;
         printBaselines("No baselines found in storage, using defaults:");
      }
   } else {
      Serial.println("Not reading baselines from storage since baselining is disabled.");
   }
}

// struct serialization based on https://stackoverflow.com/a/13775983/703200
void writeBaselinesToStorage() {
   if (isBaseliningEnabled) {
      // TODO

      printBaselines("Baselines written to storage:");
   } else {
      Serial.println("Not writing baselines to storage since baselining is disabled.");
   }
}

bool doBaselinesExist() {
   // TODO
   return false;
}

void printBaselines(String message) {
   if (message.length() > 0) {
      Serial.println(message);
   }
   Serial.printf("   isValid:      %d\n", baselines.isValid);
   Serial.printf("   unixTimeSecs: %d\n", baselines.unixTimeSecs);
   Serial.printf("   eco2:         %d\n", baselines.eco2);
   Serial.printf("   tvoc:         %d\n", baselines.tvoc);
}

//
// Return absolute humidity [mg/m^3] with approximation formula
// @param temperature [°C]
// @param humidity [%RH]
//
// Based on code from https://github.com/adafruit/Adafruit_SGP30/blob/master/examples/sgp30test/sgp30test.ino
//
uint32_t getAbsoluteHumidity(float temperature, float relativeHumidity) {
   // clamp to [0, 100]
   relativeHumidity = min(relativeHumidity, 100);
   relativeHumidity = max(relativeHumidity, 0);

   // Slightly simplified version of the formula from section 3.16 of Sensirion_Gas_Sensors_SGP30_Driver-Integration-Guide_SW_I2C.pdf
   const float numerator = 13.244704f * relativeHumidity * exp((17.62f * temperature) / (243.12f + temperature));
   const float denominator = 273.15f + temperature;
   const float absoluteHumidityGramsPerCubicMeter = numerator / denominator;

   // Note in docs explain that "the value in g/m^3 has to be multiplied by 1000 to convert to mg/m^3 and any remaining
   // decimal places have to be rounded and removed since the interface does not support floating point numbers."
   const uint32_t absoluteHumidityScaled = static_cast<uint32_t>(1000.0f * absoluteHumidityGramsPerCubicMeter);
   return absoluteHumidityScaled;
}

//======================================================================================================================
// Storage
//======================================================================================================================
void initStorage() {
   Serial.println("Storage not yet supported. :-(");
}

//======================================================================================================================
// RTC
//======================================================================================================================
void initRtc() {
   Serial.println("Initializing the RTC...");
   if (rtc.begin()) {
      // Initialization code taken from the Adafruit example
      Serial.println("Found RTC, checking state...");
      if (!rtc.initialized() || rtc.lostPower()) {
         Serial.println("RTC is NOT initialized, let's set the time!");
         // When time needs to be set on a new device, or after a power loss, the following line sets the RTC to the date
         // and time this sketch was compiled.  Not perfect, but close enough.
         rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
         // Note: allow 2 seconds after inserting battery or applying external power without battery before calling
         // adjust(). This gives the PCF8523's crystal oscillator time to stabilize. If you call adjust() very quickly
         // after the RTC is powered, lostPower() may still return true.
      } else {
         Serial.println("RTC is initialized!");
      }

      // When the RTC was stopped and stays connected to the battery, it has to be restarted by clearing the STOP bit.
      // Let's do this to ensure the RTC is running.
      rtc.start();

      hasRtc = true;
   } else {
      Serial.println("RTC not found, or could not be started!");

      // NOTE: we don't set isOk to false here, since we can still run without the RTC.  We'll lose out on SGP30
      // baselining, but that's not the end of the world.
   }
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