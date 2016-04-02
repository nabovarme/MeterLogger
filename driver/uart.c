/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: uart.c
 *
 * Description: Two UART mode configration and interrupt handler.
 *              Check your hardware connection while use this mode.
 *
 * Modification history:
 *     2014/3/12, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"
#include "osapi.h"
#include "driver/uart_register.h"
#include "user_interface.h"
#ifndef EN61107
	#include "kmp_request.h"
#else
	#include "en61107_request.h"
#endif
//#include "ssc.h"


// UartDev is defined and initialized in rom code.
extern UartDevice    UartDev;
//extern os_event_t    at_recvTaskQueue[at_recvTaskQueueLen];

LOCAL void uart0_rx_intr_handler(void *para);

/******************************************************************************
 * FunctionName : uart_config
 * Description  : Internal used function
 *                UART0 used for data TX/RX, RX buffer size is 0x100, interrupt enabled
 *                UART1 just used for debug output
 * Parameters   : uart_no, use UART0 or UART1 defined ahead
 * Returns      : NONE
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
uart_config(uint8 uart_no)
{
  if (uart_no == UART1)
  {
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
  }
  else
  {
    /* rcv_buff size if 0x100 */
    ETS_UART_INTR_ATTACH(uart0_rx_intr_handler,  &(UartDev.rcv_buff));
    PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_U0RTS);
  }

  uart_div_modify(uart_no, UART_CLK_FREQ / (UartDev.baut_rate));

  UartDev.exist_parity = (UartDev.parity == NONE_BITS ? STICK_PARITY_DIS : STICK_PARITY_EN);
  WRITE_PERI_REG(UART_CONF0(uart_no),
  			((UartDev.exist_parity & UART_PARITY_EN_M)  <<  UART_PARITY_EN_S) |
			((UartDev.parity & UART_PARITY_M) << UART_PARITY_S ) |
			((UartDev.stop_bits & UART_STOP_BIT_NUM) << UART_STOP_BIT_NUM_S) |
			((UartDev.data_bits & UART_BIT_NUM) << UART_BIT_NUM_S));

  //clear rx and tx fifo,not ready
  SET_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);
  CLEAR_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);

  //set rx fifo trigger
