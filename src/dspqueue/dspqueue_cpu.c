// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN
#define FARF_ERROR 1
#define FARF_HIGH 0
#define FARF_MEDIUM 0
//#ifndef _DEBUG
//#  define _DEBUG
//#endif
//#ifdef NDEBUG
//#  undef NDEBUG
//#endif

#include "AEEQList.h" // Needed by fastrpc_mem.h
#include "dspqueue.h"
#include "dspqueue_rpc.h"
#include "dspqueue_shared.h"
#include "dspsignal.h"
#include "fastrpc_apps_user.h"
#include "fastrpc_internal.h"
#include "fastrpc_mem.h"
#include "fastrpc_context.h"
#include "remote.h"
#include "verify.h"
#include <AEEStdErr.h>
#include <HAP_farf.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <rpcmem_internal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

struct dspqueue {
  unsigned id;
  int domain;
  struct dspqueue_multidomain mdq;
  struct dspqueue_header *header;
  void *user_queue;
  int user_queue_fd;
  uint32_t user_queue_size;
  remote_handle64 dsp_handle;
  uint64_t dsp_id;
  uint16_t seq_no;
  pthread_mutex_t mutex;
  uint32_t read_packet_count;
  uint32_t write_packet_count;
  uint32_t req_packet_count;
  uint32_t req_space_count;
  uint32_t resp_packet_count;
  uint32_t resp_space_count;
  uint32_t packet_mask;
  pthread_mutex_t packet_mutex;
  pthread_cond_t packet_cond;
  uint32_t space_mask;
  pthread_mutex_t space_mutex;
  pthread_cond_t space_cond;
  int signal_threads;
  dspqueue_callback_t packet_callback;
  dspqueue_callback_t error_callback;
  void *callback_context;
  pthread_t packet_callback_thread;
  uint64_t early_wakeup_wait;
  uint32_t early_wakeup_misses;
  uint32_t queue_count;
  int have_wait_counts;
  int have_driver_signaling;
  pthread_t error_callback_thread;
};

struct dspqueue_domain_queues {
  int domain;
  unsigned num_queues;
  pthread_mutex_t
      queue_list_mutex; // Hold this to manipulate queues[] or max_queue
  unsigned max_queue;
  struct dspqueue *queues[DSPQUEUE_MAX_PROCESS_QUEUES];
  struct dspqueue_process_queue_state *state;
  int state_fd;
  remote_handle64 dsp_handle;
  pthread_t send_signal_thread;
  pthread_mutex_t send_signal_mutex;
  pthread_cond_t send_signal_cond;
  uint32_t send_signal_mask;
  pthread_t receive_signal_thread;
  int dsp_error;
  int have_dspsignal;
};

static inline void free_skel_uri(remote_rpc_get_uri_t *dspqueue_skel) {
  if (dspqueue_skel->domain_name) {
    free(dspqueue_skel->domain_name);
  }
  if (dspqueue_skel->module_uri) {
    free(dspqueue_skel->module_uri);
  }
  if (dspqueue_skel->uri) {
    free(dspqueue_skel->uri);
  }
}

#define UNUSED_QUEUE ((struct dspqueue *)NULL)
#define INVALID_QUEUE ((struct dspqueue *)-1)

struct dspqueue_process_queues {
  pthread_mutex_t mutex; // Hold this to manipulate domain_queues or
                         // domain_queues[i]->num_queues; In other words, must
                         // hold this mutex to decide when to create/destroy a
                         // new struct dspqueue_domain_queues.
  struct dspqueue_domain_queues *domain_queues[NUM_DOMAINS_EXTEND];
  uint32_t count;
  int notif_registered[NUM_DOMAINS_EXTEND];
};

static struct dspqueue_process_queues proc_queues;
static struct dspqueue_process_queues *queues = &proc_queues;
static pthread_once_t queues_once = PTHREAD_ONCE_INIT;

static void *dspqueue_send_signal_thread(void *arg);
static void *dspqueue_receive_signal_thread(void *arg);
static void *dspqueue_packet_callback_thread(void *arg);

#define QUEUE_CACHE_ALIGN 256
#define CACHE_ALIGN_SIZE(x)                                                    \
  ((x + (QUEUE_CACHE_ALIGN - 1)) & (~(QUEUE_CACHE_ALIGN - 1)))

// Cache maintenance ops. No-op for now - assuming cache coherency.
// Leave macros in place in case we want to make the buffer non-coherent
#define cache_invalidate_word(x)
#define cache_flush_word(x)
#define cache_invalidate_line(x)
#define cache_flush_line(x)
#ifdef __ARM_ARCH
#define barrier_full() __asm__ __volatile__("dmb sy" : : : "memory")
#define barrier_store() __asm__ __volatile__("dmb st" : : : "memory");
#else
#define barrier_full() /* FIXME */
#define barrier_store() /* FIXME */
#endif
#define cache_flush(a, l)
#define cache_invalidate(a, l)
#define cache_flush_invalidate(a, l)

#define DEFAULT_EARLY_WAKEUP_WAIT 1000
#define MAX_EARLY_WAKEUP_WAIT 2500
#define EARLY_WAKEUP_SLEEP 100

// Signal ID to match a specific queue signal
#define QUEUE_SIGNAL(queue_id, signal_no)                                      \
  ((DSPQUEUE_NUM_SIGNALS * queue_id) + signal_no + DSPSIGNAL_DSPQUEUE_MIN)

// Packet/space/send signal bit mask values
#define SIGNAL_BIT_SIGNAL 1
#define SIGNAL_BIT_CANCEL 2

static int dspqueue_notif_callback(void *context, int domain, int session,
                                   remote_rpc_status_flags_t status);

// Initialize process static queue structure. This should realistically never
// fail.
static void init_process_queues_once(void) {
  if (pthread_mutex_init(&queues->mutex, NULL) != 0) {
    FARF(ERROR, "Mutex init failed");
    return;
  }
  queues->count = 1; // Start non-zero to help spot certain errors
}

// Dynamically initialize process queue structure. This allocates memory and
// creates threads for queue signaling. The resources will be freed after
// the last queue in the process is closed.
// Must hold queues->mutex.
static AEEResult init_domain_queues_locked(int domain) {

  AEEResult nErr = AEE_SUCCESS;
  pthread_attr_t tattr;
  int sendmutex = 0, sendcond = 0, sendthread = 0, recvthread = 0,
      dom = domain & DOMAIN_ID_MASK;
  struct dspqueue_domain_queues *dq = NULL;
  remote_rpc_get_uri_t dspqueue_skel = {0};
  int state_mapped = 0;
  uint32_t cap = 0;

  errno = 0;
  assert(IS_VALID_EFFECTIVE_DOMAIN_ID(domain));
  if (queues->domain_queues[domain] != NULL) {
    return AEE_SUCCESS;
  }

  VERIFYC((dq = calloc(1, sizeof(*dq))) != NULL, AEE_ENOMEMORY);
  dq->domain = domain;

  /* Get URI of session */
  dspqueue_skel.domain_name_len = (dom == CDSP1_DOMAIN_ID) ?
      strlen(CDSP1_DOMAIN_NAME) + 1 : strlen(CDSP_DOMAIN_NAME) + 1;
  VERIFYC((dspqueue_skel.domain_name = (char *)calloc(
               dspqueue_skel.domain_name_len, sizeof(char))) != NULL,
          AEE_ENOMEMORY);

  // Open session on the right DSP
  if (dom == CDSP_DOMAIN_ID) {
    strlcpy(dspqueue_skel.domain_name, CDSP_DOMAIN_NAME,
                dspqueue_skel.domain_name_len);
  } else if (dom == ADSP_DOMAIN_ID) {
    strlcpy(dspqueue_skel.domain_name, ADSP_DOMAIN_NAME,
                dspqueue_skel.domain_name_len);
  } else if (dom == CDSP1_DOMAIN_ID) {
    strlcpy(dspqueue_skel.domain_name, CDSP1_DOMAIN_NAME,
                dspqueue_skel.domain_name_len);
  } else {
    nErr = AEE_EUNSUPPORTED;
    goto bail;
  }
  dspqueue_skel.session_id = GET_SESSION_ID_FROM_DOMAIN_ID(domain);

  /* One extra character for NULL termination */
  dspqueue_skel.module_uri_len = strlen(dspqueue_rpc_URI) + 1;
  VERIFYC((dspqueue_skel.module_uri = (char *)calloc(
               dspqueue_skel.module_uri_len, sizeof(char))) != NULL,
          AEE_ENOMEMORY);
  strlcpy(dspqueue_skel.module_uri, dspqueue_rpc_URI,
              dspqueue_skel.module_uri_len);

  /* One extra character for NULL termination is already part of module_uri_len
   */
  dspqueue_skel.uri_len = dspqueue_skel.module_uri_len + FASTRPC_URI_BUF_LEN;
  VERIFYC((dspqueue_skel.uri =
               (char *)calloc(dspqueue_skel.uri_len, sizeof(char))) != NULL,
          AEE_ENOMEMORY);
  if ((nErr = remote_session_control(FASTRPC_GET_URI, (void *)&dspqueue_skel,
                                     sizeof(dspqueue_skel))) != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s: obtaining session URI for %s on domain %d, session "
         "%u failed\n",
         nErr, __func__, dspqueue_rpc_URI, domain, dspqueue_skel.session_id);
    /* In case of failure due to SSR, return corresponding error to client */
    if (nErr != AEE_ECONNRESET)
      nErr = AEE_EUNSUPPORTED;
    goto bail;
  }

  if ((nErr = dspqueue_rpc_open(dspqueue_skel.uri, &dq->dsp_handle)) != 0) {
    FARF(ERROR,
         "dspqueue_rpc_open failed with %x on domain %d - packet queue support "
         "likely not present on DSP",
         nErr, domain);
    /* In case of failure due to SSR, return corresponding error to client */
    if (nErr != AEE_ECONNRESET)
      nErr = AEE_EUNSUPPORTED;
    goto bail;
  }

  if (!queues->notif_registered[domain]) {
    // Register for process exit notifications. Only do this once for the
    // lifetime of the process to avoid multiple registrations and leaks.
    remote_rpc_notif_register_t reg = {.context = queues,
                                       .domain = domain,
                                       .notifier_fn = dspqueue_notif_callback};
    nErr = remote_session_control(FASTRPC_REGISTER_STATUS_NOTIFICATIONS,
                                  (void *)&reg, sizeof(reg));
    if (nErr == AEE_EUNSUPPORTED) {
      FARF(ERROR, "Warning 0x%x: %s: DSP doesn't support status notification",
           nErr, __func__);
      nErr = 0;
    } else if (!nErr) {
      queues->notif_registered[domain] = 1;
    } else {
      goto bail;
    }
  }

  // Allocate shared state structure and pass to DSP
  VERIFYC((dq->state = rpcmem_alloc_internal(
               RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
               sizeof(struct dspqueue_process_queue_state))) != NULL,
          AEE_ENORPCMEMORY);
  VERIFYC((((uintptr_t)dq->state) & 4095) == 0, AEE_ERPC);
  VERIFYC((dq->state_fd = rpcmem_to_fd(dq->state)) > 0, AEE_ERPC);
  VERIFY((nErr = fastrpc_mmap(domain, dq->state_fd, dq->state, 0,
                              sizeof(struct dspqueue_process_queue_state),
                              FASTRPC_MAP_FD)) == 0);
  state_mapped = 1;
  VERIFY((nErr = dspqueue_rpc_init_process_state(dq->dsp_handle,
                                                 dq->state_fd)) == 0);

  // Check if we have driver signaling (a.k.a. dspsignal) support
  nErr = fastrpc_get_cap(domain, DSPSIGNAL_DSP_SUPPORT, &cap);
  if ((nErr != 0) || (cap == 0)) {
    FARF(HIGH, "dspqueue: No driver signaling support on DSP");
  } else {
    nErr = fastrpc_get_cap(domain, DSPSIGNAL_DRIVER_SUPPORT, &cap);
    if ((nErr != 0) || (cap == 0)) {
      FARF(HIGH, "dspqueue: No driver signaling support in CPU driver");
    } else {
      FARF(HIGH, "dspqueue: Optimized driver signaling supported");
      dq->have_dspsignal = 1;
    }
  }

  if (!dq->have_dspsignal) {
    // Create thread and resources to send signals to the DSP
    VERIFY((nErr = pthread_mutex_init(&dq->send_signal_mutex, NULL)) == 0);
    sendmutex = 1;
    VERIFY((nErr = pthread_cond_init(&dq->send_signal_cond, NULL)) == 0);
    sendcond = 1;
    dq->send_signal_mask = 0;
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
    VERIFY((nErr = pthread_create(&dq->send_signal_thread, &tattr,
                                  dspqueue_send_signal_thread, dq)) == 0);
    sendthread = 1;

    // Create thread to receive signals from the DSP
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
    VERIFY((nErr = pthread_create(&dq->receive_signal_thread, &tattr,
                                  dspqueue_receive_signal_thread, dq)) == 0);
    recvthread = 1;
  }

  free_skel_uri(&dspqueue_skel);
  queues->domain_queues[domain] = dq;
  return AEE_SUCCESS;

