// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif /* VERIFY_PRINT_ERROR */

#define FARF_ERROR 1

#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "fastrpc_common.h"
#include "fastrpc_notif.h"
#include "platform_libs.h"
#include "verify.h"
#include "fastrpc_hash_table.h"

typedef struct {
  pthread_t thread;
  int init_done;
  int deinit_started;
  ADD_DOMAIN_HASH();
} notif_config;

// Fastrpc client notification request node to be queued to <notif_list>
struct fastrpc_notif {
  QNode qn;
  remote_rpc_notif_register_t notif;
};

struct other_handle_list {                   // For non-domain and reverse handle list
	QList ql;
};

/* Mutex to protect notif_list */
static pthread_mutex_t update_notif_list_mut;
/* List of all clients who registered for process status notification */
static struct other_handle_list notif_list;

void fastrpc_cleanup_notif_list();

DECLARE_HASH_TABLE(fastrpc_notif, notif_config);

static void *notif_fastrpc_thread(void *arg) {
  notif_config *me = (notif_config *)arg;
  int nErr = AEE_SUCCESS, domain = me->domain;

  do {
    nErr = get_remote_notif_response(domain);
    if (nErr)
      goto bail;
  } while (1);
bail:
  dlerror();
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s FastRPC notification worker thread exited "
                   "for domain %d (errno %s), notif_domain_deinit started %d",
                   nErr, __func__, domain, strerror(errno), me->deinit_started);
  }
  return (void *)(uintptr_t)nErr;
}

/* This function gets called in the thread context when thread has been
 * interrupted with SIGUSR1 */
void notif_thread_exit_handler(int sig) {
  FARF(ALWAYS, "Notification FastRPC worker thread exiting with signal %d\n",
       sig);
  pthread_exit(0);
}

void fastrpc_notif_init() {
  HASH_TABLE_INIT(notif_config);
  QList_Ctor(&notif_list.ql);
  pthread_mutex_init(&update_notif_list_mut, 0);
}

void fastrpc_notif_deinit() {
  HASH_TABLE_CLEANUP(notif_config);
  fastrpc_cleanup_notif_list();
  pthread_mutex_destroy(&update_notif_list_mut);
}

void fastrpc_notif_domain_deinit(int domain) {
  notif_config *me = NULL;
  int err = 0;

  GET_HASH_NODE(notif_config, domain, me);
  if (!me) {
    FARF(RUNTIME_RPC_HIGH, "Warning: %s: unable to find hash-node for domain %d",
              __func__, domain);
    return;
  }
  if (me->thread) {
    FARF(ALWAYS, "%s: Waiting for FastRPC notification worker thread to join",
         __func__);
    me->deinit_started = 1;
    err = fastrpc_exit_notif_thread(domain);
    if (err) {
      pthread_kill(me->thread, SIGUSR1);
    }
    pthread_join(me->thread, 0);
    me->thread = 0;
    FARF(ALWAYS, "%s: Fastrpc notification worker thread joined", __func__);
  }
  me->init_done = 0;
  return;
}

int fastrpc_notif_domain_init(int domain) {
  notif_config *me = NULL;
  int nErr = AEE_SUCCESS;
  struct sigaction siga;

  GET_HASH_NODE(notif_config, domain, me);
  if (!me) {
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(notif_config, domain, me);
  }
  if (me->init_done) {
    goto bail;
  }
  me->thread = 0;
  VERIFY(AEE_SUCCESS ==
         (nErr = pthread_create(&me->thread, 0, notif_fastrpc_thread,
                                (void *)me)));
  // Register signal handler to interrupt thread, while thread is waiting in
  // kernel
  memset(&siga, 0, sizeof(siga));
  siga.sa_flags = 0;
  siga.sa_handler = notif_thread_exit_handler;
  VERIFY(AEE_SUCCESS == (nErr = sigaction(SIGUSR1, &siga, NULL)));
  me->init_done = 1;
  me->deinit_started = 0;
  FARF(ALWAYS, "%s: FastRPC notification worker thread launched\n", __func__);
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s failed (errno %s)\n", nErr, __func__,
                   strerror(errno));
    fastrpc_notif_domain_deinit(domain);
  }
  return nErr;
}

