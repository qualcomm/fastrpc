// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef AEESTD_H
#define AEESTD_H
/*====================================================================

DESCRIPTION:  Standard library; general-purpose utility functions.

====================================================================*/
#include "AEEStdDef.h"
#include "string.h"

#define STD_CONSTRAIN(val, min, max)                                          \
	(((val) < (min)) ? (min) : ((val) > (max)) ? (max) : (val))
#define STD_BETWEEN(val, minGE, maxLT)                                        \
	(((unsigned long)(minGE) <= (unsigned long)(val))                     \
	 && ((unsigned long)((unsigned long)(val) - (unsigned long)(minGE))   \
	     < (unsigned long)((unsigned long)(maxLT)                         \
	                       - (unsigned long)(minGE))))
#define STD_ARRAY_SIZE(a) ((int)((sizeof((a)) / sizeof((a)[0]))))
#define STD_ARRAY_MEMBER(p, a)                                                \
	(((p) >= (a)) && ((p) < ((a) + STD_ARRAY_SIZE(a))))

#define STD_SIZEOF(x) ((int)sizeof(x))
#define STD_OFFSETOF(type, member)                                            \
	(((char *)(&((type *)1)->member)) - ((char *)1))

#define STD_RECOVER_REC(type, member, p)                                      \
	((void)((p) - &(((type *)1)->member)),                                \
	 (type *)(void *)(((char *)(void *)(p))                               \
	                  - STD_OFFSETOF(type, member)))
#define STD_MIN(a, b) ((a) < (b) ? (a) : (b))
#define STD_MAX(a, b) ((a) > (b) ? (a) : (b))
// lint -emacro(545,STD_ZEROAT)
#define STD_ZEROAT(p) memset((p), 0, sizeof(*p))

#define _STD_BITS_PER(bits) (8 * sizeof((bits)[0]))

#define STD_BIT_SET(bits, ix)                                                 \
	((bits)[(ix) / _STD_BITS_PER((bits))]                                 \
	 |= 0x1 << ((ix) & (_STD_BITS_PER((bits)) - 1)))
#define STD_BIT_CLEAR(bits, ix)                                               \
	((bits)[(ix) / _STD_BITS_PER((bits))]                                 \
	 &= ~(0x1 << ((ix) & (_STD_BITS_PER((bits)) - 1))))
#define STD_BIT_TEST(bits, ix)                                                \
	((bits)[(ix) / _STD_BITS_PER((bits))]                                 \
	 & (0x1 << ((ix) & (_STD_BITS_PER((bits)) - 1))))

//
// Error codes
//
#define STD_NODIGITS 1
#define STD_NEGATIVE 2
#define STD_OVERFLOW 3
#define STD_BADPARAM 4
#define STD_UNDERFLOW 5

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

// Path functions
extern int std_makepath(const char *cpszDir, const char *cpszFile,
                        char *pszDest, int nDestSize);
extern char *std_splitpath(const char *cpszPath, const char *cpszDir);
extern char *std_cleanpath(char *pszPath);
extern char *std_basename(const char *pszPath);

#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

