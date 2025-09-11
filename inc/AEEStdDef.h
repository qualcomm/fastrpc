// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef AEESTDDEF_H
#define AEESTDDEF_H
/*
=======================================================================

FILE:         AEEStdDef.h

DESCRIPTION:  definition of basic types, constants,
                 preprocessor macros

=======================================================================
*/

#include <stdbool.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
** Standard Types
** ----------------------------------------------------------------------- */

/** @defgroup  stdret standard return values
 *  @{
 */

//! @cond Doxygen_Suppress
#ifndef _AEEUID_DEFINED
typedef uint32_t AEEUID;
#define _AEEUID_DEFINED
#endif

#ifndef _AEEIID_DEFINED
typedef uint32_t AEEIID;
#define _AEEIID_DEFINED
#endif

#ifndef _AEECLSID_DEFINED
typedef uint32_t AEECLSID;
#define _AEECLSID_DEFINED
#endif

#ifndef _AEEPRIVID_DEFINED
typedef uint32_t AEEPRIVID;
#define _AEEPRIVID_DEFINED
#endif

#ifndef _AECHAR_DEFINED
typedef uint16_t AECHAR;
#define _AECHAR_DEFINED
#endif
//! @endcond

/**
 * @brief Return value of functions indicating success or failure. return value
 * 0 indicates success. A non zero value indicates a failure. Any data in rout
 * parameters is not propagated back.
 */
#ifndef _AEERESULT_DEFINED
typedef int AEEResult;
#define _AEERESULT_DEFINED
#endif

/**
 * @}
 */

/* -----------------------------------------------------------------------
** Function Calling Conventions
** ----------------------------------------------------------------------- */

#ifndef CDECL
#ifdef _MSC_VER
#define CDECL __cdecl
#else
#define CDECL
#endif /* _MSC_VER */
#endif /* CDECL */

/* -----------------------------------------------------------------------
** Constants
** ----------------------------------------------------------------------- */
/** @defgroup  stdminmax Standard Min and Max for all data types
 *  @{
 */

//! @cond Doxygen_Suppress
#ifndef MIN_AECHAR
#define MIN_AECHAR 0
#endif

#ifndef MAX_AECHAR
#define MAX_AECHAR 65535
#endif

//! @endcond

/**
 * @}
 */

/* -----------------------------------------------------------------------
** Preprocessor helpers
** ----------------------------------------------------------------------- */
#define __STR__(x) #x
#define __TOSTR__(x) __STR__(x)
#define __FILE_LINE__ __FILE__ ":" __TOSTR__(__LINE__)

/* -----------------------------------------------------------------------
** Types for code generated from IDL
** ----------------------------------------------------------------------- */

/** @defgroup  QIDL data types
 *  @{
 */
//! @cond Doxygen_Suppress
#ifndef __QIDL_WCHAR_T_DEFINED__
#define __QIDL_WCHAR_T_DEFINED__
typedef uint16_t _wchar_t;
#endif

/* __STRING_OBJECT__ will be deprecated in the future */

#if !defined(__QIDL_STRING_OBJECT_DEFINED__) && !defined(__STRING_OBJECT__)
#define __QIDL_STRING_OBJECT_DEFINED__
#define __STRING_OBJECT__

/**
 * @brief This structure is used to represent an IDL string when used inside a
   sequence or union.
 */
typedef struct _cstring_s {
	char *data;
	int dataLen;
	int dataLenReq;
} _cstring_t;

/**
 * @brief This structure is used to represent an IDL wstring when used inside a
   sequence or union.
 */

typedef struct _wstring_s {
	_wchar_t *data;
	int dataLen;
	int dataLenReq;
} _wstring_t;
#endif /* __QIDL_STRING_OBJECT_DEFINED__ */
//! @endcond
/**
 * @}
 */
/*
=======================================================================
  DATA STRUCTURES DOCUMENTATION
=======================================================================

=======================================================================

AEEUID

Description:
   This is a BREW unique ID.  Used to express unique types, interfaces, classes
     groups and privileges.  The BREW ClassID Generator generates
     unique IDs that can be used anywhere you need a new AEEIID, AEECLSID,
     or AEEPRIVID.

Definition:
    typedef uint32_t             AEEUID

=======================================================================

AEEIID

Description:
   This is an interface ID type, used to denote a BREW interface. It is a
special case of AEEUID.

Definition:
    typedef uint32_t             AEEIID

=======================================================================

AEECLSID

Description:
   This is a classe ID type, used to denote a BREW class. It is a special case
     of AEEUID.

Definition:
    typedef uint32_t             AEECLSID

=======================================================================

AEEPRIVID

Description:
   This is a privilege ID type, used to express a privilege.  It is a special
case of AEEUID.

Definition:
    typedef uint32_t             AEEPRIVID

=======================================================================

AECHAR

Description:
   This is a 16-bit character type.

Definition:
   typedef uint16_t             AECHAR

=======================================================================

AEEResult

Description:
   This is the standard result type.

Definition:
   typedef int                AEEResult

=======================================================================

_wchar_t

Description:
   This is a 16-bit character type corresponding to the IDL 'wchar'
   type.

Definition:
   typedef uint16_t             _wchar_t

See Also:
   _cstring_t
   _wstring_t

=======================================================================

_cstring_t

Description:
   This structure is used to represent an IDL string when used inside a
   sequence or union.

Definition:
   typedef struct _cstring_s {
      char* data;
      int dataLen;
      int dataLenReq;
   } _cstring_t;

Members:
   data       : A pointer to the NULL-terminated string.
   dataLen    : The size, in chars, of the buffer pointed to by 'data',
                including the NULL terminator.  This member is only used
                when the structure is part of an rout or inrout
                parameter, but must be supplied by the caller as an
                input in these cases.
   dataLenReq : The size that would have been required to store the
                entire result string.  This member is only used when the
                structure is part of an rout or inrout parameter, when
                it is an output value set by the callee.  The length of
                the returned string (including the NULL terminator)
                after a call is the minimum of dataLen and dataLenReq.

See Also:
   _wchar_t
   _wstring_t

=======================================================================

_wstring_t

Description:
   This structure is used to represent an IDL wstring when used inside a
   sequence or union.

Definition:
   typedef struct _wstring_s {
      _wchar_t* data;
      int dataLen;
      int dataLenReq;
   } _wstring_t;

Members:
   data       : A pointer to the NULL-terminated wide string.
   dataLen    : The size, in 16-bit characters, of the buffer pointed to
                by 'data', including the NULL terminator.  This member
                is only used when the structure is part of an rout or
                inrout parameter, but must be supplied by the caller as
                an input in these cases.
   dataLenReq : The number of 16-bit characters that would have been
                required to store the entire result string.  This member
                is only used when the structure is part of an rout or
                inrout parameter, when it is an output value set by the
                callee.  The length of the returned wstring (including
                the NULL terminator) after a call is the minimum of
                dataLen and dataLenReq.

See Also:
   _cstring_t
   _wchar_t

=======================================================================
*/

#endif /* #ifndef AEESTDDEF_H */