bail:
  if (dq) {
    if (recvthread) {
      void *res;
      dspqueue_rpc_cancel_wait_signal(dq->dsp_handle);
      pthread_join(dq->receive_signal_thread, &res);
    }
    if (sendthread) {
      void *res;
      pthread_mutex_lock(&dq->send_signal_mutex);
      dq->send_signal_mask |= SIGNAL_BIT_CANCEL;
      pthread_cond_signal(&dq->send_signal_cond);
      pthread_mutex_unlock(&dq->send_signal_mutex);
      pthread_join(dq->send_signal_thread, &res);
    }
    if (sendcond) {
      pthread_cond_destroy(&dq->send_signal_cond);
    }
    if (sendmutex) {
      pthread_mutex_destroy(&dq->send_signal_mutex);
    }
    if (state_mapped) {
      fastrpc_munmap(domain, dq->state_fd, dq->state,
                     sizeof(struct dspqueue_process_queue_state));
    }
    if (dq->dsp_handle) {
      dspqueue_rpc_close(dq->dsp_handle);
    }
    if (dq->state) {
      rpcmem_free(dq->state);
    }
    free(dq);
  }
  free_skel_uri(&dspqueue_skel);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d) errno %s", nErr, __func__,
         domain, strerror(errno));
  }
  return nErr;
}

// Must hold queues->mutex.
static AEEResult destroy_domain_queues_locked(int domain) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspqueue_domain_queues *dq = NULL;
  void *ret;

  errno = 0;
  FARF(HIGH, "destroy_domain_queues_locked");
  assert(IS_VALID_EFFECTIVE_DOMAIN_ID(domain));
  assert(queues->domain_queues[domain] != NULL);
  dq = queues->domain_queues[domain];
  assert(dq->num_queues == 0);

  if (!dq->have_dspsignal) {
    if (dq->dsp_error) {
      // Ignore errors if the DSP process died
      dspqueue_rpc_cancel_wait_signal(dq->dsp_handle);
    } else {
      VERIFY((nErr = dspqueue_rpc_cancel_wait_signal(dq->dsp_handle)) == 0);
    }
    FARF(MEDIUM, "Join receive signal thread");
    VERIFY((nErr = pthread_join(dq->receive_signal_thread, &ret)) == 0);
    FARF(MEDIUM, " - Join receive signal thread done");
    if (!dq->dsp_error) {
      VERIFY(((uintptr_t)ret) == 0);
    }

    pthread_mutex_lock(&dq->send_signal_mutex);
    dq->send_signal_mask |= SIGNAL_BIT_CANCEL;
    pthread_cond_signal(&dq->send_signal_cond);
    pthread_mutex_unlock(&dq->send_signal_mutex);
    FARF(MEDIUM, "Join send signal thread");
    VERIFY((nErr = pthread_join(dq->send_signal_thread, &ret)) == 0);
    if (!dq->dsp_error) {
      VERIFY(((uintptr_t)ret) == 0);
    }
    FARF(MEDIUM, " - Join send signal thread done");

    pthread_cond_destroy(&dq->send_signal_cond);
    pthread_mutex_destroy(&dq->send_signal_mutex);
  }

  if (dq->dsp_error) {
    dspqueue_rpc_close(dq->dsp_handle);
    fastrpc_munmap(dq->domain, dq->state_fd, dq->state,
                   sizeof(struct dspqueue_process_queue_state));
  } else {
    VERIFY((nErr = dspqueue_rpc_close(dq->dsp_handle)) == 0);
    VERIFY((nErr = fastrpc_munmap(
                dq->domain, dq->state_fd, dq->state,
                sizeof(struct dspqueue_process_queue_state))) == 0);
  }

  rpcmem_free(dq->state);
  free(dq);

  queues->domain_queues[domain] = NULL;

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed (domain %d) errno %s", nErr, __func__,
         domain, strerror(errno));
  }
  return nErr;
}

AEEResult dspqueue_create(int domain, uint32_t flags, uint32_t req_queue_size,
                          uint32_t resp_queue_size,
                          dspqueue_callback_t packet_callback,
                          dspqueue_callback_t error_callback,
                          void *callback_context, dspqueue_t *queue) {

  struct dspqueue *q = NULL;
  AEEResult nErr = AEE_SUCCESS;
  uint32_t o;
  int mutex_init = 0;
  pthread_attr_t tattr;
  unsigned id = DSPQUEUE_MAX_PROCESS_QUEUES;
  struct dspqueue_domain_queues *dq = NULL;
  int packetmutex = 0, packetcond = 0, spacemutex = 0, spacecond = 0;
  int callbackthread = 0;
  uint32_t queue_count;
  int queue_mapped = 0;
  unsigned signals = 0;

  VERIFYC(queue, AEE_EBADPARM);
  *queue = NULL;
  errno = 0;

  if (domain == -1) {
    domain = get_current_domain();
    if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
      return AEE_ERPC;
    }
  } else if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
    return AEE_EBADPARM;
  }

  // Initialize process-level and per-domain queue structures and signaling
  if (pthread_once(&queues_once, init_process_queues_once) != 0) {
    FARF(ERROR, "dspqueue init failed");
    return AEE_ERPC;
  }
  pthread_mutex_lock(&queues->mutex);
  if ((nErr = init_domain_queues_locked(domain)) != 0) {
    pthread_mutex_unlock(&queues->mutex);
    return nErr;
  }
  dq = queues->domain_queues[domain];
  if (!dq) {
    FARF(ERROR, "No queues in process for domain %d", domain);
    pthread_mutex_unlock(&queues->mutex);
    return AEE_EBADPARM;
  }
  if ((dq && (dq->num_queues >= DSPQUEUE_MAX_PROCESS_QUEUES))) {
    FARF(ERROR, "Too many queues in process for domain %d", domain);
    pthread_mutex_unlock(&queues->mutex);
    return AEE_EBADPARM;
  }
  dq->num_queues++;
  queue_count = queues->count++;
  pthread_mutex_unlock(&queues->mutex);

  // Find a free queue slot
  pthread_mutex_lock(&dq->queue_list_mutex);
  for (id = 0; id < DSPQUEUE_MAX_PROCESS_QUEUES; id++) {
    if (dq->queues[id] == UNUSED_QUEUE) {
      if (dq->max_queue < id) {
        dq->max_queue = id;
      }
      break;
    }
  }
  if (id >= DSPQUEUE_MAX_PROCESS_QUEUES) {
    FARF(ERROR, "Queue list corrupt");
    pthread_mutex_unlock(&dq->queue_list_mutex);
    nErr = AEE_ERPC;
    goto bail;
  }
  dq->queues[id] = INVALID_QUEUE;
  pthread_mutex_unlock(&dq->queue_list_mutex);

  VERIFYC(flags == 0, AEE_EBADPARM);

  // Check queue size limits
  VERIFYC(req_queue_size <= DSPQUEUE_MAX_QUEUE_SIZE, AEE_EBADPARM);
  VERIFYC(resp_queue_size <= DSPQUEUE_MAX_QUEUE_SIZE, AEE_EBADPARM);

  // Allocate internal queue structure
  VERIFYC((q = calloc(1, sizeof(*q))) != NULL, AEE_ENOMEMORY);
  VERIFY((nErr = pthread_mutex_init(&q->mutex, NULL)) == 0);
  mutex_init = 1;
  q->packet_callback = packet_callback;
  q->error_callback = error_callback;
  q->callback_context = callback_context;
  q->id = id;
  q->domain = domain;

  // Use defaults for unspecified parameters
  if (req_queue_size == 0) {
    req_queue_size = DSPQUEUE_DEFAULT_REQ_SIZE;
  }
  if (resp_queue_size == 0) {
    resp_queue_size = DSPQUEUE_DEFAULT_RESP_SIZE;
  }

  // Determine queue shared memory size and allocate memory. The memory
  // contains:
  // - Queue headers
  // - Read and write states for both request and response queues (four total)
  // - Request and response queues
  // All are aligned to QUEUE_CACHE_ALIGN.
  q->user_queue_size =
      CACHE_ALIGN_SIZE(sizeof(struct dspqueue_header)) +
      4 * CACHE_ALIGN_SIZE(sizeof(struct dspqueue_packet_queue_state)) +
      CACHE_ALIGN_SIZE(req_queue_size) + CACHE_ALIGN_SIZE(resp_queue_size);

  // Allocate queue shared memory and map to DSP
  VERIFYC((q->user_queue = rpcmem_alloc_internal(
               RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS | RPCMEM_HEAP_NOREG,
               q->user_queue_size)) != NULL,
          AEE_ENOMEMORY);
  VERIFYC((((uintptr_t)q->user_queue) & 4095) == 0, AEE_ERPC);
  VERIFYC((q->user_queue_fd = rpcmem_to_fd(q->user_queue)) > 0, AEE_ERPC);
  VERIFY((nErr = fastrpc_mmap(domain, q->user_queue_fd, q->user_queue, 0,
                              q->user_queue_size, FASTRPC_MAP_FD)) == 0);
  queue_mapped = 1;
  q->header = q->user_queue;

  // Initialize queue header, including all offsets, and clear the queue
  memset(q->header, 0, q->user_queue_size);
  q->header->version = 1;
  q->header->queue_count = queue_count;
  q->queue_count = queue_count;

  // Request packet queue (CPU->DSP)
  o = CACHE_ALIGN_SIZE(sizeof(struct dspqueue_header));
  q->header->req_queue.queue_offset = o;
  q->header->req_queue.queue_length = req_queue_size;
  o += CACHE_ALIGN_SIZE(req_queue_size);
  q->header->req_queue.read_state_offset = o;
  o += CACHE_ALIGN_SIZE(sizeof(struct dspqueue_packet_queue_state));
  q->header->req_queue.write_state_offset = o;
  o += CACHE_ALIGN_SIZE(sizeof(struct dspqueue_packet_queue_state));

  // Response packet queue (DSP->CPU)
  q->header->resp_queue.queue_offset = o;
  q->header->resp_queue.queue_length = resp_queue_size;
  o += CACHE_ALIGN_SIZE(resp_queue_size);
  q->header->resp_queue.read_state_offset = o;
  o += CACHE_ALIGN_SIZE(sizeof(struct dspqueue_packet_queue_state));
  q->header->resp_queue.write_state_offset = o;
  o += CACHE_ALIGN_SIZE(sizeof(struct dspqueue_packet_queue_state));

  assert(o == q->user_queue_size);

  dq->state->req_packet_count[id] = 0;
  cache_flush_word(&dq->state->req_packet_count[id]);
  dq->state->resp_space_count[id] = 0;
  cache_flush_word(&dq->state->resp_space_count[id]);

  // Try to create driver signals
  if (dq->have_dspsignal) {
    q->have_driver_signaling = 1;
    for (signals = 0; signals < DSPQUEUE_NUM_SIGNALS; signals++) {
      VERIFY((nErr = dspsignal_create(domain, QUEUE_SIGNAL(id, signals), 0)) ==
             AEE_SUCCESS);
    }
  }

  // First attempt to create the queue with an invalid version. If the call
  // succeeds we know the DSP side is ignoring the version and flags and does
  // not support wait counts in the header, driver signaling, or any other
  // post-v1 features. Unfortunately the initial DSP codebase ignores the
  // version and flags...
  q->header->version = UINT32_MAX;
  nErr = dspqueue_rpc_create_queue(dq->dsp_handle, q->id, q->user_queue_fd,
                                   queue_count, &q->dsp_id);
  if ((nErr == AEE_EUNSUPPORTED) ||
      (nErr == (int)(DSP_AEE_EOFFSET + AEE_EUNSUPPORTED))) {
    // OK, the DSP does pay attention to the version. It should also support
    // wait counts and optionally driver signaling. Create the queue.
    FARF(HIGH, "Initial queue create failed with %0x%x as expected", nErr);
    q->header->version = DSPQUEUE_HEADER_CURRENT_VERSION;
    q->header->flags = DSPQUEUE_HEADER_FLAG_WAIT_COUNTS;
    if (q->have_driver_signaling) {
      q->header->flags |= DSPQUEUE_HEADER_FLAG_DRIVER_SIGNALING;
    }
    VERIFY((nErr = dspqueue_rpc_create_queue(dq->dsp_handle, q->id,
                                             q->user_queue_fd, queue_count,
                                             &q->dsp_id)) == 0);
    q->have_wait_counts = 1;
    // Note that we expect the DSP will support both wait counts and driver
    // signaling or neither. However we can operate with wait counts only in
    // case we have an updated DSP image but an older CPU kernel driver without
    // driver signaling support.
  } else if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "dspqueue_rpc_create_queue failed: 0x%x", nErr);
    goto bail;
  } else {
    FARF(HIGH, "First-cut queue create succeeded unexpectedly? 0x%x", nErr);
    // No new features available, including driver signaling
    if (q->have_driver_signaling) {
      unsigned i;
      FARF(HIGH, "Driver signaling not supported on DSP, fall back to FastRPC "
                 "signaling");
      for (i = 0; i < signals; i++) {
        VERIFY((nErr = dspsignal_destroy(domain, QUEUE_SIGNAL(id, i))) == 0);
      }
      signals = 0;
    }
    q->have_driver_signaling = 0;
  }

  // Create synchronization resources
  VERIFY((nErr = pthread_mutex_init(&q->packet_mutex, NULL)) == 0);
  packetmutex = 1;
  VERIFY((nErr = pthread_mutex_init(&q->space_mutex, NULL)) == 0);
  spacemutex = 1;
  if (!q->have_driver_signaling) {
    VERIFY((nErr = pthread_cond_init(&q->packet_cond, NULL)) == 0);
    packetcond = 1;
    VERIFY((nErr = pthread_cond_init(&q->space_cond, NULL)) == 0);
    packetcond = 1;
  }

  // Callback thread (if we have a message callback)
  if (q->packet_callback) {
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
    VERIFY((nErr = pthread_create(&q->packet_callback_thread, &tattr,
                                  dspqueue_packet_callback_thread, q)) == 0);
    callbackthread = 1;
  }

  *queue = q;
  pthread_mutex_lock(&dq->queue_list_mutex);
  dq->queues[id] = q;
  pthread_mutex_unlock(&dq->queue_list_mutex);
  FARF(ALWAYS, "%s: created Queue %u, %p, DSP 0x%08x for domain %d", __func__,
       q->id, q, (unsigned)q->dsp_id, q->domain);

  return AEE_SUCCESS;

