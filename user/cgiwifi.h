#ifndef CGIWIFI_H
#define CGIWIFI_H

#include "httpd.h"

int cgiWiFiScan(HttpdConnData *connData);
void tplSetup(HttpdConnData *connData, char *token, void **arg);
int cgiWiFi(HttpdConnData *connData);
int cgiSetup(HttpdConnData *connData);
int cgiWifiSetMode(HttpdConnData *connData);

#endif