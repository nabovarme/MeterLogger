/* str_queue.c
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#include "queue.h"
#include "proto.h"

#include "esp8266.h"
#include <stdbool.h>

uint8_t *last_rb_p_r;
uint8_t *last_rb_p_w;
uint32_t last_fill_cnt;

void ICACHE_FLASH_ATTR QUEUE_Init(QUEUE *queue, int bufferSize)
{
	queue->buf = (uint8_t*)os_zalloc(bufferSize);
	RINGBUF_Init(&queue->rb, queue->buf, bufferSize);
}
int32_t ICACHE_FLASH_ATTR QUEUE_Puts(QUEUE *queue, uint8_t* buffer, uint16_t len)
{
	int32_t ret;
    int32_t i;
	
    last_rb_p_r = queue->rb.p_r;
    last_rb_p_w = queue->rb.p_w;
    last_fill_cnt = queue->rb.fill_cnt;
    
	ret = PROTO_AddRb(&queue->rb, buffer, len);
	if (ret == -1) {
        queue->rb.p_r = last_rb_p_r;
        queue->rb.p_w = last_rb_p_w;
        queue->rb.fill_cnt = last_fill_cnt;
		os_printf("QUEUE_Puts: failed to add %d to queue, rolling back\n", len);
	}
	else {
		os_printf("QUEUE_Puts: added %d to queue\n", len);
	}
#ifdef PRINT_QUEUE
    os_printf("Queue:\n");
    for (i = 0; i < queue->rb.size; i++) {
        if ((queue->rb.p_o + i) == queue->rb.p_r) {
            os_printf(" >");
        }
        if ((queue->rb.p_o + i) == queue->rb.p_w) {
            os_printf("< ");
        }
        
        os_printf("%02x", *(queue->rb.p_o + i));
    }
    if (queue->rb.p_o == queue->rb.p_w) {
        os_printf("<");
        
    }
    os_printf("\n");
#endif
    os_printf("QUEUE_Puts: queue size(%d/%d)\n\n", queue->rb.fill_cnt, queue->rb.size);
	return ret;
}
int32_t ICACHE_FLASH_ATTR QUEUE_Gets(QUEUE *queue, uint8_t* buffer, uint16_t* len, uint16_t maxLen)
{
	int32_t ret;
    int32_t i;
	
	ret = PROTO_ParseRb(&queue->rb, buffer, len, maxLen);
	if (ret == -1) {
		os_printf("QUEUE_Gets: failed to remove %d from queue\n", *len);
	}
	else {
		os_printf("QUEUE_Gets: removed %d from queue\n", *len);
	}
#ifdef PRINT_QUEUE
    os_printf("Queue:\n");
    for (i = 0; i < queue->rb.size; i++) {
        if ((queue->rb.p_o + i) == queue->rb.p_r) {
            os_printf(" >");
        }
        if ((queue->rb.p_o + i) == queue->rb.p_w) {
            os_printf("< ");
        }
        
        os_printf("%02x", *(queue->rb.p_o + i));
    }
    if (queue->rb.p_o == queue->rb.p_w) {
        os_printf("<");
        
    }
    os_printf("\n");
#endif
	os_printf("QUEUE_Puts: queue size(%d/%d)\n\n", queue->rb.fill_cnt, queue->rb.size);
	return ret;
}

BOOL ICACHE_FLASH_ATTR QUEUE_IsEmpty(QUEUE *queue)
{
	if(queue->rb.fill_cnt<=0)
		return TRUE;
	return FALSE;
}
