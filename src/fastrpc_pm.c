// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif // VERIFY_PRINT_ERROR

#define FARF_ERROR 1

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "verify.h"

#define WAKE_LOCK_FILE "/sys/power/wake_lock"
#define WAKE_UNLOCK_FILE "/sys/power/wake_unlock"

#define WAKELOCK_NAME_LEN 50

struct wake_lock {
	char wake_lock_name[WAKELOCK_NAME_LEN];
	int lock;
	int unlock;
	pthread_mutex_t wmut;
	unsigned int count;
	bool init_done;
	bool deinit_started;
};

static struct wake_lock wakelock;
static atomic_bool wakelock_wmut_int = false;

int
fastrpc_wake_lock()
{
	int nErr = AEE_SUCCESS, ret = 0;

	if(!wakelock.init_done) {
		nErr = AEE_ERPC;
		FARF(
		    ERROR,
		    "Error 0x%x : %s failed for wakelock is not initialized\n",
		    nErr, __func__);
		return nErr;
	}

	pthread_mutex_lock(&wakelock.wmut);
	if(wakelock.deinit_started) {
		nErr = AEE_ERPC;
		FARF(ERROR,
		     "Warning 0x%x : %s failed for wakelock as deinit "
		     "started\n",
		     nErr, __func__);
		goto bail;
	}
	if(!wakelock.count && wakelock.lock > 0)
		VERIFYC(
		    0 < (ret = write(wakelock.lock, wakelock.wake_lock_name,
		                     strlen(wakelock.wake_lock_name))),
		    AEE_ERPC);
	wakelock.count++;
bail:
	pthread_mutex_unlock(&wakelock.wmut);
	if(nErr) {
		FARF(ERROR,
		     "Error 0x%x (%d): %s failed for %s, fd %d (errno %s)\n",
		     nErr, ret, __func__, WAKE_LOCK_FILE, wakelock.lock,
		     strerror(errno));
	}
	return nErr;
}

int
fastrpc_wake_unlock()
{
	int nErr = AEE_SUCCESS, ret = 0;

	if(!wakelock.init_done) {
		nErr = AEE_ERPC;
		FARF(
		    ERROR,
		    "Error 0x%x : %s failed for wakelock is not initialized\n",
		    nErr, __func__);
		return nErr;
	}

	pthread_mutex_lock(&wakelock.wmut);
	if(!wakelock.count)
		goto bail;
	wakelock.count--;
	if(!wakelock.count && wakelock.unlock > 0)
		VERIFYC(
		    0 < (ret = write(wakelock.unlock, wakelock.wake_lock_name,
		                     strlen(wakelock.wake_lock_name))),
		    AEE_ERPC);
bail:
	if(nErr) {
		wakelock.count++;
		FARF(ERROR,
		     "Error 0x%x (%d): %s failed for %s, fd %d (errno %s)\n",
		     nErr, ret, __func__, WAKE_UNLOCK_FILE, wakelock.unlock,
		     strerror(errno));
	}
	pthread_mutex_unlock(&wakelock.wmut);
	return nErr;
}

static void
fastrpc_wake_lock_release()
{
	int nErr = AEE_SUCCESS;

	while(wakelock.count) {
		VERIFY(AEE_SUCCESS == (nErr = fastrpc_wake_unlock()));
	}
bail:
	return;
}

