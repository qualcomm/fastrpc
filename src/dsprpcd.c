// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define VERIFY_PRINT_INFO 0

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "fastrpc_common.h"
#include "verify.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef ADSP_LISTENER_VERSIONED
#define ADSP_LISTENER_VERSIONED "libadsp_default_listener.so.1"
#define ADSP_LISTENER_UNVERSIONED "libadsp_default_listener.so"
#endif
#ifndef CDSP_LISTENER_VERSIONED
#define CDSP_LISTENER_VERSIONED "libcdsp_default_listener.so.1"
#define CDSP_LISTENER_UNVERSIONED "libcdsp_default_listener.so"
#endif
#ifndef SDSP_LISTENER_VERSIONED
#define SDSP_LISTENER_VERSIONED "libsdsp_default_listener.so.1"
#define SDSP_LISTENER_UNVERSIONED "libsdsp_default_listener.so"
#endif
#ifndef GDSP_LISTENER_VERSIONED
#define GDSP_LISTENER_VERSIONED "libcdsp_default_listener.so.1"
#define GDSP_LISTENER_UNVERSIONED "libcdsp_default_listener.so"
#endif

typedef int (*dsp_default_listener_start_t)(int argc, char *argv[]);

// Result struct for dlopen.
struct dlopen_result {
	void *handle;
	const char *loaded_lib_name;
};

/**
 * Attempts to load a shared library using dlopen.
 * If the versioned name fails, falls back to the unversioned name.
 * Returns both the handle and the name of the library successfully loaded.
 */
static struct dlopen_result try_dlopen(const char *versioned, const char *unversioned)
{
	struct dlopen_result result = { NULL, NULL };

	result.handle = dlopen(versioned, RTLD_NOW);
	if (result.handle) {
		result.loaded_lib_name = versioned;
		return result;
	}

	if (unversioned) {
		VERIFY_IPRINTF("dlopen failed for %s: %s; attempting fallback %s", versioned,
		               dlerror(), unversioned);
		result.handle = dlopen(unversioned, RTLD_NOW);
		if (result.handle) {
			result.loaded_lib_name = unversioned;
			return result;
		}
	}
	return result;
}

int main(int argc, char *argv[])
{
	int nErr = 0;
	struct dlopen_result dlres = { NULL, NULL };
	const char *lib_versioned;
	const char *lib_unversioned;
	const char *dsp_name;
	dsp_default_listener_start_t listener_start;

#ifdef USE_ADSP
	lib_versioned = ADSP_LISTENER_VERSIONED;
	lib_unversioned = ADSP_LISTENER_UNVERSIONED;
	dsp_name = "ADSP";
#elif defined(USE_SDSP)
	lib_versioned = SDSP_LISTENER_VERSIONED;
	lib_unversioned = SDSP_LISTENER_UNVERSIONED;
	dsp_name = "SDSP";
#elif defined(USE_CDSP)
	lib_versioned = CDSP_LISTENER_VERSIONED;
	lib_unversioned = CDSP_LISTENER_UNVERSIONED;
	dsp_name = "CDSP";
#elif defined(USE_GDSP)
	lib_versioned = GDSP_LISTENER_VERSIONED;
	lib_unversioned = GDSP_LISTENER_UNVERSIONED;
	dsp_name = "GDSP";
#else
	goto bail;
#endif
	VERIFY_EPRINTF("%s daemon starting", dsp_name);

	while (1) {
		dlres = try_dlopen(lib_versioned, lib_unversioned);
		if (NULL != dlres.handle) {
			if (NULL
			    != (listener_start = (dsp_default_listener_start_t)dlsym(
				    dlres.handle, "adsp_default_listener_start"))) {
				VERIFY_IPRINTF("adsp_default_listener_start called");
				nErr = listener_start(argc, argv);
			}
			if (0 != dlclose(dlres.handle)) {
				VERIFY_EPRINTF("dlclose failed for %s", dlres.loaded_lib_name);
			}
		} else {
			VERIFY_EPRINTF("%s daemon error %s", dsp_name, dlerror());
		}

		if (nErr == AEE_ECONNREFUSED) {
			VERIFY_EPRINTF("fastRPC device is not accessible, daemon exiting...");
			break;
		}

		VERIFY_EPRINTF("%s daemon will restart after 100ms...", dsp_name);
		usleep(100000);
	}

bail:
	VERIFY_EPRINTF("daemon exiting %x", nErr);
	return nErr;
}
