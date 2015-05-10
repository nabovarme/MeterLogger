Hardware
''''''''

The hardware in this project is pretty generic except of the embedded device in the meter.
The server can be any linux server with the ability to run an ntp server, web server, a mysql database and a MQTT broker.
The wifi hot-spots can be of any kind and is not configured by Nabovarme.

The embedded device was chosen based on these features:

* Price
* Onboard wifi
* Power consumption
* Onboard flash
* Clock speed
* Ease of development

The following embedded solutions were explored:

* Arduino with cc3000 wifi modem
* Raspberry pi with usb wifi modem
* Esp8266 microcontroller (RISC)

Based on the research i created the following table:

+-----------------+---------+-------------+---------------------------+---------------+------------+----------------+
| Solution        |Price    |Onboard wifi |Transmit Power consumption |Onboard Flash  |Clock speed |Development     |
+=================+=========+=============+===========================+===============+============+================+
|Arduino with cc  |ca. 75$  |No           | ca. 270mA,                |16kb           |16Mhz       |C like,         |
|3000             |(40 + 35)|             | (20 + 250)                |               |            |easy            |
+-----------------+---------+-------------+---------------------------+---------------+------------+----------------+
|RaspberryPi,     |77       |No           | 250mA                     |None           |700Mhz      |Linux, easy     |
|edimax usb wifi  |(52 + 25)|             |(210 + 40)                 |               |            |                |
+-----------------+---------+-------------+---------------------------+---------------+------------+----------------+
|Esp8266          |2,5 $    |Yes          |215mA                      |512kb          |80Mhz       |C sdk, moderate |
+-----------------+---------+-------------+---------------------------+---------------+------------+----------------+

sources:
http://www.arduino.cc/en/Main/ArduinoBoardDuemilanove
avr atmega 328 datasheet
cc3000 datasheet
https://nurdspace.nl/ESP8266
http://raspi.tv/2014/pihut-wifi-dongle-vs-edimax-power-usage
https://github.com/esp8266/esp8266-wiki/wiki/W25Q40BVNIG

I concluded from the data above that the ESP8266 with a price of 2.5$, onboard flash and 80 mhz clock speed was ideal
as a platform for the project.

.. include:: esp8266.rst