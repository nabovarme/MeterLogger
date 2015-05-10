Esp8266 firmware
................

her is an unfinished introduction

DNS, NTP, RTC and sleep
,,,,,,,,,,,,,,,,,,,,,,,

here is some text describing what and some codee snippets showing how it was implemented

.. code-block:: c

    void ICACHE_FLASH_ATTR
    ntp_udpclient_recv(void *arg, char *pdata, unsigned short len) {
        struct tm *dt;
        char timestr[11];
        // this is NTP time (seconds since Jan 1 1900):
        uint32 timestamp = pdata[40] << 24 | pdata[41] << 16 |
            pdata[42] << 8 | pdata[43];				// BUG? is ntp 32 or 64 bit?
        timestamp =  timestamp - NTP_OFFSET;
        set_unix_time(timestamp);
    }


Kamstrup
,,,,,,,,

here is some text describing what and some codee snippets showing how it was implemented


MQTT
,,,,

here is some text describing what and some codee snippets showing how it was implemented




