Conclusion
----------

The project has been a great success and i am happy to see that it is being rolled out to more than 200 households in Christiania.

I am certain that my project will provide a data foundation for further reducing of resource spending in the Nabovarme project and that it will be robust enough to support future improvements and usecases.

I have provided ideas for reducing the power usage of the unit itself by adding a DRAM flash for persistence and using deep sleeps between an hourly sample interval.

I have failed to include a life cycle assesment because the implementation and development increased in ambition as i went along.
This will of course be a breach to the report requirements.
Instead of providing a life time analysis i have made thorough discussions on the impact of my project in an enviromental perspective. Quite simply less trees will be burned because of the esp8266!

I have fulfilled the following requirments:
R1,R2, R3, R4, R5, R7, R8
and have failed the R7 because the chosen esp8266 did not break out the needed pins in order 
to tell it to deep sleep. A solution to this can be seen in the *appendice/deep sleep*.

SUSIE course
............

The susie course has been exiting to follow since i was able to take my project into a real world perspective.
I think sometimes it was hard to reflect on the course because it included very diverse aspects such as LCA, zigbee etc versus the project i worked on. However my impression was that we were given very few restrictions in order to find and accomplish a solution to a problem. 

I think that some of the protocols we covered maybe could have been replaced by more modern protocols like MQTT instead of BacNet. Not because they cover the same aspects but because it is easier to include MQTT in your project and thereby get experience than it is to include BacNet. 

In general i think the course is very compressed in terms of subjects and very diverse.Maybe we would all gain by a fewer learning targets and more time to each of them. Maybe even present them in the form of a bigegr connected system like:

* Learn about MQTT
* Install an MQTT broker server
* Learn about linux
* Implement a http server providing visualization of received MQTT data on Raspberry Pi
* Learn about AVR and sensors
* Implement MQTT client on arduino and publish to MQTT broker and see results visualized on Raspberry pi
  
Flows like described above provide good synergy and lessons learned are easier remembered.

