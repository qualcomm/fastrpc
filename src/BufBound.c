// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*==============================================================================

FILE:  AEEBufBound.c

SERVICES:
        AEEBufBound APIs

GENERAL DESCRIPTION:
        AEEBufBound provides a "bounded buffer" API that facilitates
          measuring strings or character output.  It's design accomodates
          the implementation of functions that can have the same exact logic
          for measuring and outputting char buffer content.

REVISION HISTORY:
        Sun Mar 06 11:23:10 2005 Created

==============================================================================*/
#include "AEEBufBound.h"
#include "AEEstd.h"
#include <limits.h>

// Note on bounds-checking logic and saturation:
//
// Simple pointer comparisons are not adequate for bounds checking.  pcBuf
// and pcEnd are assumed to be valid pointers in the address space.  But
// pcWrite is not ... it is a theoretical value that can exceed pcEnd, and
// may in fact wrap around the end of the address space.  In that case the
// test for (pcWrite < pcEnd) will yield true, although pcWrite is outside
// the buffer.  Use (pcEnd-pcWrite) > 0 to be accurate.
//
// In order to ensure this works in all cases, we need to avoid integer
// overflows.  We do this by restricting pcWrite to the range
// [pcBuf..pcBuf+INT_MAX].  The ensures that pcWrite-pcBuf and pcWrite-pcBuf
// will always be valid integers.  It also allows us to ensure that
// BufBound_Wrote() will not return wildly misleading results.
//
//                                            PCSAT
//    pcBuf               pcEnd            pcBuf+MAXINT
//      |-------------------| . . . . . . . . . |
//                   ^            ^
//    pcWrite:      (a)          (b)
//

#define PCSAT(me) ((me)->pcBuf + INT_MAX)

// Advance me->pcWrite, saturating.
//
// On entry:
//    *pnLen = number of bytes to be written (non-negative)
// On exit:
//    return value = where to write (pointer into the buffer)
//    *pnLen       = number of bytes to write
//
static char *
BufBound_ValidateWrite(BufBound *me, int *pnLen)
{
	int nLen = *pnLen;
	char *pcWrite = me->pcWrite;
	int nMaxCopy = me->pcEnd - pcWrite; // could be negative!

	if(nMaxCopy < nLen) {
		// Must check PCSAT to validate advance
		int nMaxAdvance = PCSAT(me) - pcWrite; // max amount to advance

		if(nLen > nMaxAdvance) {
			nLen = nMaxAdvance;
		}
		if(nMaxCopy < 0) {
			nMaxCopy = 0;
		}
	} else {
		// Simple case: all fits in the buffer
		nMaxCopy = nLen;
	}

	*pnLen = nMaxCopy;
	me->pcWrite = pcWrite + nLen;
	return pcWrite;
}

void
BufBound_Write(BufBound *me, const char *pc, int nLen)
{
	if(nLen > 0) {
		char *pcDest = BufBound_ValidateWrite(me, &nLen);

		while(--nLen >= 0) {
			pcDest[nLen] = pc[nLen];
		}
	}
}

void
BufBound_Putnc(BufBound *me, char c, int nLen)
{
	if(nLen > 0) {
		char *pcDest = BufBound_ValidateWrite(me, &nLen);

		while(--nLen >= 0) {
			pcDest[nLen] = c;
		}
	}
}

void
BufBound_Advance(BufBound *me, int nLen)
{
	uint32_t uOffset = (uint32_t)((me->pcWrite - me->pcBuf) + nLen);

	if(uOffset > INT_MAX) {
		uOffset = INT_MAX;
		if(nLen < 0) {
			uOffset = 0;
		}
	}
	me->pcWrite = me->pcBuf + uOffset;
}

void
BufBound_Init(BufBound *me, char *pBuf, int nLen)
{
	if(nLen < 0) {
		nLen = 0;
	}
	me->pcWrite = me->pcBuf = pBuf;
	me->pcEnd = pBuf + nLen;
}

void
BufBound_Putc(BufBound *me, char c)
{
	if((me->pcEnd - me->pcWrite) > 0) {
		*me->pcWrite++ = c;
	} else if(me->pcWrite != PCSAT(me)) {
		++me->pcWrite;
	}
}

void
BufBound_ForceNullTerm(BufBound *me)
{
	if((me->pcEnd - me->pcWrite) > 0) {
		*me->pcWrite++ = '\0';
	} else {
		if(me->pcWrite != PCSAT(me)) {
			++me->pcWrite;
		}
		// ensure null termination if non-empty buffer
		if(me->pcEnd != me->pcBuf) {
			me->pcEnd[-1] = '\0';
		}
	}
}

void
BufBound_Puts(BufBound *me, const char *cpsz)
{
	BufBound_Write(me, cpsz, strlen(cpsz));
}

int
BufBound_BufSize(BufBound *me)
{
	return me->pcEnd - me->pcBuf;
}

int
BufBound_Left(BufBound *me)
{
	return (me->pcEnd - me->pcWrite);
}

int
BufBound_ReallyWrote(BufBound *me)
{
	return STD_MIN(me->pcEnd - me->pcBuf, me->pcWrite - me->pcBuf);
}

int
BufBound_Wrote(BufBound *me)
{
	return (me->pcWrite - me->pcBuf);
}
