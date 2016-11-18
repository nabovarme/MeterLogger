//
//  main.c
//  queue_test
//
//  Created by stoffer on 16/11/2016.
//  Copyright © 2016 stoffer. All rights reserved.
//

#include <stdio.h>
#include "mqtt.h"

MQTT_Client mqtt_client;
uint8_t c = 0;
uint8_t p;
int i;

int main(int argc, const char * argv[]) {
    // insert code here...
    printf("Hello, World!\n");
    MQTT_InitClient(&mqtt_client, "foo", "user", "pass", 120, 1);
    while (1) {
        MQTT_Publish(&mqtt_client, "/sample/v2/9999999/1479332897", "AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaZ", 156, 2, 0);	// QoS level 2
    }
    
    /*
    while (1) {
        RINGBUF_Put(&mqtt_client.msgQueue.rb, ++c);
        RINGBUF_Put(&mqtt_client.msgQueue.rb, c);
        RINGBUF_Put(&mqtt_client.msgQueue.rb, c);
        RINGBUF_Get(&mqtt_client.msgQueue.rb, &p);
        os_printf("\nQueue:\n");
        for (i = 0; i < mqtt_client.msgQueue.rb.size; i++) {
            if ((mqtt_client.msgQueue.rb.p_o + i) == mqtt_client.msgQueue.rb.p_r) {
                os_printf(" >");
            }
            if ((mqtt_client.msgQueue.rb.p_o + i) == mqtt_client.msgQueue.rb.p_w) {
                os_printf("< ");
            }
            
            os_printf("%02x", *(mqtt_client.msgQueue.rb.p_o + i));
        }
        if (mqtt_client.msgQueue.rb.p_o == mqtt_client.msgQueue.rb.p_w) {
            os_printf("<\n");
            
        }
        os_printf("\n");
    }
     */
    
    return 0;
}
