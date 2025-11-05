// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _ADSP_PERF_H
#define _ADSP_PERF_H
#include <AEEStdDef.h>
#include <remote.h>

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
#ifdef __cplusplus
extern "C" {
#endif
/**
 * Interface for querying the adsp for counter data
 * For example, to enable all the perf numbers:
 * 
 *     int perf_on(void) {
 *       int nErr = 0;
 *       int numKeys = 0, maxLen = 0, ii;
 *       char keys[512];
 *       char* buf = &keys[0];
 *       VERIFY(0 == adsp_perf_get_keys(keys, 512, &maxLen, &numKeys)); 
 *       assert(maxLen < 512);
 *       for(ii = 0; ii < numKeys; ++ii) {
 *          char* name = buf;
 *          buf += strlen(name) + 1;
 *          printf("perf on: %s\n", name);
 *          VERIFY(0 == adsp_perf_enable(ii));
 *       }
 *    bail:
 *       return nErr;
 *    }
 *
 * To read all the results:
 *
 *    int rpcperf_perf_result(void) {
 *       int nErr = 0;
 *       int numKeys, maxLen, ii;
 *       char keys[512];
 *       char* buf = &keys[0];
 *       long long usecs[16];
 *       VERIFY(0 == adsp_perf_get_keys(keys, 512, &maxLen, &numKeys)); 
 *       printf("perf keys: %d\n", numKeys);
 *       VERIFY(0 == adsp_perf_get_usecs(usecs, 16));
 *       assert(maxLen < 512);
 *       assert(numKeys < 16);
 *       for(ii = 0; ii < numKeys; ++ii) {
 *          char* name = buf;
 *          buf += strlen(name) + 1;
 *          printf("perf result: %s %lld\n", name, usecs[ii]);
 *       }
 *    bail:
 *       return nErr;
 *    }
 */
#define _const_adsp_perf_handle 6
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_perf_enable)(int ix) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_perf_get_usecs)(int64_t* dst, int dstLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(adsp_perf_get_keys)(char* keys, int keysLen, int* maxLen, int* numKeys) __QAIC_HEADER_ATTRIBUTE;
#ifdef __cplusplus
}
#endif
#endif //_ADSP_PERF_H
