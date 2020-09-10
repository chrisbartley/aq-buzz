![Feather Sense Device](./images/sense/banner.jpg)

# Feather Sense Device Assembly

As described in the [README](../README.md), one of the two Arduino-based devices I created for this project is built around the [Adafruit Feather nRF52840 Sense](https://learn.adafruit.com/adafruit-feather-sense).  This document contains the parts list, details of the build process, and an overview of software installation.

## Parts List

Adafruit is [currently selling through Digi-Key](https://www.adafruit.com/buyfromdigikey), so I've included Digi-Key links, too. 

* [Adafruit Feather nRF52840 Sense](https://learn.adafruit.com/adafruit-feather-sense) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/4516/1528-4516-ND/11684829)]
* [Adafruit SGP30 TVOC/eCO2 Gas Sensor](https://learn.adafruit.com/adafruit-sgp30-gas-tvoc-eco2-mox-sensor) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/3709/1528-2531-ND/8258468)]
* [Adafruit I2C FRAM Breakout](https://learn.adafruit.com/adafruit-i2c-fram-breakout) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/1895/1528-1035-ND/4990784)]
* [Adafruit PCF8523 Real Time Clock](https://learn.adafruit.com/adafruit-pcf8523-real-time-clock) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/3295/1528-1787-ND/6238007)]
* CR1220 Coin Cell Battery [[Digi-Key](https://www.digikey.com/product-detail/en/panasonic-bsg/CR1220/P033-ND/269740)]
* [FeatherWing Proto](https://www.adafruit.com/product/2884) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/2884/1528-1622-ND/5777193)]
* [Short Feather Female Headers (2)](https://www.adafruit.com/product/2940) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/2940/1528-1581-ND/5848449)]
* [Short Feather Male Headers (2)](https://www.adafruit.com/product/3002) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/3002/1528-2039-ND/6827172)]
* Right Angle Female Headers [[Digi-Key](https://www.digikey.com/product-detail/en/chip-quik-inc/HDR100IMP40F-G-RA-TH/HDR100IMP40F-G-RA-TH-ND/5978223)]
* Right Angle Male Headers [[Digi-Key](https://www.digikey.com/product-detail/en/amphenol-icc-fci/68016-236HLF/609-2226-ND/1002541)]
* Breadboard (for help in holding headers in place while soldering)
* Breadboard wire
* Optional: [Lithium Ion Polymer Battery - 3.7V 400mAh](https://www.adafruit.com/product/3898) [[Digi-Key](https://www.digikey.com/product-detail/en/adafruit-industries-llc/3898/1528-2731-ND/9685336)]

## Build It!

Disclaimer: I'm a programmer, not an electrical engineer, so don't be surprised if you sense an overarching theme of weird layout choices and lousy soldering.  But, hey, it worked, first try! 

Tip: all the thumbnails are linked to a higher-resolution version of the image.

### FeatherWing Proto

Start by soldering some short male header pins onto the FeatherWing Proto.  Make sure you put the pins in the breadboard long side down, and the protoboard is placed top-side up.

<a href="./images/sense/medium/IMG_1562.jpg" target="zoom"><img src="./images/sense/small/IMG_1562.jpg" width="300" height="300"></a>
<a href="./images/sense/medium/IMG_1564.jpg" target="zoom"><img src="./images/sense/small/IMG_1564.jpg" width="300" height="300"></a>
<a href="./images/sense/medium/IMG_1565.jpg" target="zoom"><img src="./images/sense/small/IMG_1565.jpg" width="300" height="300"></a>


## Software