int
fastrpc_wake_lock_init()
{
	int nErr = AEE_SUCCESS, ret = 0;
	const unsigned int TMPSTR_LEN = WAKELOCK_NAME_LEN / 2;
	char pid_str[TMPSTR_LEN], prog_name_str[TMPSTR_LEN];
	bool expected = false;

	if(wakelock.init_done)
		return nErr;

	wakelock.deinit_started = 0;

	if(atomic_compare_exchange_strong(&wakelock_wmut_int, &expected, true))
		VERIFY(AEE_SUCCESS
		       == (nErr = pthread_mutex_init(&wakelock.wmut, 0)));

	pthread_mutex_lock(&wakelock.wmut);

	VERIFYC(0 < (ret = snprintf(pid_str, TMPSTR_LEN, ":%d", getpid())),
	        AEE_ERPC);
	if(0
	   >= (ret = snprintf(prog_name_str, TMPSTR_LEN, "%s", __progname))) {
		nErr = AEE_ERPC;
		goto bail;
	}

	strlcpy(wakelock.wake_lock_name, prog_name_str, WAKELOCK_NAME_LEN);
	strlcat(wakelock.wake_lock_name, pid_str, WAKELOCK_NAME_LEN);

	VERIFYC(0 < (wakelock.lock = open(WAKE_LOCK_FILE, O_RDWR | O_CLOEXEC)),
	        AEE_ERPC);
	VERIFYC(
	    0 < (wakelock.unlock = open(WAKE_UNLOCK_FILE, O_RDWR | O_CLOEXEC)),
	    AEE_ERPC);

bail:
	if(nErr) {
		FARF(ERROR, "Error 0x%x (%d): %s failed (errno %s)\n", nErr,
		     ret, __func__, strerror(errno));
		if((nErr == AEE_ERPC) && (errno == ENOENT)) {
			nErr = AEE_EUNSUPPORTEDAPI;
		}
		if(wakelock.lock > 0) {
			ret = close(wakelock.lock);
			if(ret) {
				FARF(ERROR,
				     "Error %d: %s: failed to close %s with "
				     "fd %d (errno %s)",
				     ret, __func__, WAKE_LOCK_FILE,
				     wakelock.lock, strerror(errno));
			} else {
				wakelock.lock = 0;
			}
		}
		if(wakelock.unlock > 0) {
			ret = close(wakelock.unlock);
			if(ret) {
				FARF(ERROR,
				     "Error %d: %s: failed to close %s with "
				     "fd %d (errno %s)",
				     ret, __func__, WAKE_UNLOCK_FILE,
				     wakelock.unlock, strerror(errno));
			} else {
				wakelock.unlock = 0;
			}
		}
		pthread_mutex_unlock(&wakelock.wmut);
		pthread_mutex_destroy(&wakelock.wmut);
	} else {
		wakelock.init_done = true;
		pthread_mutex_unlock(&wakelock.wmut);
		FARF(ALWAYS, "%s done for %s", __func__,
		     wakelock.wake_lock_name);
	}
	return nErr;
}

int
fastrpc_wake_lock_deinit()
{
	int nErr = AEE_SUCCESS;

	if(!wakelock.init_done)
		return nErr;

	pthread_mutex_lock(&wakelock.wmut);
	wakelock.deinit_started = 1;
	pthread_mutex_unlock(&wakelock.wmut);
	fastrpc_wake_lock_release();
	pthread_mutex_lock(&wakelock.wmut);
	if(wakelock.lock > 0) {
		nErr = close(wakelock.lock);
		if(nErr) {
			FARF(ERROR,
			     "Error %d: %s: failed to close %s with fd %d "
			     "(errno %s)",
			     nErr, __func__, WAKE_LOCK_FILE, wakelock.lock,
			     strerror(errno));
		} else {
			wakelock.lock = 0;
		}
	}
	if(wakelock.unlock > 0) {
		nErr = close(wakelock.unlock);
		if(nErr) {
			FARF(ERROR,
			     "Error %d: %s: failed to close %s with fd %d "
			     "(errno %s)",
			     nErr, __func__, WAKE_UNLOCK_FILE, wakelock.unlock,
			     strerror(errno));
		} else {
			wakelock.unlock = 0;
		}
	}
	wakelock.init_done = false;
	pthread_mutex_unlock(&wakelock.wmut);

	if(nErr)
		FARF(ERROR, "Error 0x%x (%d): %s failed (errno %s)\n", nErr,
		     nErr, __func__, strerror(errno));
	else
		FARF(ALWAYS, "%s done", __func__);
	return nErr;
}