//  WRITE_PERI_REG(UART_CONF1(uart_no),
//                 ((UartDev.rcv_buff.TrigLvl & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
//                 ((96 & UART_TXFIFO_EMPTY_THRHD) << UART_TXFIFO_EMPTY_THRHD_S) |
//                 UART_RX_FLOW_EN);
  if (uart_no == UART0)
  {
    //set rx fifo trigger
    WRITE_PERI_REG(UART_CONF1(uart_no),
                   ((0x10 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
                   ((0x10 & UART_RX_FLOW_THRHD) << UART_RX_FLOW_THRHD_S) |
                   UART_RX_FLOW_EN |
                   (0x02 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S |
                   UART_RX_TOUT_EN);
    SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_TOUT_INT_ENA |
                      UART_FRM_ERR_INT_ENA);
  }
  else
  {
    WRITE_PERI_REG(UART_CONF1(uart_no),
                   ((UartDev.rcv_buff.TrigLvl & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S));
  }

  //clear all interrupt
  WRITE_PERI_REG(UART_INT_CLR(uart_no), 0xffff);
  //enable rx_interrupt
  SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA);
}

/******************************************************************************
 * FunctionName : uart1_tx_one_char
 * Description  : Internal used function
 *                Use uart1 interface to transfer one char
 * Parameters   : uint8 TxChar - character to tx
 * Returns      : OK
*******************************************************************************/
STATUS
uart_tx_one_char(uint8 uart, uint8 TxChar)
{
    while (true)
    {
      uint32 fifo_cnt = READ_PERI_REG(UART_STATUS(uart)) & (UART_TXFIFO_CNT<<UART_TXFIFO_CNT_S);
      if ((fifo_cnt >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) < 126) {
        break;
      }
    }

    WRITE_PERI_REG(UART_FIFO(uart) , TxChar);
    return OK;
}

/******************************************************************************
 * FunctionName : uart1_write_char
 * Description  : Internal used function
 *                Do some special deal while tx char is '\r' or '\n'
 * Parameters   : char c - character to tx
 * Returns      : NONE
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart1_write_char(char c)
{
  if (c == '\n')
  {
    uart_tx_one_char(UART1, '\r');
    uart_tx_one_char(UART1, '\n');
  }
  else if (c == '\r')
  {
  }
  else
  {
    uart_tx_one_char(UART1, c);
  }
}

void ICACHE_FLASH_ATTR
uart0_write_char(char c)
{
  if (c == '\n')
  {
    uart_tx_one_char(UART0, '\r');
    uart_tx_one_char(UART0, '\n');
  }
  else
  {
    uart_tx_one_char(UART0, c);
  }
}
/******************************************************************************
 * FunctionName : uart0_tx_buffer
 * Description  : use uart0 to transfer buffer
 * Parameters   : uint8 *buf - point to send buffer
 *                uint16 len - buffer len
 * Returns      :
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart0_tx_buffer(uint8 *buf, uint16 len)
{
  uint16 i;

  for (i = 0; i < len; i++)
  {
    uart_tx_one_char(UART0, buf[i]);
  }
}

/******************************************************************************
 * FunctionName : uart0_sendStr
 * Description  : use uart0 to transfer buffer
 * Parameters   : uint8 *buf - point to send buffer
 *                uint16 len - buffer len
 * Returns      :
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart0_sendStr(const char *str)
{
	while(*str)
	{
		uart_tx_one_char(UART0, *str++);
	}
}

/******************************************************************************
 * FunctionName : uart0_rx_intr_handler
 * Description  : Internal used function
 *                UART0 interrupt handler, add self handle code inside
 * Parameters   : void *para - point to ETS_UART_INTR_ATTACH's arg
 * Returns      : NONE
*******************************************************************************/
//extern void at_recvTask(void);

LOCAL void
uart0_rx_intr_handler(void *para)
{
  /* uart0 and uart1 intr combine togther, when interrupt occur, see reg 0x3ff20020, bit2, bit0 represents
    * uart1 and uart0 respectively
    */
//  RcvMsgBuff *pRxBuff = (RcvMsgBuff *)para;

	uint8 RcvChar;
	uint8 uart_no = UART0;//UartDev.buff_uart_no;


	if (UART_FRM_ERR_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_FRM_ERR_INT_ST)) {
		//os_printf("FRM_ERR\r\n");
		WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_FRM_ERR_INT_CLR);
	}

	if (UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_FULL_INT_ST)) {
		//ETS_UART_INTR_DISABLE();/////////
		while (READ_PERI_REG(UART_STATUS(UART0)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S)) {
			//WRITE_PERI_REG(0X60000914, 0x73); //WTD
			RcvChar = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
#ifdef EN61107
			en61107_fifo_put(RcvChar);
#elif defined IMPULSE
			// nothing
#else
			kmp_fifo_put(RcvChar);
#endif
			if ((RcvChar == '\r')  || (RcvChar == 0x06)) {				// if end of kmp frame received or acknowledge
#ifdef EN61107
				system_os_post(en61107_received_task_prio, 0, 0);
#elif defined IMPULSE
			// nothing
#else
				system_os_post(kmp_received_task_prio, 0, 0);
#endif
			}
		}
	}
	else if (UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_TOUT_INT_ST)) {
		//ETS_UART_INTR_DISABLE();/////////
		while (READ_PERI_REG(UART_STATUS(UART0)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S)) {
			//WRITE_PERI_REG(0X60000914, 0x73); //WTD
			RcvChar = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
#ifdef EN61107
			en61107_fifo_put(RcvChar);
#elif defined IMPULSE
			// nothing
#else
			kmp_fifo_put(RcvChar);
#endif
			if ((RcvChar == '\r')  || (RcvChar == 0x06)) {				// if end of kmp frame received or acknowledge
#ifdef EN61107
				system_os_post(en61107_received_task_prio, 0, 0);
#elif defined IMPULSE
			// nothing
#else
				system_os_post(kmp_received_task_prio, 0, 0);
#endif
			}
		}
	}

	if (UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST)) {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
	}
	else if (UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_TOUT_INT_ST)) {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
	}
	//ETS_UART_INTR_ENABLE();
}

/******************************************************************************
 * FunctionName : uart_init
 * Description  : user interface for init uart
 * Parameters   : UartBautRate uart0_br - uart0 bautrate
 *                UartBautRate uart1_br - uart1 bautrate
 * Returns      : NONE
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart_init(UartBautRate uart0_br, UartBautRate uart1_br)
{
  // rom use 74880 baut_rate, here reinitialize
#ifndef EN61107
  UartDev.baut_rate = uart0_br;
  UartDev.exist_parity = STICK_PARITY_DIS;
  UartDev.data_bits = EIGHT_BITS;
  UartDev.parity = NONE_BITS;
  UartDev.stop_bits = TWO_STOP_BIT;
#else
  UartDev.baut_rate = uart0_br;
  UartDev.exist_parity = STICK_PARITY_EN;
  UartDev.data_bits = SEVEN_BITS;
  UartDev.parity = EVEN_BITS;
  UartDev.stop_bits = TWO_STOP_BIT;
#endif
  uart_config(UART0);
#ifndef EN61107
  UartDev.baut_rate = uart1_br;
  UartDev.exist_parity = STICK_PARITY_DIS;
  UartDev.data_bits = EIGHT_BITS;
  UartDev.parity = NONE_BITS;
  UartDev.stop_bits = TWO_STOP_BIT;
#else
  UartDev.baut_rate = uart1_br;
  UartDev.exist_parity = STICK_PARITY_EN;
  UartDev.data_bits = SEVEN_BITS;
  UartDev.parity = EVEN_BITS;
  UartDev.stop_bits = TWO_STOP_BIT;
#endif
  uart_config(UART1);
  ETS_UART_INTR_ENABLE();

  // install uart1 putc callback
  os_install_putc1((void *)uart0_write_char);
}

void ICACHE_FLASH_ATTR
uart_reattach()
{
	uart_init(BIT_RATE_74880, BIT_RATE_74880);
//  ETS_UART_INTR_ATTACH(uart_rx_intr_handler_ssc,  &(UartDev.rcv_buff));
//  ETS_UART_INTR_ENABLE();
}