bail:
  if (q) {
    if (callbackthread) {
      if (q->have_driver_signaling) {
        dspsignal_cancel_wait(domain,
                              QUEUE_SIGNAL(id, DSPQUEUE_SIGNAL_RESP_PACKET));
      } else {
        pthread_mutex_lock(&q->packet_mutex);
        q->packet_mask |= SIGNAL_BIT_CANCEL;
        pthread_cond_signal(&q->packet_cond);
        pthread_mutex_unlock(&q->packet_mutex);
      }
    }
    if (q->have_driver_signaling && (signals > 0)) {
      unsigned i;
      for (i = 0; i < signals; i++) {
        dspsignal_destroy(domain, QUEUE_SIGNAL(id, i));
      }
    }
    if (packetmutex) {
      pthread_mutex_destroy(&q->packet_mutex);
    }
    if (packetcond) {
      pthread_cond_destroy(&q->packet_cond);
    }
    if (spacemutex) {
      pthread_mutex_destroy(&q->space_mutex);
    }
    if (spacecond) {
      pthread_cond_destroy(&q->space_cond);
    }
    if (q->dsp_id) {
      dspqueue_rpc_destroy_queue(dq->dsp_handle, q->dsp_id);
    }
    if (queue_mapped) {
      fastrpc_munmap(domain, q->user_queue_fd, q->user_queue,
                     q->user_queue_size);
    }
    if (q->user_queue) {
      rpcmem_free(q->user_queue);
    }
    if (mutex_init) {
      pthread_mutex_destroy(&q->mutex);
    }
    free(q);
  }
  if (dq != NULL) {
    if (id < DSPQUEUE_MAX_PROCESS_QUEUES) {
      pthread_mutex_lock(&dq->queue_list_mutex);
      dq->queues[id] = UNUSED_QUEUE;
      pthread_mutex_unlock(&dq->queue_list_mutex);
    }
    pthread_mutex_lock(&queues->mutex);
    assert(dq->num_queues > 0);
    dq->num_queues--;
    if (dq->num_queues == 0) {
      // This would have been the first queue for this domain
      destroy_domain_queues_locked(domain);
    }
    pthread_mutex_unlock(&queues->mutex);
  }
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed (domain %d, flags 0x%x, sizes %u, %u errno %s)",
         nErr, __func__, domain, (unsigned)flags, (unsigned)req_queue_size,
         (unsigned)resp_queue_size, strerror(errno));
  }
  return nErr;
}

/*
 * Clean-up multi-domain queue
 * This function calls 'dspqueue_close' on each individual queue.
 */
static int dspqueue_multidomain_close(struct dspqueue *q, bool queue_mut) {
	int nErr = AEE_SUCCESS, err = AEE_SUCCESS;
	struct dspqueue_multidomain *mdq = NULL;
	dspqueue_t queue = NULL;

	VERIFYC(q, AEE_EBADPARM);

	mdq = &q->mdq;
	VERIFYC(mdq->is_mdq, AEE_EINVALIDITEM);

	if (queue_mut) {
		pthread_mutex_lock(&q->mutex);
	}

	// Close individual queues
	for (unsigned int ii = 0; ii < mdq->num_domain_ids; ii++) {
		queue = mdq->queues[ii];
		if (!queue)
			continue;

		err = dspqueue_close(queue);
		if (err) {
			// Return the first failure's error code to client
			nErr = nErr ? nErr : err;
		}
	}
	if (mdq->effec_domain_ids) {
		free(mdq->effec_domain_ids);
	}
	if (mdq->queues) {
		free(mdq->queues);
	}
	if (mdq->dsp_ids) {
		free(mdq->dsp_ids);
	}
	if (queue_mut) {
		pthread_mutex_unlock(&q->mutex);
		pthread_mutex_destroy(&q->mutex);
	}
	FARF(ALWAYS, "%s: closed queue %p (ctx 0x%"PRIx64"), num domains %u",
		__func__, q, mdq->ctx, mdq->num_domain_ids);
	free(q);
bail:
	if (nErr) {
		FARF(ALWAYS, "Error 0x%x: %s: failed for queue %p",
			nErr, __func__, q);
	}
	return nErr;
}

AEEResult dspqueue_close(dspqueue_t queue) {

  struct dspqueue *q = queue;
  struct dspqueue_domain_queues *dq = NULL;
  AEEResult nErr = AEE_SUCCESS;
  int32_t imported;
  unsigned i;

  errno = 0;
  VERIFYC(q, AEE_EBADPARM);

  if (q->mdq.is_mdq) {
    // Recursively call 'dspqueue_close' on each individual queue
    return dspqueue_multidomain_close(q, true);
  }

  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(q->domain), AEE_EINVALIDDOMAIN);
  pthread_mutex_lock(&queues->mutex);
  dq = queues->domain_queues[q->domain];
  if (dq == NULL) {
    FARF(ERROR, "No domain queues");
    pthread_mutex_unlock(&queues->mutex);
    return AEE_ERPC;
  }
  pthread_mutex_unlock(&queues->mutex);

  // Check if the queue is still imported on the DSP
  if (!dq->dsp_error) {
    VERIFY((nErr = dspqueue_rpc_is_imported(dq->dsp_handle, q->dsp_id,
                                            &imported)) == 0);
    if (imported) {
      FARF(ERROR, "Attempting to close queue 0x%p still open on the DSP",
           queue);
      nErr = AEE_EBADPARM;
      goto bail;
    }
  }

  VERIFYC(q->header->queue_count == q->queue_count, AEE_ERPC);

  pthread_mutex_lock(&dq->queue_list_mutex);
  dq->queues[q->id] = INVALID_QUEUE;
  pthread_mutex_unlock(&dq->queue_list_mutex);

  if (q->error_callback_thread) {
    nErr = pthread_join(q->error_callback_thread, NULL);
    if (nErr == EDEADLK || nErr == EINVAL) {
      FARF(ERROR,
           "Error %d: %s: Error callback thread join failed for thread : %d",
           nErr, __func__, q->error_callback_thread);
      nErr = AEE_ERPC;
      q->error_callback_thread = 0;
      goto bail;
    }
    nErr = AEE_SUCCESS;
    q->error_callback_thread = 0;
  }

  // Cancel any outstanding blocking read/write calls (from callback threads)
  if (q->have_driver_signaling) {
    /*
     * Cancel driver signal waits
     * Not required in case of SSR (i.e AEE_ECONNRESET)
     * as signals are cancelled by the SSR handle.
     */
    if (dq->dsp_error != AEE_ECONNRESET) {
      for (i = 0; i < DSPQUEUE_NUM_SIGNALS; i++) {
        nErr = dspsignal_cancel_wait(q->domain, QUEUE_SIGNAL(q->id, i));
        if (nErr && nErr != AEE_EBADSTATE) {
          goto bail;
        }
      }
    }
  } else {
    pthread_mutex_lock(&q->packet_mutex);
    q->packet_mask |= SIGNAL_BIT_CANCEL;
    pthread_cond_broadcast(&q->packet_cond);
    pthread_mutex_unlock(&q->packet_mutex);
    pthread_mutex_lock(&q->space_mutex);
    q->space_mask |= SIGNAL_BIT_CANCEL;
    pthread_cond_broadcast(&q->space_cond);
    pthread_mutex_unlock(&q->space_mutex);
  }

  if (q->packet_callback) {
    void *ret;
    FARF(MEDIUM, "Join packet callback thread");
    nErr = pthread_join(q->packet_callback_thread, &ret);
    /* Ignore error if thread has already exited */
    if (nErr && nErr != ESRCH) {
      FARF(
          ERROR,
          "Error: %s: packet callback thread for queue %p joined with error %d",
          __func__, q, nErr);
      goto bail;
    }
    FARF(MEDIUM, " - Join packet callback thread done");
    VERIFY((uintptr_t)ret == 0);
  }

  if (dq->dsp_error) {
    // Ignore errors if the process died
    dspqueue_rpc_destroy_queue(dq->dsp_handle, q->dsp_id);
    fastrpc_munmap(dq->domain, q->user_queue_fd, q->user_queue,
                   q->user_queue_size);
  } else {
    VERIFY((nErr = dspqueue_rpc_destroy_queue(dq->dsp_handle, q->dsp_id)) == 0);
    VERIFY((nErr = fastrpc_munmap(dq->domain, q->user_queue_fd, q->user_queue,
                                  q->user_queue_size)) == 0);
  }
  rpcmem_free(q->user_queue);
  /*
   * In case of SSR (i.e., AEE_ECONNRESET), there is no need to call
   * dspsignal_destroy as the process close during SSR cleans up
   * signals.
   */
  if (q->have_driver_signaling) {
    if (dq->dsp_error != AEE_ECONNRESET) {
      FARF(MEDIUM, "%s: Destroy signals", __func__);
      for (i = 0; i < DSPQUEUE_NUM_SIGNALS; i++) {
        nErr = dspsignal_destroy(q->domain, QUEUE_SIGNAL(q->id, i));
        if (nErr && nErr != AEE_EBADSTATE) {
          goto bail;
        }
      }
    }
  }

  if (!q->have_driver_signaling) {
    pthread_cond_destroy(&q->packet_cond);
    pthread_cond_destroy(&q->space_cond);
  }
  pthread_mutex_destroy(&q->packet_mutex);
  pthread_mutex_destroy(&q->space_mutex);
  pthread_mutex_destroy(&q->mutex);

  pthread_mutex_lock(&dq->queue_list_mutex);
  dq->queues[q->id] = UNUSED_QUEUE;
  dq->max_queue = 0;
  for (i = 0; i < DSPQUEUE_MAX_PROCESS_QUEUES; i++) {
    if (dq->queues[i] != UNUSED_QUEUE) {
      dq->max_queue = i;
    }
  }
  pthread_mutex_unlock(&dq->queue_list_mutex);

  pthread_mutex_lock(&queues->mutex);
  dq->num_queues--;
  if (dq->num_queues == 0) {
    FARF(ALWAYS, "%s: destroying queues and signals for domain %d", __func__,
         q->domain);
    destroy_domain_queues_locked(q->domain);
    if (q->have_driver_signaling)
      dspsignal_domain_deinit(q->domain);
  }
  pthread_mutex_unlock(&queues->mutex);
  FARF(ALWAYS, "%s: closed Queue %u, %p, DSP 0x%08x for domain %d", __func__,
       q->id, q, (unsigned)q->dsp_id, q->domain);
  free(q);

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed errno %s", nErr, __func__,
         strerror(errno));
  }
  return nErr;
}

AEEResult dspqueue_export(dspqueue_t queue, uint64_t *queue_id) {

  struct dspqueue *q = queue;

  if (q->mdq.is_mdq) {
    FARF(ALWAYS, "Warning: %s not supported for multi-domain queue, already exported during create",
            __func__);
    return AEE_EUNSUPPORTED;
  }
  *queue_id = q->dsp_id;
  return AEE_SUCCESS;
}

