MQTT
,,,,

	MQTT is a Client Server publish/subscribe messaging transport protocol. It is light weight, open, simple, and designed so as to be easy to implement. These characteristics make it ideal for use in many situations, including constrained environments such as for communication in Machine to Machine (M2M) and Internet of Things (IoT) contexts where a small code footprint is required and/or network bandwidth is at a premium. [#mqtt_spec]_

**MQTT vs HTTP**

Why bother with MQTT when you have HTTP? The answer is compression and asynchronous messaging.
HTTP v 1.1 is synchronous which means it works in a request-response model. You can ask a question and wait for the answer. An added inconvenience is that only the client is able to ask questions.

The MQTT protocol implements a channel based mssaging platform where you publish and subscribe to channels and best of all you can implement certain degrees of quality control where you retransmit or buffer messages until they are delivered to clients that have been offline.

MQTT is also binary vs HTTPS heavy aschii headers. This is really good since we want as compressed a packet as possible in order to minimize power usage.

.. rubric:: Footnotes

.. [#mqtt_spec] http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html