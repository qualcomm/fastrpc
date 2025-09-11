// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __FASTRPC_LATENCY_H__
#define __FASTRPC_LATENCY_H__

#define FASTRPC_LATENCY_START (1)
#define FASTRPC_LATENCY_STOP (0)
#define FASTRPC_LATENCY_EXIT (2)

#define SEC_TO_NS (1000000000)
#define MS_TO_US (1000)
#define US_TO_NS MS_TO_US

#define FASTRPC_LATENCY_VOTE_ON (1)
#define FASTRPC_LATENCY_VOTE_OFF (0)
#define FASTRPC_LATENCY_WAIT_TIME_USEC (100000)
#define FASTRPC_QOS_MAX_LATENCY_USEC (10000)

/* FastRPC latency voting data for QoS handler of a session */
struct fastrpc_latency {
	int adaptive_qos;
	int state; //! latency thread handler running state
	int exit;
	int invoke;    //! invoke count in tacking window
	int vote;      //! current pm_qos vote status
	int dev;       //! associated device node
	int wait_time; //! wait time for review next voting
	int latency;   //! user requested fastrpc latency in us
	pthread_t thread;
	pthread_mutex_t mut;
	pthread_mutex_t wmut;
	pthread_cond_t cond;
};

/* Increment RPC invoke count for activity detection in a window of time
 * @param qos, pointer to domain latency data
 * return, fastrpc error codes
 */
int fastrpc_latency_invoke_incr(struct fastrpc_latency *qos);

/* Set qos enable and latency parameters for a configured domain
 * @param qos, pointer to domain latency data
 * @param enable, qos enable/disable
 * @param latency, fastrpc latency requirement from user in us
 * @return, fastrpc error codes
 */
int fastrpc_set_pm_qos(struct fastrpc_latency *qos, uint32_t enable,
                       uint32_t latency);

/* Initialization routine to initialize globals & internal data structures */
int fastrpc_latency_init(int dev, struct fastrpc_latency *qos);
/* De-init routine to cleanup globals & internal data structures*/
int fastrpc_latency_deinit(struct fastrpc_latency *qos);

#endif /*__FASTRPC_LATENCY_H__*/
