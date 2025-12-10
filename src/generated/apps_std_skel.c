// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _APPS_STD_SKEL_H
#define _APPS_STD_SKEL_H
#include "apps_std.h"

#ifndef _QAIC_ENV_H
#define _QAIC_ENV_H

#include <stdio.h>

#ifdef __GNUC__
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#else
#pragma GCC diagnostic ignored "-Wpragmas"
#endif
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#ifndef _ATTRIBUTE_UNUSED

#ifdef _WIN32
#define _ATTRIBUTE_UNUSED
#else
#define _ATTRIBUTE_UNUSED __attribute__ ((unused))
#endif

#endif // _ATTRIBUTE_UNUSED

#ifndef _ATTRIBUTE_VISIBILITY

#ifdef _WIN32
#define _ATTRIBUTE_VISIBILITY
#else
#define _ATTRIBUTE_VISIBILITY __attribute__ ((visibility("default")))
#endif

#endif // _ATTRIBUTE_VISIBILITY

#ifndef __QAIC_REMOTE
#define __QAIC_REMOTE(ff) ff
#endif //__QAIC_REMOTE

#ifndef __QAIC_HEADER
#define __QAIC_HEADER(ff) ff
#endif //__QAIC_HEADER

#ifndef __QAIC_HEADER_EXPORT
#define __QAIC_HEADER_EXPORT
#endif // __QAIC_HEADER_EXPORT

#ifndef __QAIC_HEADER_ATTRIBUTE
#define __QAIC_HEADER_ATTRIBUTE
#endif // __QAIC_HEADER_ATTRIBUTE

#ifndef __QAIC_IMPL
#define __QAIC_IMPL(ff) ff
#endif //__QAIC_IMPL

#ifndef __QAIC_IMPL_EXPORT
#define __QAIC_IMPL_EXPORT
#endif // __QAIC_IMPL_EXPORT

#ifndef __QAIC_IMPL_ATTRIBUTE
#define __QAIC_IMPL_ATTRIBUTE
#endif // __QAIC_IMPL_ATTRIBUTE

#ifndef __QAIC_STUB
#define __QAIC_STUB(ff) ff
#endif //__QAIC_STUB

#ifndef __QAIC_STUB_EXPORT
#define __QAIC_STUB_EXPORT
#endif // __QAIC_STUB_EXPORT

#ifndef __QAIC_STUB_ATTRIBUTE
#define __QAIC_STUB_ATTRIBUTE
#endif // __QAIC_STUB_ATTRIBUTE

#ifndef __QAIC_SKEL
#define __QAIC_SKEL(ff) ff
#endif //__QAIC_SKEL__

#ifndef __QAIC_SKEL_EXPORT
#define __QAIC_SKEL_EXPORT
#endif // __QAIC_SKEL_EXPORT

#ifndef __QAIC_SKEL_ATTRIBUTE
#define __QAIC_SKEL_ATTRIBUTE
#endif // __QAIC_SKEL_ATTRIBUTE

#ifdef __QAIC_DEBUG__
   #ifndef __QAIC_DBG_PRINTF__
   #include <stdio.h>
   #define __QAIC_DBG_PRINTF__( ee ) do { printf ee ; } while(0)
   #endif
#else
   #define __QAIC_DBG_PRINTF__( ee ) (void)0
#endif


#define _OFFSET(src, sof)  ((void*)(((char*)(src)) + (sof)))

#define _COPY(dst, dof, src, sof, sz)  \
   do {\
         struct __copy { \
            char ar[sz]; \
         };\
         *(struct __copy*)_OFFSET(dst, dof) = *(struct __copy*)_OFFSET(src, sof);\
   } while (0)

#define _COPYIF(dst, dof, src, sof, sz)  \
   do {\
      if(_OFFSET(dst, dof) != _OFFSET(src, sof)) {\
         _COPY(dst, dof, src, sof, sz); \
      } \
   } while (0)

_ATTRIBUTE_UNUSED
static __inline void _qaic_memmove(void* dst, void* src, int size) {
   int i = 0;
   for(i = 0; i < size; ++i) {
      ((char*)dst)[i] = ((char*)src)[i];
   }
}

#define _MEMMOVEIF(dst, src, sz)  \
   do {\
      if(dst != src) {\
         _qaic_memmove(dst, src, sz);\
      } \
   } while (0)


#define _ASSIGN(dst, src, sof)  \
   do {\
      dst = OFFSET(src, sof); \
   } while (0)

#define _STD_STRLEN_IF(str) (str == 0 ? 0 : strlen(str))

#include "AEEStdErr.h"

