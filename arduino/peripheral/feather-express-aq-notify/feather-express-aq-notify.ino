/***********************************************************************************************************************
 feather-express-aq-notify

 Chris Bartley <chris@chrisbartley.com>
 2020-09-02

 Copyright (c) 2020 Chris Bartley. Licensed under the MIT license. See LICENSE file.
***********************************************************************************************************************/

#include <Adafruit_BME680.h>
#include <Adafruit_SGP30.h>
#include <Wire.h>
#include <RTClib.h>
#include <bluefruit.h>
#include <CircularBuffer.h>
#include <LinearRegression.h>

//----------------------------------------------------------------------------------------------------------------------
// BLE stuff
//----------------------------------------------------------------------------------------------------------------------
#define NO_ADVERTISING_TIMEOUT   0 // seconds

// Service UUID used to differentiate this device. Note that the byte order is reversed.
// The UUID below corresponds to 42610001-7274-6c65-7946-656174686572
const uint8_t SERVICE_UUID[] =
      {
            0x72, 0x65, 0x68, 0x74, 0x61, 0x65, 0x46, 0x79,
            0x65, 0x6c, 0x74, 0x72, 0x01, 0x00, 0x61, 0x42
      };

// Characteristic UUID (42610002-7274-6c65-7946-656174686572)
const uint8_t CHARACTERISTIC_UUID[] =
      {
            0x72, 0x65, 0x68, 0x74, 0x61, 0x65, 0x46, 0x79,
            0x65, 0x6c, 0x74, 0x72, 0x02, 0x00, 0x61, 0x42
      };

const char ADVERTISING_NAME_PREFIX[] = "Feather";

BLEService service = BLEService(BLEUuid(SERVICE_UUID));
BLECharacteristic characteristic = BLECharacteristic(BLEUuid(CHARACTERISTIC_UUID));

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

const unsigned long DATA_SAMPLE_INTERVAL_MILLIS = 1000;     // take a sample every second
const unsigned int SECONDS_IN_ONE_HOUR = 60 * 60;
const unsigned int SECONDS_IN_TWELVE_HOURS = 12 * SECONDS_IN_ONE_HOUR;
const unsigned int SECONDS_IN_ONE_DAY = 24 * SECONDS_IN_ONE_HOUR;
const unsigned int SECONDS_IN_ONE_WEEK = 7 * SECONDS_IN_ONE_DAY;

const bool BASELINES_DEBUG_MODE = false;
const bool BUZZ_DEBUG_MODE = false;
const float MAX_TVOC_PPB = 200.0;

const unsigned int NUM_SAMPLES_IN_RUNNING_AVERAGE = 10;

Adafruit_BME680 bme680;                            // temperature and humidity
Adafruit_SGP30 sgp30;                              // tVOC and eCO2
RTC_PCF8523 rtc;                                   // real-time clock

bool isOk = true;
bool hasRtc = false;
bool hasStorage = false;
bool isBaseliningEnabled = false;
BASELINES baselines;

CircularBuffer <DATA_SAMPLE, NUM_SAMPLES_IN_RUNNING_AVERAGE> dataSamples;
LinearRegression linearRegression = LinearRegression();
double slopeAndIntercept[2];

unsigned long previousMillis = 0;   // used for calculating when to take the next data sample

typedef struct {
   time_t unixTimeSecs;
   uint16_t voc;
   float avgVoc;
   float slope;
} NOTIFICATION_PAYLOAD;

NOTIFICATION_PAYLOAD notificationPayload;
const unsigned int NOTIFICATION_PAYLOAD_SIZE = sizeof(time_t) +
                                               sizeof(uint16_t) +
                                               sizeof(float) +
                                               sizeof(float);

// union makes for an easy way to get a type as bytes (see https://stackoverflow.com/a/24420279/703200)
union TimeByteArray {
   time_t val;
   uint8_t bytes[sizeof(time_t)];
};

union Uint16ByteArray {
   uint16_t val;
   uint8_t bytes[sizeof(uint16_t)];
};

union FloatByteArray {
   float val;
   uint8_t bytes[sizeof(float)];
};

