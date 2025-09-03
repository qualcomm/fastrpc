// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// Need to always define in order to use VERIFY_EPRINTF
#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif

#define FARF_ERROR 1

#include "adsp_default_listener.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "adsp_default_listener1.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "remote.h"
#include "verify.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DOMAIN_URI_SIZE 12
#define ROOTPD_NAME "rootpd"
#define ATTACH_GUESTOS "attachguestos"
#define CREATE_STATICPD "createstaticpd:"
#define POLL_TIMEOUT	10 * 1000
#define EVENT_SIZE		( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN	( 1024 * ( EVENT_SIZE + 16 ) )
#define ADSP_SECURE_DEVICE_NAME "fastrpc-adsp-secure"
#define SDSP_SECURE_DEVICE_NAME "fastrpc-sdsp-secure"
#define MDSP_SECURE_DEVICE_NAME "fastrpc-mdsp-secure"
#define CDSP_SECURE_DEVICE_NAME "fastrpc-cdsp-secure"
#define CDSP1_SECURE_DEVICE_NAME "fastrpc-cdsp1-secure"
#define GDSP0_SECURE_DEVICE_NAME "fastrpc-gdsp0-secure"
#define GDSP1_SECURE_DEVICE_NAME "fastrpc-gdsp1-secure"
#define ADSP_DEVICE_NAME "fastrpc-adsp"
#define SDSP_DEVICE_NAME "fastrpc-sdsp"
#define MDSP_DEVICE_NAME "fastrpc-mdsp"
#define CDSP_DEVICE_NAME "fastrpc-cdsp"
#define CDSP1_DEVICE_NAME "fastrpc-cdsp1"
#define GDSP0_DEVICE_NAME "fastrpc-gdsp0"
#define GDSP1_DEVICE_NAME "fastrpc-gdsp1"

// Array of supported domain names and its corresponding ID's.
static domain_t supported_domains[] = {{ADSP_DOMAIN_ID, ADSP_DOMAIN},
                                       {MDSP_DOMAIN_ID, MDSP_DOMAIN},
                                       {SDSP_DOMAIN_ID, SDSP_DOMAIN},
                                       {CDSP_DOMAIN_ID, CDSP_DOMAIN},
                                       {CDSP1_DOMAIN_ID, CDSP1_DOMAIN},
                                       {GDSP0_DOMAIN_ID, GDSP0_DOMAIN},
                                       {GDSP1_DOMAIN_ID, GDSP1_DOMAIN}};

// Get domain name for the domain id.
static domain_t *get_domain_uri(int domain_id) {
  int i = 0;
  int size = sizeof(supported_domains) / sizeof(domain_t);

  for (i = 0; i < size; i++) {
    if (supported_domains[i].id == domain_id)
      return &supported_domains[i];
  }

  return NULL;
}

static const char *get_secure_device_name(int domain_id) {
	const char *name;
	int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);

	switch (domain) {
	case ADSP_DOMAIN_ID:
		name = ADSP_SECURE_DEVICE_NAME;
		break;
	case SDSP_DOMAIN_ID:
		name = SDSP_SECURE_DEVICE_NAME;
		break;
	case MDSP_DOMAIN_ID:
		name = MDSP_SECURE_DEVICE_NAME;
		break;
	case CDSP_DOMAIN_ID:
		name = CDSP_SECURE_DEVICE_NAME;
		break;
	case CDSP1_DOMAIN_ID:
		name = CDSP1_SECURE_DEVICE_NAME;
		break;
	case GDSP0_DOMAIN_ID:
		name = GDSP0_SECURE_DEVICE_NAME;
		break;
	case GDSP1_DOMAIN_ID:
		name = GDSP1_SECURE_DEVICE_NAME;
		break;
	default:
		name = DEFAULT_DEVICE;
		break;
	}

	return name;
}

static const char *get_default_device_name(int domain_id) {
	const char *name;
	int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);

	switch (domain) {
	case ADSP_DOMAIN_ID:
		name = ADSP_DEVICE_NAME;
		break;
	case SDSP_DOMAIN_ID:
		name = SDSP_DEVICE_NAME;
		break;
	case MDSP_DOMAIN_ID:
		name = MDSP_DEVICE_NAME;
		break;
	case CDSP_DOMAIN_ID:
		name = CDSP_DEVICE_NAME;
		break;
	case CDSP1_DOMAIN_ID:
		name = CDSP1_DEVICE_NAME;
		break;
	case GDSP0_DOMAIN_ID:
		name = GDSP0_DEVICE_NAME;
		break;
	case GDSP1_DOMAIN_ID:
		name = GDSP1_DEVICE_NAME;
		break;
	default:
		name = DEFAULT_DEVICE;
		break;
	}

	return name;
}

