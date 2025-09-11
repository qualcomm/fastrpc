// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef SBUF_H
#define SBUF_H

#include "AEEstd.h"
#include <stdint.h>
#include <string.h>

/**
 * lightweight serialize/deserialize buffer.

   For example

   struct sbuf;
   //initialize empty buffer;
   sbuf_init(&sbuf, 0, 0, 0);

   //fill it with data
   sbuf_align(&sbuf, 8);
   sbuf_write(&sbuf, ptr1, 10);
   sbuf_align(&sbuf, 8);
   sbuf_write(&sbuf, ptr2, 20);

   //allocate the memory needed
   mem = malloc(sbuf_needed(&sbuf));

   //initialize with the data
   sbuf_init(&sbuf, 0, mem, sbuf_needed(&sbuf));

   //fill it with data, since it has memory, it will actually copy
   sbuf_align(&sbuf, 8);
   sbuf_write(&sbuf, ptr1, 10);
   sbuf_align(&sbuf, 8);
   sbuf_write(&sbuf, ptr2, 20);

   See sbuf_q.c for more examples
 */

struct sbuf {
	uintptr_t buf;      //! start of valid memory
	uintptr_t bufEnd;   //! end of valid memory
	uintptr_t bufStart; //! start with optinal offset from valid mem
	uintptr_t bufCur; //! current position, could be outside of valid range
};

/**
 * @param buf, the buffer structure instance
 * @param offset, this value indicates how far ahead the data buffer is
 *                start = data - offset
 * @param data, the valid memory
 * @param dataLen, the length ov valid memory
 */
static __inline void
sbuf_init(struct sbuf *buf, int offset, void *data, int dataLen)
{
	buf->buf = (uintptr_t)data;
	buf->bufStart = buf->bufCur = (uintptr_t)data - offset;
	buf->bufEnd = (uintptr_t)data + dataLen;
}

//! move the current pointer by len
static __inline void
sbuf_advance(struct sbuf *buf, int len)
{
	buf->bufCur += len;
}

/**
 * @retval, the amount of memory needed for everything from the start (with the
 * offset) to the current position of the buffer
 */
static __inline int
sbuf_needed(struct sbuf *buf)
{
	return buf->bufCur - buf->bufStart;
}
/**
 * @retval, the space left in the buffer. A negative value indicates overflow.
 *          A positive value includes the offset.
 */
static __inline int
sbuf_left(struct sbuf *buf)
{
	int result;
	__builtin_sub_overflow(buf->bufEnd, buf->bufCur, &result);
	return result;
}

//! @retval the current head pointer
static __inline void *
sbuf_head(struct sbuf *buf)
{
	return (void *)buf->bufCur;
}

//! @retval true if the current pointer is valid
static __inline int
sbuf_valid(struct sbuf *buf)
{
	return buf->bufCur >= buf->buf && buf->bufCur < buf->bufEnd;
}

//! advance the head pointer so the "needed" is aligned to the align value
#define _SBUF_ALIGN(x, y) (((x) + ((y) - 1)) & ~((y) - 1))
static __inline void
sbuf_align(struct sbuf *buf, uint32_t align)
{
	sbuf_advance(buf,
	             _SBUF_ALIGN(sbuf_needed(buf), align) - sbuf_needed(buf));
}

/**
 * Write to the buffer.
 * @param src, the memory to read from.  Will write srcLen bytes to buf from
 * src from the buf's current position.  Only the valid portion of data will be
 * written.
 * @param srcLen, the length of src.  The buffer will be advanced by srcLen.
 */
static __inline void
sbuf_write(struct sbuf *buf, void *psrc, int srcLen)
{
	uintptr_t src = (uintptr_t)psrc;
	if(buf->bufCur + srcLen > buf->buf) {
		int writeLen;
		if(buf->bufCur < buf->buf) {
			int len = buf->buf - buf->bufCur;
			srcLen -= len;
			src += len;
			sbuf_advance(buf, len);
		}
		writeLen = STD_MIN(srcLen, sbuf_left(buf));
		if(writeLen > 0) {
			memmove((void *)buf->bufCur, (void *)src,
			        STD_MIN(buf->bufEnd - buf->bufCur, writeLen));
		}
	}
	sbuf_advance(buf, srcLen);
}

/**
 * Read from the buffer into dst.
 * @param dst, the data to write to. Will write dstLen to dst from buf
 *             from the current position of buf.  Only valid memory
 *             will be written to dst.  Invalid overlapping memory will
 *             remain untouched.
 * @param dstLen, the length of dst.  buf will be advanced by dstLen
 */
static __inline void
sbuf_read(struct sbuf *buf, void *pdst, int dstLen)
{
	uintptr_t dst = (uintptr_t)pdst;
	if(buf->bufCur + dstLen > buf->buf) {
		int readLen;
		if(buf->bufCur < buf->buf) {
			int len = buf->buf - buf->bufCur;
			dstLen -= len;
			dst += len;
			sbuf_advance(buf, len);
		}
		readLen = STD_MIN(dstLen, sbuf_left(buf));
		if(readLen > 0) {
			memmove((void *)dst, (void *)buf->bufCur, readLen);
		}
	}
	sbuf_advance(buf, dstLen);
}

#endif