static int dspqueue_multidomain_create(dspqueue_create_req *create) {
	int nErr = AEE_SUCCESS;
	bool queue_mut = false;
	unsigned int *effec_domain_ids = NULL;
	unsigned int num_domain_ids = 0, size = 0;
	struct dspqueue *q = NULL;
	struct dspqueue_multidomain *mdq = NULL;

	VERIFY(AEE_SUCCESS == (nErr = fastrpc_context_get_domains(create->ctx,
		&effec_domain_ids, &num_domain_ids)));

	// Validate output parameter pointers
	VERIFYC(create->ids && !create->priority && !create->flags,
		AEE_EBADPARM);
	VERIFYC(create->num_ids >= num_domain_ids, AEE_EBADPARM);

	create->queue = NULL;
	size = create->num_ids * sizeof(*(create->ids));
	memset(create->ids, 0, size);
	errno = 0;

	VERIFYC(AEE_SUCCESS == (nErr = pthread_once(&queues_once,
		init_process_queues_once)), AEE_ENOTINITIALIZED);

	// Alloc & init queue struct
	VERIFYC(NULL != (q = calloc(1, sizeof(*q))), AEE_ENOMEMORY);
	mdq = &q->mdq;
	mdq->is_mdq = true;
	mdq->ctx = create->ctx;

	VERIFYC(AEE_SUCCESS == (nErr = pthread_mutex_init(&q->mutex, NULL)),
		AEE_ENOTINITIALIZED);
	queue_mut = true;

	// Alloc & init multi-domain queue specific info
	mdq->num_domain_ids = num_domain_ids;

	size = num_domain_ids * sizeof(*(mdq->effec_domain_ids));
	VERIFYC(NULL != (mdq->effec_domain_ids = calloc(1, size)),
		AEE_ENOMEMORY);
	memcpy(mdq->effec_domain_ids, effec_domain_ids, size);

	VERIFYC(NULL != (mdq->queues = calloc(num_domain_ids,
		sizeof(*(mdq->queues)))), AEE_ENOMEMORY);

	size = num_domain_ids * sizeof(*(mdq->dsp_ids));
	VERIFYC(NULL != (mdq->dsp_ids = calloc(1, size)), AEE_ENOMEMORY);

	// Create queue on each individual domain
	for (unsigned int ii = 0; ii < num_domain_ids; ii++) {
		VERIFY(AEE_SUCCESS == (nErr = dspqueue_create(effec_domain_ids[ii],
			create->flags, create->req_queue_size, create->resp_queue_size,
			create->packet_callback, create->error_callback,
			create->callback_context, &mdq->queues[ii])));

		// Export queue and get queue id for that domain
		VERIFY(AEE_SUCCESS == (nErr = dspqueue_export(mdq->queues[ii],
			&mdq->dsp_ids[ii])));
	}

	// Return queue handle and list of queue ids to user
	create->queue = q;
	memcpy(create->ids, mdq->dsp_ids, size);

	FARF(ALWAYS, "%s: created queue %p for ctx 0x%"PRIx64", sizes: req %u, rsp %u, num domains %u",
		__func__, q, create->ctx, create->req_queue_size,
			create->resp_queue_size, num_domain_ids);
bail:
	if (nErr) {
		FARF(ERROR, "Error 0x%x: %s failed for ctx 0x%"PRIx64", queue sizes: req %u, rsp %u, num ids %u",
			nErr, __func__, create->ctx, create->req_queue_size,
			create->resp_queue_size, create->num_ids);

		dspqueue_multidomain_close(q, queue_mut);
	}
	return nErr;
}

int dspqueue_request(dspqueue_request_payload *req) {
	int nErr = AEE_SUCCESS, req_id = -1;

	VERIFYC(req, AEE_EBADPARM);
	req_id = req->id;

	switch(req_id) {
		case DSPQUEUE_CREATE:
		{
			VERIFY(AEE_SUCCESS == (nErr =
				dspqueue_multidomain_create(&req->create)));
			break;
		}
		default:
			nErr = AEE_EUNSUPPORTED;
			break;
	}
bail:
	if (nErr)
		FARF(ALWAYS, "Error 0x%x: %s failed", nErr, __func__);

	return nErr;
}

static void get_queue_state_write(void *memory,
                                  struct dspqueue_packet_queue_header *pq,
                                  uint32_t *space_left, uint32_t *read_pos,
                                  uint32_t *write_pos) {

  struct dspqueue_packet_queue_state *read_state =
      (struct dspqueue_packet_queue_state *)(((uintptr_t)memory) +
                                             pq->read_state_offset);
  struct dspqueue_packet_queue_state *write_state =
      (struct dspqueue_packet_queue_state *)(((uintptr_t)memory) +
                                             pq->write_state_offset);
  uint32_t qsize = pq->queue_length;
  uint32_t r, w, qleft;

  cache_invalidate_word(&read_state->position);
  r = read_state->position;
  barrier_full();
  w = write_state->position;
  assert(((r & 7) == 0) && ((w & 7) == 0));
  if (space_left != NULL) {
    if (r == w) {
      qleft = qsize - 8;
    } else if (w > r) {
      qleft = qsize - w + r - 8;
    } else {
      qleft = r - w - 8;
    }
    assert((qleft & 7) == 0);
    *space_left = qleft;
  }
  if (read_pos != NULL) {
    *read_pos = r;
  }
  if (write_pos != NULL) {
    *write_pos = w;
  }
}

static void get_queue_state_read(void *memory,
                                 struct dspqueue_packet_queue_header *pq,
                                 uint32_t *data_left, uint32_t *read_pos,
                                 uint32_t *write_pos) {

  struct dspqueue_packet_queue_state *read_state =
      (struct dspqueue_packet_queue_state *)(((uintptr_t)memory) +
                                             pq->read_state_offset);
  struct dspqueue_packet_queue_state *write_state =
      (struct dspqueue_packet_queue_state *)(((uintptr_t)memory) +
                                             pq->write_state_offset);
  uint32_t qsize = pq->queue_length;
  uint32_t r, w, qleft;

  cache_invalidate_word(&write_state->position);
  w = write_state->position;
  barrier_full();
  r = read_state->position;
  assert(((r & 7) == 0) && ((w & 7) == 0));
  if (data_left != NULL) {
    if (r == w) {
      qleft = 0;
    } else if (w > r) {
      qleft = w - r;
    } else {
      qleft = qsize - r + w;
    }
    assert((qleft & 7) == 0);
    *data_left = qleft;
  }
  if (read_pos != NULL) {
    *read_pos = r;
  }
  if (write_pos != NULL) {
    *write_pos = w;
  }
}

static inline uint32_t write_64(volatile uint8_t *packet_queue,
                                uint32_t write_pos, uint32_t queue_len,
                                uint64_t data) {
  *(volatile uint64_t *)((uintptr_t)packet_queue + write_pos) = data;
  cache_flush_line((void *)((uintptr_t)packet_queue + write_pos));
  write_pos += 8;
  if (write_pos >= queue_len) {
    write_pos = 0;
  }
  return write_pos;
}

static inline uint32_t write_data(volatile uint8_t *packet_queue,
                                  uint32_t write_pos, uint32_t queue_len,
                                  const void *data, uint32_t data_len) {

  uintptr_t qp = (uintptr_t)packet_queue;

  assert(data != NULL);
  assert(data_len > 0);
  assert((write_pos & 7) == 0);
  assert((queue_len - write_pos) >= data_len);

  memcpy((void *)(qp + write_pos), data, data_len);
  cache_flush((void *)(qp + write_pos), data_len);
  write_pos += (data_len + 7) & (~7);
  assert(write_pos <= queue_len);
  if (write_pos >= queue_len) {
    write_pos = 0;
  }

  return write_pos;
}

// Timespec difference in microseconds (a-b). If a<b returns zero; if the
// difference is >UINT32_MAX returns UINT32_MAX
static uint32_t timespec_diff_us(struct timespec *a, struct timespec *b) {
  int64_t diffsec = ((int64_t)a->tv_sec) - ((int64_t)b->tv_sec);
  int64_t diffnsec = a->tv_nsec - b->tv_nsec;
  int64_t diffusec;

  if ((diffsec < 0) || ((diffsec == 0) && (diffnsec < 0))) {
    return 0;
  }
  if (diffsec > UINT32_MAX) {
    // Would overflow for sure
    return UINT32_MAX;
  }
  diffusec = (diffsec * 1000000LL) + (diffnsec / 1000LL);
  if (diffusec > UINT32_MAX) {
    return UINT32_MAX;
  }
  return (uint32_t)diffusec;
}

// Send a signal
static AEEResult send_signal(struct dspqueue *q, uint32_t signal_no) {

  struct dspqueue_domain_queues *dq = queues->domain_queues[q->domain];
  int nErr = AEE_SUCCESS;

  if (q->have_driver_signaling) {
    VERIFYC(signal_no < DSPQUEUE_NUM_SIGNALS, AEE_EBADPARM);
    VERIFY((nErr = dspsignal_signal(q->domain,
                                    QUEUE_SIGNAL(q->id, signal_no))) == 0);
  } else {
    pthread_mutex_lock(&dq->send_signal_mutex);
    dq->send_signal_mask |= SIGNAL_BIT_SIGNAL;
    pthread_cond_signal(&dq->send_signal_cond);
    pthread_mutex_unlock(&dq->send_signal_mutex);
  }

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for queue %p signal %u", nErr, __func__,
         q, (unsigned)signal_no);
  }
  return nErr;
}

// Wait for a signal with an optional timeout.
// Set timeout to the expiry time (not length of timeout) or NULL for an
// infinite wait. Only DSPQUEUE_SIGNAL_REQ_SPACE and DSPQUEUE_SIGNAL_RESP_PACKET
// supported. The appropriate mutex must be locked.
static AEEResult wait_signal_locked(struct dspqueue *q, uint32_t signal_no,
                                    struct timespec *timeout) {

  AEEResult nErr = AEE_SUCCESS;

  if (q->have_driver_signaling) {
    uint32_t to_now;

    if (timeout) {
      struct timespec now;
      VERIFYC(clock_gettime(CLOCK_REALTIME, &now) == 0, AEE_EFAILED);
      to_now = timespec_diff_us(timeout, &now); // microseconds until expiry
      if (to_now == 0) {
        return AEE_EEXPIRED;
      }
    } else {
      to_now = DSPSIGNAL_TIMEOUT_NONE;
    }

    VERIFY((nErr = dspsignal_wait(q->domain, QUEUE_SIGNAL(q->id, signal_no),
                                  to_now)) == 0);

  } else {
    // Not using driver signaling, wait for the appropriate condition variable
    // and its associated state
    uint32_t *count;
    uint32_t *mask;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    uint32_t c;

    if (signal_no == DSPQUEUE_SIGNAL_REQ_SPACE) {
      count = &q->req_space_count;
      mask = &q->space_mask;
      mutex = &q->space_mutex;
      cond = &q->space_cond;
    } else if (signal_no == DSPQUEUE_SIGNAL_RESP_PACKET) {
      count = &q->resp_packet_count;
      mask = &q->packet_mask;
      mutex = &q->packet_mutex;
      cond = &q->packet_cond;
    } else {
      nErr = AEE_EBADPARM;
      goto bail;
    }

    c = *count;
    if (timeout) {
      int rc = 0;
      while ((c == *count) && (rc == 0)) {
        if (*mask & 2) {
          return AEE_EINTERRUPTED;
        }
        rc = pthread_cond_timedwait(cond, mutex, timeout);
        if (rc == ETIMEDOUT) {
          return AEE_EEXPIRED;
        }
        VERIFY(rc == 0);
      }
    } else {
      while (c == *count) {
        pthread_cond_wait(cond, mutex);
        if (*mask & 2) {
          return AEE_EINTERRUPTED;
        }
      }
    }
  }

bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for queue %p signal %u", nErr, __func__,
         q, (unsigned)signal_no);
  }
  return nErr;
}

/* Write packet to multi-domain queue */
static int dspqueue_multidomain_write(struct dspqueue *q, uint32_t flags,
	uint32_t num_buffers, struct dspqueue_buffer *buffers,
	uint32_t message_length, const uint8_t *message,
	uint32_t timeout_us, bool block) {
	int nErr = AEE_SUCCESS;
	struct dspqueue_multidomain *mdq = NULL;
	bool locked = false;

	VERIFYC(q, AEE_EBADPARM);

	mdq = &q->mdq;
	VERIFYC(mdq->is_mdq, AEE_EINVALIDITEM);

	// Only one multi-domain write request at a time.
	pthread_mutex_lock(&q->mutex);
	locked = true;

	// Write packet to individual queues
	for (unsigned int ii = 0; ii < mdq->num_domain_ids; ii++) {
		if (block) {
			/*
			 * Multi-domain blocking write operation will serially block
			 * on each individual queue.
			 */
			VERIFY(AEE_SUCCESS == (nErr = dspqueue_write(mdq->queues[ii],
				flags, num_buffers, buffers, message_length, message,
				timeout_us)));
		} else {
			VERIFY(AEE_SUCCESS == (nErr = dspqueue_write_noblock(
				mdq->queues[ii], flags, num_buffers, buffers,
				message_length, message)));
		}
	}
bail:
	if (locked)
		pthread_mutex_unlock(&q->mutex);

	if (nErr) {
		/*
		 * If multi-domain write operation failed on some but not all
		 * queues, then the MDQ is now in an irrecoverable bad-state as
		 * the packet was partially written to some domains but not all
		 * and it cannot be "erased". Client is expected to close queue.
		 */
		nErr = AEE_EBADSTATE;
		FARF(ERROR, "Error 0x%x: %s (block %d): failed for queue %p, flags 0x%x, num bufs %u, msg len %u, timeout %u",
			nErr, __func__, block, q, flags, num_buffers,
			message_length, timeout_us);
	}
	return nErr;
}

