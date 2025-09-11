// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef FARF_ERROR
#define FARF_ERROR 1
#endif

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "fastrpc_latency.h"
#include "verify.h"

int
fastrpc_latency_invoke_incr(struct fastrpc_latency *qp)
{
	if(qp == NULL || qp->state == FASTRPC_LATENCY_STOP)
		goto bail;
	qp->invoke++;
	if(qp->vote == FASTRPC_LATENCY_VOTE_OFF) {
		pthread_mutex_lock(&qp->wmut);
		pthread_cond_signal(&qp->cond);
		pthread_mutex_unlock(&qp->wmut);
	}
bail:
	return 0;
}

int
fastrpc_latency_init(int dev, struct fastrpc_latency *qos)
{
	int nErr = 0;

	VERIFYC(qos && dev != -1, AEE_ERPC);

	qos->dev = dev;
	qos->state = FASTRPC_LATENCY_STOP;
	qos->thread = 0;
	qos->wait_time = FASTRPC_LATENCY_WAIT_TIME_USEC;
	pthread_mutex_init(&qos->mut, 0);
	pthread_mutex_init(&qos->wmut, 0);
	pthread_cond_init(&qos->cond, NULL);
bail:
	return nErr;
}

int
fastrpc_latency_deinit(struct fastrpc_latency *qos)
{
	int nErr = 0;

	VERIFYC(qos, AEE_ERPC);
	if(qos->state == FASTRPC_LATENCY_START) {
		pthread_mutex_lock(&qos->wmut);
		qos->exit = FASTRPC_LATENCY_EXIT;
		pthread_cond_signal(&qos->cond);
		pthread_mutex_unlock(&qos->wmut);
		if(qos->thread) {
			pthread_join(qos->thread, 0);
			qos->thread = 0;
			FARF(ALWAYS, "latency thread joined");
		}
		pthread_mutex_destroy(&qos->mut);
		pthread_mutex_destroy(&qos->wmut);
	}
bail:
	return nErr;
}

/* FastRPC QoS handler votes for pm_qos latency based on
 * RPC activity in a window of time.
 */
static void *
fastrpc_latency_thread_handler(void *arg)
{
	int nErr = 0;
	long ns = 0;
	struct timespec tw;
	struct timeval tp;
	int invoke = 0;
	struct fastrpc_ioctl_control qos = { 0 };
	struct fastrpc_ctrl_latency lp = { 0 };
	struct fastrpc_latency *qp = (struct fastrpc_latency *)arg;

	if(qp == NULL) {
		nErr = AEE_ERPC;
		FARF(ERROR, "Error 0x%x: %s failed \n", nErr, __func__);
		return NULL;
	}
	VERIFYC(qp->dev != -1, AEE_ERPC);

	FARF(ALWAYS, "%s started for QoS with activity window %d ms", __func__,
	     FASTRPC_LATENCY_WAIT_TIME_USEC / MS_TO_US);

	// Look for RPC activity in 100 ms window
	qp->wait_time = FASTRPC_LATENCY_WAIT_TIME_USEC;
	qp->invoke++;
	while(1) {
		nErr = gettimeofday(&tp, NULL);
		/* valid values for "tv_nsec" are [0, 999999999] */
		ns = ((tp.tv_usec + qp->wait_time) * US_TO_NS);
		tw.tv_sec = tp.tv_sec + (ns / SEC_TO_NS);
		tw.tv_nsec = (ns % SEC_TO_NS);

		pthread_mutex_lock(&qp->wmut);
		if(qp->wait_time)
			pthread_cond_timedwait(&qp->cond, &qp->wmut, &tw);
		else
			pthread_cond_wait(&qp->cond, &qp->wmut);
		pthread_mutex_unlock(&qp->wmut);

		if(qp->exit == FASTRPC_LATENCY_EXIT) {
			qp->exit = 0;
			break;
		}

		pthread_mutex_lock(&qp->mut);
		invoke = qp->invoke;
		qp->invoke = 0;
		pthread_mutex_unlock(&qp->mut);

		if(invoke) {
			// found RPC activity in window. vote for pm_qos.
			qp->wait_time = FASTRPC_LATENCY_WAIT_TIME_USEC;
			if(qp->vote == FASTRPC_LATENCY_VOTE_OFF) {
				lp.enable = FASTRPC_LATENCY_VOTE_ON;
				lp.latency = qp->latency;
				nErr = ioctl_control(
				    qp->dev, DSPRPC_CONTROL_LATENCY, &lp);
				if(nErr == AEE_SUCCESS) {
					qp->vote = FASTRPC_LATENCY_VOTE_ON;
				} else if(nErr == AEE_EUNSUPPORTED) {
					goto bail;
				} else {
					FARF(ERROR,
					     "Error %d: %s: PM QoS ON request "
					     "failed with errno %d (%s)",
					     nErr, __func__, errno,
					     strerror(errno));
				}
			}
		} else {
			// No RPC activity detected in a window. Remove pm_qos
			// vote.
			qp->wait_time = 0;
			if(qp->vote == FASTRPC_LATENCY_VOTE_ON) {
				lp.enable = FASTRPC_LATENCY_VOTE_OFF;
				lp.latency = 0;
				nErr = ioctl_control(
				    qp->dev, DSPRPC_CONTROL_LATENCY, &lp);
				if(nErr == AEE_SUCCESS) {
					qp->vote = FASTRPC_LATENCY_VOTE_OFF;
				} else if(nErr == AEE_EUNSUPPORTED) {
					goto bail;
				} else {
					FARF(
					    ERROR,
					    "Error %d: %s: PM QoS OFF request "
					    "failed with errno %d (%s)",
					    nErr, __func__, errno,
					    strerror(errno));
				}
			}
		}
	}
	FARF(ALWAYS, "FastRPC latency thread for QoS exited");
bail:
	if(nErr != AEE_SUCCESS) {
		FARF(ERROR,
		     "Error 0x%x: %s failed for wait time %d latency control "
		     "enable %d "
		     "latency %d\n",
		     nErr, __func__, qp->wait_time, qos.lp.enable,
		     qos.lp.latency);
	}
	return NULL;
}

int
fastrpc_set_pm_qos(struct fastrpc_latency *qos, uint32_t enable,
                   uint32_t latency)
{
	int nErr = AEE_SUCCESS;
	int state = 0;

	VERIFYC(qos != NULL, AEE_EBADPARM);
	if(qos->exit == FASTRPC_LATENCY_EXIT)
		goto bail;
	pthread_mutex_lock(&qos->mut);
	state = qos->state;
	qos->latency = latency;
	pthread_mutex_unlock(&qos->mut);

	if(!enable && state == FASTRPC_LATENCY_START) {
		qos->exit = FASTRPC_LATENCY_EXIT;
		pthread_mutex_lock(&qos->wmut);
		pthread_cond_signal(&qos->cond);
		pthread_mutex_unlock(&qos->wmut);
	}

	if(enable && state == FASTRPC_LATENCY_STOP) {
		qos->state = FASTRPC_LATENCY_START;
		VERIFY(AEE_SUCCESS
		       == (nErr = pthread_create(
			       &qos->thread, 0, fastrpc_latency_thread_handler,
			       (void *)qos)));
	}
bail:
	return nErr;
}
