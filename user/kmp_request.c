#include "user_interface.h"
#include "osapi.h"
#include "driver/uart.h"

#include "mqtt.h"
#include "kmp.h"
#include "kmp_request.h"

#define QUEUE_SIZE 256

// fifo
volatile unsigned int fifo_head, fifo_tail;
volatile unsigned char fifo_buffer[QUEUE_SIZE];

// allocate frame to send
unsigned char frame[KMP_FRAME_L];
unsigned int frame_length;
uint16_t register_list[8];

// allocate struct for response
kmp_response_t response;
unsigned char unit_string[8];

MQTT_Client *mqtt_client;

ICACHE_FLASH_ATTR
void kmp_request_init(MQTT_Client* client) {
	mqtt_client = client;
	system_os_task(kmp_received_task, kmp_received_task_prio, kmp_received_task_queue, kmp_received_task_queue_length);
}

ICACHE_FLASH_ATTR
void kmp_request_send() {
	// get serial
	// prepare frame
	frame_length = kmp_get_serial(frame);
	uart0_tx_buffer(frame, frame_length);     // send kmp request
	
	
    os_delay_us(20000);             // sleep 2 seconds

    // get registers
    // prepare frame
    register_list[0] = 0x3c;    // heat energy (E1)
    register_list[1] = 0x44;    // volume register (V1)
    register_list[2] = 0x3EC;   // operational hour counter (HR)
    register_list[3] = 0x56;    // current flow temperature (T1)
    register_list[4] = 0x57;    // current return flow temperature (T2)
    register_list[5] = 0x59;    // current temperature difference (T1-T2)
    register_list[6] = 0x4A;    // current flow in flow (FLOW1)
    register_list[7] = 0x50;    // current power calculated on the basis of V1-T1-T2 (EFFEKT1)
    frame_length = kmp_get_register(frame, register_list, 8);
	
    // send frame
    uart0_tx_buffer(frame, frame_length);     // send kmp request
    
    os_delay_us(20000);             // sleep 2 seconds
}

/**
  * @brief  Uart receive task.
  * @param  events: contain the uart receive data
  * @retval None
  */