AEEResult dspqueue_write_noblock(dspqueue_t queue, uint32_t flags,
                                 uint32_t num_buffers,
                                 struct dspqueue_buffer *buffers,
                                 uint32_t message_length,
                                 const uint8_t *message) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspqueue *q = queue;
  struct dspqueue_packet_queue_header *pq = NULL;
  volatile uint8_t *qp = NULL;
  struct dspqueue_packet_queue_state *read_state = NULL;
  struct dspqueue_packet_queue_state *write_state = NULL;
  struct dspqueue_domain_queues *dq = NULL;
  unsigned len, alen;
  uint32_t r, w;
  uint32_t qleft = 0, qsize = 0;
  int locked = 0;
  uint64_t phdr;
  int wrap = 0;
  uint32_t i;
  uint32_t buf_refs = 0;

  if (q->mdq.is_mdq) {
    // Recursively call 'dspqueue_write_noblock' on individual queues
    return dspqueue_multidomain_write(q, flags, num_buffers, buffers,
              message_length, message, 0, false);
  }

  pq = &q->header->req_queue;
  qp = (volatile uint8_t*) (((uintptr_t)q->header) + pq->queue_offset);
  read_state = (struct dspqueue_packet_queue_state*) (((uintptr_t)q->header)
                        + pq->read_state_offset);
  write_state = (struct dspqueue_packet_queue_state*) (((uintptr_t)q->header)
                        + pq->write_state_offset);
  dq = queues->domain_queues[q->domain];
  qsize = pq->queue_length;

  // Check properties
  VERIFYC(num_buffers <= DSPQUEUE_MAX_BUFFERS, AEE_EBADPARM);
  VERIFYC(message_length <= DSPQUEUE_MAX_MESSAGE_SIZE, AEE_EBADPARM);

  // Prepare flags
  if (num_buffers > 0) {
    flags |= DSPQUEUE_PACKET_FLAG_BUFFERS;
    VERIFYC(buffers != NULL, AEE_EBADPARM);
  } else {
    flags &= ~DSPQUEUE_PACKET_FLAG_BUFFERS;
  }
  if (message_length > 0) {
    flags |= DSPQUEUE_PACKET_FLAG_MESSAGE;
    VERIFYC(message != NULL, AEE_EBADPARM);
  } else {
    flags &= ~DSPQUEUE_PACKET_FLAG_MESSAGE;
  }

  // Calculate packet length in the queue
  assert(sizeof(struct dspqueue_buffer) == 24);
  len = 8 + num_buffers * sizeof(struct dspqueue_buffer) + message_length;
  alen = (len + 7) & (~7);

  if (alen > (qsize - 8)) {
    FARF(ERROR, "Packet size %u too large for queue size %u", (unsigned)len,
         (unsigned)qsize);
    nErr = AEE_EBADPARM;
    goto bail;
  }

  pthread_mutex_lock(&q->mutex);
  locked = 1;

  VERIFYC(q->header->queue_count == q->queue_count, AEE_ERPC);

  // Check that we have space for the packet in the queue
  get_queue_state_write(q->header, pq, &qleft, &r, &w);
  if (qleft < alen) {
    pthread_mutex_unlock(&q->mutex);
    return AEE_EWOULDBLOCK;
  }
  if ((qsize - w) < alen) {
    // Don't wrap the packet around queue end, but rather move it to the
    // beginning, replicating the header
    wrap = 1;
    if ((qleft - (qsize - w)) < alen) {
      pthread_mutex_unlock(&q->mutex);
      return AEE_EWOULDBLOCK;
    }
  }

  // Go through buffers
  for (i = 0; i < num_buffers; i++) {
    struct dspqueue_buffer *b = &buffers[i];
    void *va;
    size_t size;

    // Find buffer in internal FastRPC structures and handle refcounts
    if (b->flags & DSPQUEUE_BUFFER_FLAG_REF) {
      VERIFYC((b->flags & DSPQUEUE_BUFFER_FLAG_DEREF) == 0, AEE_EBADPARM);
      nErr = fastrpc_buffer_ref(q->domain, b->fd, 1, &va, &size);
    } else if (b->flags & DSPQUEUE_BUFFER_FLAG_DEREF) {
      nErr = fastrpc_buffer_ref(q->domain, b->fd, -1, &va, &size);
    } else {
      nErr = fastrpc_buffer_ref(q->domain, b->fd, 0, &va, &size);
    }
    if (nErr == AEE_ENOSUCHMAP) {
      FARF(ERROR, "Buffer FD %d in queue message not mapped to domain %d",
           b->fd, q->domain);
      goto bail;
    }
    VERIFY(nErr == 0);
    buf_refs = i + 1;

    // Ensure buffer offset and size are within the buffer as mapped.
    // Use mapped size if not specified by the client
    if (b->size != 0) {
      uint64_t bend = ((uint64_t)b->offset) + ((uint64_t)b->size);
      VERIFYC(bend <= size, AEE_EBADPARM);
      // Calculate new bounds for cache ops
      va = (void *)(((uintptr_t)va) + b->offset);
      size = b->size;
    } else {
      VERIFYC(b->offset == 0, AEE_EBADPARM);
    }
  }

  // Write packet header
  flags |= DSPQUEUE_PACKET_FLAG_USER_READY;
  phdr =
      (((uint64_t)(len & 0xffffffff)) | (((uint64_t)(flags & 0xffff)) << 32) |
       (((uint64_t)(num_buffers & 0xff)) << 48) |
       (((uint64_t)(q->seq_no & 0xff)) << 56));
  w = write_64(qp, w, qsize, phdr);
  if (wrap) {
    // Write the packet at the beginning of the queue,
    // replicating the header
    w = write_64(qp, 0, qsize, phdr);
  }

  // Write buffer information
  if (num_buffers > 0) {
    w = write_data(qp, w, qsize, buffers,
                   num_buffers * sizeof(struct dspqueue_buffer));
  }

  // Write message
  if (message_length > 0) {
    w = write_data(qp, w, qsize, message, message_length);
  }

  // Update write pointer. This marks the message available in the user queue
  q->write_packet_count++;
  barrier_store();
  write_state->position = w;
  write_state->packet_count = q->write_packet_count;
  cache_flush_line(write_state);

  // Signal that we've written a packet
  q->req_packet_count++;
  dq->state->req_packet_count[q->id] = q->req_packet_count;
  FARF(LOW, "Queue %u req_packet_count %u", (unsigned)q->id,
       (unsigned)q->req_packet_count);
  cache_flush_word(&dq->state->req_packet_count[q->id]);
  if (q->have_wait_counts) {
    // Only send a signal if the other end is potentially waiting
    barrier_full();
    cache_invalidate_word(&read_state->wait_count);
    if (read_state->wait_count) {
      FARF(MEDIUM, "%s: Send signal", __func__);
      VERIFY((nErr = send_signal(q, DSPQUEUE_SIGNAL_REQ_PACKET)) == 0);
    } else {
      FARF(MEDIUM, "%s: Don't send signal", __func__);
    }
  } else {
    FARF(MEDIUM, "%s: No wait counts - send signal", __func__);
    VERIFY((nErr = send_signal(q, DSPQUEUE_SIGNAL_REQ_PACKET)) == 0);
  }

  q->seq_no++;

  pthread_mutex_unlock(&q->mutex);
  locked = 0;
  return 0;

bail:
  for (i = 0; i < buf_refs; i++) {
    // Undo buffer reference changes
    struct dspqueue_buffer *b = &buffers[i];
    if (b->flags & DSPQUEUE_BUFFER_FLAG_REF) {
      fastrpc_buffer_ref(q->domain, b->fd, -1, NULL, NULL);
    } else if (b->flags & DSPQUEUE_BUFFER_FLAG_DEREF) {
      fastrpc_buffer_ref(q->domain, b->fd, 1, NULL, NULL);
    }
  }
  if (locked) {
    pthread_mutex_unlock(&q->mutex);
  }
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for queue %p (flags 0x%x, num_buffers %u, "
         "message_length %u)",
         nErr, __func__, queue, (unsigned)flags, (unsigned)num_buffers,
         (unsigned)message_length);
  }
  return nErr;
}

static void timespec_add_us(struct timespec *ts, uint32_t us) {
  uint64_t ns = (uint64_t)ts->tv_nsec + (uint64_t)(1000 * (us % 1000000));
  if (ns > 1000000000ULL) {
    ts->tv_nsec = (long)(ns - 1000000000ULL);
    ts->tv_sec += (us / 1000000) + 1;
  } else {
    ts->tv_nsec = ns;
    ts->tv_sec += us / 1000000;
  }
}

AEEResult dspqueue_write(dspqueue_t queue, uint32_t flags, uint32_t num_buffers,
                         struct dspqueue_buffer *buffers,
                         uint32_t message_length, const uint8_t *message,
                         uint32_t timeout_us) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspqueue *q = queue;
  struct dspqueue_packet_queue_header *pq = NULL;
  struct dspqueue_packet_queue_state *write_state = NULL;
  _Atomic uint32_t *wait_count = NULL;
  int waiting = 0;
  struct timespec *timeout_ts = NULL; // no timeout by default
  struct timespec ts;

  errno = 0;
  if (q->mdq.is_mdq) {
    // Recursively call 'dspqueue_write' on individual queues.
    return dspqueue_multidomain_write(q, flags, num_buffers, buffers,
                    message_length, message, timeout_us, true);
  }

  pq = &q->header->req_queue;
  write_state = (struct dspqueue_packet_queue_state*)(((uintptr_t)q->header)
                        + pq->write_state_offset);
  wait_count = (_Atomic uint32_t*) &write_state->wait_count;

  pthread_mutex_lock(&q->space_mutex);

  // Try a write first before dealing with timeouts
  nErr = dspqueue_write_noblock(queue, flags, num_buffers, buffers,
                                message_length, message);
  if (nErr != AEE_EWOULDBLOCK) {
    // Write got through or failed permanently
    goto bail;
  }

  if (q->have_wait_counts) {
    // Flag that we're potentially waiting and try again
    atomic_fetch_add(wait_count, 1);
    cache_flush_word(wait_count);
    waiting = 1;
    nErr = dspqueue_write_noblock(queue, flags, num_buffers, buffers,
                                  message_length, message);
    if (nErr != AEE_EWOULDBLOCK) {
      goto bail;
    }
  }

  if (timeout_us != DSPQUEUE_TIMEOUT_NONE) {
    // Calculate timeout expiry and use timeout
    VERIFYC(clock_gettime(CLOCK_REALTIME, &ts) == 0, AEE_EFAILED);
    timespec_add_us(&ts, timeout_us);
    timeout_ts = &ts;
  }

  while (1) {
    FARF(LOW, "Queue %u wait space", (unsigned)q->id);
    VERIFY((nErr = wait_signal_locked(q, DSPQUEUE_SIGNAL_REQ_SPACE,
                                      timeout_ts)) == 0);
    FARF(LOW, "Queue %u got space", (unsigned)q->id);
    nErr = dspqueue_write_noblock(queue, flags, num_buffers, buffers,
                                  message_length, message);
    if (nErr != AEE_EWOULDBLOCK) {
      goto bail;
    }
  }

bail:
  if (waiting) {
    atomic_fetch_sub(wait_count, 1);
    cache_flush_word(wait_count);
  }
  pthread_mutex_unlock(&q->space_mutex);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for queue %p (flags 0x%x, num_buffers %u, "
         "message_length %u errno %s)",
         nErr, __func__, queue, (unsigned)flags, (unsigned)num_buffers,
         (unsigned)message_length, strerror(errno));
  }
  return nErr;
}

AEEResult dspqueue_write_early_wakeup_noblock(dspqueue_t queue,
                                              uint32_t wakeup_delay,
                                              uint32_t packet_flags) {

  uint32_t flags = packet_flags | DSPQUEUE_PACKET_FLAG_WAKEUP;
  struct dspqueue *q = queue;
  const uint8_t *msg = wakeup_delay ? (const uint8_t *)&wakeup_delay : NULL;
  uint32_t msg_len = wakeup_delay ? sizeof(wakeup_delay) : 0;

  if (q->mdq.is_mdq) {
    // Recursively call 'dspqueue_write_noblock' on individual queues
    return dspqueue_multidomain_write(q, flags, 0, NULL,
                msg_len, msg, 0, false);
  }
  return dspqueue_write_noblock(queue, flags, 0, NULL, msg_len, msg);
}