#define _TRY(ee, func) \
   do { \
      if (AEE_SUCCESS != ((ee) = func)) {\
         __QAIC_DBG_PRINTF__((__FILE__ ":%d:error:%d:%s\n", __LINE__, (int)(ee),#func));\
         goto ee##bail;\
      } \
   } while (0)

#define _CATCH(exception) exception##bail: if (exception != AEE_SUCCESS)

#define _ASSERT(nErr, ff) _TRY(nErr, 0 == (ff) ? AEE_EBADPARM : AEE_SUCCESS)

#ifdef __QAIC_DEBUG__
#define _ALLOCATE(nErr, pal, size, alignment, pv) _TRY(nErr, _allocator_alloc(pal, __FILE_LINE__, size, alignment, (void**)&pv));\
                                                  _ASSERT(nErr,pv || !(size))
#else
#define _ALLOCATE(nErr, pal, size, alignment, pv) _TRY(nErr, _allocator_alloc(pal, 0, size, alignment, (void**)&pv));\
                                                  _ASSERT(nErr,pv || !(size))
#endif


#endif // _QAIC_ENV_H

#include <string.h>
#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#include <stdlib.h>
#include <stdint.h>

typedef struct _heap _heap;
struct _heap {
   _heap* pPrev;
   const char* loc;
   uint64_t buf;
};

typedef struct _allocator {
   _heap* pheap;
   uint8_t* stack;
   uint8_t* stackEnd;
   int nSize;
} _allocator;

_ATTRIBUTE_UNUSED
static __inline int _heap_alloc(_heap** ppa, const char* loc, int size, void** ppbuf) {
   _heap* pn = 0;
   pn = malloc((size_t)size + sizeof(_heap) - sizeof(uint64_t));
   if(pn != 0) {
      pn->pPrev = *ppa;
      pn->loc = loc;
      *ppa = pn;
      *ppbuf = (void*)&(pn->buf);
      return 0;
   } else {
      return -1;
   }
}
#define _ALIGN_SIZE(x, y) (((x) + (y-1)) & ~(y-1))

_ATTRIBUTE_UNUSED
static __inline int _allocator_alloc(_allocator* me,
                                    const char* loc,
                                    int size,
                                    unsigned int al,
                                    void** ppbuf) {
   if(size < 0) {
      return -1;
   } else if (size == 0) {
      *ppbuf = 0;
      return 0;
   }
   if((_ALIGN_SIZE((uintptr_t)me->stackEnd, al) + (size_t)size) < (uintptr_t)me->stack + (size_t)me->nSize) {
      *ppbuf = (uint8_t*)_ALIGN_SIZE((uintptr_t)me->stackEnd, al);
      me->stackEnd = (uint8_t*)_ALIGN_SIZE((uintptr_t)me->stackEnd, al) + size;
      return 0;
   } else {
      return _heap_alloc(&me->pheap, loc, size, ppbuf);
   }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_deinit(_allocator* me) {
   _heap* pa = me->pheap;
   while(pa != 0) {
      _heap* pn = pa;
      const char* loc = pn->loc;
      (void)loc;
      pa = pn->pPrev;
      free(pn);
   }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_init(_allocator* me, uint8_t* stack, int stackSize) {
   me->stack =  stack;
   me->stackEnd =  stack + stackSize;
   me->nSize = stackSize;
   me->pheap = 0;
}


#endif // _ALLOCATOR_H

#ifndef SLIM_H
#define SLIM_H

#include <stdint.h>

//a C data structure for the idl types that can be used to implement
//static and dynamic language bindings fairly efficiently.
//
//the goal is to have a minimal ROM and RAM footprint and without
//doing too many allocations.  A good way to package these things seemed
//like the module boundary, so all the idls within  one module can share
//all the type references.


#define PARAMETER_IN       0x0
#define PARAMETER_OUT      0x1
#define PARAMETER_INOUT    0x2
#define PARAMETER_ROUT     0x3
#define PARAMETER_INROUT   0x4

//the types that we get from idl
#define TYPE_OBJECT             0x0
#define TYPE_INTERFACE          0x1
#define TYPE_PRIMITIVE          0x2
#define TYPE_ENUM               0x3
#define TYPE_STRING             0x4
#define TYPE_WSTRING            0x5
#define TYPE_STRUCTURE          0x6
#define TYPE_UNION              0x7
#define TYPE_ARRAY              0x8
#define TYPE_SEQUENCE           0x9

//these require the pack/unpack to recurse
//so it's a hint to those languages that can optimize in cases where
//recursion isn't necessary.
#define TYPE_COMPLEX_STRUCTURE  (0x10 | TYPE_STRUCTURE)
#define TYPE_COMPLEX_UNION      (0x10 | TYPE_UNION)
#define TYPE_COMPLEX_ARRAY      (0x10 | TYPE_ARRAY)
#define TYPE_COMPLEX_SEQUENCE   (0x10 | TYPE_SEQUENCE)


typedef struct Type Type;

#define INHERIT_TYPE\
   int32_t nativeSize;                /*in the simple case its the same as wire size and alignment*/\
   union {\
      struct {\
         const uintptr_t         p1;\
         const uintptr_t         p2;\
      } _cast;\
      struct {\
         uint32_t  iid;\
         uint32_t  bNotNil;\
      } object;\
      struct {\
         const Type  *arrayType;\
         int32_t      nItems;\
      } array;\
      struct {\
         const Type *seqType;\
         int32_t      nMaxLen;\
      } seqSimple; \
      struct {\
         uint32_t bFloating;\
         uint32_t bSigned;\
      } prim; \
      const SequenceType* seqComplex;\
      const UnionType  *unionType;\
      const StructType *structType;\
      int32_t         stringMaxLen;\
      uint8_t        bInterfaceNotNil;\
   } param;\
   uint8_t    type;\
   uint8_t    nativeAlignment\

typedef struct UnionType UnionType;
typedef struct StructType StructType;
typedef struct SequenceType SequenceType;
struct Type {
   INHERIT_TYPE;
};

struct SequenceType {
   const Type *         seqType;
   uint32_t               nMaxLen;
   uint32_t               inSize;
   uint32_t               routSizePrimIn;
   uint32_t               routSizePrimROut;
};

//unsigned char offset from the start of the case values for
//this unions case value array.  it MUST be aligned
//at the alignment requrements for the descriptor
//
//if negative it means that the unions cases are
//simple enumerators, so the value read from the descriptor
//can be used directly to find the correct case
typedef union CaseValuePtr CaseValuePtr;
union CaseValuePtr {
   const uint8_t*   value8s;
   const uint16_t*  value16s;
   const uint32_t*  value32s;
   const uint64_t*  value64s;
};

//these are only used in complex cases
//so I pulled them out of the type definition as references to make
//the type smaller
struct UnionType {
   const Type           *descriptor;
   uint32_t               nCases;
   const CaseValuePtr   caseValues;
   const Type * const   *cases;
   int32_t               inSize;
   int32_t               routSizePrimIn;
   int32_t               routSizePrimROut;
   uint8_t                inAlignment;
   uint8_t                routAlignmentPrimIn;
   uint8_t                routAlignmentPrimROut;
   uint8_t                inCaseAlignment;
   uint8_t                routCaseAlignmentPrimIn;
   uint8_t                routCaseAlignmentPrimROut;
   uint8_t                nativeCaseAlignment;
   uint8_t              bDefaultCase;
};

struct StructType {
   uint32_t               nMembers;
   const Type * const   *members;
   int32_t               inSize;
   int32_t               routSizePrimIn;
   int32_t               routSizePrimROut;
   uint8_t                inAlignment;
   uint8_t                routAlignmentPrimIn;
   uint8_t                routAlignmentPrimROut;
};

typedef struct Parameter Parameter;
struct Parameter {
   INHERIT_TYPE;
   uint8_t    mode;
   uint8_t  bNotNil;
};

#define SLIM_IFPTR32(is32,is64) (sizeof(uintptr_t) == 4 ? (is32) : (is64))
#define SLIM_SCALARS_IS_DYNAMIC(u) (((u) & 0x00ffffff) == 0x00ffffff)

typedef struct Method Method;
struct Method {
   uint32_t                    uScalars;            //no method index
   int32_t                     primInSize;
   int32_t                     primROutSize;
   int                         maxArgs;
   int                         numParams;
   const Parameter * const     *params;
   uint8_t                       primInAlignment;
   uint8_t                       primROutAlignment;
};

typedef struct Interface Interface;

struct Interface {
   int                            nMethods;
   const Method  * const          *methodArray;
   int                            nIIds;
   const uint32_t                   *iids;
   const uint16_t*                  methodStringArray;
   const uint16_t*                  methodStrings;
   const char*                    strings;
};


#endif //SLIM_H


#ifndef _APPS_STD_SLIM_H
#define _APPS_STD_SLIM_H
#include <stdint.h>

#ifndef __QAIC_SLIM
#define __QAIC_SLIM(ff) ff
#endif
#ifndef __QAIC_SLIM_EXPORT
#define __QAIC_SLIM_EXPORT
#endif

static const Type types[8];
static const Type* const typeArrays[15] = {&(types[2]),&(types[2]),&(types[2]),&(types[6]),&(types[6]),&(types[2]),&(types[2]),&(types[7]),&(types[7]),&(types[7]),&(types[7]),&(types[7]),&(types[7]),&(types[3]),&(types[4])};
static const StructType structTypes[3] = {{0x1,&(typeArrays[0]),0x8,0x0,0x8,0x8,0x1,0x8},{0x2,&(typeArrays[13]),0x104,0x0,0x104,0x4,0x1,0x4},{0xd,&(typeArrays[0]),0x60,0x0,0x60,0x8,0x1,0x8}};
static const SequenceType sequenceTypes[1] = {{&(types[1]),0x0,0x4,0x4,0x0}};
static const Type types[8] = {{0x1,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x1},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)0x0,0}}, 4,SLIM_IFPTR32(0x4,0x8)},{0x8,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x8},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4},{0xff,{{(const uintptr_t)&(types[5]),(const uintptr_t)0xff}}, 8,0x1},{0x1,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x1},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4},{0x8,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x8}};
static const Parameter parameters[18] = {{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)0x0,0}}, 4,SLIM_IFPTR32(0x4,0x8),0,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,0,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,3,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),0,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,0,0},{0x4,{{0,0}}, 3,0x4,0,0},{0x8,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x8,3,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)0x0,0}}, 4,SLIM_IFPTR32(0x4,0x8),3,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(sequenceTypes[0]),0}}, 25,SLIM_IFPTR32(0x4,0x8),3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,3,0},{0x2,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x2,3,0},{0x1,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x1,3,0},{0x8,{{(const uintptr_t)&(structTypes[0]),0}}, 6,0x8,3,0},{0x8,{{(const uintptr_t)&(structTypes[0]),0}}, 6,0x8,0,0},{0x104,{{(const uintptr_t)&(structTypes[1]),0}}, 6,0x4,3,0},{0x60,{{(const uintptr_t)&(structTypes[2]),0}}, 6,0x8,3,0}};
static const Parameter* const parameterArrays[52] = {(&(parameters[0])),(&(parameters[0])),(&(parameters[0])),(&(parameters[0])),(&(parameters[4])),(&(parameters[4])),(&(parameters[0])),(&(parameters[0])),(&(parameters[10])),(&(parameters[11])),(&(parameters[12])),(&(parameters[0])),(&(parameters[0])),(&(parameters[0])),(&(parameters[0])),(&(parameters[1])),(&(parameters[2])),(&(parameters[5])),(&(parameters[4])),(&(parameters[4])),(&(parameters[2])),(&(parameters[3])),(&(parameters[4])),(&(parameters[4])),(&(parameters[2])),(&(parameters[0])),(&(parameters[0])),(&(parameters[1])),(&(parameters[15])),(&(parameters[16])),(&(parameters[4])),(&(parameters[0])),(&(parameters[0])),(&(parameters[6])),(&(parameters[0])),(&(parameters[9])),(&(parameters[4])),(&(parameters[2])),(&(parameters[6])),(&(parameters[7])),(&(parameters[0])),(&(parameters[17])),(&(parameters[0])),(&(parameters[14])),(&(parameters[2])),(&(parameters[1])),(&(parameters[0])),(&(parameters[13])),(&(parameters[2])),(&(parameters[8])),(&(parameters[2])),(&(parameters[4]))};
static const Method methods[27] = {{REMOTE_SCALARS_MAKEX(0,0,0x3,0x1,0x0,0x0),0x8,0x4,3,3,(&(parameterArrays[13])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x3,0x1,0x0,0x0),0xc,0x4,4,4,(&(parameterArrays[24])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x4,0x0,1,1,(&(parameterArrays[16])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x2,0x0,0x0),0x8,0x8,6,4,(&(parameterArrays[20])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x8,0x8,5,4,(&(parameterArrays[16])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x2,0x0,0x0),0x8,0x4,5,3,(&(parameterArrays[20])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x0,0x0,0x0),0x8,0x0,3,2,(&(parameterArrays[16])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x1,0x0,0x0),0x4,0x4,2,2,(&(parameterArrays[50])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0xc,0x0,3,3,(&(parameterArrays[37])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x1,0x0,0x0),0x4,0x8,2,2,(&(parameterArrays[48])),0x4,0x8},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x0,0x0,0x0),0x4,0x0,1,1,(&(parameterArrays[0])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x2,0x0,0x0),0x8,0x4,5,3,(&(parameterArrays[34])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x3,0x0,0x0,0x0),0xc,0x0,3,3,(&(parameterArrays[31])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x5,0x1,0x0,0x0),0x10,0x4,5,5,(&(parameterArrays[11])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,255,255,15,15),0xc,0x6,7,5,(&(parameterArrays[6])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x4,0x1,2,2,(&(parameterArrays[46])),0x4,0x1},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x1,0x0,0x0),0x4,0x4,2,2,(&(parameterArrays[44])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x4,0x8,2,2,(&(parameterArrays[42])),0x4,0x8},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x8,0x0,1,1,(&(parameterArrays[28])),0x8,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x1,0x0,0x0),0x8,0x108,3,3,(&(parameterArrays[28])),0x8,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x0,0x0,0x0),0x8,0x0,2,2,(&(parameterArrays[32])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x4,0x60,2,2,(&(parameterArrays[40])),0x4,0x8},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x8,0x0,2,2,(&(parameterArrays[37])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x3,0x0,0x0,0x0),0x8,0x0,2,2,(&(parameterArrays[0])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x3,0x1,0x0,0x0),0x8,0x8,4,4,(&(parameterArrays[2])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x4,0x0,1,1,(&(parameterArrays[33])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x5,0x1,0x0,0x0),0x10,0x8,6,6,(&(parameterArrays[0])),0x4,0x4}};
static const Method* const methodArrays[37] = {&(methods[0]),&(methods[1]),&(methods[2]),&(methods[2]),&(methods[3]),&(methods[4]),&(methods[5]),&(methods[6]),&(methods[7]),&(methods[8]),&(methods[9]),&(methods[2]),&(methods[7]),&(methods[7]),&(methods[2]),&(methods[10]),&(methods[11]),&(methods[12]),&(methods[10]),&(methods[13]),&(methods[5]),&(methods[14]),&(methods[15]),&(methods[2]),&(methods[10]),&(methods[16]),&(methods[17]),&(methods[18]),&(methods[19]),&(methods[20]),&(methods[10]),&(methods[21]),&(methods[22]),&(methods[23]),&(methods[24]),&(methods[25]),&(methods[26])};
static const char strings[568] = "get_search_paths_with_env\0fopen_with_env_fd\0fdopen_decrypt\0fopen_with_env\0print_string\0bytesWritten\0fileExists\0maxPathLen\0envvarname\0fclose_fd\0ctimensec\0mtimensec\0atimensec\0valLenReq\0posLenReq\0bytesRead\0fopen_fd\0closedir\0numPaths\0unsetenv\0override\0clearerr\0newname\0oldname\0frename\0readdir\0opendir\0fremove\0fsetpos\0fgetpos\0freopen\0ftrunc\0dirent\0handle\0exists\0getenv\0ferror\0rewind\0whence\0offset\0fwrite\0fclose\0fflush\0ctime\0mtime\0atime\0nlink\0rmdir\0mkdir\0fsync\0paths\0fgets\0delim\0fseek\0ftell\0fread\0psout\0fopen\0size\0rdev\0stat\0path\0feof\0flen\0bEOF\0mode\0tsz\0ino\0val\0str\0buf\0sin\0";
static const uint16_t methodStrings[143] = {513,128,513,543,509,547,538,431,508,503,425,163,419,153,413,143,26,122,467,128,538,41,529,281,217,343,336,547,128,533,0,122,467,455,221,111,59,122,467,128,538,491,203,128,538,41,529,392,563,559,87,533,485,563,559,193,533,321,563,128,538,491,289,128,217,343,461,563,559,533,232,128,551,239,357,128,551,173,473,563,385,378,313,563,309,183,497,128,538,491,273,265,257,329,563,385,443,128,538,212,217,343,44,563,491,100,518,350,364,563,253,523,563,533,528,563,529,479,563,309,305,563,309,133,41,437,128,297,128,449,563,230,128,74,555,248,563,371,563,399,563,406,563};
static const uint16_t methodStringsArrays[37] = {86,57,141,139,52,47,82,120,117,78,114,137,111,108,135,133,74,70,131,36,66,30,105,129,127,102,62,99,23,96,125,0,93,90,42,123,16};
__QAIC_SLIM_EXPORT const Interface __QAIC_SLIM(apps_std_slim) = {37,&(methodArrays[0]),0,0,&(methodStringsArrays [0]),methodStrings,strings};
#endif //_APPS_STD_SLIM_H
extern int adsp_mmap_fd_getinfo(int, uint32_t *);
#ifdef __cplusplus
extern "C" {
#endif
_ATTRIBUTE_VISIBILITY uint32_t apps_std_skel_invoke_qaic_version = 10042;
static __inline int _skel_method(int (*_pfn)(const char*, const char*, const char*, const char*, int*, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   char* _in2[1] = {0};
   uint32_t _in2Len[1] = {0};
   char* _in3[1] = {0};
   uint32_t _in3Len[1] = {0};
   char* _in4[1] = {0};
   uint32_t _in4Len[1] = {0};
   uint32_t _rout5[1] = {0};
   uint32_t _rout6[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==5);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((5 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 20);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 8);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _COPY(_in2Len, 0, _primIn, 8, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in2Len[0]));
   _in2[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in2Len[0] > 0) && (_in2[0][(_in2Len[0] - 1)] == 0));
   _COPY(_in3Len, 0, _primIn, 12, 4);
   _ASSERT(_nErr, ((_praIn[2].buf.nLen / 1)) >= (size_t)(_in3Len[0]));
   _in3[0] = _praIn[2].buf.pv;
   _ASSERT(_nErr, (_in3Len[0] > 0) && (_in3[0][(_in3Len[0] - 1)] == 0));
   _COPY(_in4Len, 0, _primIn, 16, 4);
   _ASSERT(_nErr, ((_praIn[3].buf.nLen / 1)) >= (size_t)(_in4Len[0]));
   _in4[0] = _praIn[3].buf.pv;
   _ASSERT(_nErr, (_in4Len[0] > 0) && (_in4[0][(_in4Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in1, (const char*)*_in2, (const char*)*_in3, (const char*)*_in4, (int*)_rout5, (int*)_rout6));
   _COPY(_primROut, 0, _rout5, 0, 4);
   _COPY(_primROut, 4, _rout6, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_1(int (*_pfn)(int), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _in1[1] = {0};
   uint32_t* _primIn= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1, 0, _primIn, 4, 4);
   _TRY(_nErr, _pfn((int)*_in1));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_2(int (*_pfn)(const char*, const char*, int*, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   char* _in2[1] = {0};
   uint32_t _in2Len[1] = {0};
   uint32_t _rout3[1] = {0};
   uint32_t _rout4[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==3);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((3 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 8);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _COPY(_in2Len, 0, _primIn, 8, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in2Len[0]));
   _in2[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in2Len[0] > 0) && (_in2[0][(_in2Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in1, (const char*)*_in2, (int*)_rout3, (int*)_rout4));
   _COPY(_primROut, 0, _rout3, 0, 4);
   _COPY(_primROut, 4, _rout4, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_3(int (*_pfn)(const char*, const char*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   char* _in2[1] = {0};
   uint32_t _in2Len[1] = {0};
   uint32_t* _primIn= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==3);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((3 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _COPY(_in2Len, 0, _primIn, 8, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in2Len[0]));
   _in2[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in2Len[0] > 0) && (_in2[0][(_in2Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in1, (const char*)*_in2));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_4(int (*_pfn)(apps_std_FILE, int), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _in1[1] = {0};
   uint32_t _in2[1] = {0};
   uint32_t* _primIn= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1, 0, _primIn, 4, 4);
   _COPY(_in2, 0, _primIn, 8, 4);
   _TRY(_nErr, _pfn((apps_std_FILE)*_in1, (int)*_in2));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_5(int (*_pfn)(const char*, apps_std_STAT*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   uint64_t _rout2[12] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint64_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 96);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in1, (apps_std_STAT*)_rout2));
   _COPY(_primROut, 0, _rout2, 0, 96);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_invoke(uint32_t _mid, uint32_t _sc, remote_arg* _pra) {
   switch(_mid)
   {
      case 31:
      return _skel_method_5(__QAIC_IMPL(apps_std_stat), _sc, _pra);
      case 32:
      return _skel_method_4(__QAIC_IMPL(apps_std_ftrunc), _sc, _pra);
      case 33:
      return _skel_method_3(__QAIC_IMPL(apps_std_frename), _sc, _pra);
      case 34:
      return _skel_method_2(__QAIC_IMPL(apps_std_fopen_fd), _sc, _pra);
      case 35:
      return _skel_method_1(__QAIC_IMPL(apps_std_fclose_fd), _sc, _pra);
      case 36:
      return _skel_method(__QAIC_IMPL(apps_std_fopen_with_env_fd), _sc, _pra);
   }
   return AEE_EUNSUPPORTED;
}
static __inline int _skel_method_6(int (*_pfn)(const char*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   uint32_t* _primIn= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in0));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_7(int (*_pfn)(const char*, int), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   uint32_t _in1[1] = {0};
   uint32_t* _primIn= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _COPY(_in1, 0, _primIn, 4, 4);
   _TRY(_nErr, _pfn((const char*)*_in0, (int)*_in1));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_8(int (*_pfn)(const apps_std_DIR*, apps_std_DIRENT*, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint64_t _in0[1] = {0};
   uint32_t _rout1[65] = {0};
   uint32_t _rout2[1] = {0};
   uint64_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 264);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 8);
   _TRY(_nErr, _pfn((const apps_std_DIR*)_in0, (apps_std_DIRENT*)_rout1, (int*)_rout2));
   _COPY(_primROut, 0, _rout1, 0, 260);
   _COPY(_primROut, 260, _rout2, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_9(int (*_pfn)(const apps_std_DIR*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint64_t _in0[1] = {0};
   uint64_t* _primIn= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 8);
   _TRY(_nErr, _pfn((const apps_std_DIR*)_in0));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_10(int (*_pfn)(const char*, apps_std_DIR*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   uint64_t _rout1[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint64_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 8);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in0, (apps_std_DIR*)_rout1));
   _COPY(_primROut, 0, _rout1, 0, 8);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_11(int (*_pfn)(apps_std_FILE, apps_std_FILE*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _rout1[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (apps_std_FILE*)_rout1));
   _COPY(_primROut, 0, _rout1, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_12(int (*_pfn)(apps_std_FILE), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t* _primIn= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_13(int (*_pfn)(const char*, bool*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   uint8_t _rout1[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint8_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 1);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in0, (bool*)_rout1));
   _COPY(_primROut, 0, _rout1, 0, 1);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_pack(_ATTRIBUTE_UNUSED remote_arg* _praROutPost, _ATTRIBUTE_UNUSED remote_arg* _ppraROutPost[1], _ATTRIBUTE_UNUSED void* _primROut, _ATTRIBUTE_UNUSED char* _rout0[1], _ATTRIBUTE_UNUSED uint32_t _rout0Len[1]) {
   int _nErr = 0;
   remote_arg* _praROutPostStart = _praROutPost;
   remote_arg** _ppraROutPostStart = _ppraROutPost;
   _ppraROutPost = &_praROutPost;
   _ppraROutPostStart[0] += (_praROutPost - _praROutPostStart) +1;
   return _nErr;
}
static __inline int _skel_unpack(_ATTRIBUTE_UNUSED _allocator* _al, _ATTRIBUTE_UNUSED remote_arg* _praIn, _ATTRIBUTE_UNUSED remote_arg* _ppraIn[1], _ATTRIBUTE_UNUSED remote_arg* _praROut, _ATTRIBUTE_UNUSED remote_arg* _ppraROut[1], _ATTRIBUTE_UNUSED remote_arg* _praHIn, _ATTRIBUTE_UNUSED remote_arg* _ppraHIn[1], _ATTRIBUTE_UNUSED remote_arg* _praHROut, _ATTRIBUTE_UNUSED remote_arg* _ppraHROut[1], _ATTRIBUTE_UNUSED void* _primIn, _ATTRIBUTE_UNUSED void* _primROut, _ATTRIBUTE_UNUSED char* _rout0[1], _ATTRIBUTE_UNUSED uint32_t _rout0Len[1]) {
   int _nErr = 0;
   remote_arg* _praInStart = _praIn;
   remote_arg** _ppraInStart = _ppraIn;
   remote_arg* _praROutStart = _praROut;
   remote_arg** _ppraROutStart = _ppraROut;
   _ppraIn = &_praIn;
   _ppraROut = &_praROut;
   _COPY(_rout0Len, 0, _primIn, 0, 4);
   _ASSERT(_nErr, ((_praROut[0].buf.nLen / 1)) >= (size_t)(_rout0Len[0]));
   _rout0[0] = _praROut[0].buf.pv;
   _ppraInStart[0] += (_praIn - _praInStart) + 0;
   _ppraROutStart[0] += (_praROut - _praROutStart) +1;
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_14(int (*_pfn)(const char*, const char*, _cstring1_t*, int, uint32_t*, uint16_t*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   void* _rout2[1] = {0};
   uint32_t _rout2Len[1] = {0};
   uint32_t _rout3[1] = {0};
   uint16_t _rout4[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   int _numInH[1] = {0};
   int _numROut[1] = {0};
   remote_arg* _praIn = 0;
   remote_arg* _praROut = 0;
   remote_arg* _praROutPost = 0;
   remote_arg** _ppraROutPost = &_praROutPost;
   _allocator _al[1] = {{0}};
   remote_arg** _ppraIn = &_praIn;
   remote_arg** _ppraROut = &_praROut;
   remote_arg* _praHIn = 0;
   remote_arg** _ppraHIn = &_praHIn;
   remote_arg* _praHROut = 0;
   remote_arg** _ppraHROut = &_praHROut;
   char* _seq_primIn2  = 0;
   char* _seq_nat2  = 0;
   int _ii = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)>=1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)>=1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((4 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 6);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _numInH[0] = REMOTE_SCALARS_INHANDLES(_sc);
   _numROut[0] = REMOTE_SCALARS_OUTBUFS(_sc);
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 1);
   _praROutPost = _praROut;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _COPY(_rout2Len, 0, _primIn, 8, 4);
   _allocator_init(_al, 0, 0);
   if(_praHIn == 0)
   {
      _praHIn = ((_praROut + _numROut[0]) + 1);
   }
   if(_praHROut == 0)
      (_praHROut = _praHIn + _numInH[0] + 0);
   _ASSERT(_nErr, ((_praIn[2].buf.nLen / 4)) >= (size_t)(_rout2Len[0]));
   _ALLOCATE(_nErr, _al, (_rout2Len[0] * SLIM_IFPTR32(8, 16)), SLIM_IFPTR32(4, 8), _rout2[0]);
   for(_ii = 0, _seq_primIn2 = (char*)_praIn[2].buf.pv, _seq_nat2 = (char*)_rout2[0];_ii < (int)_rout2Len[0];++_ii, _seq_primIn2 = (_seq_primIn2 + 4), _seq_nat2 = (_seq_nat2 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_unpack(_al, (_praIn + 3), _ppraIn, (_praROut + 0), _ppraROut, _praHIn, _ppraHIn, _praHROut, _ppraHROut, _seq_primIn2, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat2)[0]), (char**)&(((uint64_t*)_seq_nat2)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat2)[1]), (uint32_t*)&(((uint32_t*)_seq_nat2)[2]))));
   }
   _TRY(_nErr, _pfn((const char*)*_in0, (const char*)*_in1, (_cstring1_t*)*_rout2, (int)*_rout2Len, (uint32_t*)_rout3, (uint16_t*)_rout4));
   for(_ii = 0, _seq_nat2 = (char*)_rout2[0];_ii < (int)_rout2Len[0];++_ii, _seq_nat2 = (_seq_nat2 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_pack((_praROutPost + 0), _ppraROutPost, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat2)[0]), (char**)&(((uint64_t*)_seq_nat2)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat2)[1]), (uint32_t*)&(((uint32_t*)_seq_nat2)[2]))));
   }
   _COPY(_primROut, 0, _rout3, 0, 4);
   _COPY(_primROut, 4, _rout4, 0, 2);
   _CATCH(_nErr) {}
   _allocator_deinit(_al);
   return _nErr;
}
static __inline int _skel_method_15(int (*_pfn)(apps_std_FILE, unsigned char*, int, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _rout1[1] = {0};
   uint32_t _rout1Len[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   remote_arg* _praROut = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 2) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_rout1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 1);
   _ASSERT(_nErr, ((_praROut[0].buf.nLen / 1)) >= (size_t)(_rout1Len[0]));
   _rout1[0] = _praROut[0].buf.pv;
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (unsigned char*)*_rout1, (int)*_rout1Len, (int*)_rout2));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_16(int (*_pfn)(const char*, const char*, const char*, const char*, apps_std_FILE*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   char* _in2[1] = {0};
   uint32_t _in2Len[1] = {0};
   char* _in3[1] = {0};
   uint32_t _in3Len[1] = {0};
   uint32_t _rout4[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==5);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((5 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 16);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _COPY(_in2Len, 0, _primIn, 8, 4);
   _ASSERT(_nErr, ((_praIn[2].buf.nLen / 1)) >= (size_t)(_in2Len[0]));
   _in2[0] = _praIn[2].buf.pv;
   _ASSERT(_nErr, (_in2Len[0] > 0) && (_in2[0][(_in2Len[0] - 1)] == 0));
   _COPY(_in3Len, 0, _primIn, 12, 4);
   _ASSERT(_nErr, ((_praIn[3].buf.nLen / 1)) >= (size_t)(_in3Len[0]));
   _in3[0] = _praIn[3].buf.pv;
   _ASSERT(_nErr, (_in3Len[0] > 0) && (_in3[0][(_in3Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in0, (const char*)*_in1, (const char*)*_in2, (const char*)*_in3, (apps_std_FILE*)_rout4));
   _COPY(_primROut, 0, _rout4, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_17(int (*_pfn)(const char*, const char*, int), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   uint32_t _in2[1] = {0};
   uint32_t* _primIn= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==3);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((3 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _COPY(_in2, 0, _primIn, 8, 4);
   _TRY(_nErr, _pfn((const char*)*_in0, (const char*)*_in1, (int)*_in2));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_18(int (*_pfn)(const char*, char*, int, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   char* _rout1[1] = {0};
   uint32_t _rout1Len[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   remote_arg* _praROut = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 2) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _COPY(_rout1Len, 0, _primIn, 4, 4);
   _praROut = (_praIn + _numIn[0] + 1);
   _ASSERT(_nErr, ((_praROut[0].buf.nLen / 1)) >= (size_t)(_rout1Len[0]));
   _rout1[0] = _praROut[0].buf.pv;
   _TRY(_nErr, _pfn((const char*)*_in0, (char*)*_rout1, (int)*_rout1Len, (int*)_rout2));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_19(int (*_pfn)(apps_std_FILE, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _rout1[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (int*)_rout1));
   _COPY(_primROut, 0, _rout1, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_20(int (*_pfn)(apps_std_FILE, uint64_t*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint64_t _rout1[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint64_t* _primROut= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 8);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (uint64_t*)_rout1));
   _COPY(_primROut, 0, _rout1, 0, 8);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_21(int (*_pfn)(apps_std_FILE, int, apps_std_SEEK), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _in1[1] = {0};
   uint32_t _in2[1] = {0};
   uint32_t* _primIn= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1, 0, _primIn, 4, 4);
   _COPY(_in2, 0, _primIn, 8, 4);
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (int)*_in1, (apps_std_SEEK)*_in2));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_22(int (*_pfn)(apps_std_FILE, const unsigned char*, int), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   uint32_t* _primIn= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 0) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (const unsigned char*)*_in1, (int)*_in1Len));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_23(int (*_pfn)(apps_std_FILE, const unsigned char*, int, int*, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t _rout3[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((2 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 8);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (const unsigned char*)*_in1, (int)*_in1Len, (int*)_rout2, (int*)_rout3));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _COPY(_primROut, 4, _rout3, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_24(int (*_pfn)(apps_std_FILE, unsigned char*, int, int*, int*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _rout1[1] = {0};
   uint32_t _rout1Len[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t _rout3[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   remote_arg* _praROut = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==2);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((1 + 2) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 8);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_rout1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 1);
   _ASSERT(_nErr, ((_praROut[0].buf.nLen / 1)) >= (size_t)(_rout1Len[0]));
   _rout1[0] = _praROut[0].buf.pv;
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (unsigned char*)*_rout1, (int)*_rout1Len, (int*)_rout2, (int*)_rout3));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _COPY(_primROut, 4, _rout3, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_25(int (*_pfn)(apps_std_FILE, const char*, const char*, apps_std_FILE*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   char* _in2[1] = {0};
   uint32_t _in2Len[1] = {0};
   uint32_t _rout3[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==3);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((3 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _COPY(_in2Len, 0, _primIn, 8, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in2Len[0]));
   _in2[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in2Len[0] > 0) && (_in2[0][(_in2Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((apps_std_FILE)*_in0, (const char*)*_in1, (const char*)*_in2, (apps_std_FILE*)_rout3));
   _COPY(_primROut, 0, _rout3, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_26(int (*_pfn)(const char*, const char*, apps_std_FILE*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   char* _in1[1] = {0};
   uint32_t _in1Len[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==3);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _ASSERT(_nErr, (_pra + ((3 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _ASSERT(_nErr, ((_praIn[1].buf.nLen / 1)) >= (size_t)(_in1Len[0]));
   _in1[0] = _praIn[1].buf.pv;
   _ASSERT(_nErr, (_in1Len[0] > 0) && (_in1[0][(_in1Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in0, (const char*)*_in1, (apps_std_FILE*)_rout2));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_SKEL_EXPORT int __QAIC_SKEL(apps_std_skel_invoke)(uint32_t _sc, remote_arg* _pra) __QAIC_SKEL_ATTRIBUTE {
   switch(REMOTE_SCALARS_METHOD(_sc))
   {
      case 0:
      return _skel_method_26(__QAIC_IMPL(apps_std_fopen), _sc, _pra);
      case 1:
      return _skel_method_25(__QAIC_IMPL(apps_std_freopen), _sc, _pra);
      case 2:
      return _skel_method_12(__QAIC_IMPL(apps_std_fflush), _sc, _pra);
      case 3:
      return _skel_method_12(__QAIC_IMPL(apps_std_fclose), _sc, _pra);
      case 4:
      return _skel_method_24(__QAIC_IMPL(apps_std_fread), _sc, _pra);
      case 5:
      return _skel_method_23(__QAIC_IMPL(apps_std_fwrite), _sc, _pra);
      case 6:
      return _skel_method_15(__QAIC_IMPL(apps_std_fgetpos), _sc, _pra);
      case 7:
      return _skel_method_22(__QAIC_IMPL(apps_std_fsetpos), _sc, _pra);
      case 8:
      return _skel_method_19(__QAIC_IMPL(apps_std_ftell), _sc, _pra);
      case 9:
      return _skel_method_21(__QAIC_IMPL(apps_std_fseek), _sc, _pra);
      case 10:
      return _skel_method_20(__QAIC_IMPL(apps_std_flen), _sc, _pra);
      case 11:
      return _skel_method_12(__QAIC_IMPL(apps_std_rewind), _sc, _pra);
      case 12:
      return _skel_method_19(__QAIC_IMPL(apps_std_feof), _sc, _pra);
      case 13:
      return _skel_method_19(__QAIC_IMPL(apps_std_ferror), _sc, _pra);
      case 14:
      return _skel_method_12(__QAIC_IMPL(apps_std_clearerr), _sc, _pra);
      case 15:
      return _skel_method_6(__QAIC_IMPL(apps_std_print_string), _sc, _pra);
      case 16:
      return _skel_method_18(__QAIC_IMPL(apps_std_getenv), _sc, _pra);
      case 17:
      return _skel_method_17(__QAIC_IMPL(apps_std_setenv), _sc, _pra);
      case 18:
      return _skel_method_6(__QAIC_IMPL(apps_std_unsetenv), _sc, _pra);
      case 19:
      return _skel_method_16(__QAIC_IMPL(apps_std_fopen_with_env), _sc, _pra);
      case 20:
      return _skel_method_15(__QAIC_IMPL(apps_std_fgets), _sc, _pra);
      case 21:
      return _skel_method_14(__QAIC_IMPL(apps_std_get_search_paths_with_env), _sc, _pra);
      case 22:
      return _skel_method_13(__QAIC_IMPL(apps_std_fileExists), _sc, _pra);
      case 23:
      return _skel_method_12(__QAIC_IMPL(apps_std_fsync), _sc, _pra);
      case 24:
      return _skel_method_6(__QAIC_IMPL(apps_std_fremove), _sc, _pra);
      case 25:
      return _skel_method_11(__QAIC_IMPL(apps_std_fdopen_decrypt), _sc, _pra);
      case 26:
      return _skel_method_10(__QAIC_IMPL(apps_std_opendir), _sc, _pra);
      case 27:
      return _skel_method_9(__QAIC_IMPL(apps_std_closedir), _sc, _pra);
      case 28:
      return _skel_method_8(__QAIC_IMPL(apps_std_readdir), _sc, _pra);
      case 29:
      return _skel_method_7(__QAIC_IMPL(apps_std_mkdir), _sc, _pra);
      case 30:
      return _skel_method_6(__QAIC_IMPL(apps_std_rmdir), _sc, _pra);
      case 31:
      {
         uint32_t* _mid;
         if(REMOTE_SCALARS_INBUFS(_sc) < 1 || _pra[0].buf.nLen < 4) { return AEE_EBADPARM; }
         _mid = (uint32_t*)_pra[0].buf.pv;
         return _skel_invoke(*_mid, _sc, _pra);
      }
   }
   return AEE_EUNSUPPORTED;
}
#ifdef __cplusplus
}
#endif
#endif //_APPS_STD_SKEL_H
