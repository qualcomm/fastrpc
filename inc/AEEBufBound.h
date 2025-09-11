// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef AEEBUFBOUND_H
#define AEEBUFBOUND_H
/*==============================================================================

FILE:  AEEBufBound.h

SERVICES:
        BufBound APIs

GENERAL DESCRIPTION:
        BufBound provides a "bounded buffer" API that facilitates
          measuring strings or character output.  It's design accomodates
          the implementation of functions that can have the same exact logic
          for measuring and outputting char buffer content.

REVISION HISTORY:
        Fri Aug 08 17:38:29 2003: Created

==============================================================================*/

typedef struct BufBound {
	char *pcBuf;   /* original buffer */
	char *pcWrite; /* write pointer */
	char *pcEnd;   /* first illegal write pointer */
} BufBound;

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

extern void BufBound_Init(BufBound *me, char *pBuf, int nLen);
extern void BufBound_Write(BufBound *me, const char *pc, int nLen);
extern void BufBound_Putc(BufBound *me, char c);
extern void BufBound_Putnc(BufBound *me, char c, int nCount);
extern void BufBound_ForceNullTerm(BufBound *me);
extern void BufBound_Puts(BufBound *me, const char *cpsz);
extern void BufBound_Advance(BufBound *me, int nLen);
extern int BufBound_BufSize(BufBound *me);
extern int BufBound_Left(BufBound *me);
extern int BufBound_ReallyWrote(BufBound *me);
extern int BufBound_Wrote(BufBound *me);

static __inline int
BufBound_IsFull(BufBound *me)
{
	return (BufBound_Left(me) <= 0);
}

// Deprecated:
static __inline int
BufBound_IsCounter(BufBound *me)
{
	return BufBound_BufSize(me) == 0;
}

#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

