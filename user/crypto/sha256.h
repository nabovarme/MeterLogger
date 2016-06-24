/**
 * Copyright (c) 2000-2001 Aaron D. Gifford
 * Copyright (c) 2013-2014 Pavol Rusnak
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTOR(S) ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTOR(S) BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __SHA2_H__
#define __SHA2_H__

#include <esp8266.h>
#include <stdint.h>
#include "crypto/crypto.h"

#define SHA256_BLOCK_LENGTH		64
#define SHA256_DIGEST_LENGTH		32
#define SHA256_DIGEST_STRING_LENGTH	(SHA256_DIGEST_LENGTH * 2 + 1)

typedef _align_32_bit struct {
	uint32_t	state[8];
	uint64_t	bitcount;
	uint8_t	buffer[SHA256_BLOCK_LENGTH];
} sha256_ctx_t;

ICACHE_FLASH_ATTR void sha256_init(sha256_ctx_t *context);
ICACHE_FLASH_ATTR void sha256_update(sha256_ctx_t *context, const uint8_t *data, size_t len);
ICACHE_FLASH_ATTR void sha256_final(sha256_ctx_t *context, uint8_t digest[SHA256_DIGEST_LENGTH]);
ICACHE_FLASH_ATTR char* sha256_end(sha256_ctx_t *context, char buffer[SHA256_DIGEST_STRING_LENGTH]);
ICACHE_FLASH_ATTR void sha256_raw(const uint8_t* data, size_t len, uint8_t digest[SHA256_DIGEST_LENGTH]);
ICACHE_FLASH_ATTR char* sha256_data(const uint8_t* data, size_t len, char digest[SHA256_DIGEST_STRING_LENGTH]);

#endif
