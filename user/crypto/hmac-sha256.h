/**
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT HMAC_SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __HMAC_H__
#define __HMAC_H__

#include <esp8266.h>
#include <stdint.h>
#include "sha256.h"

typedef struct {
	uint8_t o_key_pad[SHA256_BLOCK_LENGTH];
	sha256_ctx_t ctx;
} hmac_sha256_ctx_t;

ICACHE_FLASH_ATTR void hmac_sha256_init(hmac_sha256_ctx_t *hctx, const uint8_t *key, const uint32_t keylen);
ICACHE_FLASH_ATTR void hmac_sha256_update(hmac_sha256_ctx_t *hctx, const uint8_t *msg, const uint32_t msglen);
ICACHE_FLASH_ATTR void hmac_sha256_final(hmac_sha256_ctx_t *hctx, uint8_t *hmac);
ICACHE_FLASH_ATTR void hmac_sha256(const uint8_t *key, const uint32_t keylen, const uint8_t *msg, const uint32_t msglen, uint8_t *hmac);

#endif
