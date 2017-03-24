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
  
LED_ON_AC=1  
use led to indicate ac state  

SERIAL=9999999  
serial number for meter used for device's SSID: IMPULSE meters EL_9999999 and other Kamstrup meters KAM_99999. Used in wifisetup make target.   

KEY=ef500c9268cf749016d26d6cbfaaf7bf
master key for crypto, 16 bytes
Fist 8 bytes hex encoded (ef500c9268cf7490) is wifi setup password. Master key is sha256 hashed to 32 bit and first first 16 bytes is aes key and last 16 bytes is hmac sha256 key.

Crypto is applies on mqtt packages like: first 32 bytes of mqtt_message contains hmac sha256, next 16 bytes contains IV last part is aes encrypted data

**MQTT format for messages sent _to_ meter**  

| Topic                                      | Message                                                            |
| :----------------------------------------- | :----------------------------------------------------------------- |
| /config/v1/9999999/ping                    |                                                                    |
| /config/v1/9999999/open                    | [unix time]                                                        |
| /config/v1/9999999/close                   | [unix time]                                                        |
| /config/v1/9999999/status                  |                                                                    |
| /config/v1/9999999/set_cron                | minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=open |
| /config/v1/9999999/cron                    |                                                                    |
| /config/v1/9999999/clear_cron              | [unix time]                                                        |
| /config/v1/9999999/ping                    |                                                                    |
| /config/v1/9999999/version                 |                                                                    |
| /config/v1/9999999/uptime                  |                                                                    |
| /config/v1/9999999/rssi                    |                                                                    |
| /config/v1/9999999/ssid                    |                                                                    |
| /config/v1/9999999/set_ssid                | [ssid]                                                             |
| /config/v1/9999999/set_pwd                 | [pwd]                                                              |
| /config/v1/9999999/wifi_status             |                                                                    |
| /config/v1/9999999/save (only pulse meter) |                                                                    |
| /config/v1/9999999/mem                     |                                                                    |
| /config/v1/9999999/reset_reason            |                                                                    |
  
**MQTT format for messages sent _from_ meter**  

| Topic                                           | Message                                                                                              |
| :---------------------------------------------- | :--------------------------------------------------------------------------------------------------- |
| /sample/v1/9999999/[unix time]                  | heap=20000&t1=25.00 C&t2=15.00 C&tdif=10.00 K&flow1=0 l/h&effect1=0.0 kW&hr=0 h&v1=0.00 m3&e1=0 kWh& |
| /cron/v1/9999999/[unix time]                    | 12                                                                                                   |
| /ping/v1/9999999/[unix time]                    |                                                                                                      |
| /version/v1/9999999/[unix time]                 | [sdk version]-[git version]                                                                          |
| /status/v1/9999999/[unix time]                  | [open|close]                                                                                         |
| /uptime/v1/9999999/[unix time]                  | [uptime in seconds]                                                                                  |
| /rssi/v1/9999999/[unix time]                    | [rssi of the wifi it is connected to (in dBm, 31 if fail)]                                           |
| /ssid/v1/9999999/[unix time]                    | [ssid of the wifi it is connected to]                                                                |
| /wifi_status/v1/9999999/[unix time]             | [connected or disconnected]                                                                          |
| /save/v1/9999999/[unix time] (only pulse meter) | saved                                                                                                |
| /mem/v1/9999999/[unix time]                     | heap=9672&                                                                                           |
| /reset_reason/v1/9999999/[unix time]            |                                                                                                      |



