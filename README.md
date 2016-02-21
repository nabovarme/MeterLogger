**BUILD OPTIONS**  
DEBUG=1  
enable serial debugging  
  
DEBUG_NO_METER=1  
dont wait for meter to answer  
  
DEBUG_SHORT_WEB_CONFIG_TIME=1  
dont wait for web config  
  
EN61107=0  
meter type is KMP (Multical 601)  
  
EN61107=1  
meter type is en61107 (Multical 66)  
  
THERMO_NC=0  
thermo actuator is normal open  
  
THERMO_NC=1  
thermo actuator is normal closed  
  
  
**MQTT parameters**  
```
/config/v1/9999999/ping =>  
/config/v1/9999999/open =>  
/config/v1/9999999/close =>  
/config/v1/9999999/set_cron => minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=open  
/config/v1/9999999/ping =>  
/config/v1/9999999/get_cron =>  
```