/**
 * fastrpc_dev_exists() - Check if device exists
 * @dev_name: Device name
 *
 * Return:
 *	True: Device node exists
 *	False: Device node does not exist
 */
static bool fastrpc_dev_exists(const char* dev_name)
{
	struct stat buffer;
	char *path = NULL;
	uint64_t len;

	len = snprintf(0, 0, "/dev/%s", dev_name) + 1;
	if(NULL == (path = (char *)malloc(len * sizeof(char))))
		return false;

	snprintf(path, (int)len, "/dev/%s", dev_name);

	return (stat(path, &buffer) == 0);
}

/**
 * fastrpc_wait_for_device() - Wait for fastrpc device nodes
 * @domain: Domain ID
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int fastrpc_wait_for_device(int domain)
{
	int inotify_fd = -1, watch_fd = -1, err = 0;
	const char *sec_dev_name = NULL, *def_dev_name = NULL;
	struct pollfd pfd[1];

	sec_dev_name = get_secure_device_name(domain);
	def_dev_name = get_default_device_name(domain);

	if (fastrpc_dev_exists(sec_dev_name) || fastrpc_dev_exists(def_dev_name))
		return 0;

	inotify_fd = inotify_init();
	if (inotify_fd < 0) {
		VERIFY_EPRINTF("Error: inotify_init failed, invalid fd errno = %s\n", strerror(errno));
		return AEE_EINVALIDFD;
	}

	watch_fd = inotify_add_watch(inotify_fd, "/dev/", IN_CREATE);
	if (watch_fd < 0) {
		close(inotify_fd);
		VERIFY_EPRINTF("Error: inotify_add_watch failed, invalid fd errno = %s\n", strerror(errno));
		return AEE_EINVALIDFD;
	}

	if (fastrpc_dev_exists(sec_dev_name) || fastrpc_dev_exists(def_dev_name))
		goto bail;

	memset(pfd, 0 , sizeof(pfd));
	pfd[0].fd = inotify_fd;
	pfd[0].events = POLLIN;

	while (1) {
		int ret = 0;
		char buffer[EVENT_BUF_LEN];
		struct inotify_event *event;

		ret = poll(pfd, 1, POLL_TIMEOUT);
		if(ret < 0){
			VERIFY_EPRINTF("Error: %s: polling for event failed errno(%s)\n", __func__, strerror(errno));
			err = AEE_EPOLL;
			break;
		}
		if(ret == 0){
			VERIFY_EPRINTF("Error: %s: Poll timeout\n", __func__);
			err = AEE_EPOLL;
			break;
		}
		/* read on inotify fd never reads partial events. */
		ssize_t len = read(inotify_fd, buffer, sizeof(buffer));
		if (len < 0) {
			VERIFY_EPRINTF("Error: %s: read failed, errno = %s\n", __func__, strerror(errno));
			err = AEE_EEVENTREAD;
			break;
		}
		 /* Loop over all events in the buffer. */
		for (char *ptr = buffer; ptr < buffer + len;
					ptr += sizeof(struct inotify_event) + event->len) {
			event = (struct inotify_event *) ptr;
			/* Check if the event corresponds to the creation of the device node. */
			if (event->wd == watch_fd && (event->mask & IN_CREATE) &&
				((strcmp(sec_dev_name, event->name) == 0) ||
				(strcmp(def_dev_name, event->name) == 0))) {
				/* Device node created, process proceed to open and use it. */
				VERIFY_IPRINTF("Device node %s created!\n", event->name);
				goto bail; /* Exit the loop after device creation is detected. */
			}
		}
	}
bail:
	inotify_rm_watch(inotify_fd, watch_fd);
	close(inotify_fd);

	return err;
}