static AEEResult peek_locked(volatile const uint8_t *qp, uint32_t r,
                             uint32_t *flags, uint32_t *num_buffers,
                             uint32_t *message_length, uint64_t *raw_header) {

  AEEResult nErr = 0;
  uint32_t f, nb, len;
  uint64_t d;

  // Read packet header
  cache_invalidate_line((void *)((uintptr_t)qp + r));
  d = *((uint64_t *)((uintptr_t)qp + r));
  len = d & 0xffffffff;
  f = (d >> 32) & 0xffff;
  if (f & DSPQUEUE_PACKET_FLAG_BUFFERS) {
    nb = (d >> 48) & 0xff;
  } else {
    nb = 0;
  }
  VERIFYC(len >= (8 + nb * sizeof(struct dspqueue_buffer)), AEE_EBADITEM);

  // Populate response
  if (flags != NULL) {
    *flags = f;
  }
  if (num_buffers != NULL) {
    *num_buffers = nb;
  }
  if (message_length != NULL) {
    if (f & DSPQUEUE_PACKET_FLAG_MESSAGE) {
      *message_length = len - 8 - nb * sizeof(struct dspqueue_buffer);
    } else {
      *message_length = 0;
    }
  }
  if (raw_header != NULL) {
    *raw_header = d;
  }

  // Fall through
bail:
  return nErr;
}

/*
 * Read / peek packet from multi-domain queue
 *
 * Individual queue of each domain will be read / peeked in a serial
 * manner. Packet / packet info will be returned from the first queue
 * where a valid packet is found.
 *
 * If multiple domains have written responses to their queues, then client
 * is expected to call dspqueue_read on the multi-domain queue as many
 * times to consume all the packets.
 *
 * In case of peeking, client is expected to read the packet already
 * peeked before peeking the next packet.
 *
 * If all the individual queues are empty, then no packet is read.
 */
static int dspqueue_multidomain_read(struct dspqueue *q, uint32_t *flags,
	uint32_t max_buffers, uint32_t *num_buffers,
	struct dspqueue_buffer *buffers,
	uint32_t max_message_length, uint32_t *message_length,
	uint8_t *message, uint32_t timeout_us, bool read, bool block) {
	int nErr = AEE_SUCCESS;
	struct dspqueue_multidomain *mdq = NULL;
	bool locked = false;
	dspqueue_t cq = NULL;

	VERIFYC(q, AEE_EBADPARM);

	mdq = &q->mdq;
	VERIFYC(mdq->is_mdq, AEE_EINVALIDITEM);

	if (block && mdq->num_domain_ids > 1) {
		/*
		 * Blocked read cannot be supported on multi-domain queues because
		 * the read operation is done in a serial manner on the individual
		 * queues of each domain and the first queue with a valid packet
		 * is returned to client.
		 * Say if the first domain's queue does not have any response
		 * packets to consume but the second domain's queue does, then
		 * the read call will just block indefinitely on the first queue
		 * without ever checking the subsequence queues.
		 */
		 nErr = AEE_EUNSUPPORTED;
		 FARF(ALWAYS, "Error 0x%x: %s: not supported for multi-domain queue %p",
				nErr, __func__, q);
		 return nErr;
	}
	// Only one multi-domain read request at a time.
	pthread_mutex_lock(&q->mutex);
	locked = true;

	// Read packet from individual queues
	for (unsigned int ii = 0; ii < mdq->num_domain_ids; ii++) {
		cq = mdq->queues[ii];
		if (read) {
			if (block) {
				nErr = dspqueue_read(cq, flags, max_buffers,
						num_buffers, buffers, max_message_length,
						message_length, message, timeout_us);
			} else {
				nErr = dspqueue_read_noblock(cq, flags,
						max_buffers, num_buffers, buffers,
						max_message_length, message_length, message);
			}
		} else {
			if (block) {
				nErr = dspqueue_peek(cq, flags,
						num_buffers, message_length, timeout_us);
			} else {
				nErr = dspqueue_peek_noblock(cq, flags,
						num_buffers, message_length);
			}
		}
		if (!nErr) {
			// Packet found in an individual queue
			break;
		} else if (nErr == AEE_EWOULDBLOCK) {
			// If queue is empty, proceed to next queue
			continue;
		} else {
			// If any queue is in bad state, bail out with error
			goto bail;
		}
	}
bail:
	if (locked)
		pthread_mutex_unlock(&q->mutex);

	if (nErr && nErr != AEE_EWOULDBLOCK) {
		FARF(ALWAYS, "Error 0x%x: %s (read %d, block %d): failed for queue %p, max bufs %u, max msg len %u",
			nErr, __func__, read, block, q, max_buffers, max_message_length);
	}
	return nErr;
}

AEEResult dspqueue_peek_noblock(dspqueue_t queue, uint32_t *flags,
                                uint32_t *num_buffers,
                                uint32_t *message_length) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspqueue *q = queue;
  struct dspqueue_packet_queue_header *pq = NULL;
  volatile const uint8_t *qp = NULL;
  uint32_t r, qleft;
  int locked = 0;

  if (q->mdq.is_mdq) {
    // Recursively call 'dspqueue_peek_noblock' on individual queues
    return dspqueue_multidomain_read(q, flags, 0, num_buffers,
              NULL, 0, message_length, NULL, 0, false, false);
  }

  pq = &q->header->resp_queue;
  qp = (volatile const uint8_t*) (((uintptr_t)q->header)
              + pq->queue_offset);

  pthread_mutex_lock(&q->mutex);
  locked = 1;

  // Check if we have a packet available
  get_queue_state_read(q->header, pq, &qleft, &r, NULL);
  if (qleft < 8) {
    pthread_mutex_unlock(&q->mutex);
    return AEE_EWOULDBLOCK;
  }

  VERIFY((nErr = peek_locked(qp, r, flags, num_buffers, message_length,
                             NULL)) == 0);

  // Fall through
bail:
  if (locked) {
    pthread_mutex_unlock(&q->mutex);
  }
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for queue %p", nErr, __func__, queue);
  }
  return nErr;
}

AEEResult dspqueue_peek(dspqueue_t queue, uint32_t *flags,
                        uint32_t *num_buffers, uint32_t *message_length,
                        uint32_t timeout_us) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspqueue *q = queue;
  struct dspqueue_packet_queue_header *pq = NULL;
  struct dspqueue_packet_queue_state *read_state = NULL;
  _Atomic uint32_t *wait_count = NULL;
  int waiting = 0;
  struct timespec *timeout_ts = NULL; // no timeout by default
  struct timespec ts;

  if (q->mdq.is_mdq) {
    // Recursively call 'dspqueue_read_noblock' on individual queues
    return dspqueue_multidomain_read(q, flags, 0, num_buffers,
            NULL, 0, message_length, NULL, timeout_us, false, true);
  }

  pq = &q->header->resp_queue;
  read_state =(struct dspqueue_packet_queue_state *) (((uintptr_t)q->header)
                      + pq->read_state_offset);
  wait_count = (_Atomic uint32_t *) &read_state->wait_count;

  pthread_mutex_lock(&q->packet_mutex);

  // Try a read first before dealing with timeouts
  nErr = dspqueue_peek_noblock(queue, flags, num_buffers, message_length);
  if (nErr != AEE_EWOULDBLOCK) {
    // Have a packet or got an error
    goto bail;
  }

  if (q->have_wait_counts) {
    // Mark that we're potentially waiting and try again
    atomic_fetch_add(wait_count, 1);
    cache_flush_word(wait_count);
    waiting = 1;
    nErr = dspqueue_peek_noblock(queue, flags, num_buffers, message_length);
    if (nErr != AEE_EWOULDBLOCK) {
      goto bail;
    }
  }

  if (timeout_us != DSPQUEUE_TIMEOUT_NONE) {
    // Calculate timeout expiry and use timeout
    VERIFYC(clock_gettime(CLOCK_REALTIME, &ts) == 0, AEE_EFAILED);
    timespec_add_us(&ts, timeout_us);
    timeout_ts = &ts;
  }

  while (1) {
    FARF(LOW, "Queue %u wait packet", (unsigned)q->id);
    VERIFY((nErr = wait_signal_locked(q, DSPQUEUE_SIGNAL_RESP_PACKET,
                                      timeout_ts)) == 0);
    FARF(LOW, "Queue %u got packet", (unsigned)q->id);
    nErr = dspqueue_peek_noblock(queue, flags, num_buffers, message_length);
    if (nErr != AEE_EWOULDBLOCK) {
      // Have a packet or got an error
      goto bail;
    }
  }

bail:
  if (waiting) {
    atomic_fetch_sub(wait_count, 1);
    cache_flush_word(wait_count);
  }
  pthread_mutex_unlock(&q->packet_mutex);
  return nErr;
}

static uint64_t get_time_usec(uint64_t *t) {

  struct timespec ts;
  int err = clock_gettime(CLOCK_MONOTONIC, &ts);
  *t = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
  return err;
}

static inline uint32_t read_data(volatile const uint8_t *packet_queue,
                                 uint32_t read_pos, uint32_t queue_len,
                                 void *data, uint32_t data_len) {

  uintptr_t qp = (uintptr_t)packet_queue;

  assert(data != NULL);
  assert(data_len > 0);
  assert((read_pos & 7) == 0);
  assert((queue_len - read_pos) >= data_len);

  cache_invalidate((void *)(qp + read_pos), data_len);
  memcpy(data, (void *)(qp + read_pos), data_len);
  read_pos += (data_len + 7) & (~7);
  assert(read_pos <= queue_len);
  if (read_pos >= queue_len) {
    read_pos = 0;
  }

  return read_pos;
}

AEEResult dspqueue_read_noblock(dspqueue_t queue, uint32_t *flags,
                                uint32_t max_buffers, uint32_t *num_buffers,
                                struct dspqueue_buffer *buffers,
                                uint32_t max_message_length,
                                uint32_t *message_length, uint8_t *message) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspqueue *q = queue;
  struct dspqueue_domain_queues *dq = NULL;
  struct dspqueue_packet_queue_header *pq = NULL;
  volatile const uint8_t *qp = NULL;
  struct dspqueue_packet_queue_state *read_state = NULL;
  struct dspqueue_packet_queue_state *write_state = NULL;
  uint32_t r, qleft;
  uint32_t f, num_b, msg_l, qsize = 0;
  uint32_t len;
  int locked = 0;
  unsigned i;
  uint32_t buf_refs = 0;
  uint64_t header;

  errno = 0;

