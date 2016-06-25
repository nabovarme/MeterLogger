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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <esp8266.h>
#include <string.h>
#include "crypto/crypto.h"
#include "crypto/hmac-sha256.h"

ICACHE_FLASH_ATTR
void hmac_sha256_init(hmac_sha256_ctx_t *hctx, uint8_t *key, uint32_t keylen) {
    int i;
	_align_32_bit uint8_t i_key_pad[SHA256_BLOCK_LENGTH];
	memset(i_key_pad, 0, SHA256_BLOCK_LENGTH);
	if (keylen > SHA256_BLOCK_LENGTH) {
		sha256_raw(key, keylen, i_key_pad);
	} else {
		memcpy(i_key_pad, key, keylen);
	}
	for (i = 0; i < SHA256_BLOCK_LENGTH; i++) {
		hctx->o_key_pad[i] = i_key_pad[i] ^ 0x5c;
		i_key_pad[i] ^= 0x36;
	}
	sha256_init(&(hctx->ctx));
	sha256_update(&(hctx->ctx), i_key_pad, SHA256_BLOCK_LENGTH);
	memset(i_key_pad, 0, sizeof(i_key_pad));
}

ICACHE_FLASH_ATTR
void hmac_sha256_update(hmac_sha256_ctx_t *hctx, uint8_t *msg, uint32_t msglen) {
	sha256_update(&(hctx->ctx), msg, msglen);
}

ICACHE_FLASH_ATTR
void hmac_sha256_final(hmac_sha256_ctx_t *hctx, uint8_t *hmac) {
	_align_32_bit uint8_t hash[SHA256_DIGEST_LENGTH];
	sha256_final(&(hctx->ctx), hash);
	sha256_init(&(hctx->ctx));
	sha256_update(&(hctx->ctx), hctx->o_key_pad, SHA256_BLOCK_LENGTH);
	sha256_update(&(hctx->ctx), hash, SHA256_DIGEST_LENGTH);
	sha256_final(&(hctx->ctx), hmac);
	memset(hash, 0, sizeof(hash));
	memset(hctx, 0, sizeof(hmac_sha256_ctx_t));
}

ICACHE_FLASH_ATTR
void hmac_sha256(uint8_t *key, uint32_t keylen, uint8_t *msg, uint32_t msglen, uint8_t *hmac) {
	hmac_sha256_ctx_t hctx;
	hmac_sha256_init(&hctx, key, keylen);
	hmac_sha256_update(&hctx, msg, msglen);
	hmac_sha256_final(&hctx, hmac);
}
