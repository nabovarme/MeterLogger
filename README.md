To build and flash for KMP type meter (Multical 601):  
```  
KEY=ef500c9268cf749016d26d6cbfaaf7bf AP=1 make clean all flashall  
```  
  
To build and flash for en61107, sub type Multical 66 C:  
```  
EN61107=1 KEY=ef500c9268cf749016d26d6cbfaaf7bf AP=1 make clean all flashall  
```  
  
To build and flash for en61107, sub type Multical 66 B:  
```  
MC_66B=1 KEY=ef500c9268cf749016d26d6cbfaaf7bf AP=1 make clean all flashall  
```  
  
To build and flash for impulse based electricity meter:  
```  
IMPULSE=1 KEY=ef500c9268cf749016d26d6cbfaaf7bf AP=1 make clean all flashall  
```  
  
To configure wireless network:  
```  
SERIAL=9999999 KEY=ef500c9268cf749016d26d6cbfaaf7bf make wifisetup  
```  
  
**BUILD OPTIONS**  
DEBUG=1  
enable serial debugging  
  
DEBUG_NO_METER=1  
dont wait for meter to answer  
  
DEBUG_SHORT_WEB_CONFIG_TIME=1  
dont wait for web config  
  
IMPULSE=1  
meter type is impulse based (100 mS)  
  
EN61107=0  
meter type is KMP (Multical 601)  
  
EN61107=1  
meter type is en61107, sub type Multical 66 C  
  
MC_66B=1  
meter type is en61107, sub type Multical 66 B  
  
THERMO_NO=0  
thermo actuator is normal closed  
  
THERMO_NO=1  
thermo actuator is normal open  

THERMO_ON_AC_2  
thermo actuator connected on ac 2  
  
LED_ON_AC=1  
use led to indicate ac state  

SERIAL=9999999  
serial number for meter used for device's SSID: IMPULSE meters EL_9999999 and other Kamstrup meters KAM_9999999. Used in make wifisetup target and if DEBUG_NO_METER=1 

KEY=ef500c9268cf749016d26d6cbfaaf7bf
master key for crypto, 16 bytes
Fist 8 bytes hex encoded (ef500c9268cf7490) is wifi setup password. Master key is sha256 hashed to 32 bit and first first 16 bytes is aes key and last 16 bytes is hmac sha256 key.

Crypto is applies on mqtt packages like: first 32 bytes of mqtt_message contains hmac sha256, next 16 bytes contains IV last part is aes encrypted data

AP=1
enable wireless extender; wireless AP
enables open source lwip and uses more memory so mqtt buffer is smaller when this option is set. 

**MQTT format for messages sent _to_ meter**  

| Topic                                                  | Message                                                            |
| :----------------------------------------------------- | :----------------------------------------------------------------- |
| /config/v2/9999999/[unix time]/ping                    |                                                                    |
| /config/v2/9999999/[unix time]/open                    | [unix time]                                                        |
| /config/v2/9999999/[unix time]/open_until              | [kWh when meter should close]                                      |
| /config/v2/9999999/[unix time]/open_until_delta        | [[kWh when meter should close as delta]                            |
| /config/v2/9999999/[unix time]/close                   | [unix time]                                                        |
| /config/v2/9999999/[unix time]/status                  |                                                                    |
| /config/v2/9999999/[unix time]/set_cron                | minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=open |
| /config/v2/9999999/[unix time]/cron                    |                                                                    |
| /config/v2/9999999/[unix time]/clear_cron              | [unix time]                                                        |
| /config/v2/9999999/[unix time]/ping                    |                                                                    |
| /config/v2/9999999/[unix time]/version                 |                                                                    |
| /config/v2/9999999/[unix time]/uptime                  |                                                                    |
| /config/v2/9999999/[unix time]/vdd                     |                                                                    |
| /config/v2/9999999/[unix time]/rssi                    |                                                                    |
| /config/v2/9999999/[unix time]/ssid                    |                                                                    |
| /config/v2/9999999/[unix time]/scan                    |                                                                    |
| /config/v2/9999999/[unix time]/set_ssid                | [ssid]                                                             |
| /config/v2/9999999/[unix time]/set_pwd                 | [pwd]                                                              |
| /config/v2/9999999/[unix time]/wifi_status             |                                                                    |
| /config/v2/9999999/[unix time]/start_ap                | [start ap + save to flash if changed]                              |
| /config/v2/9999999/[unix time]/stop_ap                 | [stop ap + save to flash if changed]                               |
| /config/v2/9999999/[unix time]/ap_status               |                                                                    |
| /config/v2/9999999/[unix time]/reconnect               |                                                                    |
| /config/v2/9999999/[unix time]/save (only pulse meter) |                                                                    |
| /config/v2/9999999/[unix time]/mem                     |                                                                    |
| /config/v2/9999999/[unix time]/reset_reason            |                                                                    |
  
**MQTT format for messages sent _from_ meter**  

| Topic                                           | Message                                                                                              |
| :---------------------------------------------- | :--------------------------------------------------------------------------------------------------- |
| /sample/v2/9999999/[unix time]                  | heap=20000&t1=25.00 C&t2=15.00 C&tdif=10.00 K&flow1=0 l/h&effect1=0.0 kW&hr=0 h&v1=0.00 m3&e1=0 kWh& |
| /cron/v2/9999999/[unix time]                    | 12                                                                                                   |
| /ping/v2/9999999/[unix time]                    |                                                                                                      |
| /version/v2/9999999/[unix time]                 | [sdk version]-[git version]                                                                          |
| /status/v2/9999999/[unix time]                  | [open|close]                                                                                         |
| /open_until/v2/9999999/[unix time]              | [kWh when meter should close]                                                                        |
| /open_until_delta/v2/9999999/[unix time]        | [kWh when meter should close]                                                                        |
| /uptime/v2/9999999/[unix time]                  | [uptime in seconds]                                                                                  |
| /vdd/v2/9999999/[unix time]                     | [power supply voltage level]                                                                         |
| /rssi/v2/9999999/[unix time]                    | [rssi of the wifi it is connected to (in dBm, 31 if fail)]                                           |
| /ssid/v2/9999999/[unix time]                    | [ssid of the wifi it is connected to]                                                                |
| /scan_result/v2/9999999/[unix time]             | ssid=Loppen Public&rssi=-51&channel=11                                                               |
| /wifi_status/v2/9999999/[unix time]             | [connected or disconnected]                                                                          |
| /ap_status/v2/9999999/[unix time]               | [started or stopped]                                                                                 |
| /save/v2/9999999/[unix time] (only pulse meter) | saved                                                                                                |
| /mem/v2/9999999/[unix time]                     | heap=9672&                                                                                           |
| /reset_reason/v2/9999999/[unix time]            |                                                                                                      |