ICACHE_FLASH_ATTR
static void kmp_received_task(os_event_t *events) {
	uint8_t c;
	unsigned int i;
	//unsigned char topic[128];
	unsigned char message[1024];
	//int topic_l;
	int message_l;
	unsigned char hex_char[3];

	c = events->par;
	i = 0;
	if ((c == '\r') || (c == '\n')) {
		memset(message, 0x00, 1024);			// clear it
		while (kmp_fifo_get(&c)) {				// DEBUG: possible BUFFER OVERFLOW here
			//os_printf("%c", c);
			//memcpy(message + i, &c, 1);
			os_sprintf(hex_char, "%02x", c);
			strcat(message, hex_char);
			i++;
		}
		// send it
		//os_printf("%s\n\r", message);
				
		message_l = strlen(message);
		MQTT_Publish(mqtt_client, "/console", message, message_l, 0, 0);
	}

//  temp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
	/*
//  temp = 'X';
  //add transparent determine
  while(READ_PERI_REG(UART_STATUS(UART0)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S))
  {
//    temp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
    WRITE_PERI_REG(0X60000914, 0x73); //WTD

    if(at_state != at_statIpTraning)
    {
      temp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
      if((temp != '\n') && (echoFlag))
      {
        uart_tx_one_char(temp); //display back
      }
    }
	/*
//    if((at_state != at_statIpTraning) && (temp != '\n') && (echoFlag))
//    {
//      uart_tx_one_char(temp); //display back
//    }

    switch(at_state)
    {
    case at_statIdle: //serch "AT" head
      atHead[0] = atHead[1];
      atHead[1] = temp;
      if((os_memcmp(atHead, "AT", 2) == 0) || (os_memcmp(atHead, "at", 2) == 0))
      {
        at_state = at_statRecving;
        pCmdLine = at_cmdLine;
        atHead[1] = 0x00;
      }
      else if(temp == '\n') //only get enter
      {
        uart0_sendStr("\r\nError\r\n");
      }
      break;

    case at_statRecving: //push receive data to cmd line
      *pCmdLine = temp;
      if(temp == '\n')
      {
        system_os_post(at_procTaskPrio, 0, 0);
        at_state = at_statProcess;
        if(echoFlag)
        {
          uart0_sendStr("\r\n"); ///////////
        }
      }
      else if(pCmdLine >= &at_cmdLine[at_cmdLenMax - 1])
      {
        at_state = at_statIdle;
      }
      pCmdLine++;
      break;

    case at_statProcess: //process data
      if(temp == '\n')
      {
//      system_os_post(at_busyTaskPrio, 0, 1);
        uart0_sendStr("\r\nbusy p...\r\n");
      }
      break;

    case at_statIpSending:
      *pDataLine = temp;
      if((pDataLine >= &at_dataLine[at_sendLen - 1]) || (pDataLine >= &at_dataLine[at_dataLenMax - 1]))
      {
        system_os_post(at_procTaskPrio, 0, 0);
        at_state = at_statIpSended;
      }
      pDataLine++;
//    *pDataLine = temp;
//    if (pDataLine == &UartDev.rcv_buff.pRcvMsgBuff[at_sendLen-1])
//    {
//      system_os_post(at_procTaskPrio, 0, 0);
//      at_state = at_statIpSended;
//    }
//    pDataLine++;
      break;

    case at_statIpSended: //send data
      if(temp == '\n')
      {
//      system_os_post(at_busyTaskPrio, 0, 2);
        uart0_sendStr("busy s...\r\n");
      }
      break;

    case at_statIpTraning:
      os_timer_disarm(&at_delayChack);
//      *pDataLine = temp;
      if(pDataLine > &at_dataLine[at_dataLenMax - 1])
      {
        os_printf("exceed\r\n");
        return;
      }
      else if(pDataLine == &at_dataLine[at_dataLenMax - 1])
      {
        temp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
        *pDataLine = temp;
        pDataLine++;
        at_tranLen++;
        os_timer_arm(&at_delayChack, 1, 0);
        return;
      }
      else
      {
        temp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
        *pDataLine = temp;
        pDataLine++;
        at_tranLen++;
//        if(ipDataSendFlag == 0)
//        {
//          os_timer_arm(&at_delayChack, 20, 0);
//        }
        os_timer_arm(&at_delayChack, 20, 0);
      }
      break;

//      os_timer_disarm(&at_delayChack);
//      *pDataLine = temp;
//      if(pDataLine >= &at_dataLine[at_dataLenMax - 1])
//      {
////        ETS_UART_INTR_DISABLE();
////      pDataLine++;
//        at_tranLen++;
////      os_timer_arm(&at_delayChack, 1, 0); /////
//        system_os_post(at_procTaskPrio, 0, 0);
//        break;
//      }
//      pDataLine++;
//      at_tranLen++;
//      if(ipDataSendFlag == 0)
//      {
//        os_timer_arm(&at_delayChack, 20, 0);
//      }
//      break;

    default:
      if(temp == '\n')
      {
      }
      break;
    }
  }
  */
  
  if(UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST))
  {
    WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
  }
  else if(UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_TOUT_INT_ST))
  {
    WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
  }
  ETS_UART_INTR_ENABLE();
}


// fifo
ICACHE_FLASH_ATTR
unsigned int kmp_fifo_in_use() {
	return fifo_head - fifo_tail;
}

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_put(unsigned char c) {
	if (kmp_fifo_in_use() != QUEUE_SIZE) {
		fifo_buffer[fifo_head++ % QUEUE_SIZE] = c;
		// wrap
		if (fifo_head == QUEUE_SIZE) {
			fifo_head = 0;
		}
		return 1;
	}
	else {
		return 0;
	}
}

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_get(unsigned char *c) {
	if (kmp_fifo_in_use() != 0) {
		*c = fifo_buffer[fifo_tail++ % QUEUE_SIZE];
		// wrap
		if (fifo_tail == QUEUE_SIZE) {
			fifo_tail = 0;
		}
		return 1;
	}
	else {
		return 0;
	}
}

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_snoop(unsigned char *c, unsigned int pos) {
	if (kmp_fifo_in_use() > (pos)) {
        *c = fifo_buffer[(fifo_tail + pos) % QUEUE_SIZE];
		return 1;
	}
	else {
		return 0;
	}
}