int fastrpc_notif_register(int domain,
                           struct remote_rpc_notif_register *notif) {
  int nErr = AEE_SUCCESS;
  struct fastrpc_notif *lnotif = NULL;

  // Initialize fastrpc structures, if in case this is the first call to library
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);

  // Allocate client notification request node
  VERIFYC(NULL != (lnotif = calloc(1, sizeof(struct fastrpc_notif))),
          AEE_ENOMEMORY);
  QNode_CtorZ(&lnotif->qn);
  memcpy(&lnotif->notif, notif, sizeof(remote_rpc_notif_register_t));

  // Add client node to notification list
  pthread_mutex_lock(&update_notif_list_mut);
  QList_AppendNode(&notif_list.ql, &lnotif->qn);
  pthread_mutex_unlock(&update_notif_list_mut);

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d", nErr, __func__, domain);
  }
  return nErr;
}

/* Internal function to notify clients, if there is any notification request */
static int fastrpc_notify_status(int domain, int session, int status) {
  QNode *pn, *pnn;
  struct fastrpc_notif *lnotif = NULL;
  int nErr = AEE_SUCCESS;

  pthread_mutex_lock(&update_notif_list_mut);
  if (!QList_IsEmpty(&notif_list.ql)) {
    QLIST_NEXTSAFE_FOR_ALL(&notif_list.ql, pn, pnn) {
      lnotif = STD_RECOVER_REC(struct fastrpc_notif, qn, pn);
      if (lnotif && (lnotif->notif.domain == domain)) {
        lnotif->notif.notifier_fn(lnotif->notif.context, domain, session,
                                  status);
      }
    }
  }
  pthread_mutex_unlock(&update_notif_list_mut);
  return nErr;
}

void fastrpc_cleanup_notif_list() {
  QNode *pn = NULL, *pnn = NULL;
  struct fastrpc_notif *lnotif = NULL;

  pthread_mutex_lock(&update_notif_list_mut);
  if (!QList_IsEmpty(&notif_list.ql)) {
    QLIST_NEXTSAFE_FOR_ALL(&notif_list.ql, pn, pnn) {
      lnotif = STD_RECOVER_REC(struct fastrpc_notif, qn, pn);
      if (lnotif) {
        free(lnotif);
        lnotif = NULL;
      }
    }
  }
  pthread_mutex_unlock(&update_notif_list_mut);
}

/* Function to wait in kernel for an update in remote process status */
int get_remote_notif_response(int domain) {
  int nErr = AEE_SUCCESS, dev;
  int dom = -1, session = -1, status = -1;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
  nErr = ioctl_invoke2_notif(dev, &dom, &session, &status);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    goto bail;
  }
  FARF(ALWAYS, "%s: received status notification %u for domain %d, session %d",
       __func__, status, dom, session);
  fastrpc_notify_status(dom, session, status);
bail:
  if (nErr && (errno != EBADF) && (nErr != AEE_EEXPIRED)) {
    FARF(ERROR,
         "Error 0x%x: %s failed to get notification response data errno %s",
         nErr, __func__, strerror(errno));
  }
  return nErr;
}

// Make IOCTL call to exit notif thread
int fastrpc_exit_notif_thread(int domain) {
  int nErr = AEE_SUCCESS, dev;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));
  nErr = ioctl_control(dev, DSPRPC_NOTIF_WAKE, NULL);
bail:
  if (nErr)
    FARF(ERROR,
         "Error 0x%x: %s failed for domain %d (errno: %s), ignore if ioctl not "
         "supported, try pthread kill ",
         nErr, __func__, domain, strerror(errno));
  return nErr;
}