if (q->mdq.is_mdq) {
        // Recursively call 'dspqueue_read_noblock' on individual queues
        return dspqueue_multidomain_read(q, flags, max_buffers,
                num_buffers, buffers, max_message_length,
                message_length, message, 0, true, false);
    }

  dq = queues->domain_queues[q->domain];
  pq = &q->header->resp_queue;
  qp = (volatile const uint8_t *) (((uintptr_t)q->header) + pq->queue_offset);
  read_state = (struct dspqueue_packet_queue_state *) (((uintptr_t)q->header)
                        + pq->read_state_offset);
  write_state = (struct dspqueue_packet_queue_state *) (((uintptr_t)q->header)
                        + pq->write_state_offset);
  qsize = pq->queue_length;

  pthread_mutex_lock(&q->mutex);
  locked = 1;

  VERIFYC(q->header->queue_count == q->queue_count, AEE_ERPC);

  // Check if we have a packet available
  FARF(LOW, "Queue %u wp %u", (unsigned)q->id, (unsigned)write_state->position);
  get_queue_state_read(q->header, pq, &qleft, &r, NULL);
  if (qleft < 8) {
    pthread_mutex_unlock(&q->mutex);
    return AEE_EWOULDBLOCK;
  }

  // Get and parse packet header
  VERIFY((nErr = peek_locked(qp, r, &f, &num_b, &msg_l, &header)) == 0);

  // Check if this is an early wakeup packet; handle accordingly
  if (f & DSPQUEUE_PACKET_FLAG_WAKEUP) {
    uint32_t wakeup = 0;
    uint32_t waittime = DEFAULT_EARLY_WAKEUP_WAIT;
    uint64_t t1 = 0;

    // Read packet, handling possible wraparound
    VERIFYC(num_b == 0, AEE_ERPC);
    len = 8 + msg_l;
    if ((qsize - r) < len) {
      assert((qleft - (qsize - r)) >= len);
      r = 8;
    } else {
      r += 8;
    }
    if (msg_l > 0) {
      VERIFYC(msg_l == 4, AEE_EBADITEM);
      r = read_data(qp, r, qsize, (uint8_t *)&wakeup, 4);
      if (wakeup > MAX_EARLY_WAKEUP_WAIT) {
        waittime = MAX_EARLY_WAKEUP_WAIT;
      } else {
        if (wakeup != 0) {
          waittime = wakeup;
        }
      }
    }

    // Update read pointer
    q->read_packet_count++;
    barrier_full();
    read_state->position = r;
    read_state->packet_count = q->read_packet_count;
    cache_flush_line(read_state);

    // Signal that we've consumed a packet
    q->resp_space_count++;
    dq->state->resp_space_count[q->id] = q->resp_space_count;
    FARF(LOW, "Queue %u resp_space_count %u", (unsigned)q->id,
         (unsigned)q->resp_space_count);
    cache_flush_word(&dq->state->resp_space_count[q->id]);
    if (q->have_wait_counts) {
      // Only signal if the other end is potentially waiting
      cache_invalidate_word(&write_state->wait_count);
      if (write_state->wait_count) {
        VERIFY((nErr = send_signal(q, DSPQUEUE_SIGNAL_RESP_SPACE)) ==
               AEE_SUCCESS);
      }
    } else {
      VERIFY((nErr = send_signal(q, DSPQUEUE_SIGNAL_RESP_SPACE)) ==
             AEE_SUCCESS);
    }

    // Wait for a packet to become available
    FARF(LOW, "Early wakeup, %u usec", (unsigned)waittime);
    get_queue_state_read(q->header, pq, &qleft, &r, NULL);
    if (qleft < 8) {
      VERIFY((nErr = get_time_usec(&t1)) == 0);
      uint64_t t2 = 0;
      do {
        VERIFY((nErr = get_time_usec(&t2)) == 0);
        if (((t1 + waittime) > t2) &&
            (((t1 + waittime) - t2) > EARLY_WAKEUP_SLEEP)) {
          FARF(LOW, "No sleep %u", (unsigned)EARLY_WAKEUP_SLEEP);
        }
        get_queue_state_read(q->header, pq, &qleft, &r, NULL);
        if (qleft >= 8) {
          if (t2 != 0) {
            q->early_wakeup_wait += t2 - t1;
            FARF(LOW, "Got packet after %uus", (unsigned)(t2 - t1));
          } else {
            FARF(LOW, "Got packet");
          }
          break;
        }
      } while ((t1 + waittime) > t2);

      if (qleft < 8) {
        // The next packet didn't get here in time
        q->early_wakeup_wait += t2 - t1;
        q->early_wakeup_misses++;
        FARF(LOW, "Didn't get packet after %uus", (unsigned)(t2 - t1));
        pthread_mutex_unlock(&q->mutex);
        return AEE_EWOULDBLOCK;
      }
    }

    // Have the next packet. Parse header and continue
    VERIFY((nErr = peek_locked(qp, r, &f, &num_b, &msg_l, &header)) == 0);
  }

  // Check the client has provided enough space for the packet
  if ((f & DSPQUEUE_PACKET_FLAG_BUFFERS) && (buffers != NULL)) {
    if (max_buffers < num_b) {
      FARF(ERROR,
           "Too many buffer references in packet to fit in output buffer");
      nErr = AEE_EBADPARM;
      goto bail;
    }
  }
  VERIFYC(num_b <= DSPQUEUE_MAX_BUFFERS, AEE_EBADITEM);
  if ((f & DSPQUEUE_PACKET_FLAG_MESSAGE) && (message != NULL)) {
    if (max_message_length < msg_l) {
      FARF(ERROR, "Message in packet too large to fit in output buffer");
      nErr = AEE_EBADPARM;
      goto bail;
    }
  }

  // Check if the packet can fit to the queue without being split by the
  // queue end. If not, the writer has wrapped it around to the
  // beginning of the queue
  len = 8 + num_b * sizeof(struct dspqueue_buffer) + msg_l;
  if ((qsize - r) < len) {
    assert((qleft - (qsize - r)) >= len);
    r = 8;
  } else {
    r += 8;
  }

  VERIFYC(f & DSPQUEUE_PACKET_FLAG_USER_READY, AEE_EBADITEM);

  // Read packet data
  if (flags != NULL) {
    *flags = f;
  }
  if (num_b > 0) {
    if (buffers != NULL) {
      r = read_data(qp, r, qsize, buffers,
                    num_b * sizeof(struct dspqueue_buffer));
    } else {
      r += num_b * sizeof(struct dspqueue_buffer);
    }
  }
  if (msg_l > 0) {
    if (message != NULL) {
      r = read_data(qp, r, qsize, message, msg_l);
    } else {
      r += (msg_l + 7) & (~7);
    }
  }
  if (message_length != NULL) {
    *message_length = msg_l;
  }

  // Update read pointer
  assert(r <= qsize);
  if (r >= qsize) {
    r = 0;
  }
  q->read_packet_count++;
  barrier_full();
  read_state->position = r;
  read_state->packet_count = q->read_packet_count;
  cache_flush_line(read_state);

  // Signal that we've consumed a packet
  q->resp_space_count++;
  dq->state->resp_space_count[q->id] = q->resp_space_count;
  FARF(LOW, "Queue %u resp_space_count %u", (unsigned)q->id,
       (unsigned)q->resp_space_count);
  cache_flush_word(&dq->state->resp_space_count[q->id]);
  if (q->have_wait_counts) {
    // Only signal if the other end is potentially waiting
    cache_invalidate_word(&write_state->wait_count);
    if (write_state->wait_count) {
      VERIFY((nErr = send_signal(q, DSPQUEUE_SIGNAL_RESP_SPACE)) ==
             AEE_SUCCESS);
    }
  } else {
    VERIFY((nErr = send_signal(q, DSPQUEUE_SIGNAL_RESP_SPACE)) == AEE_SUCCESS);
  }

  // Go through buffers
  if ((buffers != NULL) && (num_b > 0)) {
    for (i = 0; i < num_b; i++) {
      struct dspqueue_buffer *b = &buffers[i];
      void *va;
      size_t size;

      // Find buffer in internal FastRPC structures and handle refcounts
      if (b->flags & DSPQUEUE_BUFFER_FLAG_REF) {
        VERIFYC((b->flags & DSPQUEUE_BUFFER_FLAG_DEREF) == 0, AEE_EBADPARM);
        nErr = fastrpc_buffer_ref(q->domain, b->fd, 1, &va, &size);
      } else if (b->flags & DSPQUEUE_BUFFER_FLAG_DEREF) {
        nErr = fastrpc_buffer_ref(q->domain, b->fd, -1, &va, &size);
      } else {
        nErr = fastrpc_buffer_ref(q->domain, b->fd, 0, &va, &size);
      }
      if (nErr == AEE_ENOSUCHMAP) {
        FARF(ERROR, "Buffer FD %d in queue message not mapped to domain %d",
             b->fd, q->domain);
        goto bail;
      }
      VERIFY(nErr == 0);
      buf_refs = i + 1;

      // Check and use offset and size from the packet if specified
      if (b->size != 0) {
        uint64_t bend = ((uint64_t)b->offset) + ((uint64_t)b->size);
        VERIFYC(bend <= size, AEE_EBADITEM);
        va = (void *)(((uintptr_t)va) + b->offset);
        b->ptr = va;
        size = b->size;
      } else {
        VERIFYC(b->offset == 0, AEE_EBADITEM);
        b->ptr = va;
        b->size = size;
      }
    }
  }

  if (num_buffers != NULL) {
    *num_buffers = num_b;
  }

  pthread_mutex_unlock(&q->mutex);
  locked = 0;
  return AEE_SUCCESS;

bail:
  for (i = 0; i < buf_refs; i++) {
    // Undo buffer reference changes
    struct dspqueue_buffer *b = &buffers[i];
    if (b->flags & DSPQUEUE_BUFFER_FLAG_REF) {
      nErr = fastrpc_buffer_ref(q->domain, b->fd, -1, NULL, NULL);
    } else if (b->flags & DSPQUEUE_BUFFER_FLAG_DEREF) {
      nErr = fastrpc_buffer_ref(q->domain, b->fd, 1, NULL, NULL);
    }
  }
  if (locked) {
    pthread_mutex_unlock(&q->mutex);
  }
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for queue %p errno %s", nErr, __func__,
         queue, strerror(errno));
  }
  return nErr;
}

AEEResult dspqueue_read(dspqueue_t queue, uint32_t *flags, uint32_t max_buffers,
                        uint32_t *num_buffers, struct dspqueue_buffer *buffers,
                        uint32_t max_message_length, uint32_t *message_length,
                        uint8_t *message, uint32_t timeout_us) {

  AEEResult nErr = AEE_SUCCESS;
  struct dspqueue *q = queue;
  struct dspqueue_packet_queue_header *pq = NULL;
  struct dspqueue_packet_queue_state *read_state = NULL;
  _Atomic uint32_t *wait_count = NULL;
  int waiting = 0;
  struct timespec *timeout_ts = NULL; // no timeout by default
  struct timespec ts;

  if (q->mdq.is_mdq) {
    // Recursively call 'dspqueue_read_noblock' on individual queues
    return dspqueue_multidomain_read(q, flags, max_buffers,
                  num_buffers, buffers, max_message_length,
                  message_length, message, timeout_us, true, true);
  }

  pq = &q->header->resp_queue;
  read_state = (struct dspqueue_packet_queue_state *) (((uintptr_t)q->header)
                          + pq->read_state_offset);
  wait_count = (_Atomic uint32_t *) &read_state->wait_count;

  pthread_mutex_lock(&q->packet_mutex);

  // Try a read first before dealing with timeouts
  nErr = dspqueue_read_noblock(queue, flags, max_buffers, num_buffers, buffers,
                               max_message_length, message_length, message);
  if (nErr != AEE_EWOULDBLOCK) {
    // Have a packet or got an error
    goto bail;
  }

  if (q->have_wait_counts) {
    // Mark that we're potentially waiting and try again
    atomic_fetch_add(wait_count, 1);
    cache_flush_word(wait_count);
    waiting = 1;
    nErr =
        dspqueue_read_noblock(queue, flags, max_buffers, num_buffers, buffers,
                              max_message_length, message_length, message);
    if (nErr != AEE_EWOULDBLOCK) {
      goto bail;
    }
  }

  if (timeout_us != DSPQUEUE_TIMEOUT_NONE) {
    // Calculate timeout expiry and use timeout
    VERIFYC(clock_gettime(CLOCK_REALTIME, &ts) == 0, AEE_EFAILED);
    timespec_add_us(&ts, timeout_us);
    timeout_ts = &ts;
  }

  while (1) {
    FARF(LOW, "Queue %u wait packet", (unsigned)q->id);
    VERIFY((nErr = wait_signal_locked(q, DSPQUEUE_SIGNAL_RESP_PACKET,
                                      timeout_ts)) == 0);
    FARF(LOW, "Queue %u got packet", (unsigned)q->id);
    nErr =
        dspqueue_read_noblock(queue, flags, max_buffers, num_buffers, buffers,
                              max_message_length, message_length, message);
    if (nErr != AEE_EWOULDBLOCK) {
      // Have a packet or got an error
      goto bail;
    }
  }

bail:
  if (waiting) {
    atomic_fetch_sub(wait_count, 1);
    cache_flush_word(wait_count);
  }
  pthread_mutex_unlock(&q->packet_mutex);
  return nErr;
}

/* Get stats of multi-domain queue */
static int dspqueue_multidomain_get_stat(struct dspqueue *q,
	enum dspqueue_stat stat, uint64_t *value) {
	int nErr = AEE_SUCCESS;
	struct dspqueue_multidomain *mdq = NULL;
	bool locked = false;

	VERIFYC(q, AEE_EBADPARM);

	mdq = &q->mdq;
	VERIFYC(mdq->is_mdq, AEE_EINVALIDITEM);

	if (mdq->num_domain_ids > 1) {
		/* Stats requests are not supported for multi-domain queues */
		nErr = AEE_EUNSUPPORTED;
		goto bail;
	}

	// Only one multi-domain stats request at a time.
	pthread_mutex_lock(&q->mutex);
	locked = true;

	/*
	 * Always return stats from first queue only as this api is currently
	 * supported for single-domain queues only.
	 */
	VERIFY(AEE_SUCCESS == (nErr = dspqueue_get_stat(mdq->queues[0],
		stat, value)));
bail:
	if (locked)
		pthread_mutex_unlock(&q->mutex);

	if (nErr) {
		FARF(ALWAYS, "Error 0x%x: %s: failed for queue %p, stat %d",
			nErr, __func__, q, stat);
	}
	return nErr;
}