/*
=======================================================================
MACROS DOCUMENTATION
=======================================================================

STD_CONTSTRAIN()

Description:
  STD_CONTSTRAIN() constrains a number to be between two other numbers.

Definition:
   STD_CONSTRAIN( val, min, max ) \
          (((val) < (min)) ? (min) : ((val) > (max)) ? (max) : (val))

Parameters:
  val: number to constrain
  min: number to stay greater than or equal to
  max: number to stay less than or equal to

Evaluation Value:
   the constrained number

=======================================================================

STD_BETWEEN()

Description:
    STD_BETWEEN() tests whether a number is between two other numbers.

Definition:
    STD_BETWEEN( val, minGE, maxLT ) \
               ((unsigned)((unsigned)(val) - (unsigned)(minGE)) < \
                (unsigned)((unsigned)(maxLT) - (unsigned)(minGE)))

Parameters:
     val: value to test
     minGE: lower bound
     maxLT: upper bound

Evaluation Value:
     1 if val >= minGE and val < maxLT

=======================================================================

STD_ARRAY_SIZE()

Description:
   STD_ARRAY_SIZE() gives the number of elements in a statically allocated
array.

Definition:
    STD_ARRAY_SIZE(a) (sizeof((a))/sizeof((a)[0]))

Parameters:
    a: array to test

Evaluation Value:
    number of elements in a

=======================================================================

STD_ARRAY_MEMBER()

Description:
   STD_ARRAY_MEMBER() tests whether an item is a member of a statically
allocated array.

Definition:
   STD_ARRAY_MEMBER(p,a) (((p) >= (a)) && ((p) < ((a) + STD_ARRAY_SIZE(a))))

Parameters:
    p: item to test
    a: array to check

Evaluation Value:
    1 if p is in a

=======================================================================

STD_OFFSETOF()

Description:
  STD_OFFSETOF() gives the offset of member of a struct.

Definition:
   STD_OFFSETOF(type,member) (((char *)(&((type *)0)->member))-((char *)0))

Parameters:
    type: structured type
    member: name of member in the struct

Evaluation Value:
    offset of member (in bytes) in type

=======================================================================

STD_RECOVER_REC()

Description:
  STD_RECOVER_REC() provides a safe cast from a pointer to a member
    of a struct to a pointer to the containing struct

Definition:
  STD_RECOVER_REC(type,member,p)
((type*)(((char*)(p))-STD_OFFSETOF(type,member)))

Parameters:
    type: structured type
    member: name of member in the struct
    p: pointer to the member of the struct

Evaluation Value:
    a pointer of type type to the containing struct

=======================================================================

STD_MIN()

Description:
   STD_MIN() finds the smaller of two values.

Definition:
   STD_MIN(a,b)   ((a)<(b)?(a):(b))

Parameters:
    a, b: values to compare

Evaluation Value:
    smaller of a and b

=======================================================================

STD_MAX()

Description:
  STD_MAX() finds the larger of two values.

Definition:
   STD_MAX(a,b)   ((a)>(b)?(a):(b))

Parameters:
    a, b: values to compare

Evaluation Value:
    larger of a and b

=======================================================================

STD_ZEROAT()

Description:
   STD_ZEROAT() zero-initializes the contents of a typed chunk of memory.

Definition:
   STD_ZEROAT(p)  memset((p), 0, sizeof(*p))

Parameters:
    p: the chunk to initialize

Evaluation Value:
    p

=======================================================================

STD_BIT_SET()

Description:
   STD_BIT_SET(bits, ix) sets the bit in the memory stored in bits at
                         index ix

Parameters:
    bits: the memory address holding the  bits
    ix:   the index of the bit to set;

=======================================================================

STD_BIT_CLEAR()

Description:
   STD_BIT_CLEAR(bits, ix) clears the bit in the memory stored in bits
                           at index ix

Parameters:
    bits: the memory address holding the  bits
    ix:   the index of the bit to clear

=======================================================================

STD_BIT_TEST()

Description:
   STD_BIT_TEST(bits, ix) returns the bit in the memory stored in bits
                          at index ix

Parameters:
    bits: the memory address holding the bits
    ix:   the index of the bit to test

Evaluation Value:
    0x1 if set 0x0 if not set

=====================================================================
INTERFACES DOCUMENTATION
=======================================================================

std Interface

Description:
   This library provides a set of general-purpose utility functions.
   Functionality may overlap that of a subset of the C standard library, but
   this library differs in a few respects:

   - Functions are fully reentrant and avoid use of static variables.

   - The library can be supported consistently across all environments.
   Compiler-supplied libraries sometimes behave inconsistently and are
   unavailable in some environments.

   - Omits "unsafe" functions.  C standard library includes many functions
   that are best avoided entirely: strcpy, strcat, strtok, etc.


=======================================================================

std_memscpy - Size bounded memory copy.

Description:

  Copies bytes from the source buffer to the destination buffer.

  This function ensures that there will not be a copy beyond
  the size of the destination buffer.

  The result of calling this on overlapping source and destination
  buffers is undefined.

Prototype:

   int std_memscpy(void *dst, int dst_size, const void *src, int src_size);

Parameters:

  @param[out] dst       Destination buffer.
  @param[in]  dst_size  Size of the destination buffer in bytes.
  @param[in]  src       Source buffer.
  @param[in]  src_size  Number of bytes to copy from source buffer.

Return value:

  The number of bytes copied to the destination buffer.  It is the
  caller's responsibility to check for trunction if it cares about it -
  truncation has occurred if the return value is less than src_size.
  Returs a negative value on error.

=======================================================================

std_memsmove()

Description:

  Size bounded memory move.

  Moves bytes from the source buffer to the destination buffer.

  This function ensures that there will not be a copy beyond
  the size of the destination buffer.

  This function should be used in preference to memscpy() if there
  is the possiblity of source and destination buffers overlapping.
  The result of the operation is defined to be as if the copy were from
  the source to a temporary buffer that overlaps neither source nor
  destination, followed by a copy from that temporary buffer to the
  destination.

Prototype:

  int std_memsmove(void *dst, int dst_size, const void *src, int src_size);

Parameters:
  @param[out] dst       Destination buffer.
  @param[in]  dst_size  Size of the destination buffer in bytes.
  @param[in]  src       Source buffer.
  @param[in]  src_size  Number of bytes to copy from source buffer.

Return value:
  The number of bytes copied to the destination buffer.  It is the
  caller's responsibility to check for trunction if it cares about it -
  truncation has occurred if the return value is less than src_size.
  A negative return value indicates an error

=======================================================================

std_memrchrbegin()

Description:
   The std_memrchrbegin() finds the last occurrence of a character in a
   memory buffer.

Prototype:

   void *std_memrchrbegin(const void* s, int c, int n);

Parameters:
   s: buffer to search
   c: value of unsigned char to look for
   n: size of s in bytes

Return Value:
   a pointer to the last occurrence of c, or s if not found

=======================================================================

std_strbegins()

Description:
   The std_strbegins() tests whether a string begins with a particular
   prefix string.

Prototype:

   char *std_strbegins(const char* cpsz, const char* cpszPrefix);

Parameters:
   cpsz: string to test
   cpszPrefix: prefix to test for

Return Value:
   cpsz + std_strlen(cpszPrefix) if cpsz does begin with cpszPrefix,
     NULL otherwise

=======================================================================

std_makepath()

Description:
   The std_makepath() constructs a path from a directory portion and a file
   portion, using forward slashes, adding necessary slashes and deleting extra
    slashes.  This function guarantees NUL-termination of pszDest

Prototype:

   int std_makepath(const char *cpszDir, const char *cpszFile,
                    char *pszDest, int nDestSize)

Parameters:
   cpszDir: directory part
   cpszFile: file part
   pszDest: output buffer
   nDestSize: size of output buffer in bytes

Return Value:
   the required length to construct the path, not including
   NUL-termination

Comments:
   The following list of examples shows the strings returned by
   std_makepath() for different paths.

Example:

   cpszDir  cpszFile  std_makepath()
   ""        ""           ""
   ""        "/"          ""
   "/"       ""           "/"
   "/"       "/"          "/"
   "/"       "f"          "/f"
   "/"       "/f"         "/f"
   "d"       "f"          "d/f"
   "d/"      "f"          "d/f"
   "d"       "/f"         "d/f"
   "d/"      "/f"         "d/f"

See Also:
   std_splitpath

=======================================================================

std_splitpath()

Description:
   The std_splitpath() finds the filename part of a path given an inclusive
   directory, tests for cpszPath being in cpszDir. The forward slashes are
   used as directory delimiters.

Prototype:

   char *std_splitpath(const char *cpszPath, const char *cpszDir);

Parameters:
   cpszPath: path to test for inclusion
   cpszDir: directory that cpszPath might be in

Return Value:
   the part of cpszPath that actually falls beneath cpszDir, NULL if
   cpszPath is not under cpszDir

Comments:
   The std_splitpath() is similar to strbegins(), but it ignores trailing
   slashes on cpszDir, and it returns a pointer to the first character of
   the subpath.

   The return value of std_splitpath() will never begin with a '/'.

   The following list of examples shows the strings returned by
   std_splitpath() for different paths.

Example:
   cpszPath cpszDir  std_splitpath()
   ""        ""           ""
   ""        "/"          ""
   "/"       ""           ""
   "/"       "/"          ""
   "/d"      "d"          null
   "/d"      "/"          "d"
   "/d/"     "/d"         ""
   "/d/f"    "/"          "d/f"
   "/d/f"    "/d"         "f"
   "/d/f"    "/d/"        "f"

See Also:
   std_makepath

=======================================================================

std_cleanpath()

Description:
   The std_cleanpath() removes double slashes, ".", and ".." from
   slash-delimited paths,. It operates in-place.

Prototype:

   char *std_cleanpath(char *pszPath);

Parameters:
   pszPath[in/out]: path to "clean"

Return Value:
   pszPath

Comments:
   Passing an "fs:/" path to this function may produce undesirable
   results.  This function assumes '/' is the root.

Examples:
       pszPath  std_cleanpath()
         "",           "",
         "/",          "/",

         // here"s, mostly alone
         "./",         "/",
         "/.",         "/",
         "/./",        "/",

         // "up"s, mostly alone
         "..",         "",
         "/..",        "/",
         "../",        "/",
         "/../",       "/",

         // fun with x
         "x/.",        "x",
         "x/./",       "x/",
         "x/..",       "",
         "/x/..",      "/",
         "x/../",      "/",
         "/x/../",     "/",
         "/x/../..",   "/",
         "x/../..",    "",
         "x/../../",   "/",
         "x/./../",    "/",
         "x/././",     "x/",
         "x/.././",    "/",
         "x/../.",     "",
         "x/./..",     "",
         "../x",       "/x",
         "../../x",    "/x",
         "/../x",      "/x",
         "./../x",     "/x",

         // double slashes
         "//",         "/",
         "///",        "/",
         "////",       "/",
         "x//x",       "x/x",


Side Effects:
   None

See Also:
   None


=======================================================================

std_basename()

Description:
   The std_basename() returns the filename part of a string,
   assuming '/' delimited filenames.

Prototype:

   char *std_basename(const char *cpszPath);

Parameters:
   cpszPath: path of interest

Return Value:
   pointer into cpszPath that denotes part of the string immediately
   following the last '/'

Examples:
     cpszPath       std_basename()
         ""            ""
         "/"           ""
         "x"           "x"
         "/x"          "x"
         "y/x"         "x"
         "/y/x"        "x"

 See Also:
    None

=======================================================================*/

#endif // AEESTD_H
