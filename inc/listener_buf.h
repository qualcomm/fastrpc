// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef LISTENER_BUF_H
#define LISTENER_BUF_H

#include "remote.h"
#include "sbuf.h"
#include "verify.h"

static __inline void
pack_in_bufs(struct sbuf *buf, remote_arg *pra, int nBufs)
{
	int ii;
	uint32_t len;
	C_ASSERT(sizeof(len) == 4);
	for(ii = 0; ii < nBufs; ++ii) {
		len = (uint32_t)pra[ii].buf.nLen;
		sbuf_write(buf, (uint8_t *)&len, 4);
		if(len) {
			sbuf_align(buf, 8);
			sbuf_write(buf, pra[ii].buf.pv, len);
		}
	}
}

static __inline void
pack_out_lens(struct sbuf *buf, remote_arg *pra, int nBufs)
{
	int ii;
	uint32_t len;
	C_ASSERT(sizeof(len) == 4);
	for(ii = 0; ii < nBufs; ++ii) {
		len = (uint32_t)pra[ii].buf.nLen;
		sbuf_write(buf, (uint8_t *)&len, 4);
	}
}

static __inline void
unpack_in_bufs(struct sbuf *buf, remote_arg *pra, int nBufs)
{
	int ii;
	uint32_t len = 0;
	C_ASSERT(sizeof(len) == 4);
	for(ii = 0; ii < nBufs; ++ii) {
		sbuf_read(buf, (uint8_t *)&len, 4);
		pra[ii].buf.nLen = len;
		if(pra[ii].buf.nLen) {
			sbuf_align(buf, 8);
			if((int)pra[ii].buf.nLen <= sbuf_left(buf)) {
				pra[ii].buf.pv = sbuf_head(buf);
			}
			sbuf_advance(buf, pra[ii].buf.nLen);
		}
	}
}

static __inline void
unpack_out_lens(struct sbuf *buf, remote_arg *pra, int nBufs)
{
	int ii;
	uint32_t len = 0;
	C_ASSERT(sizeof(len) == 4);
	for(ii = 0; ii < nBufs; ++ii) {
		sbuf_read(buf, (uint8_t *)&len, 4);
		pra[ii].buf.nLen = len;
	}
}

// map out buffers on the hlos side to the remote_arg array
// dst is the space required for buffers we coun't map from the adsp
static __inline void
pack_out_bufs(struct sbuf *buf, remote_arg *pra, int nBufs)
{
	int ii;
	uint32_t len;
	C_ASSERT(sizeof(len) == 4);
	for(ii = 0; ii < nBufs; ++ii) {
		len = (uint32_t)pra[ii].buf.nLen;
		sbuf_write(buf, (uint8_t *)&len, 4);
		if(pra[ii].buf.nLen) {
			sbuf_align(buf, 8);
			if((int)pra[ii].buf.nLen <= sbuf_left(buf)) {
				pra[ii].buf.pv = sbuf_head(buf);
			}
			sbuf_advance(buf, pra[ii].buf.nLen);
		}
	}
}

// on the aDSP copy the data from buffers we had to copy to the local
// remote_arg structure
static __inline int
unpack_out_bufs(struct sbuf *buf, remote_arg *pra, int nBufs)
{
	int ii, nErr = 0;
	uint32_t len;
	C_ASSERT(sizeof(len) == 4);
	for(ii = 0; ii < nBufs; ++ii) {
		sbuf_read(buf, (uint8_t *)&len, 4);
		VERIFY(len == pra[ii].buf.nLen);
		if(pra[ii].buf.nLen) {
			sbuf_align(buf, 8);
			sbuf_read(buf, pra[ii].buf.pv, pra[ii].buf.nLen);
		}
	}
bail:
	return nErr;
}

#endif