AEEResult dspqueue_get_stat(dspqueue_t queue, enum dspqueue_stat stat,
                            uint64_t *value) {

  AEEResult nErr = 0;
  struct dspqueue *q = queue;

  if (q->mdq.is_mdq)
    return dspqueue_multidomain_get_stat(q, stat, value);

  pthread_mutex_lock(&q->mutex);

  switch (stat) {
  case DSPQUEUE_STAT_EARLY_WAKEUP_WAIT_TIME:
    *value = q->early_wakeup_wait;
    q->early_wakeup_wait = 0;
    break;

  case DSPQUEUE_STAT_EARLY_WAKEUP_MISSES:
    *value = q->early_wakeup_misses;
    q->early_wakeup_misses = 0;
    break;

  case DSPQUEUE_STAT_READ_QUEUE_PACKETS: {
    struct dspqueue_packet_queue_header *pq = &q->header->resp_queue;
    struct dspqueue_packet_queue_state *write_state =
        (struct dspqueue_packet_queue_state *)(((uintptr_t)q->header) +
                                               pq->write_state_offset);
    uint32_t c;
    cache_invalidate_word(&write_state->packet_count);
    c = write_state->packet_count - q->read_packet_count;
    *value = c;
    break;
  }

  case DSPQUEUE_STAT_WRITE_QUEUE_PACKETS: {
    struct dspqueue_packet_queue_header *pq = &q->header->req_queue;
    struct dspqueue_packet_queue_state *read_state =
        (struct dspqueue_packet_queue_state *)(((uintptr_t)q->header) +
                                               pq->read_state_offset);
    uint32_t c;
    cache_invalidate_word(&read_state->packet_count);
    c = q->write_packet_count - read_state->packet_count;
    *value = c;
    break;
  }

  case DSPQUEUE_STAT_READ_QUEUE_BYTES: {
    struct dspqueue_packet_queue_header *pq = &q->header->resp_queue;
    uint32_t b;
    get_queue_state_read(q->header, pq, &b, NULL, NULL);
    *value = b;
    break;
  }

  case DSPQUEUE_STAT_WRITE_QUEUE_BYTES: {
    struct dspqueue_packet_queue_header *pq = &q->header->req_queue;
    uint32_t b;
    get_queue_state_write(q->header, pq, &b, NULL, NULL);
    *value = pq->queue_length - b - 8;
    break;
  }

  case DSPQUEUE_STAT_SIGNALING_PERF: {
    if (q->have_driver_signaling) {
      *value = DSPQUEUE_SIGNALING_PERF_OPTIMIZED_SIGNALING;
    } else if (q->have_wait_counts) {
      *value = DSPQUEUE_SIGNALING_PERF_REDUCED_SIGNALING;
    } else {
      *value = 0;
    }
    break;
  }

  default:
    FARF(ERROR, "Unsupported statistic %d", (int)stat);
    nErr = AEE_EBADPARM;
    goto bail;
  }

bail:
  pthread_mutex_unlock(&q->mutex);
  return nErr;
}

struct error_callback_args {
  struct dspqueue *queue;
  AEEResult error;
};

static void *error_callback_thread(void *arg) {
  struct error_callback_args *a = (struct error_callback_args *)arg;
  assert(a->queue->error_callback);
  FARF(ALWAYS, "%s starting for queue %p with id %u", __func__, a->queue,
       a->queue->id);
  a->queue->error_callback(a->queue, a->error, a->queue->callback_context);
  free(a);
  return NULL;
}

// Make an error callback in a separate thread. We do this to ensure the queue
// can be destroyed safely from the callback - all regular threads can exit
// while the callback is in progres. This function won't return error codes; if
// error reporting fails there isn't much we can do to report errors...
static void error_callback(struct dspqueue_domain_queues *dq, AEEResult error) {

  unsigned i;

  FARF(HIGH, "error_callback %d", (int)error);

  // Only report errors once per domain
  if (dq->dsp_error != 0) {
    return;
  }

  if ((error == (AEEResult)0x8000040d) || (error == -1)) {
    // Process died (probably)
    error = AEE_ECONNRESET;
  }
  dq->dsp_error = error;

  // Send error callbacks to all queues attached to this domain
  pthread_mutex_lock(&dq->queue_list_mutex);
  for (i = 0; i <= dq->max_queue; i++) {
    if ((dq->queues[i] != UNUSED_QUEUE) && (dq->queues[i] != INVALID_QUEUE)) {
      struct dspqueue *q = dq->queues[i];
      // Cancel pending waits
      if (q->have_driver_signaling) {
        int s;
        FARF(HIGH, "%s: Cancel all signal waits", __func__);
        for (s = 0; s < DSPQUEUE_NUM_SIGNALS; s++) {
          dspsignal_cancel_wait(q->domain, QUEUE_SIGNAL(q->id, s));
        }
      } else {
        pthread_mutex_lock(&q->packet_mutex);
        q->packet_mask |= SIGNAL_BIT_CANCEL;
        pthread_cond_broadcast(&q->packet_cond);
        pthread_mutex_unlock(&q->packet_mutex);
        pthread_mutex_lock(&q->space_mutex);
        q->space_mask |= SIGNAL_BIT_CANCEL;
        pthread_cond_broadcast(&q->space_cond);
        pthread_mutex_unlock(&q->space_mutex);
      }
      if (q->error_callback != NULL) {
        struct error_callback_args *a;
        pthread_attr_t tattr;
        a = calloc(1, sizeof(*a));
        if (a != NULL) {
          int err;
          pthread_attr_init(&tattr);
          pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
          a->queue = q;
          a->error = error;
          err = pthread_create(&q->error_callback_thread, &tattr,
                               error_callback_thread, a);
          if (err != 0) {
            FARF(ERROR, "Error callback thread creation failed: %d", err);
            free(a);
          }
        } else {
          FARF(ERROR, "Out of memory");
        }
      }
    }
  }
  pthread_mutex_unlock(&dq->queue_list_mutex);
}

static void *dspqueue_send_signal_thread(void *arg) {

  struct dspqueue_domain_queues *dq = (struct dspqueue_domain_queues *)arg;
  AEEResult nErr = 0;

  errno = 0;
  while (1) {
    pthread_mutex_lock(&dq->send_signal_mutex);
    while (dq->send_signal_mask == 0) {
      pthread_cond_wait(&dq->send_signal_cond, &dq->send_signal_mutex);
    }
    if (dq->send_signal_mask & SIGNAL_BIT_CANCEL) {
      // Exit
      pthread_mutex_unlock(&dq->send_signal_mutex);
      return NULL;
    } else if (dq->send_signal_mask & SIGNAL_BIT_SIGNAL) {
      dq->send_signal_mask = dq->send_signal_mask & (~SIGNAL_BIT_SIGNAL);
      pthread_mutex_unlock(&dq->send_signal_mutex);
      FARF(LOW, "Send signal");
      VERIFY((nErr = dspqueue_rpc_signal(dq->dsp_handle)) == 0);
    }
  }

bail:
  FARF(ERROR, "dspqueue_send_signal_thread failed with %d errno %s", nErr,
       strerror(errno));
  error_callback(dq, nErr);
  return (void *)(uintptr_t)nErr;
}

static void *dspqueue_receive_signal_thread(void *arg) {

  struct dspqueue_domain_queues *dq = (struct dspqueue_domain_queues *)arg;
  AEEResult nErr = 0;
  unsigned i;

  errno = 0;
  while (1) {
    int32_t signal;
    VERIFY((nErr = dspqueue_rpc_wait_signal(dq->dsp_handle, &signal)) == 0);

    if (signal == -1) {
      // Exit
      assert(dq->num_queues == 0);
      return NULL;
    }

    // Got a signal - at least one queue has more packets or space. Find out
    // which one and signal it.
    FARF(LOW, "Got signal");

    // Ensure we have visibility into updates from the DSP
    cache_invalidate(dq->state->req_space_count,
                     sizeof(dq->state->req_space_count));
    cache_invalidate(dq->state->resp_packet_count,
                     sizeof(dq->state->resp_packet_count));

    pthread_mutex_lock(&dq->queue_list_mutex);
    FARF(LOW, "Go through queues");
    for (i = 0; i <= dq->max_queue; i++) {
      if ((dq->queues[i] != UNUSED_QUEUE) && (dq->queues[i] != INVALID_QUEUE)) {
        struct dspqueue *q = dq->queues[i];
        assert(!q->have_driver_signaling);
        pthread_mutex_lock(&q->packet_mutex);
        if (q->resp_packet_count != dq->state->resp_packet_count[i]) {
          q->resp_packet_count = dq->state->resp_packet_count[i];
          FARF(LOW, "Queue %u new resp_packet_count %u", i,
               (unsigned)q->resp_packet_count);
          pthread_cond_broadcast(&q->packet_cond);
        }
        pthread_mutex_unlock(&q->packet_mutex);
        pthread_mutex_lock(&q->space_mutex);
        if (q->req_space_count != dq->state->req_space_count[i]) {
          q->req_space_count = dq->state->req_space_count[i];
          FARF(LOW, "Queue %u new req_space_count %u", i,
               (unsigned)q->req_space_count);
          pthread_cond_broadcast(&q->space_cond);
        }
        pthread_mutex_unlock(&q->space_mutex);
      }
    }
    FARF(LOW, "Done");
    pthread_mutex_unlock(&dq->queue_list_mutex);
  }

bail:
  FARF(ERROR, "dspqueue_receive_signal_thread failed with %d errno %s", nErr,
       strerror(errno));
  if (nErr == -1) {
    // Process died (probably)
    nErr = AEE_ECONNRESET;
  }
  error_callback(dq, nErr);
  return (void *)(uintptr_t)nErr;
}

static void *dspqueue_packet_callback_thread(void *arg) {

  struct dspqueue *q = (struct dspqueue *)arg;
  struct dspqueue_packet_queue_header *pq = &q->header->resp_queue;
  struct dspqueue_packet_queue_state *read_state =
      (struct dspqueue_packet_queue_state *)(((uintptr_t)q->header) +
                                             pq->read_state_offset);
  struct dspqueue_packet_queue_state *write_state =
      (struct dspqueue_packet_queue_state *)(((uintptr_t)q->header) +
                                             pq->write_state_offset);
  _Atomic uint32_t *wait_count = (_Atomic uint32_t *)&read_state->wait_count;
  uint32_t packet_count = 0;
  AEEResult nErr = AEE_SUCCESS;

  FARF(ALWAYS, "%s starting for queue %p", __func__, q);
  while (1) {
    pthread_mutex_lock(&q->packet_mutex);

    // Call the callback if we have any packets we haven't seen yet.
    cache_invalidate_word(&write_state->packet_count);
    if (packet_count != write_state->packet_count) {
      packet_count = write_state->packet_count;
      q->packet_callback(q, 0, q->callback_context);
    }

    // Mark we're waiting and call again if we just got more packets
    if (q->have_wait_counts) {
      atomic_fetch_add(wait_count, 1);
      cache_flush_word(wait_count);
    }
    cache_invalidate_word(&write_state->packet_count);
    if (packet_count != write_state->packet_count) {
      packet_count = write_state->packet_count;
      q->packet_callback(q, 0, q->callback_context);
    }

    // Wait for a signal
    nErr = wait_signal_locked(q, DSPQUEUE_SIGNAL_RESP_PACKET, NULL);
    if (nErr == AEE_EINTERRUPTED || nErr == AEE_EBADSTATE) {
      FARF(HIGH, "Queue %u exit callback thread", (unsigned)q->id);
      if (q->have_wait_counts) {
        atomic_fetch_sub(wait_count, 1);
        cache_flush_word(wait_count);
      }
      pthread_mutex_unlock(&q->packet_mutex);
      goto bail;
    } else if (nErr != AEE_SUCCESS) {
      pthread_mutex_unlock(&q->packet_mutex);
      FARF(ERROR, "Error: %s: wait_signal failed with 0x%x (queue %p)",
           __func__, nErr, q);
      return (void *)((intptr_t)nErr);
    }

    // Mark we aren't waiting right now
    if (q->have_wait_counts) {
      atomic_fetch_sub(wait_count, 1);
      cache_flush_word(wait_count);
    }

    pthread_mutex_unlock(&q->packet_mutex);
  }
bail:
  FARF(ALWAYS, "%s exiting", __func__);
  return NULL;
}

static int dspqueue_notif_callback(void *context, int domain, int session,
                                   remote_rpc_status_flags_t status) {
  int nErr = AEE_SUCCESS, effec_domain_id = domain;

  if (status == FASTRPC_USER_PD_UP) {
    return 0;
  }
  // All other statuses are some kind of process exit or DSP crash.
  assert(context == queues);
  if (session && domain < NUM_DOMAINS) {
    // Did not receive effective domain ID for extended session. Compute it.
    effec_domain_id = GET_EFFECTIVE_DOMAIN_ID(domain, session);
  }
  FARF(ALWAYS, "%s for domain %d, session %d, status %u", __func__, domain,
       session, status);
  assert(IS_VALID_EFFECTIVE_DOMAIN_ID(effec_domain_id));

  // Send different error codes for SSR and remote-process exit
  nErr = (status == FASTRPC_DSP_SSR) ? AEE_ECONNRESET : AEE_ENOSUCH;
  if (queues->domain_queues[effec_domain_id] != NULL) {
    error_callback(queues->domain_queues[effec_domain_id], nErr);
  }
  return 0;
}
