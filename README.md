# MeterLogger

MeterLogger is a custom daughterboard you can insert in Kamstrup Meters and get them to transmit their samples 
to a remote MQTT server.
MeterLogger is part of the [**Nabovarme**](https://github.com/nabovarme) orgnaization.

Check out the [WIKI](https://github.com/nabovarme/MeterLogger/wiki) for more details!

## Build details

To build and flash for KMP type meter (Multical 601):  
```  
KEY=ef500c9268cf749016d26d6cbfaaf7bf make clean all flashall  
```  
  
To build and flash for en61107, sub type Multical 66 C:  
```  
EN61107=1 KEY=ef500c9268cf749016d26d6cbfaaf7bf make clean all flashall  
```  
  
To build and flash for en61107, sub type Multical 66 B:  
```  
MC_66B=1 KEY=ef500c9268cf749016d26d6cbfaaf7bf make clean all flashall  
```  
  
To build and flash for KMP/en61107 type, forced as flow meter:  
```  
FLOW_METER=1 KEY=ef500c9268cf749016d26d6cbfaaf7bf make clean all flashall  
```  
  
To build and flash for impulse based electricity meter:  
```  
IMPULSE=1 KEY=ef500c9268cf749016d26d6cbfaaf7bf make clean all flashall  
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
  
FLOW_METER=1  
meter is forced to run as a flow meter and close when volume is reached  
  
AUTO_CLOSE=1  
automatically check if energy is larger than the value set by open_until mqtt command 
  
DEBUG_STACK_TRACE=1  
Enable exception stack trace dump at to flash at address STACK_TRACE_SEC * SPI_FLASH_SEC_SIZE (0x80000), STACK_TRACE_N bytes  

NO_CRON=1  
disable cron stuff and save 2688 bytes of RAM. Reconfiguration is needed after changing this  

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
First 8 bytes hex encoded (ef500c9268cf7490) is wifi setup password. Master key is sha256 hashed to 32 bit and first 16 bytes is aes key and last 16 bytes is hmac sha256 key.

```  
master key                            wifi key (first 16 bytes)
+--------------------------------+    +----------------+
|ef500c9268cf749016d26d6cbfaaf7bf| -> |ef500c9268cf7490| 
+--------------------------------+    +----------------+
                |
          [sha256 hash]
                |
                v
+----------------------------------------------------------------+
|89a5d4f82ad86bc9ec27427e246fd15481663afea8c463d93cc93c967c9b31be|
+----------------------------------------------------------------+
                |                                        |
aes key         v                   hmac sha256 key      v
+--------------------------------+  +--------------------------------+
|89a5d4f82ad86bc9ec27427e246fd154|  |81663afea8c463d93cc93c967c9b31be|
+--------------------------------+  +--------------------------------+

```  

Crypto is applies on mqtt packages like this:
1) at 32 bytes offset in mqtt_message contains IV, 16 bytes random bytes used for AES128 CBC encryption.

2) cleartext (0-terminated) is AES128 CBC encrypted, with aes key and IV. Result is written to mqtt_message at 32 + 16 bytes offset (binary data not 0-terminated).

3) HMAC SHA256 checksum is calculated on topic + IV + encrypted data and written to first 32 bytes.

```  
hmac sha256                                                       IV                               encrypted data
+----------------------------------------------------------------+--------------------------------+------------------------------------+
|b218d7ec2f7977c942b5bf16d535de3cb5f0e012638148e5a7fc6e09efe440a7|54cd3d04f024e7e5c4876248cc146c41|9616bb291182861b7a5ea238a0c267115...|
+--------------------------------------------------------------------------------------------------------------------------------------+


IV
+--------------------------------+
|54cd3d04f024e7e5c4876248cc146c41| 
+--------------------------------+

cleartext message (null terminated)
+-----------------------------------------------------------------------------------------------------------------+
|heap=21376&t1=23.61 C&t2=22.19 C&tdif=1.42 K&flow1=0 l/h&effect1=0.0 kW&hr=73327 h&v1=1321.27 m3&e1=56.726 MWh&\0|
+-----------------------------------------------------------------------------------------------------------------+

+

hmac sha256 key
```  


**MQTT format for messages sent _to_ meter**  

| Topic                                                  | Message                                                                                             |
| :----------------------------------------------------- | :-------------------------------------------------------------------------------------------------- |
| /config/v2/9999999/[unix time]/ping                    |                                                                                                     |
| /config/v2/9999999/[unix time]/open                    | [unix time]                                                                                         |
| /config/v2/9999999/[unix time]/open_until              | [kWh when meter should close (only write to flash if changed and not negative value)]               |
| /config/v2/9999999/[unix time]/open_until_delta        | [[kWh when meter should close as delta (only write to flash if changed and not negative value)]     |
| /config/v2/9999999/[unix time]/close                   | [unix time]                                                                                         |
| /config/v2/9999999/[unix time]/status                  |                                                                                                     |
| /config/v2/9999999/[unix time]/set_cron                | minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=open                                  |
| /config/v2/9999999/[unix time]/set_cron                | minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=close                                 |
| /config/v2/9999999/[unix time]/set_cron                | minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=set_ssid_pwd=ssid=the_ssid&pwd=secret |
| /config/v2/9999999/[unix time]/set_cron                | minute=30&hour=*&day_of_month=*&month=*&day_of_week=*&command=clear_cron                            |
| /config/v2/9999999/[unix time]/cron                    |                                                                                                     |
| /config/v2/9999999/[unix time]/clear_cron              | [unix time]                                                                                         |
| /config/v2/9999999/[unix time]/ping                    |                                                                                                     |
| /config/v2/9999999/[unix time]/version                 |                                                                                                     |
| /config/v2/9999999/[unix time]/uptime                  |                                                                                                     |
| /config/v2/9999999/[unix time]/stack_trace             | [enable stack trace dumps until restart i.e. once (only if built with DEBUG_STACK_TRACE=1)]         |
| /config/v2/9999999/[unix time]/vdd                     |                                                                                                     |
| /config/v2/9999999/[unix time]/rssi                    |                                                                                                     |
| /config/v2/9999999/[unix time]/ssid                    |                                                                                                     |
| /config/v2/9999999/[unix time]/scan                    |                                                                                                     |
| /config/v2/9999999/[unix time]/set_ssid                | [ssid]                                                                                              |
| /config/v2/9999999/[unix time]/set_pwd                 | [pwd]                                                                                               |
| /config/v2/9999999/[unix time]/set_ssid_pwd            | [ssid=name&pwd=secret]                                                                              |
| /config/v2/9999999/[unix time]/wifi_status             |                                                                                                     |
| /config/v2/9999999/[unix time]/start_ap                | [start ap + save to flash if changed]                                                               |
| /config/v2/9999999/[unix time]/stop_ap                 | [stop ap + save to flash if changed]                                                                |
| /config/v2/9999999/[unix time]/ap_status               |                                                                                                     |
| /config/v2/9999999/[unix time]/reconnect               |                                                                                                     |
| /config/v2/9999999/[unix time]/save (only pulse meter) |                                                                                                     |
| /config/v2/9999999/[unix time]/mem                     |                                                                                                     |
| /config/v2/9999999/[unix time]/reset_reason            |                                                                                                     |
  
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
| /stack_trace/v2/9999999/[unix time]             |                                                                                                      |
| /vdd/v2/9999999/[unix time]                     | [power supply voltage level]                                                                         |
| /rssi/v2/9999999/[unix time]                    | [rssi of the wifi it is connected to (in dBm, 31 if fail)]                                           |
| /ssid/v2/9999999/[unix time]                    | [ssid of the wifi it is connected to]                                                                |
| /set_ssid/v2/9999999/[unix time]                | [ssid of the wifi it is set to connect to]                                                           |
| /set_pwd/v2/9999999/[unix time]                 | [password for the wifi it is set to connect to]                                                      |
| /set_ssid_pwd/v2/9999999/[unix time]            | [combined ssid and password for the wifi it is set to connect to]                                    |
| /scan_result/v2/9999999/[unix time]             | [ssid=Loppen Public&rssi=-51&channel=11]                                                             |
| /wifi_status/v2/9999999/[unix time]             | [connected or disconnected]                                                                          |
| /ap_status/v2/9999999/[unix time]               | [started or stopped]                                                                                 |
| /save/v2/9999999/[unix time] (only pulse meter) | saved                                                                                                |
| /mem/v2/9999999/[unix time]                     | heap=9672                                                                                            |
| /reset_reason/v2/9999999/[unix time]            |                                                                                                      |



