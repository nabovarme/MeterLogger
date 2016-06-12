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
meter type is en61107 (Multical 66)  
  
AES=1  
enable aes encryption of samples  
  
THERMO_NO=0  
thermo actuator is normal closed  
  
THERMO_NO=1  
thermo actuator is normal open  
  
LED_ON_AC=1  
use led to indicate ac state  

**MQTT format for messages sent _to_ meter**  

| Topic                                      | Message                                                            |
| :----------------------------------------- | :----------------------------------------------------------------- |
| /config/v1/9999999/ping                    |                                                                    |
| /config/v1/9999999/open                    |                                                                    |
| /config/v1/9999999/close                   |                                                                    |
| /config/v1/9999999/status                  |                                                                    |
| /config/v1/9999999/set_cron                | minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=open |
| /config/v1/9999999/cron                    |                                                                    |
| /config/v1/9999999/ping                    |                                                                    |
| /config/v1/9999999/version                 |                                                                    |
| /config/v1/9999999/uptime                  |                                                                    |
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
| /save/v1/9999999/[unix time] (only pulse meter) | saved                                                                                                |
| /mem/v1/9999999/[unix time]                     | heap=9672&                                                                                           |
| /reset_reason/v1/9999999/[unix time]            |                                                                                                      |



