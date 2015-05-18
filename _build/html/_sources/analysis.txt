Analysis
--------

The objective of this project is not to create a power efficient meter logger but to reduce overall heating usage for ca 1000 people.
So to talk about power usage in this project can not be isolated from the greater scale of the Nabovarme project.

The ESP8266's power usage statistics can be seen in the table below:

+-----------------------------------+---------+-------+
| Mode                              | Typical | Units |
+===================================+=========+=======+
| 802.11b, CCK 1Mbps, POUT=+19.5dBm | 215     | mA    |
+-----------------------------------+---------+-------+
| Deep sleep                        | 10      | mA    |
+-----------------------------------+---------+-------+
| Standby                           | 0.9     | uA    |
+-----------------------------------+---------+-------+

*source:* 
[#nurdspace_esp8266]_


In this table a few sample situations and their current drain can be seen.

The implications of meter logger on the Nabovarme project can be:

If the ESP8266 knows the temperature the water in each household then we dont have to produce and distribute
max heated water to all households unless they need it.
We imagine that an meter logger notifies the heating production when its local warm water container drops below
a certain threshold. This will have a major influence when for example at night when nobody is using warm water etc.

Another dimension is to give people a live view of their usage.
We imagine that if a user can see and follow a live correlation between heating usage and money spent they will react by changing their behaviour.
The reasons can be economically but also ecologically since we can also corellate usage with ressource usage and notify people on how many trees are flowing through their radiators.

Power considerations
....................

The esp8266 firmware is designed with minimum power consumption in mind and the ambition is to implement the following power usage decreasing messures:

**deep sleep**

Implement deep sleep and reduce sample frequency to once an hour.
The esp8266 only uses about 10mA when in deep sleep and the rtc continues to hold precision.

**MQTT**

When using MQTT we are using a very compressed binary payload with minimal protocol header. This mean a very power efficient frame.

MQTT together with sleep will create a unique low power scenario where we can do the following:

.. figure:: 
   diagrams/deepsleep_mqtt.png
   :figwidth: 80%
   :scale: 200%


.. df
	.. seqdiag::
	   
	   seqdiag{

	   esp8266; "MQTT broker";

	   "MQTT broker" -> "MQTT broker" [label = "offline"];
	   esp8266 -> esp8266 [label = "new sample:\nA"]
	   esp8266 -> "MQTT broker" [label = "publish A", failed];
	   === deep sleep ===
	   esp8266 -> esp8266 [label = "new sample:\nB"];
	   esp8266 -> "MQTT broker" [label = "publish B", failed];
	   esp8266 -> "MQTT broker" [label = "publish A", failed];
	   "MQTT broker" -> "MQTT broker" [label = "online"];
	   === deep sleep ===
	   esp8266 -> esp8266 [label = "new sample:\nC"];
	   esp8266 -> "MQTT broker" [label = "publish C"];
	   esp8266 -> "MQTT broker" [label = "publish B"];
	   esp8266 -> "MQTT broker" [label = "publish A"];
	   }

The diagram shows how the buffering together with deep sleep creates the foundation for infrequent bursts of transmission when the conenction is established again later.
The size of the buffer, and thereby connected spi flash, describes the max size of the buffer queue and thereby how many sleep cycles you can go through before loosing data.

.. rubric:: Footnotes

.. [#nurdspace_esp8266] https://nurdspace.nl/ESP8266#Power