/*=====================================================================
=======================================================================
DATA STRUCTURE DOCUMENTATION
=======================================================================

BufBound

Description:
   An BufBound keeps track of whether appending to a bounded buffer
    has overflowed.

Definition:
   typedef struct BufBound
   {
      char* pcBuf;
      char* pcWrite;
      char* pcEnd;
   } BufBound;

Members:
      pcBuf: original start pointer
      pcWrite: current write location
      pcEnd: first illegal write position

See Also:
      BufBound Interface

=======================================================================
INTERFACE DOCUMENTATION
=======================================================================
BufBound Interface

  BufBound is a statically-linked interface.

  BufBound provides functions for safely appending to a character buffer.  On
  initialization, the buffer start address and size are provided.  Subsequent
  write operations are checked against the buffer bounds.

  Once the buffer bounds are exceeded, no bytes will be written but the
  BufBound will continue to increment its internal "write pointer" to reflect
  the number of bytes that would have been written (had the bounds not been
  exceeded).

  When initialized with a buffer size of zero, a BufBound simply counts the
  number of bytes that would be required to contain the result.  This design
  accommodates implementations that use the same logic for generating output
  and measuring the space required for generated output.

  BufBound protects clients from numerical overflow by limiting the write
  pointer to a maximum offset of INT_MAX from the start of the buffer.
  Functions that write data into the buffer safely ignore negative size inputs
  (Write and Putnc).

=======================================================================
BufBound_Init()

Description:
   initialize a BufBound for appending to a buffer

Prototype:

   void BufBound_Init(BufBound *me, char *pBuf, int nLen);

Parameters:
   me: the BufBound
   pBuf: the bounded buffer
   nLen: size of pBuf, in bytes

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================

BufBound_Write()

Description:
   Appends some number of bytes to a BufBound, if possible.

   When a negative size is passed, it is safely treated as zero.

Prototype:

   void BufBound_Write(BufBound *me, const char *pc, int nLen);

Parameters:
   me: the BufBound
   pc: pointer to bytes to append
   int nLen: number of bytes to write

Return Value:
   None

Comments:
   If the BufBound has overflowed, no bytes are written, but pcWrite is
      *always* advanced by nLen.

Side Effects:
   None

See Also:
   None

=======================================================================

BufBound_Advance()

Description:

   Moves the write pointer.  Advance is like a relative seek operation.  It
   does not change the contents of the buffer, so when using a forward seek
   (positive advance) be careful of advancing over uninitialized data.

   Negative numbers will decrease the write pointer down to 0 (the start of
   the buffer) and not below.  Positive numbers will increase the write
   pointer up to offset INT_MAX and not beyond.

Prototype:

   void BufBound_Advance(BufBound *me, int nDelta);

Parameters:
   me: the BufBound
   int nLen: number of bytes to advance

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================

BufBound_Putc()

Description:
   Appends one byte to a BufBound, if possible.

Prototype:

   void BufBound_Putc(BufBound *me, char c);

Parameters:
   me: the BufBound
   c: the byte

Return Value:
   None

Comments:
   If the BufBound has overflowed, no byte is written, but pcWrite is
      *always* advanced by 1.

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_Putnc()

Description:
   Appends a byte to a BufBound repeatedly.

   When a negative size is passed, it is safely treated as zero.

Prototype:

   void BufBound_Putnc(BufBound *me, char c, int nCount);

Parameters:
   me: the BufBound
   c: the byte
   nCount: number of times to append c

Return Value:
   None

Comments:
   If the BufBound has overflowed, no byte is written, but pcWrite is
      *always* advanced by nCount.

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_ForceNullTerm()

Description:
   Appends a null terminating character to a BufBound, if possible.
     If the BufBound has overflowed, the last legal location is
     set to '\0'.

Prototype:
   void BufBound_ForceNullTerm(BufBound *me);

Parameters:
   me: the BufBound

Return Value:
   None

Comments:
   pcWrite is *always* advanced by 1.

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_Puts()

Description:
   Appends a null-terminated string to a BufBound, if possible

Prototype:

   void BufBound_Puts(BufBound *me, const char* cpsz);

Parameters:
   me: the BufBound
   cpsz: the string to append

Return Value:

Comments:
   If the BufBound has overflowed, no bytes are written, but pcWrite is
      *always* advanced by strlen(cpsz).

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_BufSize()

Description:
   Returns the size of the buffer owned by the BufBound.  This is
   the same as the number passed to BufBound_Init (MAXed with zero).

Prototype:

   int BufBound_IsCounter(BufBound* me);

Parameters:
   me: the BufBound

Return Value:
   1 if the BufBound is a counter, 0 otherwise

Comments:
   None

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_Left()

Description:
   Returns the number of bytes the BufBound can still accomodate,
   without overflowing.  If overflow has occurred, it will return
   a negative number.

Prototype:

   int BufBound_Left(BufBound* me);

Parameters:
    me: the BufBound

Return Value:
   The number of bytes the BufBound can still accomodate,
     without overflowing.

Comments:
   The return value may be negative, if overflow has already occurred.

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_ReallyWrote()

Description:
   Returns the number of bytes actually written to the BufBound,
     not including any overflow.

Prototype:

   int BufBound_ReallyWrote(BufBound* me);

Parameters:
    me: the BufBound

Return Value:
   The number of bytes actually written to the BufBound,
     not including any overflow.

Comments:
   None

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_Wrote()

Description:

   Returns the number of bytes written to the BufBound, including any
   overflow, up to INT_MAX.

Prototype:

   int BufBound_Wrote(BufBound* me);

Parameters:
    me: the BufBound

Return Value:

   The number of bytes written to the BufBound, including any overflow.

Comments:
   None

Side Effects:
   None

See Also:
   None


=======================================================================

BufBound_IsFull()

Description:
   Tests whether an AEEBuffBound has overflowed.

Prototype:

   int BufBound_IsFull(BufBound* me);

Parameters:
    me: the BufBound

Return Value:
   1 if the BufBound has overflowed, 0 otherwise

Comments:
   None

Side Effects:
   None

See Also:
   None

======================================================================= */
#endif /* #ifndef AEEBUFBOUND_H */
