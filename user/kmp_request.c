#include "user_interface.h"
#include "osapi.h"
#include "driver/uart.h"

#include "kmp.h"
#include "kmp_request.h"

// allocate frame to send
unsigned char frame[KMP_FRAME_L];
unsigned int frame_length;
uint16_t register_list[8];

// allocate struct for response
kmp_response_t response;
unsigned char unit_string[8];

/**
  * @brief  Uart receive task.
  * @param  events: contain the uart receive data
  * @retval None
  */
ICACHE_FLASH_ATTR
static void at_recvTask(os_event_t *events)
{
	
//  static uint8_t atHead[2];
//  static uint8_t *pCmdLine;
  uint8_t temp;

  temp = events->par;
  os_printf("%c", temp);
  os_printf("rx\n");
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


ICACHE_FLASH_ATTR
void kmp_request_init() {
	//system_os_task(at_recvTask, at_recvTaskPrio, at_recvTaskQueue, at_recvTaskQueueLen);
}

ICACHE_FLASH_ATTR
void kmp_request_send() {
	// get serial
	// prepare frame
//	frame_length = kmp_get_serial(frame);
//	uart0_tx_buffer(frame, frame_length);     // send kmp request
}
