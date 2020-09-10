# Air Quality + Buzz

Arduino code and an iOS app for using the [Neosensory Buzz](https://neosensory.com/), enabling you to feel changes in the air quality around you.  

## A Note About "Air Quality"

The term *air quality* can mean a lot of different things.  If you look at the variety of things various commercial air quality monitors sense, you'll find particulates (PM), VOCs, CO2, CO, radon, etc.  Examples here are currently limited to VOCs (as detected by the [Sensirion SGP30](https://www.sensirion.com/kr/environmental-sensors/gas-sensors/sgp30/)) but there's no reason one couldn't change/extend it to include whatever pollutant(s) you're interested in.  For the home, particulate matter is probably the most obvious and useful addition.

## Hardware

For the ease of putting something together quickly, both hardware and software, I went with Adafruit Feathers and breakout boards for the Arduino devices. Highly recommended! Thanks to Adafruit's' excellent documentation and example code, I had a first working version in about two days.

I ended up building two similar, but slightly different Arduino Feather devices.  I now generally regard this as A Bad Move, because it ended up creating more work, code-wise.  But it was motivated mostly by what was in stock at the time, but also a little bit by pricing, what I already had on hand, and my curiosity.  One device is built around the [Adafruit Feather nRF52840 Sense](https://learn.adafruit.com/adafruit-feather-sense), while the other uses the [Adafruit Feather nRF52840 Express](https://learn.adafruit.com/introducing-the-adafruit-nrf52840-feather).

Both devices use the [Adafruit SGP30 breakout board](https://learn.adafruit.com/adafruit-sgp30-gas-tvoc-eco2-mox-sensor) for measuring total VOCs (tVOC).  The differences in the resulting devices come in the extras required--or at least recommended--to use the SGP30 properly.  The SGP30 does baseline calibration, and you can read out calibration values so that, upon power-cyle, you can initialize the sensor with the last known baseline values.  If you don't, then Sensirion recommends waiting at least 12 hours before you can trust the readings.  And you should also read and store the baseline values hourly after the initial 12-hour calibration.  *And* not trust any baseline values older than one week.  All of which imply that you have both some means of storing baseline values in non-volatile memory *and* some means of knowing what time it is now, and when you last wrote the values...so you need a real-time clock on board too.  To make things a little more complex, they also recommend you have a humidity sensor so you can take advantage of the SGP30's optional humidity compensation feature.

Details such as actual parts lists and assembly instructions are separate documents:

* [Feather Sense Assembly](https://chrisbartley.github.io/aq-buzz/docs/feather-sense-assembly.html)
* [Feather Express Assembly](https://chrisbartley.github.io/aq-buzz/docs/feather-express-assembly.html)

## Network Topologies

Examples here fall under two primary device networking/communication topologies, but include beginnings and implementation notes for a third.

All communication with the Buzz is done via Bluetooth Low Energy (BLE).  All three topologies share the common features of the Buzz being a BLE peripheral and one or more Arduino-based devices + sensors doing the environmental sensing.  The topologies differ in the device acting as the BLE central.  The three communication topologies presented here are:

1. A single Arduino device acting as the BLE central, communicating directly with the Buzz.
2. One or more Arduino devices as BLE peripherals, with an iOS app as BLE central to both the Arduino(s) and the Buzz.
3. One or more Arduino devices as connection-less, broadcast-only BLE peripherals, with the iOS app as BLE central.

Following are brief discussions of each of the above three network topologies, along with pointers to code in this repository.

### Arduino as BLE Central

In this scenario, a single Arduino device is the BLE central, communicating directly with the Buzz.  An example use case could be wearing the Arduino (e.g. battery-powered, and on a wrist strap) and having it continuosly monitor VOCs in your immediate vicinity, which you feel with the Buzz.

Relevant Arduino code is under `arduino/central`.

### iOS App as Central + Arduino(s) as Connected Peripheral(s)

This scenario consists of one or more Arduino devices as BLE peripherals, with an iOS app as BLE central.  The iOS app connects to all the Arduino peripherals it finds, as well as to the Buzz, and acts as a proxy between sensors and the Buzz.  The benefits of this model are:

1. No need to wear another "thing", assuming you're already carrying your phone around with you.
2. Multiple sensors--for example, several distributed around your house--enable the phone to either aggregate sensor data so you can feel (literally!) the general, overall air quality in the entire house *or* for the phone to automatically detect which Arduino you're closest to and report only those sensor's readings to your Buzz.  The iOS app included here, AQ Buzz, takes the latter approach.  I'll leave the aggregation model as an exercise for the reader :-)
3. Since the phone has internet access, one could easily extend the iOS app to upload sensor data to some sort of online data repository for later processing/viewing.
4. The phone could also pull in data from other sensors, either local or in the cloud, and incorporate into the vibration commands sent to the Buzz.
    
For this implementation, I chose for the Arduino peripherals to publish data samples via a single BLE notify characteristic.  The central subscribes to notifications, and the peripheral publishes them on a periodic basis (here, 1 Hz).

Relevant Arduino code is under `arduino/peripheral/feather-express-aq-notify` and `arduino/peripheral/feather-sense-aq-notify`.  Relevant iOS app is under `ios/AQ Buzz`.

### iOS App as Central + Arduino(s) as Connectionless, Broadcast-Only Peripheral(s)

This scenario consists of one or more Arduino devices as connection-less, broadcast-only BLE peripherals, with the iOS app as BLE central.  I briefly started down this path before learning that, when an iOS app is the background and scanning for peripheral advertisements, ["multiple discoveries of an advertising peripheral are coalesced into a single discovery event"](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html#//apple_ref/doc/uid/TP40013257-CH7-SW6). Thus a peripheral broadcasting data samples within its BLE advertisement would essentially be ignored by a central which is running in the background on iOS.  So, no go unless you want to always keep the app in the foreground...not exactly ideal.  One could imagine a scenario where some other device is acting as the central and aggregating samples from broadcasting peripherals for delivery to the Buzz, e.g. a Raspberry Pi or an Arduino (even one of the sensing Arduinos), but then you're either back to wearing another thing *or* you lose the proximity awareness gained by carrying the central around with you (e.g. an app on your phone).  

Advertising payloads are also more limited in size than a BLE characteristic, so it's much harder to include enough useful information in an advertisement.

Finally, for home use, the security/privacy implications of broadcast-only peripherals are another strike against.  It's pretty easy to get a sense of whether someone is home, or at least to find trends when no one is home, by looking at a couple weeks of air quality data.  Broadcast-only peripherals provide no option to control who's "listening", in contrast to peripherals requiring a connection.  So it's not inconceivable that a Malicious Person Intent on Robbing You could set up a device within BLE range of your broadcast-only sensors and pretty quickly learn the times when you're typically not home.  The peripheral code referenced in the previous section doesn't include any sort of authentication/authorization, but it's at least an option and not too hard to implement.

Relevant Arduino code is under `arduino/peripheral/feather-sense-aq-broadcast`, but I scrapped the iOS app for receiving the broadcasts because it seemed a little pointless.  
