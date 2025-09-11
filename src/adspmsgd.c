// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

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

msgd androidmsgd_handle[NUM_DOMAINS_EXTEND];

void
readMessage(int domain)
{
	int index = 0;
	msgd *msgd_handle = &androidmsgd_handle[domain];
	unsigned long long lreadIndex = msgd_handle->readIndex;
	memset(msgd_handle->message, 0, BUFFER_SIZE);
	if(msgd_handle->readIndex >= msgd_handle->bufferSize) {
		lreadIndex = msgd_handle->readIndex = 0;
	}
	while((lreadIndex != *(msgd_handle->currentIndex))
	      && (msgd_handle->headPtr[lreadIndex] == '\0')) {
		lreadIndex++;
		if(lreadIndex >= msgd_handle->bufferSize) {
			lreadIndex = 0;
		}
	}
	while(msgd_handle->headPtr[lreadIndex] != '\0') {
		*(msgd_handle->message + index)
		    = msgd_handle->headPtr[lreadIndex];
		index++;
		lreadIndex++;
		if(lreadIndex >= msgd_handle->bufferSize) {
			lreadIndex = 0;
		}
		if(index >= BUFFER_SIZE) {
			break;
		}
	}
	if(*(msgd_handle->message + 0) != '\0') {
		if(msgd_handle->log_file_fd != NULL) {
			fputs(msgd_handle->message, msgd_handle->log_file_fd);
			fputs("\n", msgd_handle->log_file_fd);
		}
		adspmsgd_log_message("%s", msgd_handle->message);
		msgd_handle->readIndex = lreadIndex + 1;
	}
}
// function to flush messages to logcat
static void *
adspmsgd_reader(void *arg)
{
	remote_handle64 handle = (remote_handle64)arg;
	int domain = DEFAULT_DOMAIN_ID;
	int nErr = AEE_SUCCESS;
	uint64_t bytesToRead = 0;
	msgd *msgd_handle;

	FARF(RUNTIME_RPC_HIGH, "%s thread starting for domain %d\n", __func__,
	     domain);
	VERIFY(AEE_SUCCESS
	       == (nErr = get_domain_from_handle(handle, &domain)));
	msgd_handle = &androidmsgd_handle[domain];
	msgd_handle->threadStop = 0;
	while(!(msgd_handle->threadStop)) {
		if(*(msgd_handle->currentIndex) == msgd_handle->readIndex) {
			// wait till messages are ready from DSP
			adspmsgd_adsp1_wait(handle, &bytesToRead);
		}
		readMessage(domain);
	}
	while(*(msgd_handle->currentIndex) != msgd_handle->readIndex) {
		readMessage(domain);
	}
	msgd_handle->threadStop = -1;
bail:
	if(nErr != AEE_SUCCESS) {
		VERIFY_EPRINTF(
		    "Error 0x%x: %s thread of domain %d for handle 0x%lx "
		    "exiting (errno %s)\n",
		    nErr, __func__, domain, handle, strerror(errno));
	} else {
		FARF(ALWAYS, "%s thread exiting for domain %d\n", __func__,
		     domain);
	}
	return (void *)(uintptr_t)nErr;
}
// function to create msgd shared buffer and logger thread to flush messages to
// logcat
int
adspmsgd_init(remote_handle64 handle, int filter)
{
	int nErr = AEE_SUCCESS;
	int domain = DEFAULT_DOMAIN_ID;
	uint64_t vapps = 0;
	errno = 0;
	char *filename = NULL;
	msgd *msgd_handle = &androidmsgd_handle[DEFAULT_DOMAIN_ID];
	VERIFY(AEE_SUCCESS
	       == (nErr = get_domain_from_handle(handle, &domain)));
	msgd_handle = &androidmsgd_handle[domain];
	if(msgd_handle->thread_running) {
		androidmsgd_handle[domain].threadStop = 1;
		adspmsgd_adsp1_deinit(handle);
		adspmsgd_stop(domain);
	}
	msgd_handle->message = NULL;
	// If daemon already running, adspmsgd_adsp1_init3 already happened,
	// vapps return NULL
	VERIFY(AEE_SUCCESS
	       == (nErr = adspmsgd_adsp1_init3(handle, 0, RPCMEM_HEAP_DEFAULT,
	                                       filter, DEFAULT_MEMORY_SIZE,
	                                       &vapps)));
	VERIFYC(vapps, AEE_EBADITEM);
	msgd_handle->headPtr = (char *)vapps;
	msgd_handle->bufferSize
	    = DEFAULT_MEMORY_SIZE - sizeof(*(msgd_handle->currentIndex));
	msgd_handle->readIndex = 0;
	msgd_handle->currentIndex
	    = (unsigned int *)(vapps + msgd_handle->bufferSize);
	VERIFYC(0 != (msgd_handle->message = calloc(1, BUFFER_SIZE)),
	        AEE_ENOMEMORY);
	VERIFY(
	    AEE_SUCCESS
	    == (nErr = pthread_create(&(msgd_handle->msgreader_thread), NULL,
	                              adspmsgd_reader, (void *)handle)));
	msgd_handle->thread_running = true;
	filename = fastrpc_config_get_runtime_farf_file();
	if(filename) { // Check "Runtime farf logs collection into a file" is
		       // enabled
		msgd_handle->log_file_fd = fopen(filename, "w");
		if(msgd_handle->log_file_fd == NULL) {
			VERIFY_EPRINTF("Error 0x%x: %s failed to collect "
			               "runtime farf logs into "
			               "file %s with errno %s\n",
			               nErr, __func__, filename,
			               strerror(errno));
		}
	}
bail:
	if((nErr != AEE_SUCCESS)
	   && (nErr != (int)(AEE_EUNSUPPORTED + DSP_AEE_EOFFSET))) {
		VERIFY_EPRINTF("Error 0x%x: %s failed for handle 0x%lx filter "
		               "%d with errno %s\n",
		               nErr, __func__, handle, filter,
		               strerror(errno));
		if(msgd_handle->message) {
			free(msgd_handle->message);
			msgd_handle->message = NULL;
		}
		adspmsgd_adsp1_deinit(handle);
	}
	return nErr;
}

// function to stop logger thread
void
adspmsgd_stop(int dom)
{
	if(!androidmsgd_handle[dom].thread_running)
		return;
	if(androidmsgd_handle[dom].threadStop == 0) {
		androidmsgd_handle[dom].threadStop = 1;
		while(androidmsgd_handle[dom].threadStop != -1)
			;
		pthread_join(androidmsgd_handle[dom].msgreader_thread, NULL);
		androidmsgd_handle[dom].msgreader_thread = 0;
		androidmsgd_handle[dom].thread_running = false;
		if(androidmsgd_handle[dom].message) {
			free(androidmsgd_handle[dom].message);
			androidmsgd_handle[dom].message = NULL;
		}
		if(androidmsgd_handle[dom].log_file_fd) {
			fclose(androidmsgd_handle[dom].log_file_fd);
		}
	}
}
