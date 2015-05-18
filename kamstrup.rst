Kamstrup multical 601
,,,,,,,,,,,,,,,,,,,,,

The kamstrup multical 601 protocol is a serial protocol with 1200 bits per second.
The protocol is a request-response protocol where the data is encoded in many frames
where each is 8 bits.


The data frame is based on the OSI model. In this protocol, only three layers of the OSI model are used. These are
the physical layer, data link and the application layer. The figure below show how each of the layers (shown as shaded)
is related to the data frame.

.. image::
   images/kamstrup_frame.png

**Physical layer**

Data is transmitted byte wise in a binary data format. 8 data bit represent one byte of data.
The physical layer uses ’Byte stuffing’ to compensate for byte values reserved as start, stop and acknowledge.
The method is to substitute the reserved bytes values with a pair of byte values.

**Data link layer**

The destination address is included in order to prepare a future enhanced version of the protocol.
For heat meters the destination address is 3Fh. The logger top module use 7Fh and the logger base module use BFh.
Included in the data link layer is a CRC with reference to the CCITT-standard using the polynomial 1021h. Only deviation from the standard is the initial value, which is 0000h instead of FFFFh.
The CRC result is calculated for destination address, CID and data. CRC is transmitted with MSByte first and LSByte last.

I decided to include a table of all possible crc checksums for the given length in my code so i did not have
to compute it on each frame.
In the *user/kmp.c file you can see the checksum table.

**Application layer**

Most data in the application layer is handled in a KMP register format.
You use command id's (CID's) to tell the kamstrup which registers you are interested in.

The kamstrup specifies a variable lenght register format.
This format includes three bytes show below:

.. image::
   images/register_format.png

* The first byte defines which unit the value is in
* the second byte defines the byte length of the value
* the third byte tells about the sign and exponent of the value.


Kamstrup implements floating numbers by the use of the following equation:

.. image::
   images/floating_point.png

SI tells if the value is negative, if SI is 1 then the value is negative.
The SE tells if the value is a decimal fraction, if SE is 1 then the value is less than zero.
The integer represents the value before translated into the floating point value.

Because the Kamstrup protocol only uses base 10 floating points, later computations are easily done
by modifying the point index by dividing with 10.

In order to compute power of values i needed a power function. This exist in libmath. But i didnt want to link
all libmath into this project just to gain a power function, and when i did it also gave compile errors for lack of flash.
So i wrote my own power function, this can be seen in *user/kmp.c - kmp_pow*.

Libc printf does not have its own string formatter for floating point numbers. So a function had to be written in order
to translate the received numbers into strings ready for transmission.



