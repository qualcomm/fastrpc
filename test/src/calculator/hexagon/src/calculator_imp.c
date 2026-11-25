// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "HAP_farf.h"
#include "calculator.h"

int calculator_open(const char*uri, remote_handle64* handle) {
   void *tptr = NULL;
  /* can be any value or ignored, rpc layer doesn't care
   * also ok
   * *handle = 0;
   * *handle = 0xdeadc0de;
   */
   tptr = (void *)malloc(1);
   *handle = (remote_handle64)tptr;
   return 0;
}

/**
 * @param handle, the value returned by open
 * @retval, 0 for success, should always succeed
 */
int calculator_close(remote_handle64 handle) {
   if (handle)
      free((void*)handle);
   return 0;
}

int calculator_sum(remote_handle64 h, const int* vec, int vecLen, int64_t* res)
{
  int ii = 0;
  *res = 0;
  for (ii = 0; ii < vecLen; ++ii)
    *res = *res + vec[ii];

  FARF(ALWAYS, "===============     DSP: sum result %" PRId64 " ===============", *res);
  return 0;
}

int calculator_max(remote_handle64 h, const int* vec, int vecLen, int* res) {
  int ii = 0;
  int max = 0;

  for (ii = 0; ii < vecLen; ++ii)
    max = (vec[ii] > max) ? vec[ii] : max;

  *res = max;
  FARF(ALWAYS, "===============     DSP: maximum result %d ==============", *res);
  return 0;
}