void setup() {
   initSerial();

   Serial.println("=========================");
   Serial.println("Feather Express AQ Notify");
   Serial.println("=========================\n");

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

   // Setup BLE broadcasting
   Bluefruit.begin();
   Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values

   // read MAC address and construct the advertising name
   char name[sizeof(ADVERTISING_NAME_PREFIX) + 12 + 1];
   uint8_t mac[6];
   Bluefruit.getAddr(mac);
   char macStr[12 + 1];
   sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
   strcpy(name, ADVERTISING_NAME_PREFIX);
   strcat(name, macStr);
   Serial.printf("Setting advertising name to [%s]\n", name);
   Bluefruit.setName(name);

   // Set the connect/disconnect callback handlers
   Bluefruit.Periph.setConnectCallback(connectCallback);
   Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

   // setup BLE service and characteristic
   setupServiceAndCharacteristic();

   // Start advertising
   startAdvertising();
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

               // get the slope and Y intercept from the linear regression
               linearRegression.parameters(slopeAndIntercept);

               // fill the notification payload
               notificationPayload.unixTimeSecs = hasRtc ? rtc.now().unixtime() : 0;
               notificationPayload.voc = sgp30.TVOC;
               notificationPayload.avgVoc = sum / (float) dataSamples.size();
               notificationPayload.slope = (float) slopeAndIntercept[0];

               // print in a format that makes the Serial Plotter happy (see
               // https://diyrobocars.com/2020/05/04/arduino-serial-plotter-the-missing-manual/)
               Serial.print("VOC:");
               Serial.print(notificationPayload.voc);
               Serial.print(",");
               Serial.print("AvgVOC:");
               Serial.print(notificationPayload.avgVoc);
               Serial.print(",");
               Serial.print("Slope:");
               Serial.printf("%5.5f", notificationPayload.slope);
               Serial.println();

               if (Bluefruit.connected()) {
                  sendNotification();
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
// BLE
//======================================================================================================================
void setupServiceAndCharacteristic() {
   // Note: You must call .begin() on the BLEService before calling .begin() on
   // any characteristic(s) within that service definition.. Calling .begin() on
   // a BLECharacteristic will cause it to be added to the last BLEService that
   // was 'begin()'ed!
   service.begin();

   // Configure the characteristic
   characteristic.setProperties(CHR_PROPS_NOTIFY);                // we want this to be a notify characteristic
   characteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS); // set security

   // how many bytes the characteristic contains (normally 1-20)
   characteristic.setFixedLen(NOTIFICATION_PAYLOAD_SIZE);

   // Capture CCCD updates (just for debugging, we don't do anything with them)
   characteristic.setCccdWriteCallback(cccdCallback);

   characteristic.begin();

   // put initial values into the characteristic
   uint8_t data[NOTIFICATION_PAYLOAD_SIZE] = {};

   // use write here, for the initial setup, but will use notify in the loop
   characteristic.write(data, NOTIFICATION_PAYLOAD_SIZE);
}

void startAdvertising() {
   Bluefruit.Advertising.clearData();
   Bluefruit.ScanResponse.clearData();

   // Advertising packet
   Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
   Bluefruit.Advertising.addTxPower();
   Bluefruit.Advertising.addService(service);

   // Not enough room in the advertising packet for name so store it in the Scan Response instead
   Bluefruit.ScanResponse.addName();

   Bluefruit.Advertising.setStopCallback(advertisingTimeoutCallback);
   Bluefruit.Advertising.restartOnDisconnect(true);
   Bluefruit.Advertising.setInterval(100, 400);    // in units of 0.625 ms
   Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode

   // Connectable and scannable undirected advertising events
   Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);

   Bluefruit.Advertising.start(NO_ADVERTISING_TIMEOUT);   // advertising doesn't timeout if this is 0
}

void sendNotification() {
   union TimeByteArray unixTimeSecs;
   union Uint16ByteArray voc;
   union FloatByteArray avgVoc;
   union FloatByteArray slope;

   unixTimeSecs.val = notificationPayload.unixTimeSecs;
   voc.val = notificationPayload.voc;
   avgVoc.val = notificationPayload.avgVoc;
   slope.val = notificationPayload.slope;

   if (Bluefruit.connected()) {
      uint8_t data[NOTIFICATION_PAYLOAD_SIZE] = {0};

      // Copy the payload values to the data array
      int destIndex = 0;
      for (int i = 0; i < sizeof(unixTimeSecs.bytes); i++) {
         data[destIndex] = unixTimeSecs.bytes[i];
         destIndex++;
      }
      for (int i = 0; i < sizeof(voc.bytes); i++) {
         data[destIndex] = voc.bytes[i];
         destIndex++;
      }
      for (int i = 0; i < sizeof(avgVoc.bytes); i++) {
         data[destIndex] = avgVoc.bytes[i];
         destIndex++;
      }
      for (int i = 0; i < sizeof(slope.bytes); i++) {
         data[destIndex] = slope.bytes[i];
         destIndex++;
      }

      // If it is connected but CCCD is not enabled, the characteristic's value is still updated although notification
      // is not sent
      if (!characteristic.notify(data, sizeof(data))) {
         Serial.println("ERROR: Notify not set in the CCCD or not connected!");
      }
   }
}

void cccdCallback(uint16_t conn_hdl, BLECharacteristic *chr, uint16_t cccd_value) {
   // Display the raw request packet
   Serial.print("CCCD Updated: ");
   //Serial.printBuffer(request->data, request->len);
   Serial.print(cccd_value);
   Serial.println("");

   // Check the characteristic this CCCD update is associated with in case
   // this handler is used for multiple CCCD records.
   if (chr->uuid == characteristic.uuid) {
      if (chr->notifyEnabled(conn_hdl)) {
         Serial.println("AQ measurement 'Notify' enabled");
      } else {
         Serial.println("AQ measurement 'Notify' disabled");
      }
   }
}

void connectCallback(uint16_t conn_handle) {
   // Get the reference to current connection
   BLEConnection *connection = Bluefruit.Connection(conn_handle);

   char central_name[32] = {0};
   connection->getPeerName(central_name, sizeof(central_name));

   Serial.print("Connected to ");
   Serial.println(central_name);
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
   (void) conn_handle;
   (void) reason;

   Serial.print("Disconnected, reason = 0x");
   Serial.println(reason, HEX);
   Serial.println("Advertising!");
}

// Callback invoked when advertising is stopped by timeout
void advertisingTimeoutCallback() {
   Serial.println("Advertising has timed out.");
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