int adsp_default_listener_start(int argc, char *argv[]) {
  struct pollfd pfd;
  eventfd_t event = 0;
  remote_handle64 event_fd = INVALID_HANDLE;
  remote_handle64 fd = INVALID_HANDLE;
  remote_handle64 listener_fd = INVALID_HANDLE;
  int nErr = AEE_SUCCESS, domain_id = INVALID_DOMAIN_ID;
  char *name = NULL;
  char *adsp_default_listener1_URI_domain = NULL;
  int adsp_default_listener1_URI_domain_len =
      strlen(adsp_default_listener1_URI) + MAX_DOMAIN_URI_SIZE;
  domain_t *dsp_domain = NULL;
  int namelen = 0;
  char *eventfd_domain = NULL;
  int eventfdlen = 0;
  (void)argc;
  (void)argv;

  if (argc > 2) {
    /* Two arguments are passed in below format
     * Example: ./adsprpcd audiopd adsp
     * Get domain name from arguments and use domains API.
     */
    VERIFY_IPRINTF("%s started with arguments %s and %s\n", __func__, argv[1],
                   argv[2]);
    VERIFYC(INVALID_DOMAIN_ID != (domain_id = get_domain_from_name(
                                      argv[2], DOMAIN_NAME_STAND_ALONE)),
            AEE_EINVALIDDOMAIN);
    VERIFYC(NULL != (dsp_domain = get_domain_uri(domain_id)),
            AEE_EINVALIDDOMAIN);

    VERIFYC(AEE_SUCCESS == (nErr = fastrpc_wait_for_device(domain_id)), AEE_ECONNREFUSED);
    // Allocate memory for URI. Example: "ITRANSPORT_PREFIX
    // createstaticpd:audiopd&dom=adsp"
    namelen = strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + strlen(argv[1]) +
              strlen(ADSP_DOMAIN);
    VERIFYC(NULL != (name = (char *)malloc((namelen + 1) * sizeof(char))),
            AEE_ENOMEMORY);

    // Copy URI to allocated memory
    if (!strncmp(argv[1], ROOTPD_NAME, strlen(argv[1]))) {
      strlcpy(name, ITRANSPORT_PREFIX ATTACH_GUESTOS,
                  strlen(ITRANSPORT_PREFIX ATTACH_GUESTOS) + 1);
    } else {
      strlcpy(name, ITRANSPORT_PREFIX CREATE_STATICPD,
                  strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + 1);
      strlcat(name, argv[1],
                  strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + strlen(argv[1]) +
                      1);
    }

    // Concatenate domain to the URI
    strlcat(name, dsp_domain->uri, namelen + 1);

    // Open static process handle
    VERIFY(AEE_SUCCESS == (nErr = remote_handle64_open(name, &fd)));
    goto start_listener;
  } else if (argc > 1) {
    /* One arguments is passed in below format
     * Example: ./adsprpcd "createstaticpd:audiopd&dom=adsp" (or) ./adsprpcd
     * audiopd Get domain name from arguments and use domains API.
     */
    VERIFY_IPRINTF("%s started with arguments %s\n", __func__, argv[1]);
    domain_id = get_domain_from_name(argv[1], DOMAIN_NAME_IN_URI);

    // If domain name part of arguments, use domains API
    if (domain_id != INVALID_DOMAIN_ID) {
      VERIFYC(AEE_SUCCESS == (nErr = fastrpc_wait_for_device(domain_id)), AEE_ECONNREFUSED);
      VERIFY(AEE_SUCCESS == (nErr = remote_handle64_open(argv[1], &fd)));
      goto start_listener;
    }

    // Allocate memory for URI. Example: "ITRANSPORT_PREFIX
    // createstaticpd:audiopd"
    namelen = strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + strlen(argv[1]);
    VERIFYC(NULL != (name = (char *)malloc((namelen + 1) * sizeof(char))),
            AEE_ENOMEMORY);

    // Copy URI to allocated memory
    strlcpy(name, ITRANSPORT_PREFIX CREATE_STATICPD,
                strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + 1);
    strlcat(name, argv[1], namelen + 1);
  } else {
    // If no arguments passed, default/rootpd daemon of remote subsystem
    namelen = strlen(ITRANSPORT_PREFIX ATTACH_GUESTOS);
    VERIFYC(NULL != (name = (char *)malloc((namelen + 1) * sizeof(char))),
            AEE_ENOMEMORY);
    strlcpy(name, ITRANSPORT_PREFIX ATTACH_GUESTOS,
                strlen(ITRANSPORT_PREFIX ATTACH_GUESTOS) + 1);
  }

  VERIFYC(AEE_SUCCESS == (nErr = fastrpc_wait_for_device(DEFAULT_DOMAIN_ID)), AEE_ECONNREFUSED);
  // Default case: Open non-domain static process handle
  VERIFY_IPRINTF("%s started with arguments %s\n", __func__, name);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_open(name, (remote_handle *)&fd)));
