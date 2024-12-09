// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif

#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "adspmsgd_adsp1.h"
#include "adspmsgd_internal.h"
#include "fastrpc_internal.h"
#include "rpcmem.h"
#include "verify.h"


#define BUFFER_SIZE 256
#define DEFAULT_MEMORY_SIZE 256 * 1024
#include "fastrpc_common.h"


extern char *fastrpc_config_get_runtime_farf_file(void);

DECLARE_HASH_TABLE(msgd_handle, msgd);

int fastrpc_adspmsgd_init(void) {
	HASH_TABLE_INIT(msgd);
	return 0;
}

void fastrpc_adspmsgd_deinit(void) {
	HASH_TABLE_CLEANUP(msgd);
}

void readMessage(int domain) {
  int index = 0;
  msgd *msgd_handle = NULL;
  unsigned int lreadIndex = 0;

  GET_HASH_NODE(msgd, domain, msgd_handle);
  if (!msgd_handle) {
    FARF(ERROR, "Error: %s: unable to find hash node for domain %d",
          __func__, domain);
    return;
  }
  lreadIndex = msgd_handle->readIndex;
  memset(msgd_handle->message, 0, BUFFER_SIZE);
  if (msgd_handle->readIndex >= msgd_handle->bufferSize) {
    lreadIndex = msgd_handle->readIndex = 0;
  }
  while ((lreadIndex != *(msgd_handle->currentIndex)) &&
         (msgd_handle->headPtr[lreadIndex] == '\0')) {
    lreadIndex++;
    if (lreadIndex >= msgd_handle->bufferSize) {
      lreadIndex = 0;
    }
  }
  while (msgd_handle->headPtr[lreadIndex] != '\0') {
    *(msgd_handle->message + index) = msgd_handle->headPtr[lreadIndex];
    index++;
    lreadIndex++;
    if (lreadIndex >= msgd_handle->bufferSize) {
      lreadIndex = 0;
    }
    if (index >= BUFFER_SIZE) {
      break;
    }
  }
  if (*(msgd_handle->message + 0) != '\0') {
    if (msgd_handle->log_file_fd != NULL) {
      fputs(msgd_handle->message, msgd_handle->log_file_fd);
      fputs("\n", msgd_handle->log_file_fd);
    }
    adspmsgd_log_message("%s", msgd_handle->message);
    msgd_handle->readIndex = lreadIndex + 1;
  }
}
// function to flush messages to logcat
static void *adspmsgd_reader(void *arg) {
  remote_handle64 handle = (remote_handle64)arg;
  int domain = DEFAULT_DOMAIN_ID;
  int nErr = AEE_SUCCESS;
  unsigned long long bytesToRead = 0;
  msgd *msgd_handle = NULL;

  FARF(RUNTIME_RPC_HIGH, "%s thread starting for domain %d\n", __func__,
       domain);
  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(handle, &domain)));
  GET_HASH_NODE(msgd, domain, msgd_handle);
  if (!msgd_handle) {
    FARF(ERROR, "Error: %s: unable to find hash node for domain %d",
          __func__, domain);
    return (void*)(-1);
  }
  msgd_handle->threadStop = 0;
  while (!(msgd_handle->threadStop)) {
    if (*(msgd_handle->currentIndex) == msgd_handle->readIndex) {
      // wait till messages are ready from DSP
      adspmsgd_adsp1_wait(handle, &bytesToRead);
    }
    readMessage(domain);
  }
  while (*(msgd_handle->currentIndex) != msgd_handle->readIndex) {
    readMessage(domain);
  }
  msgd_handle->threadStop = -1;
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s thread of domain %d for handle 0x%lx "
                   "exiting (errno %s)\n",
                   nErr, __func__, domain, handle, strerror(errno));
  } else {
    FARF(ALWAYS, "%s thread exiting for domain %d\n", __func__, domain);
  }
  return (void *)(uintptr_t)nErr;
}
// function to create msgd shared buffer and logger thread to flush messages to
// logcat
int adspmsgd_init(remote_handle64 handle, int filter) {
  int nErr = AEE_SUCCESS;
  int domain = DEFAULT_DOMAIN_ID;
  unsigned long long vapps = 0;
  errno = 0;
  char *filename = NULL;
  msgd *msgd_handle = NULL;

  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(handle, &domain)));
  GET_HASH_NODE(msgd, domain, msgd_handle);
  if (!msgd_handle)
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(msgd, domain, msgd_handle);
  if (msgd_handle->thread_running) {
    msgd_handle->threadStop = 1;
    adspmsgd_adsp1_deinit(handle);
    adspmsgd_stop(domain);
  }
  msgd_handle->message = NULL;
  // If daemon already running, adspmsgd_adsp1_init3 already happened, vapps
  // return NULL
  VERIFY(AEE_SUCCESS ==
         (nErr = adspmsgd_adsp1_init3(handle, 0, RPCMEM_HEAP_DEFAULT, filter,
                                      DEFAULT_MEMORY_SIZE, &vapps)));
  VERIFYC(vapps, AEE_EBADITEM);
  msgd_handle->headPtr = (char *)vapps;
  msgd_handle->bufferSize =
      DEFAULT_MEMORY_SIZE - sizeof(*(msgd_handle->currentIndex));
  msgd_handle->readIndex = 0;
  msgd_handle->currentIndex = (unsigned int *)(vapps + msgd_handle->bufferSize);
  VERIFYC(0 != (msgd_handle->message = calloc(1, BUFFER_SIZE)), AEE_ENOMEMORY);
  VERIFY(AEE_SUCCESS ==
         (nErr = pthread_create(&(msgd_handle->msgreader_thread), NULL,
                                adspmsgd_reader, (void *)handle)));
  msgd_handle->thread_running = true;
  filename = fastrpc_config_get_runtime_farf_file();
  if (filename) { // Check "Runtime farf logs collection into a file" is enabled
    msgd_handle->log_file_fd = fopen(filename, "w");
    if (msgd_handle->log_file_fd == NULL) {
      VERIFY_EPRINTF("Error 0x%x: %s failed to collect runtime farf logs into "
                     "file %s with errno %s\n",
                     nErr, __func__, filename, strerror(errno));
    }
  }
bail:
  if ((nErr != AEE_SUCCESS) &&
      (nErr != (int)(AEE_EUNSUPPORTED + DSP_AEE_EOFFSET))) {
    VERIFY_EPRINTF(
        "Error 0x%x: %s failed for handle 0x%lx filter %d with errno %s\n",
        nErr, __func__, handle, filter, strerror(errno));
    if (msgd_handle->message) {
      free(msgd_handle->message);
      msgd_handle->message = NULL;
    }
    adspmsgd_adsp1_deinit(handle);
  }
  return nErr;
}

// function to stop logger thread
void adspmsgd_stop(int dom)
{
  msgd *msgd_handle = NULL;

  GET_HASH_NODE(msgd, dom, msgd_handle);
    if (!msgd_handle) {
      FARF(ERROR, "Error: %s: unable to find hash node for domain %d", 
          __func__, dom);
      return;
    }
  if (!msgd_handle->thread_running) {
    return;
  }
  if (msgd_handle->threadStop == 0) {
    msgd_handle->threadStop = 1;
    while (msgd_handle->threadStop != -1);
    pthread_join(msgd_handle->msgreader_thread, NULL);
    msgd_handle->msgreader_thread = 0;
    msgd_handle->thread_running = false;
    if (msgd_handle->message) {
      free(msgd_handle->message);
      msgd_handle->message = NULL;
    }
    if (msgd_handle->log_file_fd) {
      fclose(msgd_handle->log_file_fd);
    }
  }
}
