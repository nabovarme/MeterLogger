Design
======

The system can be described using the diagram beneath:

TODO: image of whole system

The heart of the system is the Kamstrup Multical 601 meter. This meter is found in households in Denmark and other countries as well.
The Meter is normally the property of the company providing the heating infrastructure. Therefore they are mostly
locked to keep endusers from mangling and manipulating the device.
This however is not the case at Christiania where we have bought the meters and installed them ourself.
This gives us a rare opportunity to open the meter and install new electronics inside it where Kamsturp left room
for expansion boards like shown underneath:

TODO: image of open kamstrup meter

Connected to the kamstrup meter is a microcontroller using a UART connection to talk to the Kamstrup meter.
The microcontroller has onboard wifi and and external antenna is connected to the device with an extension cable.
Both the microcontroller and the Kamstrup meter gets power from a builtin power supply.

The "meter unit" (Kamstrup meter and wifi microcontroller) has an established connection to a nearby wifi hot-spot.
The meter unit transmits samples through the wifi hot-spot, over the internet to a central server.

The central server collects samples and serves a web page where the samples can be seen in a graph.