start_listener:
  VERIFYC(!setenv("ADSP_LISTENER_MEM_CACHE_SIZE", "1048576", 0), AEE_ESETENV);

  // If domain name part of arguments, use domains API
  if (domain_id != INVALID_DOMAIN_ID) {
    // Allocate memory and copy adsp_default_listener1 URI Example:
    // "adsp_default_listener1_URI&dom=adsp"
    VERIFYC(NULL !=
                (adsp_default_listener1_URI_domain = (char *)malloc(
                     (adsp_default_listener1_URI_domain_len) * sizeof(char))),
            AEE_ENOMEMORY);
    VERIFYC(NULL != (dsp_domain = get_domain_uri(domain_id)),
            AEE_EINVALIDDOMAIN);
    nErr = snprintf(adsp_default_listener1_URI_domain,
                    adsp_default_listener1_URI_domain_len, "%s%s",
                    adsp_default_listener1_URI, dsp_domain->uri);
    if (nErr < 0) {
      VERIFY_EPRINTF("ERROR: %s: %d returned from snprintf\n", __func__, nErr);
      nErr = AEE_EFAILED;
      goto bail;
    }

    // Open default listener handle
    nErr = adsp_default_listener1_open(adsp_default_listener1_URI_domain,
                                       &listener_fd);

    // Register daemon as default listener to static process
    if (nErr == AEE_SUCCESS) {
      VERIFY(0 == (nErr = adsp_default_listener1_register(listener_fd)));
      goto start_poll;
    }
  }

  // Default case: Register non-domain default listener
  VERIFY_IPRINTF("%s domains support is not available for "
                 "adsp_default_listener1, using non-domain API\n",
                 __func__);
  VERIFY(0 ==
         (nErr = remote_handle_open("adsp_default_listener", (remote_handle *)&listener_fd)));
  VERIFY(0 == (nErr = adsp_default_listener_register()));
start_poll:
  // If domain name part of arguments, use domains API
  if (domain_id != INVALID_DOMAIN_ID) {
    // Allocate memory and copy geteventfd URI Example: "ITRANSPORT_PREFIX
    // geteventfd&dom=adsp"
    eventfdlen = strlen(ITRANSPORT_PREFIX "geteventfd") + MAX_DOMAIN_URI_SIZE;
    VERIFYC(NULL !=
                (eventfd_domain = (char *)malloc((eventfdlen) * sizeof(char))),
            AEE_ENOMEMORY);
    nErr = snprintf(eventfd_domain, eventfdlen, "%s%s",
                    ITRANSPORT_PREFIX "geteventfd", dsp_domain->uri);
    if (nErr < 0) {
      VERIFY_EPRINTF("ERROR: %s: %d returned from snprintf\n", __func__, nErr);
      nErr = AEE_EFAILED;
      goto bail;
    }

    // Get even FD to poll on listener thread
    VERIFY(0 == (nErr = remote_handle64_open(eventfd_domain, &event_fd)));
    pfd.fd = (remote_handle)event_fd;
  } else {
    VERIFY(0 == (nErr = remote_handle_open(ITRANSPORT_PREFIX "geteventfd",
                                           (remote_handle *)&pfd.fd)));
  }
  if (name != NULL) {
    free(name);
    name = NULL;
  }
  if (eventfd_domain != NULL) {
    free(eventfd_domain);
    eventfd_domain = NULL;
  }
  if (adsp_default_listener1_URI_domain != NULL) {
    free(adsp_default_listener1_URI_domain);
    adsp_default_listener1_URI_domain = NULL;
  }
  // Poll on listener thread
  pfd.events = POLLIN;
  pfd.revents = 0;
  while (1) {
    VERIFYC(0 < poll(&pfd, 1, -1), AEE_EPOLL);
    VERIFYC(0 == eventfd_read(pfd.fd, &event), AEE_EEVENTREAD);
    if (event) {
      break;
    }
  }
bail:
  if (listener_fd != INVALID_HANDLE) {
    adsp_default_listener1_close(listener_fd);
  }
  if (nErr != AEE_SUCCESS) {
    if (name != NULL) {
      free(name);
      name = NULL;
    }
    if (eventfd_domain != NULL) {
      free(eventfd_domain);
      eventfd_domain = NULL;
    }
    if (adsp_default_listener1_URI_domain != NULL) {
      free(adsp_default_listener1_URI_domain);
      adsp_default_listener1_URI_domain = NULL;
    }
    VERIFY_EPRINTF("Error 0x%x: %s exiting\n", nErr, __func__);
  }
  return nErr;
}
