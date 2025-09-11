// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_NOTIF_H
#define FASTRPC_NOTIF_H

#include "fastrpc_internal.h"
#include "remote64.h"

/*
 * Internal function to create fastrpc status notification thread
 * @ domain: domain to which notification is initialized
 * returns 0 on success
 */
int fastrpc_notif_domain_init(int domain);

/*
 * Internal function to exit fastrpc status notification thread
 * @ domain: domain to which notification is de-initialized
 * returns 0 on success
 */
void fastrpc_notif_domain_deinit(int domain);

/*
 * Internal function to get notification response from kernel. Waits in kernel
 * until notifications are received from DSP
 * @ domain: domain to which notification needs to be received
 * returns 0 on success
 */
int get_remote_notif_response(int domain);

/*
 * API to cleanup all the clients registered for notifcation
 */
void fastrpc_cleanup_notif_list();
/*
 * API to register a notification mechanism for a state change in DSP Process.
 * state changes can be PD start, PD exit, PD crash.
 */
int fastrpc_notif_register(int domain,
                           struct remote_rpc_notif_register *notif);

/*
 * API to initialize notif module in fastRPC
 * interal function to initialize globals and other
 * data structures used in notif module
 */
void fastrpc_notif_init();
/*
 * API to de-initialize notif module in fastRPC
 * interal function to free-up globals and other
 * data structures used in notif module
 */
void fastrpc_notif_deinit();

#endif // FASTRPC_NOTIF_H
