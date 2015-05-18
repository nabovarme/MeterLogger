Software
''''''''

Software had to be developed to extract the samples
from the Kamstrup meter and send them to a central server
In order to do this a few core protocols must be known.


.. include::
   protocols.rst

.. include::
   esp8266firmware.rst

Vizualisation
.............

A prototype was created for visualizing the data created by the meter unit.

The visualization is provided by the same server running the MQTT broker and sql database.

A Perl script serves a html site where a javascript generated graph is populated with latest entries from the sql database.

The graph is zoomable in both x and y and a screenshot is shown below:

.. figure:: 
   images/graph1.png

   When you hold the mouse above a point in the graph you can observe the different data fields.

Here is a zoomed in version

.. figure:: 
   images/graph2.png

   As you can see the sample resolution is 1 minute intervals. And the flow is has a climax around 22:00 in the evening.



