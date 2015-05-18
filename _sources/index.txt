.. susie report documentation master file, created by
   sphinx-quickstart on Sun May 10 20:54:02 2015.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Kamstrup-Wifi documentation
===========================

.. figure:: 
   images/kamstrup_wifi.png


This page documents our succesfull try at implementing wifi logging for the kamstrup multical heating meters.
We have successfully reverse engineered the kamstrup protocol and offer here a look into our process.

This project serves as the foundation for Christiania Nabovarme heating infrastructure.
Its a collective heating infrastructure with a basis on a handfull of wooden pellet stoves that function like this:

.. figure::
   images/stove.gif
   :figwidth: 40%

The project is based around a kamstrup meter and an **esp8266** with custom firmware for wifi transmission over mqtt to a central server. the central server serves a website with graphs on the data collected looking like:

.. figure::
   images/graph2.png




.. toctree::
   :maxdepth: 4

   design
   implementation


