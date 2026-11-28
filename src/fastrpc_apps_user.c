// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

//#ifndef VERIFY_PRINT_ERROR
//#define VERIFY_PRINT_ERROR
//#endif // VERIFY_PRINT_ERROR
//#ifndef VERIFY_PRINT_INFO
//#define VERIFY_PRINT_INFO
//#endif // VERIFY_PRINT_INFO
#define _GNU_SOURCE
#ifndef VERIFY_PRINT_WARN
#define VERIFY_PRINT_WARN
#endif // VERIFY_PRINT_WARN
#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FARF_ERROR 1
#define FARF_HIGH 1
#define FARF_MED 1
#define FARF_LOW 1
#define FARF_CRITICAL 1 // Push log's to all hooks and persistent buffer.

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "adsp_current_process.h"
#include "adsp_current_process1.h"
#include "adsp_listener1.h"
#include "adsp_perf1.h"
#include "adspmsgd_adsp1.h"
#include "adspmsgd_internal.h"
#include "apps_mem_internal.h"
#include "apps_std_internal.h"
#include "dspsignal.h"
#include "fastrpc_apps_user.h"
#include "fastrpc_cap.h"
#include "fastrpc_common.h"
#include "fastrpc_config.h"
#include "fastrpc_internal.h"
#include "fastrpc_log.h"
#include "fastrpc_mem.h"
#include "fastrpc_notif.h"
#include "fastrpc_perf.h"
#include "fastrpc_pm.h"
#include "fastrpc_procbuf.h"
#include "listener_android.h"
#include "log_config.h"
#include "platform_libs.h"
#include "remotectl.h"
#include "remotectl1.h"
#include "rpcmem_internal.h"
#include "shared.h"
#include "verify.h"
#include "fastrpc_context.h"
#include "fastrpc_process_attributes.h"
#include "fastrpc_trace.h"
#include "fastrpc_config_parser.h"

#define VENDOR_DSP_LOCATION "/vendor/dsp/"
#define VENDOR_DOM_LOCATION "/vendor/dsp/xdsp/"
#define HEXAGON_LIBS_PATH_PREFIX CONFIG_BASE_DIR "/hexagon"

char DSP_LIBS_LOCATION[PATH_MAX] = DEFAULT_DSP_SEARCH_PATHS;
static char DSP_SEARCH_PATHS_CACHE[NUM_DOMAINS][PATH_MAX] = {{0}};

#ifdef LE_ENABLE
#define PROPERTY_VALUE_MAX                                                     \
  92 // as this macro is defined in cutils for Android platforms, defined
     // explicitly for LE platform
#elif (defined _ANDROID) || (defined ANDROID)
/// TODO: Bharath #include "cutils/properties.h"
#define PROPERTY_VALUE_MAX 92
#else
#define PROPERTY_VALUE_MAX 92
#endif

#ifndef _WIN32
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#endif // __WIN32

#ifndef INT_MAX
#define INT_MAX (int)(-1)
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX (unsigned long long int)(-1)
#endif

#define MAX_DMA_HANDLES 256
#define MAX_DLERRSTR_LEN 255
#define ENV_PATH_LEN 256

#define FASTRPC_TRACE_INVOKE_START "fastrpc_trace_invoke_start"
#define FASTRPC_TRACE_INVOKE_END "fastrpc_trace_invoke_end"
#define FASTRPC_TRACE_LOG(k, handle, sc)                                       \
  if (fastrpc_trace == 1 && !IS_STATIC_HANDLE(handle)) {                       \
    FARF(ALWAYS, "%s: sc 0x%x", (k), (sc));                                    \
  }

/* Number of dsp library instances allowed per process. */
#define MAX_LIB_INSTANCE_ALLOWED 1
#define ERRNO (errno ? errno : nErr ? nErr : -1)

static void check_multilib_util(void);

/* Array to store fastrpc library names. */
static const char *fastrpc_library[NUM_DOMAINS] = {
    "libadsprpc.so", "libmdsprpc.so", "libsdsprpc.so", "libcdsprpc.so", "libcdsprpc.so", "libcdsprpc.so", "libcdsprpc.so"};

/* Array to store env variable names. */
static char *fastrpc_dsp_lib_refcnt[NUM_DOMAINS];
static int total_dsp_lib_refcnt = 0;

/* Function to free the memory allocated for env variable names */
inline static void deinit_fastrpc_dsp_lib_refcnt(void) {
  int ii = 0;

  FOR_EACH_DOMAIN_ID(ii) {
    if (fastrpc_dsp_lib_refcnt[ii]) {
      unsetenv(fastrpc_dsp_lib_refcnt[ii]);
      free(fastrpc_dsp_lib_refcnt[ii]);
      fastrpc_dsp_lib_refcnt[ii] = NULL;
    }
  }
}

enum fastrpc_proc_attr {
  FASTRPC_MODE_DEBUG = 0x1,
  FASTRPC_MODE_PTRACE = 0x2,
  FASTRPC_MODE_CRC = 0x4,
  FASTRPC_MODE_UNSIGNED_MODULE = 0x8,
  FASTRPC_MODE_ADAPTIVE_QOS = 0x10,
  FASTRPC_MODE_SYSTEM_PROCESS = 0x20,
  FASTRPC_MODE_PRIVILEGED = 0x40, // this attribute will be populated in kernel
  // Attribute to enable pd dump feature for both signed/unsigned pd
  FASTRPC_MODE_ENABLE_PDDUMP = 0x80,
  // System attribute to enable pd dump debug data collection on rooted devices
  FASTRPC_MODE_DEBUG_PDDUMP = 0x100,
  // Attribute to enable kernel perf keys data collection
  FASTRPC_MODE_PERF_KERNEL = 0x200,
  // Attribute to enable dsp perf keys data collection
  FASTRPC_MODE_PERF_DSP = 0x400,
  // Attribute to log iregion buffer
  FASTRPC_MODE_ENABLE_IREGION_LOG = 0x800,
  // Attribute to enable QTF tracing on DSP
  FASTRPC_MODE_ENABLE_QTF_TRACING = 0x1000,
  // Attribute to enable debug logging
  FASTRPC_MODE_ENABLE_DEBUG_LOGGING = 0x2000,
  // Attribute to set caller level for heap
  FASTRPC_MODE_CALLER_LEVEL_MASK = 0xE000,
  // Attribute to enable uaf for heap
  FASTRPC_MODE_ENABLE_UAF = 0x10000,
  // Attribute to launch system unsignedPD on CDSP
  FASTRPC_MODE_SYSTEM_UNSIGNED_PD = 0x20000,
  // Reserved attribute bit for sys mon application
  FASTRPC_MODE_SYSMON_RESERVED_BIT = 0x40000000,
  // Attribute to enable log packet
  FASTRPC_MODE_LOG_PACKET = 0x40000,
  // Attribute to set Leak detect for heap. Bits 19-20 are reserved for leak
  // detect.
  FASTRPC_MODE_ENABLE_LEAK_DETECT = 0x180000,
  // Attribute to change caller stack for heap. Bits 21-23 are reserved the call
  // stack num
  FASTRPC_MODE_CALLER_STACK_NUM = 0xE00000,
};

#define M_CRCLIST (64)
#define IS_DEBUG_MODE_ENABLED(var) (var & FASTRPC_MODE_DEBUG)
#define IS_CRC_CHECK_ENABLED(var) (var & FASTRPC_MODE_CRC)
extern int perf_v2_kernel;
extern int perf_v2_dsp;
#define IS_KERNEL_PERF_ENABLED(var)                                            \
  ((var & FASTRPC_MODE_PERF_KERNEL) && perf_v2_kernel)
#define IS_DSP_PERF_ENABLED(var) ((var & FASTRPC_MODE_PERF_DSP) && perf_v2_dsp)
#define IS_QTF_TRACING_ENABLED(var) (var & FASTRPC_MODE_ENABLE_QTF_TRACING)

#define POLY32                                                                 \
  0x04C11DB7 // G(x) = x^32+x^26+x^23+x^22+x^16+x^12
             // +x^11+x^10+x^8+x^7+x^5+x^4+x^2+x+1

#define DEFAULT_UTHREAD_PRIORITY 0xC0
#define DEFAULT_UTHREAD_STACK_SIZE 16 * 1024
#define DEFAULT_PD_INITMEM_SIZE 3 * 1024 * 1024

/* Valid QuRT thread priorities are 1 to 255 */
#define MIN_THREAD_PRIORITY 1
#define MAX_THREAD_PRIORITY 255

/* Remote thread stack size should be between 16 KB and 8 MB */
#define MIN_UTHREAD_STACK_SIZE (16 * 1024)
#define MAX_UTHREAD_STACK_SIZE (8 * 1024 * 1024)

/* Remote userpd init memlen should be between 3MB and 200MB */
#define MIN_PD_INITMEM_SIZE (3 * 1024 * 1024)
#define MAX_PD_INITMEM_SIZE (200 * 1024 * 1024)

#define FASTRPC_QOS_MAX_LATENCY_USEC (10000)
#define PM_TIMEOUT_MS 5

enum handle_list_id {
  MULTI_DOMAIN_HANDLE_LIST_ID = 1,
  NON_DOMAIN_HANDLE_LIST_ID = 2,
  REVERSE_HANDLE_LIST_ID = 3,
};

const char *ENV_DEBUG_VAR_NAME[] = {"FASTRPC_PROCESS_ATTRS",
                                    "FASTRPC_DEBUG_TRACE",
                                    "FASTRPC_DEBUG_TESTSIG",
                                    "FASTRPC_PERF_KERNEL",
                                    "FASTRPC_PERF_ADSP",
                                    "FASTRPC_PERF_FREQ",
                                    "FASTRPC_DEBUG_SYSTRACE",
                                    "FASTRPC_DEBUG_PDDUMP",
                                    "FASTRPC_PROCESS_ATTRS_PERSISTENT",
                                    "ro.debuggable"};
const char *ANDROIDP_DEBUG_VAR_NAME[] = {"vendor.fastrpc.process.attrs",
                                         "vendor.fastrpc.debug.trace",
                                         "vendor.fastrpc.debug.testsig",
                                         "vendor.fastrpc.perf.kernel",
                                         "vendor.fastrpc.perf.adsp",
                                         "vendor.fastrpc.perf.freq",
                                         "vendor.fastrpc.debug.systrace",
                                         "vendor.fastrpc.debug.pddump",
                                         "persist.vendor.fastrpc.process.attrs",
                                         "ro.build.type"};
const char *ANDROID_DEBUG_VAR_NAME[] = {"fastrpc.process.attrs",
                                        "fastrpc.debug.trace",
                                        "fastrpc.debug.testsig",
                                        "fastrpc.perf.kernel",
                                        "fastrpc.perf.adsp",
                                        "fastrpc.perf.freq",
                                        "fastrpc.debug.systrace",
                                        "fastrpc.debug.pddump",
                                        "persist.fastrpc.process.attrs",
                                        "ro.build.type"};

const char *SUBSYSTEM_NAME[] = {"adsp", "mdsp", "sdsp", "cdsp", "cdsp1", "gdsp0", "gdsp1", "reserved"};

/* Strings for trace event logging */
#define INVOKE_BEGIN_TRACE_STR "fastrpc_msg: userspace_call: begin"
#define INVOKE_END_TRACE_STR "fastrpc_msg: userspace_call: end"

static const size_t invoke_begin_trace_strlen =
    sizeof(INVOKE_BEGIN_TRACE_STR) - 1;
static const size_t invoke_end_trace_strlen = sizeof(INVOKE_END_TRACE_STR) - 1;

int NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS =
    sizeof(ENV_DEBUG_VAR_NAME) / sizeof(char *);
int NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS =
    sizeof(ANDROIDP_DEBUG_VAR_NAME) / sizeof(char *);
int NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS =
    sizeof(ANDROID_DEBUG_VAR_NAME) / sizeof(char *);

/* Shell prefix for signed and unsigned */
const char *const SIGNED_SHELL = "fastrpc_shell_";
const char *const UNSIGNED_SHELL = "fastrpc_shell_unsigned_";

struct handle_info {
  QNode qn;
  int domain; /* effective domain id of the session this handle is on */
  remote_handle64 local;
  remote_handle64 remote;
  char *name;
};

// Fastrpc client notification request node to be queued to <notif_list>
struct fastrpc_notif {
  QNode qn;
  remote_rpc_notif_register_t notif;
};

struct other_handle_list { // For non-domain and reverse handle list
  QList ql;
};

// Fastrpc timer function for RPC timeout
typedef struct fastrpc_timer_info {
  timer_t timer;
  uint64_t timeout_millis;
  int domain;
  int sc;
  int handle;
  pid_t tid;
} fastrpc_timer;

static int attach_guestos(int domain);

/*
 * hlist is a hash table of per-session state keyed by effective domain id
 * (domain + NUM_DOMAINS * session). An effective domain id is valid if and
 * only if it has an entry in this table. The legacy entries (sessions 0 and
 * 1 of every domain) are created at init; any other session is created when
 * it is reserved or first used.
 */
DECLARE_HASH_TABLE(hlist, struct handle_list)

/* True once fastrpc_apps_user_init() has set up the table, false again
 * after fastrpc_apps_user_deinit(). */
static bool hlist_table_ready = false;

/* Recursive mutex attribute used for every entry's "mut" mutex. */
static pthread_mutexattr_t hlist_mutexattr;

/* Allocate an entry for the effective domain id and fully initialize it.
 * The entry is not in the table yet, so no other thread can see it. */
static struct handle_list *hlist_alloc_node(int effec_domain) {
  struct handle_list *hl = calloc(1, sizeof(*hl));

  if (!hl)
    return NULL;
  hl->domain = effec_domain;
  hl->dev = -1;
  hl->th_params.thread_priority = DEFAULT_UTHREAD_PRIORITY;
  hl->info = -1;
  hl->th_params.stack_size = DEFAULT_UTHREAD_STACK_SIZE;
  sem_init(&hl->th_params.r_sem, 0, 0);
  hl->dsppd = attach_guestos(effec_domain);
  hl->trace_marker_fd = -1;
  hl->state = FASTRPC_DOMAIN_STATE_CLEAN;
  hl->pd_initmem_size = DEFAULT_PD_INITMEM_SIZE;
  QList_Ctor(&hl->ql);
  QList_Ctor(&hl->nql);
  QList_Ctor(&hl->rql);
  pthread_mutex_init(&hl->mut, &hlist_mutexattr);
  pthread_mutex_init(&hl->lmut, 0);
  pthread_mutex_init(&hl->init, 0);
  return hl;
}

static void hlist_free_node(struct handle_list *hl) {
  sem_destroy(&hl->th_params.r_sem);
  pthread_mutex_destroy(&hl->mut);
  pthread_mutex_destroy(&hl->lmut);
  pthread_mutex_destroy(&hl->init);
  free(hl);
}

/* Lookup-only accessor. NULL if the table isn't ready or the id has no entry. */
static inline struct handle_list *hlist_lookup(int domain) {
  struct handle_list *me = NULL;
  if (!hlist_table_ready)
    return NULL;
  GET_HASH_NODE(struct handle_list, domain, me);
  return me;
}

/* Public lookup-only accessor for other translation units. */
struct handle_list *fastrpc_get_hlist_node(int domain) {
  return hlist_lookup(domain);
}

/*
 * Return the entry for (domain, session), creating it if it doesn't exist.
 * For legacy domains the effective id is (domain + NUM_DOMAINS * session);
 * for dynamic domains it is (logical_id * EFFEC_DOMAIN_ID_MULTIPLIER +
 * session), which only has room for session 0..EFFEC_DOMAIN_ID_MULTIPLIER-1
 * -- a session at or past that wraps into the next logical domain's id
 * range instead of erroring, so it must be rejected here rather than
 * against the legacy-wide MAX_SESSIONS_LIMIT. A new entry is built before
 * taking the table lock and dropped if another thread added the same
 * session first, so other threads never see a partly built entry. Creating
 * an entry does not reserve the session.
 */
static struct handle_list *hlist_get_or_create_session(int domain, int session) {
  struct handle_list *me = NULL, *existing = NULL;
  int effec_domain;
  int max_session = IS_DYNAMIC_DOMAIN_ID(domain) ?
      EFFEC_DOMAIN_ID_MULTIPLIER : MAX_SESSIONS_LIMIT;

  if (!hlist_table_ready || !IS_VALID_DOMAIN_ID(domain) || session < 0 ||
      session >= max_session)
    return NULL;
  effec_domain = GET_EFFECTIVE_DOMAIN_ID(domain, session);
  existing = hlist_lookup(effec_domain);
  if (existing)
    return existing;
  me = hlist_alloc_node(effec_domain);
  if (!me)
    return NULL;
  pthread_mutex_lock(&info.mut);
  HASH_FIND_INT(info.tbl, &effec_domain, existing);
  if (!existing)
    HASH_ADD_INT(info.tbl, domain, me);
  pthread_mutex_unlock(&info.mut);
  if (existing) {
    hlist_free_node(me);
    return existing;
  }
  return me;
}

/* See declaration in fastrpc_internal.h. Best-effort: a NULL return (table
 * not ready yet, or domain somehow invalid) just leaves normalize_effec_
 * domain_id()'s bare-logical-id fallback with nothing to find later. */
void fastrpc_ensure_default_session(int logical_domain) {
  (void)hlist_get_or_create_session(logical_domain, 0);
}

/* Effective id of (domain, session), creating the session's entry if it is
 * missing. Does not reserve the session. -1 if it can't be created. */
static int fastrpc_get_or_create_effec_domain(int domain, int session) {
  struct handle_list *hl = hlist_get_or_create_session(domain, session);

  return hl ? hl->domain : -1;
}

bool fastrpc_is_valid_effec_domain(int effec_domain_id) {
  return effec_domain_id >= 0 && hlist_lookup(effec_domain_id) != NULL;
}

int fastrpc_next_effec_domain(int prev) {
  struct handle_list *hl, *tmp;
  int next = -1;

  if (!hlist_table_ready)
    return -1;
  pthread_mutex_lock(&info.mut);
  HASH_ITER(hh, info.tbl, hl, tmp) {
    if (hl->domain > prev && (next < 0 || hl->domain < next))
      next = hl->domain;
  }
  pthread_mutex_unlock(&info.mut);
  return next;
}

// Macro to check if a remote session is already open on given domain
#define IS_SESSION_OPEN_ALREADY(domain) fastrpc_is_session_open_already(domain)
static inline bool fastrpc_is_session_open_already(int domain) {
  struct handle_list *hl = hlist_lookup(domain);

  return hl != NULL && hl->dev != -1;
}

/* Mutex to protect notif_list */
static pthread_mutex_t update_notif_list_mut;

static pthread_key_t tlsKey = INVALID_KEY;

// Flag to check if there is any client notification request
static bool fastrpc_notif_flag = false;
static int fastrpc_trace = 0;

static int domain_init(int domain, int *dev);
static void domain_deinit(int domain);
static int close_device_node(int domain_id, int dev);
static const char *get_domain_name_str(int domain_id);
extern int apps_mem_table_init(void);
extern void apps_mem_table_deinit(void);

static uint32_t crc_table[256];
static atomic_bool timer_expired = false;

/*
 * Builds "<HEXAGON_LIBS_PATH_PREFIX>/<arch_str>;<DSP_LIBS_LOCATION>" into
 * out (size out_len), so the arch-specific directory is searched before the
 * generic board/YAML paths. Returns false (out left untouched by the
 * caller's responsibility to fall back) on any truncation.
 */
static bool build_arch_search_path(const char *arch_str, int domain,
                                    char *out, size_t out_len) {
  char arch_path[PATH_MAX] = {0};

  if (snprintf(arch_path, sizeof(arch_path), "%s/%s",
               HEXAGON_LIBS_PATH_PREFIX, arch_str) >= (int)sizeof(arch_path)) {
    FARF(ALWAYS,
         "Warning: %s: ARCH path truncated for domain %d, "
         "using DSP_LIBS_LOCATION only",
         __func__, domain);
    return false;
  }

  strlcpy(out, arch_path, out_len);
  strlcat(out, ";", out_len);
  if (strlcat(out, DSP_LIBS_LOCATION, out_len) >= out_len) {
    FARF(ALWAYS,
         "Warning: %s: Failed to build search list for domain %d, "
         "using DSP_LIBS_LOCATION only",
         __func__, domain);
    return false;
  }

  return true;
}

static void build_dsp_search_path_cache_for_domain(int domain) {
  int nErr = AEE_SUCCESS;
  fastrpc_capability cap = {0, ARCH_VER, 0};
  char arch_str[64] = {0};
  char *domain_path_cache = NULL;
  char dynamic_path[PATH_MAX] = {0};
  const char *yaml_arch = NULL;

  if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
    FARF(ALWAYS, "Warning: %s: Invalid domain %d", __func__, domain);
    return;
  }

  /* Runs once per SESSION, but the arch is per-physical-DSP -- skip the
   * ioctl once an earlier session already resolved and cached it. */
  if (IS_DYNAMIC_EFFEC_DOMAIN_ID(domain)) {
    fastrpc_domain_internal d;
    if (fastrpc_get_domain_by_logical_id(GET_LOGICAL_FROM_DYNAMIC_EFFEC(domain), &d) &&
        d.dsp_search_path[0] != '\0')
      return;
  } else if (DSP_SEARCH_PATHS_CACHE[GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain)][0] != '\0') {
    return;
  }

  /*
   * ARCH_VER is queried for every valid domain, dynamic or legacy -- it's
   * cheap (one capability ioctl) and the result below drives both the
   * legacy array-based cache and the dynamic-domain Level-1 table cache.
   */
  cap.domain = domain;
  nErr = fastrpc_get_cap(cap.domain, cap.attribute_ID, &cap.capability);
  FARF(RUNTIME_RPC_HIGH, "%s: domain %d: ARCH_VER query nErr=0x%x capability=0x%x",
       __func__, domain, nErr, cap.capability);

  if (IS_DYNAMIC_EFFEC_DOMAIN_ID(domain)) {
    /*
     * Dynamic domains can't use DSP_SEARCH_PATHS_CACHE[] (sized for the
     * NUM_DOMAINS legacy ids only) -- cache into the Level-1 domain table
     * instead, keyed by logical id. No YAML fallback for dynamic domains
     * (DSP_ARCH_KEY/DSP_ARCH_FROM_YAML are legacy-id-only); if the
     * capability isn't available, leave it uncached and
     * get_dsp_search_path_for_domain() falls back to DSP_LIBS_LOCATION.
     */
    if (nErr != AEE_SUCCESS || cap.capability == 0)
      return;

    snprintf(arch_str, sizeof(arch_str), "v%02x", cap.capability & 0xFF);
    if (build_arch_search_path(arch_str, domain, dynamic_path, sizeof(dynamic_path)))
      fastrpc_set_cached_dsp_search_path(GET_LOGICAL_FROM_DYNAMIC_EFFEC(domain),
                                          dynamic_path);
    return;
  }

  /*
   * domain here is still the effective id (routing domain + NUM_DOMAINS *
   * session, e.g. 8 for session 1 of routing domain 0) -- DSP_SEARCH_PATHS_CACHE[]
   * and get_dsp_arch_from_yaml() are both sized/keyed by the bare routing
   * domain only (0..NUM_DOMAINS-1), so every legacy domain opened with a
   * session >= 1 would otherwise index past the end of the array.
   */
  int routing_domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);
  domain_path_cache = DSP_SEARCH_PATHS_CACHE[routing_domain];

  /* YAML is the fallback for legacy targets where the capability is not supported. */
  if (nErr == AEE_SUCCESS && cap.capability != 0) {
    snprintf(arch_str, sizeof(arch_str), "v%02x", cap.capability & 0xFF);
    FARF(RUNTIME_RPC_HIGH, "%s: domain %d: resolved ARCH from capability 0x%x: %s",
         __func__, domain, cap.capability, arch_str);
  } else {
    /* Legacy target: capability API not supported, use YAML ARCH config. */
    FARF(ALWAYS,
         "Warning 0x%x: %s: ARCH_VER capability not supported for domain %d, "
         "trying YAML ARCH config",
         nErr, __func__, domain);
    yaml_arch = get_dsp_arch_from_yaml(routing_domain);
    if (!yaml_arch || yaml_arch[0] == '\0') {
      FARF(ALWAYS,
           "Warning: %s: No YAML ARCH config for domain %d, "
           "using DSP_LIBS_LOCATION only",
           __func__, domain);
      strlcpy(domain_path_cache, DSP_LIBS_LOCATION, PATH_MAX);
      return;
    }
    strlcpy(arch_str, yaml_arch, sizeof(arch_str));
    FARF(RUNTIME_RPC_HIGH, "%s: domain %d: resolved ARCH from YAML config: %s",
         __func__, domain, arch_str);
  }

  /*
   * Build the arch-specific path and put it at the front of the search list,
   * followed by DSP_LIBS_LOCATION (board/YAML paths + generic fallback).
   * The user-set ADSP_LIBRARY_PATH env is prepended at open-time by
   * apps_std_imp, so the effective resolution order is:
   *   1. ADSP_LIBRARY_PATH (user/container, highest priority)
   *   2. arch-specific path  (CONFIG_BASE_DIR/hexagon/<arch_str>)
   *   3. DSP_LIBS_LOCATION   (board-specific path from YAML + generic fallback)
   */
  if (!build_arch_search_path(arch_str, domain, domain_path_cache, PATH_MAX)) {
    strlcpy(domain_path_cache, DSP_LIBS_LOCATION, PATH_MAX);
    return;
  }

  FARF(RUNTIME_RPC_HIGH, "%s: domain %d: search path: %s", __func__, domain,
       domain_path_cache);
}

static const char *get_dsp_search_path_for_domain(int domain) {
  if (IS_DYNAMIC_DOMAIN_ID(domain)) {
    fastrpc_domain_internal d;
    if (fastrpc_get_domain_by_logical_id(domain, &d) && d.dsp_search_path[0] != '\0') {
      static __thread char buf[PATH_MAX];
      strlcpy(buf, d.dsp_search_path, sizeof(buf));
      return buf;
    }
    return DSP_LIBS_LOCATION;
  }

  if (IS_VALID_DOMAIN_ID(domain) && DSP_SEARCH_PATHS_CACHE[domain][0] != '\0')
    return DSP_SEARCH_PATHS_CACHE[domain];

  return DSP_LIBS_LOCATION;
}

void set_thread_context(int domain) {
  if (tlsKey != INVALID_KEY) {
    /* Store the effective domain id itself, offset by one so domain 0 doesn't
     * collide with the NULL "unset" value. See get_current_domain() for the
     * matching decode. */
    pthread_setspecific(tlsKey, (void *)(intptr_t)(domain + 1));
  }
}

int get_device_fd(int domain) {
  struct handle_list *hl = hlist_lookup(domain);
  if (hl && (hl->dev != -1)) {
    return hl->dev;
  } else {
    return -1;
  }
}

int fastrpc_session_open(int domain, int *dev) {
  int device = -1;

  if (IS_SESSION_OPEN_ALREADY(domain)) {
    *dev = hlist_lookup(domain)->dev;
    return 0;
  }

  device = open_device_node(domain);
  if (device >= 0) {
    *dev = device;
    return 0;
  }
  return AEE_ECONNREFUSED;
}

void fastrpc_session_close(int domain, int dev) {
  struct handle_list *hl = hlist_lookup(domain);
  if (!hl)
    return;
  if ((hl->dev == INVALID_DEVICE) &&
      (dev != INVALID_DEVICE)) {
    close(dev);
  } else if ((hl->dev != INVALID_DEVICE) &&
            (dev == INVALID_DEVICE)) {
    close(hl->dev);
    hl->dev = INVALID_DEVICE;
  }
  return;
}

int fastrpc_session_get(int domain) {
  int ref = -1;
  struct handle_list *hl = hlist_lookup(domain);
  do {
    if (hl) {
      pthread_mutex_lock(&hl->mut);
      if (hl->state == FASTRPC_DOMAIN_STATE_DEINIT) {
        pthread_mutex_unlock(&hl->mut);
        return AEE_ENOTINITIALIZED;
      }
      hl->ref++;
      ref = hl->ref;
      pthread_mutex_unlock(&hl->mut);
      set_thread_context(domain);
      FARF(RUNTIME_RPC_HIGH, "%s, domain %d, state %d, ref %d\n", __func__, domain,
           hl->state, ref);
    } else {
      return AEE_ENOTINITIALIZED;
    }
  } while (0);
  return 0;
}

int fastrpc_session_put(int domain) {
  int ref = -1;
  struct handle_list *hl = hlist_lookup(domain);
  do {
    if (hl) {
      pthread_mutex_lock(&hl->mut);
      if (hl->ref > 0)
        hl->ref--;
      ref = hl->ref;
      pthread_mutex_unlock(&hl->mut);
      FARF(RUNTIME_RPC_HIGH, "%s, domain %d, state %d, ref %d\n", __func__, domain,
           hl->state, ref);
    } else {
      return AEE_ENOTINITIALIZED;
    }
  } while (0);
  return ref;
}

int fastrpc_session_dev(int domain, int *dev) {
  struct handle_list *hl;
  *dev = INVALID_DEVICE;
  if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain))
    return AEE_ENOTINITIALIZED;
  hl = hlist_lookup(domain);
  do {
    if (hl) {
      pthread_mutex_lock(&hl->mut);
      if (hl->state == FASTRPC_DOMAIN_STATE_DEINIT) {
        pthread_mutex_unlock(&hl->mut);
        return AEE_ENOTINITIALIZED;
      }
      if (hl->dev < 0) {
        pthread_mutex_unlock(&hl->mut);
        return AEE_ENOTINITIALIZED;
      } else {
        *dev = hl->dev;
        pthread_mutex_unlock(&hl->mut);
        return AEE_SUCCESS;
      }
      pthread_mutex_unlock(&hl->mut);
    } else {
      return AEE_ENOTINITIALIZED;
    }
  } while (0);
  return AEE_ENOTINITIALIZED;
}

int check_rpc_error(int err) {
  if (check_error_code_change_present() == 1) {
    if (err > KERNEL_ERRNO_START && err <= HLOS_ERR_END) // driver or HLOS err
      return 0;
    else if (err > (int)DSP_AEE_EOFFSET &&
             err <= (int)DSP_AEE_EOFFSET + 1024) // DSP err
      return 0;
    else if (err == AEE_ENOSUCH ||
             err == AEE_EINTERRUPTED) // common DSP HLOS err
      return 0;
    else
      return -1;
  } else
    return 0;
}

static void GenCrc32Tab(uint32_t GenPoly, uint32_t *crctab) {
  uint32_t crc;
  int i, j;

  for (i = 0; i < 256; i++) {
    crc = i << 24;
    for (j = 0; j < 8; j++) {
      crc = (crc << 1) ^ (crc & 0x80000000 ? GenPoly : 0);
    }
    crctab[i] = crc;
  }
}

static uint32_t crc32_lut(unsigned char *data, int nbyte, uint32_t *crctab) {
  uint32_t crc = 0;
  if (!data || !crctab)
    return 0;

  while (nbyte--) {
    crc = (crc << 8) ^ crctab[(crc >> 24) ^ *data++];
  }
  return crc;
}

int property_get_int32(const char *name, int value) { return 0; }

int property_get(const char *name, int *def, int *value) { return 0; }

int fastrpc_get_property_int(fastrpc_properties UserPropKey, int defValue) {
  if (((int)UserPropKey > NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ENV_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return defValue;
  }
  const char *env = getenv(ENV_DEBUG_VAR_NAME[UserPropKey]);
  if (env != 0)
    return (int)atoi(env);
#if !defined(LE_ENABLE) // Android platform
#if !defined(SYSTEM_RPC_LIBRARY) // vendor library
  if (((int)UserPropKey > NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(
        ERROR,
        "%s: Index %d out-of-bound for ANDROIDP_DEBUG_VAR_NAME array of len %d",
        __func__, UserPropKey, NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return defValue;
  }
  return (int)property_get_int32(ANDROIDP_DEBUG_VAR_NAME[UserPropKey],
                                 defValue);
#else // system library
  if (((int)UserPropKey > NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ANDROID_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return defValue;
  }
  return (int)property_get_int32(ANDROID_DEBUG_VAR_NAME[UserPropKey], defValue);
#endif
#else // non-Android platforms
  return defValue;
#endif
}
int fastrpc_get_property_string(fastrpc_properties UserPropKey, char *value,
                                char *defValue) {
  int len = 0;
  if (((int)UserPropKey > NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ENV_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ENV_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return len;
  }
  char *env = getenv(ENV_DEBUG_VAR_NAME[UserPropKey]);
  if (env != 0) {
    strncpy(value, env, PROPERTY_VALUE_MAX - 1);
    value[PROPERTY_VALUE_MAX - 1] = '\0';
    return strlen(env);
  }
#if !defined(LE_ENABLE) // Android platform
#if !defined(SYSTEM_RPC_LIBRARY) // vendor library
  if (((int)UserPropKey > NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(
        ERROR,
        "%s: Index %d out-of-bound for ANDROIDP_DEBUG_VAR_NAME array of len %d",
        __func__, UserPropKey, NO_ANDROIDP_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return len;
  }
  return property_get(ANDROIDP_DEBUG_VAR_NAME[UserPropKey], (int *)value,
                      (int *)defValue);
#else // system library
  if (((int)UserPropKey > NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS)) {
    FARF(ERROR,
         "%s: Index %d out-of-bound for ANDROID_DEBUG_VAR_NAME array of len %d",
         __func__, UserPropKey, NO_ANDROID_DEBUG_VAR_NAME_ARRAY_ELEMENTS);
    return len;
  }
  return property_get(ANDROID_DEBUG_VAR_NAME[UserPropKey], value, defValue);
#endif
#else // non-Android platforms
  if (defValue != NULL) {
    strncpy(value, defValue, PROPERTY_VALUE_MAX - 1);
    value[PROPERTY_VALUE_MAX - 1] = '\0';
    return strlen(defValue);
  }
  return len;
#endif
}

/*
        is_first_reverse_rpc_call:

        Checks if first reverse RPC call is already done
        Args:
                @domain	-	Remote subsystem domain ID
                @handle	-	Session handle
                @sc	-	Scalar
        Returns: 1 - if first reverse RPC call already done
                 0 - otherwise
*/

static inline int is_first_reverse_rpc_call(int domain, remote_handle handle,
                                            uint32_t sc) {
  int ret = 0;

  if (IS_REVERSE_RPC_CALL(handle, sc) && IS_SESSION_OPEN_ALREADY(domain)) {
    if (hlist_lookup(domain)->first_revrpc_done)
      ret = 0;
    else {
      hlist_lookup(domain)->first_revrpc_done = 1;
      ret = 1;
    }
  }
  return ret;
}

static inline void trace_marker_init(int domain) {
  const char TRACE_MARKER_FILE[] = "/sys/kernel/tracing/trace_marker";

  if (IS_QTF_TRACING_ENABLED(hlist_lookup(domain)->procattrs)) {
    hlist_lookup(domain)->trace_marker_fd = open(TRACE_MARKER_FILE, O_WRONLY);
    ;
    if (hlist_lookup(domain)->trace_marker_fd < 0) {
      FARF(ERROR, "Error: %s: failed to open '%s' for domain %d, errno %d (%s)",
           __func__, TRACE_MARKER_FILE, domain, errno, strerror(errno));
    }
  }
}

static inline void trace_marker_deinit(int domain) {
  if (hlist_lookup(domain)->trace_marker_fd > 0) {
    close(hlist_lookup(domain)->trace_marker_fd);
    hlist_lookup(domain)->trace_marker_fd = -1;
  }
}

int get_logger_state(int domain) {
  int ret = AEE_EFAILED;
  struct handle_list *hl = hlist_lookup(domain);

  if (hl && hl->disable_exit_logs) {
    ret = AEE_SUCCESS;
  }
  return ret;
}

/* Thread function that will be invoked to update remote user PD parameters */
int fastrpc_set_remote_uthread_params(int domain) {
  int nErr = AEE_SUCCESS, paramsLen = 2;
  remote_handle64 handle = INVALID_HANDLE;
  struct fastrpc_thread_params *th_params = &hlist_lookup(domain)->th_params;

  VERIFYC(th_params != NULL, AEE_ERPC);
  if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
    nErr = remotectl1_set_param(handle, th_params->reqID, (uint32_t *)th_params,
                                paramsLen);
    if (nErr) {
      FARF(ALWAYS,
           "Warning 0x%x: %s: remotectl1 domains not supported for domain %d\n",
           nErr, __func__, domain);
      fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                 _const_remotectl1_handle, NULL, NULL);

      // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls are
      // non-domain calls
      hlist_lookup(domain)->remotectlhandle = INVALID_HANDLE;
      VERIFY(AEE_SUCCESS ==
             (nErr = remotectl_set_param(th_params->reqID,
                                         (uint32_t *)th_params, paramsLen)));
    }
  } else {
    VERIFY(AEE_SUCCESS ==
           (nErr = remotectl_set_param(th_params->reqID, (uint32_t *)th_params,
                                       paramsLen)));
  }
bail:
  if (nErr != AEE_SUCCESS) {
    if (th_params) {
      FARF(ERROR,
           "Error 0x%x: %s failed domain %d thread priority %d stack size %d "
           "(errno %s)",
           nErr, __func__, domain, th_params->thread_priority,
           th_params->stack_size, strerror(errno));
    } else {
      FARF(ERROR, "Error 0x%x: %s failed", nErr, __func__);
    }
  } else {
    FARF(ALWAYS,
         "Successfully set remote user thread priority to %d and stack size to "
         "%d for domain %d",
         th_params->thread_priority, th_params->stack_size, domain);
  }
  return nErr;
}

static inline bool is_valid_local_handle(int domain, struct handle_info *hinfo) {
  QNode *pn;
  int ii = 0;
  struct handle_list *hl;
  if(domain == -1) {
    FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
      hl = hlist_lookup(ii);
      if (!hl)
        continue;
      pthread_mutex_lock(&hl->lmut);
      QLIST_FOR_ALL(&hl->ql, pn) {
        struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
        if (hi == hinfo) {
          pthread_mutex_unlock(&hl->lmut);
          return true;
        }
      }
      pthread_mutex_unlock(&hl->lmut);
    }
  } else {
      hl = hlist_lookup(domain);
      if (!hl)
        return false;
      pthread_mutex_lock(&hl->lmut);
      QLIST_FOR_ALL(&hl->ql, pn) {
        struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
        if (hi == hinfo) {
          pthread_mutex_unlock(&hl->lmut);
          return true;
        }
      }
      pthread_mutex_unlock(&hl->lmut);
  }
  return false;
}

static int verify_local_handle(int domain, remote_handle64 local) {
  struct handle_info *hinfo = (struct handle_info *)(uintptr_t)local;
  int nErr = AEE_SUCCESS;

  VERIFYC((local != (remote_handle64)-1) && hinfo, AEE_EINVHANDLE);
  VERIFYC(is_valid_local_handle(domain, hinfo), AEE_EINVHANDLE);
  /* Verify that a session entry exists for the domain this handle
   * claims to belong to. */
  VERIFYC(hinfo->domain >= 0 && NULL != hlist_lookup(hinfo->domain),
          AEE_ERPC);
  VERIFYC(QNode_IsQueuedZ(&hinfo->qn), AEE_EINVHANDLE);
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(RUNTIME_RPC_HIGH, "Error 0x%x: %s failed. handle 0x%" PRIx64 "\n",
         nErr, __func__, local);
  }
  return nErr;
}

int get_domain_from_handle(remote_handle64 local, int *domain) {
  struct handle_info *hinfo = (struct handle_info *)(uintptr_t)local;
  int dom, nErr = AEE_SUCCESS;

  VERIFY(AEE_SUCCESS == (nErr = verify_local_handle(-1, local)));
  dom = hinfo->domain;
  VERIFYM(IS_VALID_EFFECTIVE_DOMAIN_ID(dom), AEE_EINVHANDLE,
          "Error 0x%x: domain mapped to handle is out of range domain %d "
          "handle 0x%" PRIx64 "\n",
          nErr, dom, local);
  *domain = dom;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(RUNTIME_RPC_HIGH, "Error 0x%x: %s failed. handle 0x%" PRIx64 "\n",
         nErr, __func__, local);
  }
  return nErr;
}

/**
 * Function to get domain id from domain name
 * @param[in] : Domain name of DSP
 * @param[int]: Domain name length
 * @return : Domain ID
 */
static int get_domain_from_domain_name(const char *domain_name,
                                       int domain_name_len) {
  int domain = INVALID_DOMAIN_ID;

  if (domain_name_len < strlen(SUBSYSTEM_NAME[ADSP_DOMAIN_ID])) {
    FARF(ERROR, "ERROR: %s Invalid domain name length: %u\n", __func__,
         domain_name_len);
    goto bail;
  }
  if (domain_name) {
    if (!strncmp(domain_name, SUBSYSTEM_NAME[ADSP_DOMAIN_ID],
                     strlen(SUBSYSTEM_NAME[ADSP_DOMAIN_ID]))) {
      domain = ADSP_DOMAIN_ID;
    } else if (!strncmp(domain_name, SUBSYSTEM_NAME[MDSP_DOMAIN_ID],
                            strlen(SUBSYSTEM_NAME[MDSP_DOMAIN_ID]))) {
      domain = MDSP_DOMAIN_ID;
    } else if (!strncmp(domain_name, SUBSYSTEM_NAME[SDSP_DOMAIN_ID],
                            strlen(SUBSYSTEM_NAME[SDSP_DOMAIN_ID]))) {
      domain = SDSP_DOMAIN_ID;
    } else if (!strncmp(domain_name, SUBSYSTEM_NAME[CDSP1_DOMAIN_ID],
                            strlen(SUBSYSTEM_NAME[CDSP1_DOMAIN_ID]))) {
      domain = CDSP1_DOMAIN_ID;
    } else if (!strncmp(domain_name, SUBSYSTEM_NAME[CDSP_DOMAIN_ID],
                            strlen(SUBSYSTEM_NAME[CDSP_DOMAIN_ID]))) {
      domain = CDSP_DOMAIN_ID;
    } else if (!strncmp(domain_name, SUBSYSTEM_NAME[GDSP0_DOMAIN_ID],
                            strlen(SUBSYSTEM_NAME[GDSP0_DOMAIN_ID]))) {
      domain = GDSP0_DOMAIN_ID;
    } else if (!strncmp(domain_name, SUBSYSTEM_NAME[GDSP1_DOMAIN_ID],
                            strlen(SUBSYSTEM_NAME[GDSP1_DOMAIN_ID]))) {
      domain = GDSP1_DOMAIN_ID;
    } else {
      FARF(ERROR, "ERROR: %s Invalid domain name: %s\n", __func__, domain_name);
    }
  }
  VERIFY_IPRINTF("%s: %d\n", __func__, domain);
bail:
  return domain;
}

#define IS_CONST_HANDLE(h) (((h) < 0xff) ? 1 : 0)

static int get_handle_remote(remote_handle64 local, remote_handle64 *remote) {
  struct handle_info *hinfo = (struct handle_info *)(uintptr_t)local;
  int nErr = AEE_SUCCESS;

  VERIFY(AEE_SUCCESS == (nErr = verify_local_handle(-1, local)));
  *remote = hinfo->remote;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error %x: get handle remote failed %p\n", nErr, &local);
  }
  return nErr;
}

/**
 * print_open_handles() - Function to print all open handles.
 * @domain: domain of handles to be printed.
 * Return: void.
 */
static void print_open_handles(int domain) {
	struct handle_info *hi = NULL;
	QNode *pn = NULL;

	FARF(RUNTIME_RPC_HIGH, "List of open handles on domain %d:\n", domain);
	pthread_mutex_lock(&hlist_lookup(domain)->mut);
	QLIST_FOR_ALL(&hlist_lookup(domain)->ql, pn) {
		hi = STD_RECOVER_REC(struct handle_info, qn, pn);
		if (hi->name)
			FARF(RUNTIME_RPC_HIGH, "%s, handle 0x%"PRIx64"",
				hi->name, hi->remote);
	}
	pthread_mutex_unlock(&hlist_lookup(domain)->mut);
}

/**
 * get_lib_name() - Function to get lib name from uri.
 * @uri: uri for the lib.
 * Return: @lib_name or NULL
 */
static char* get_lib_name(const char *uri) {
	char *library_name = NULL;
	const char SO_EXTN[] = ".so";
	const char LIB_EXTN[] = "lib";
	const char *start = NULL, *end = NULL;
	unsigned int length = 0;
	int nErr = AEE_SUCCESS;

  VERIFY(uri);
  start = strstr(uri, LIB_EXTN);
	if (start) {
		end = strstr(start, SO_EXTN);
		if (end && end > start) {
			/* add extension size to print .so also */
			length = (unsigned int)(end - start) + strlen(SO_EXTN);
			/* allocate length + 1 to include \0 */
			VERIFYC(NULL != (library_name =
				(char*)calloc(1, length + 1)), AEE_ENOMEMORY);
			strlcpy(library_name, start, length + 1);
			return library_name;
		}
	}
bail:
	FARF(RUNTIME_RPC_ERROR, "Warning 0x%x: %s failed for uri %s",
		nErr, __func__, uri);
  return NULL;
}

static int fastrpc_alloc_handle(int domain, QList *me, remote_handle64 remote,
                                remote_handle64 *local, const char *name) {
  struct handle_info *hinfo = {0};
  int nErr = 0;
  char *libname = NULL;

  VERIFYC(NULL != (hinfo = calloc(1, sizeof(*hinfo))), AEE_ENOMEMORY);
  hinfo->local = (remote_handle64)(uintptr_t)hinfo;
  hinfo->remote = remote;
  libname = get_lib_name(name);
  hinfo->name = libname;
  hinfo->domain = domain;
  *local = hinfo->local;

  QNode_CtorZ(&hinfo->qn);
  pthread_mutex_lock(&hlist_lookup(domain)->lmut);
  QList_PrependNode(me, &hinfo->qn);
  pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for local handle 0x%x, remote handle 0x%x, "
         "domain %d\n",
         nErr, __func__, local, remote, domain);
  }
  return nErr;
}

static int fastrpc_free_handle(int domain, QList *me, remote_handle64 remote) {
  pthread_mutex_lock(&hlist_lookup(domain)->lmut);
  if (!QList_IsEmpty(me)) {
    QNode *pn = NULL, *pnn = NULL;
    QLIST_NEXTSAFE_FOR_ALL(me, pn, pnn) {
      struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
      if (hi->remote == remote) {
        QNode_DequeueZ(&hi->qn);
        if(hi->name)
          free(hi->name);
        free(hi);
        hi = NULL;
        break;
      }
    }
  }
  pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
  return 0;
}

int fastrpc_update_module_list(uint32_t req, int domain, remote_handle64 h,
                               remote_handle64 *local, const char *name) {
  int nErr = AEE_SUCCESS;

  switch (req) {
  case DOMAIN_LIST_PREPEND: {
    VERIFY(AEE_SUCCESS ==
           (nErr = fastrpc_alloc_handle(domain, &hlist_lookup(domain)->ql, h, local, name)));
    if(IS_CONST_HANDLE(h)) {
      pthread_mutex_lock(&hlist_lookup(domain)->lmut);
      hlist_lookup(domain)->constCount++;
      pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    } else {
      pthread_mutex_lock(&hlist_lookup(domain)->lmut);
      hlist_lookup(domain)->domainsCount++;
      pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    }
    break;
  }
  case DOMAIN_LIST_DEQUEUE: {
    VERIFY(AEE_SUCCESS ==
           (nErr = fastrpc_free_handle(domain, &hlist_lookup(domain)->ql, h)));
    if(IS_CONST_HANDLE(h)) {
      pthread_mutex_lock(&hlist_lookup(domain)->lmut);
      hlist_lookup(domain)->constCount--;
      pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    } else {
      pthread_mutex_lock(&hlist_lookup(domain)->lmut);
      hlist_lookup(domain)->domainsCount--;
      pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    }
    break;
  }
  case NON_DOMAIN_LIST_PREPEND: {
    VERIFY(AEE_SUCCESS ==
           (nErr = fastrpc_alloc_handle(domain, &hlist_lookup(domain)->nql, h, local, name)));
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    hlist_lookup(domain)->nondomainsCount++;
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    break;
  }
  case NON_DOMAIN_LIST_DEQUEUE: {
    VERIFY(AEE_SUCCESS ==
           (nErr = fastrpc_free_handle(domain, &hlist_lookup(domain)->nql, h)));
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    hlist_lookup(domain)->nondomainsCount--;
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    break;
  }
  case REVERSE_HANDLE_LIST_PREPEND: {
    VERIFY(AEE_SUCCESS ==
           (nErr = fastrpc_alloc_handle(domain, &hlist_lookup(domain)->rql, h, local, name)));
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    hlist_lookup(domain)->reverseCount++;
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    break;
  }
  case REVERSE_HANDLE_LIST_DEQUEUE: {
    VERIFY(AEE_SUCCESS ==
           (nErr = fastrpc_free_handle(domain, &hlist_lookup(domain)->rql, h)));
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    hlist_lookup(domain)->reverseCount--;
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    break;
  }
  default: {
    nErr = AEE_EUNSUPPORTEDAPI;
    goto bail;
  }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for request ID %u, handle 0x%x, domain %d\n",
         nErr, __func__, req, h, domain);
  } else {
    FARF(RUNTIME_RPC_HIGH, "Library D count %d, C count %d, N count %d, R count %d\n", hlist_lookup(domain)->domainsCount, hlist_lookup(domain)->constCount, hlist_lookup(domain)->nondomainsCount, hlist_lookup(domain)->reverseCount);
  }
  return nErr;
}

static void fastrpc_clear_handle_list(uint32_t req, int domain) {
  int nErr = AEE_SUCCESS;
  QNode *pn = NULL;
  char dlerrstr[MAX_DLERRSTR_LEN];
  int dlerr = 0;

  switch (req) {
  case MULTI_DOMAIN_HANDLE_LIST_ID: {
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    if (!QList_IsNull(&hlist_lookup(domain)->ql)) {
      while ((pn = QList_Pop(&hlist_lookup(domain)->ql))) {
        struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
        if (hi->name)
          free(hi->name);
        free(hi);
        hi = NULL;
      }
    }
    hlist_lookup(domain)->domainsCount = 0;
    hlist_lookup(domain)->constCount = 0;
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    break;
  }
  case NON_DOMAIN_HANDLE_LIST_ID: {
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    if (!QList_IsNull(&hlist_lookup(domain)->nql)) {
      while ((pn = QList_Pop(&hlist_lookup(domain)->nql))) {
        struct handle_info *h = STD_RECOVER_REC(struct handle_info, qn, pn);
        if (h->name)
          free(h->name);
        free(h);
        h = NULL;
      }
    }
    hlist_lookup(domain)->nondomainsCount = 0;
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    break;
  }
  case REVERSE_HANDLE_LIST_ID: {
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    if (!QList_IsNull(&hlist_lookup(domain)->rql)) {
      while ((pn = QList_Pop(&hlist_lookup(domain)->rql))) {
        struct handle_info *hi = STD_RECOVER_REC(struct handle_info, qn, pn);
        pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
        close_reverse_handle(hi->local, dlerrstr, sizeof(dlerrstr), &dlerr);
        pthread_mutex_lock(&hlist_lookup(domain)->lmut);
        if (hi->name)
          free(hi->name);
        free(hi);
        hi = NULL;
      }
    }
    hlist_lookup(domain)->reverseCount = 0;
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    break;
  }
  default: {
    nErr = AEE_EUNSUPPORTEDAPI;
    goto bail;
  }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %u, domain %d\n", nErr,
         __func__, req, domain);
  }
  return;
}

// Notify kernel to awake PM
static int wakelock_control_kernel_pm(int domain, int dev, uint32_t timeout) {
  int nErr = AEE_SUCCESS;
  struct fastrpc_ctrl_pm pm = {0};

  pm.timeout = timeout;
  nErr = ioctl_control(dev, DSPRPC_PM, &pm);
  if (nErr) {
    if (errno == EBADRQC || errno == ENOTTY) {
      VERIFY_WPRINTF("Warning: %s: kernel does not support PM management (%s)",
                     __func__, strerror(errno));
    } else if (errno == EACCES || errno == EPERM) {
      VERIFY_WPRINTF("Warning: %s: application does not have permission for PM "
                     "management (%s)",
                     __func__, strerror(errno));
    }
    FARF(ERROR,
         "Error 0x%x: %s PM control failed for domain %d, dev %d with timeout "
         "%d (errno %s)",
         nErr, __func__, domain, dev, timeout, strerror(errno));
  }
  return nErr;
}

// Callback function for posix timer
static void fastrpc_timer_callback(void *ptr) {
  fastrpc_timer *frpc_timer = (fastrpc_timer *)ptr;
  int nErr = AEE_SUCCESS;
  remote_rpc_process_exception data;
  bool expected = false;

  atomic_compare_exchange_strong(&timer_expired, &expected, true);
  if (expected == true)
    return;

  FARF(ALWAYS,
       "%s fastrpc time out of %d ms on thread %d on domain %d sc 0x%x handle 0x%x\n",
       __func__, frpc_timer->timeout_millis, frpc_timer->tid,
       frpc_timer->domain, frpc_timer->sc, frpc_timer->handle);
  data.domain = frpc_timer->domain;
  nErr = remote_session_control(FASTRPC_REMOTE_PROCESS_EXCEPTION, &data,
                                sizeof(remote_rpc_process_exception));
  if (nErr) {
    FARF(ERROR,
         "%s: Failed to create exception in the remote process on domain %d "
         "(errno %s)",
         __func__, data.domain, strerror(errno));
  }
}

// Function to add timer before remote RPC call
static void fastrpc_add_timer(fastrpc_timer *frpc_timer) {
  struct sigevent sigevent;
  struct itimerspec time_spec;
  int err = 0;

  memset(&sigevent, 0, sizeof(sigevent));
  sigevent.sigev_notify = SIGEV_THREAD;
  sigevent.sigev_notify_function =
      (void (*)(union sigval))fastrpc_timer_callback;
  sigevent.sigev_value.sival_ptr = frpc_timer;
  err = timer_create(CLOCK_MONOTONIC, &sigevent, &(frpc_timer->timer));
  if (err) {
    FARF(ERROR, "%s: failed to create timer with error 0x%x\n", __func__, err);
    goto bail;
  }

  time_spec.it_value.tv_sec = frpc_timer->timeout_millis / 1000;
  time_spec.it_value.tv_nsec =
      (frpc_timer->timeout_millis % 1000) * 1000 * 1000;
  err = timer_settime(frpc_timer->timer, 0, &time_spec, NULL);
  if (err) {
    FARF(ERROR, "%s: failed to set timer with error 0x%x\n", __func__, err);
    goto bail;
  }
bail:
  return;
}

// Function to delete timer after remote RPC call
static void fastrpc_delete_timer(timer_t *timer) {
  int nErr = AEE_SUCCESS;
  nErr = timer_delete(*timer);
  if (nErr) {
    FARF(ERROR, "%s: Failed to delete timer", __func__);
  }
}

int remote_handle_invoke_domain(int domain, remote_handle handle,
                                uint32_t sc, remote_arg *pra) {
  int dev, total, bufs, handles, i, nErr = 0, wake_lock = 0, rpc_timeout = 0;
  unsigned req;
  uint32_t len;
  struct handle_list *list;
  uint32_t *crc_remote = NULL;
  uint32_t *crc_local = NULL;
  uint64_t *perf_kernel = NULL;
  uint64_t *perf_dsp = NULL;
  fastrpc_timer frpc_timer;
  int trace_marker_fd = hlist_lookup(domain)->trace_marker_fd;
  bool trace_enabled = false;
  struct fastrpc_invoke_args* args = NULL; 

  if (IS_QTF_TRACING_ENABLED(hlist_lookup(domain)->procattrs) &&
      !IS_STATIC_HANDLE(handle) && trace_marker_fd > 0) {
    /* Write begin trace marker; only enable tracing if write succeeds.
     * This ensures we don't attempt to write an end marker if begin failed. */
    ssize_t ret = write(trace_marker_fd, INVOKE_BEGIN_TRACE_STR, invoke_begin_trace_strlen);
    if (ret > 0)
      trace_enabled = true;
  }

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_session_dev(domain, &dev)));

  errno = 0;
  if (hlist_lookup(domain)->wake_lock_enable) {
    if (!IS_REVERSE_RPC_CALL(handle, sc) ||
        is_first_reverse_rpc_call(domain, handle, sc)) {
      if (!fastrpc_wake_lock())
        wake_lock = 1;
    } else if (IS_REVERSE_RPC_CALL(handle, sc))
      /* Since wake-lock is not released at the end of previous
       * "remote_handle_invoke" for subsequent reverse RPC calls, it doesn't
       * have to be taken again here.
       * It will be released before "ioctl invoke" call to kernel */
      wake_lock = 1;
  }

  list = hlist_lookup(domain);
  if (list->setmode) {
    list->setmode = 0;
    nErr = ioctl_setmode(dev, list->mode);
    if (nErr) {
      nErr = convert_kernel_to_user_error(nErr, errno);
      goto bail;
    }
  }

  bufs = REMOTE_SCALARS_INBUFS(sc) + REMOTE_SCALARS_OUTBUFS(sc);
  handles = REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc);
  total = bufs + handles;

  INITIALIZE_REMOTE_ARGS(total);

  req = INVOKE;
  VERIFYC(!(NULL == pra && total > 0), AEE_EBADPARM);
  for (i = 0; i < bufs; i++) {
    set_args(i, pra[i].buf.pv, pra[i].buf.nLen, -1, 0);
    if (pra[i].buf.nLen) {
      void *base;
      int nova = 0, attr = 0, fd = -1;
      VERIFY(AEE_SUCCESS ==
             (nErr = fdlist_fd_from_buf(pra[i].buf.pv, (int)pra[i].buf.nLen,
                                        &nova, &base, &attr, &fd)));
      if (fd != -1) {
        set_args_fd(i, fd);
        req = INVOKE_FD;
      }
      if (nova) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_NOVA);
        // pra[i].buf.pv = (void*)((uintptr_t)pra[i].buf.pv - (uintptr_t)base);
        VERIFY_IPRINTF("nova buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_NON_COHERENT) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_NON_COHERENT);
        VERIFY_IPRINTF("non-coherent buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_COHERENT) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_COHERENT);
        VERIFY_IPRINTF("coherent buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_FORCE_NOFLUSH) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_FORCE_NOFLUSH);
        VERIFY_IPRINTF("force no flush for buffer idx: %d addr: %p size: %d", i,
                       pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_FORCE_NOINVALIDATE) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_FORCE_NOINVALIDATE);
        VERIFY_IPRINTF("force no invalidate buffer idx: %d addr: %p size: %d",
                       i, pra[i].buf.pv, pra[i].buf.nLen);
      }
      if (attr & FASTRPC_ATTR_KEEP_MAP) {
        req = INVOKE_ATTRS;
        append_args_attr(i, FASTRPC_ATTR_KEEP_MAP);
        VERIFY_IPRINTF("invoke: mapping with attribute KEEP_MAP");
      }
    }
  }

  for (i = bufs; i < total; i++) {
    unsigned int attr = 0;
    int dma_fd = -1;

    req = INVOKE_ATTRS;
    unregister_dma_handle(pra[i].dma.fd, &len, &attr);
    if (hlist_lookup(domain)->dma_handle_reverse_rpc_map_capability &&
        (attr & FASTRPC_ATTR_NOMAP)) {
      // Register fd again, for reverse RPC call to retrive FASTRPC_ATTR_NOMAP
      // flag for fd
      remote_register_dma_handle_attr(pra[i].dma.fd, len, FASTRPC_ATTR_NOMAP);
    }
    dma_fd = pra[i].dma.fd;
    set_args(i, (void *)(uintptr_t)pra[i].dma.offset, len, dma_fd, attr);
    append_args_attr(i, FASTRPC_ATTR_NOVA);
  }

  if (IS_CRC_CHECK_ENABLED(hlist_lookup(domain)->procattrs) &&
      (!IS_STATIC_HANDLE(handle))) {
    int nInBufs = REMOTE_SCALARS_INBUFS(sc);
    crc_local = (uint32_t *)calloc(M_CRCLIST, sizeof(uint32_t));
    crc_remote = (uint32_t *)calloc(M_CRCLIST, sizeof(uint32_t));
    VERIFYC(crc_local != NULL && crc_remote != NULL, AEE_ENOMEMORY);
    VERIFYC(!(NULL == pra && nInBufs > 0), AEE_EBADPARM);
    for (i = 0; (i < nInBufs) && (i < M_CRCLIST); i++)
      crc_local[i] = crc32_lut((unsigned char *)pra[i].buf.pv,
                               (int)pra[i].buf.nLen, crc_table);
    req = INVOKE_CRC;
  }

  if (IS_KERNEL_PERF_ENABLED(hlist_lookup(domain)->procattrs) &&
      (!IS_STATIC_HANDLE(handle))) {
    perf_kernel = (uint64_t *)calloc(PERF_KERNEL_KEY_MAX, sizeof(uint64_t));
    VERIFYC(perf_kernel != NULL, AEE_ENOMEMORY);
    req = INVOKE_PERF;
  }
  if (IS_DSP_PERF_ENABLED(hlist_lookup(domain)->procattrs) &&
      (!IS_STATIC_HANDLE(handle))) {
    perf_dsp = (uint64_t *)calloc(PERF_DSP_KEY_MAX, sizeof(uint64_t));
    VERIFYC(perf_dsp != NULL, AEE_ENOMEMORY);
    req = INVOKE_PERF;
  }

  if (!IS_STATIC_HANDLE(handle)) {
    if ((rpc_timeout = fastrpc_config_get_rpctimeout()) > 0) {
      frpc_timer.domain = domain;
      frpc_timer.sc = sc;
      frpc_timer.handle = handle;
      frpc_timer.timeout_millis = rpc_timeout;
      frpc_timer.tid = gettid();
      fastrpc_add_timer(&frpc_timer);
    }
  }

  FASTRPC_TRACE_LOG(FASTRPC_TRACE_INVOKE_START, handle, sc);
  if (wake_lock) {
    wakelock_control_kernel_pm(domain, dev, PM_TIMEOUT_MS);
    fastrpc_wake_unlock();
    wake_lock = 0;
  }
  // Macros are initializing and destroying pfds and pattrs.
  nErr = ioctl_invoke(dev, req, handle, sc, get_args(), pfds, pattrs,
                      crc_remote, perf_kernel, perf_dsp);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
  }

  if (hlist_lookup(domain)->wake_lock_enable) {
    if (!fastrpc_wake_lock())
      wake_lock = 1;
  }
  FASTRPC_TRACE_LOG(FASTRPC_TRACE_INVOKE_END, handle, sc);
  if (!IS_STATIC_HANDLE(handle) && rpc_timeout > 0) {
    fastrpc_delete_timer(&(frpc_timer.timer));
  }

  if (IS_CRC_CHECK_ENABLED(hlist_lookup(domain)->procattrs) &&
      (!IS_STATIC_HANDLE(handle))) {
    int nInBufs = REMOTE_SCALARS_INBUFS(sc);
    VERIFYC(crc_local != NULL && crc_remote != NULL, AEE_ENOMEMORY);
    for (i = nInBufs; i < bufs; i++)
      crc_local[i] = crc32_lut((unsigned char *)pra[i].buf.pv,
                               (int)pra[i].buf.nLen, crc_table);
    for (i = 0; (i < bufs) && (i < M_CRCLIST); i++) {
      if (crc_local[i] != crc_remote[i]) {
        FARF(ERROR, "CRC mismatch for buffer %d[%d], crc local %x remote %x", i,
             bufs, crc_local[i], crc_remote[i]);
        break;
      }
    }
  }

  if (IS_KERNEL_PERF_ENABLED(hlist_lookup(domain)->procattrs) &&
      (!IS_STATIC_HANDLE(handle))) {
    VERIFYC(perf_kernel != NULL, AEE_ENOMEMORY);
    FARF(ALWAYS,
         "RPCPERF-K  H:0x%x SC:0x%x C:%" PRIu64 " F:%" PRIu64 " ns M:%" PRIu64
         " ns CP:%" PRIu64 " ns L:%" PRIu64 " ns G:%" PRIu64 " ns P:%" PRIu64
         " ns INV:%" PRIu64 " ns INVOKE:%" PRIu64 " ns\n",
         handle, sc, perf_kernel[0], perf_kernel[1], perf_kernel[2],
         perf_kernel[3], perf_kernel[4], perf_kernel[5], perf_kernel[6],
         perf_kernel[7], perf_kernel[8]);
  }
  if (IS_DSP_PERF_ENABLED(hlist_lookup(domain)->procattrs) &&
      (!IS_STATIC_HANDLE(handle))) {
    VERIFYC(perf_dsp != NULL, AEE_ENOMEMORY);
    FARF(ALWAYS,
         "RPCPERF-D  H:0x%x SC:0x%x C:%" PRIu64 " M_H:%" PRIu64 " us M:%" PRIu64
         " us G:%" PRIu64 " us INVOKE:%" PRIu64 " us P:%" PRIu64
         " us CACHE:%" PRIu64 " us UM:%" PRIu64 " us "
         "UM_H:%" PRIu64 " us R:%" PRIu64 " us E_R:%" PRIu64
         " us J_S_T:%" PRIu64 " us\n",
         handle, sc, perf_dsp[0], perf_dsp[1], perf_dsp[2], perf_dsp[3],
         perf_dsp[4], perf_dsp[5], perf_dsp[6], perf_dsp[7], perf_dsp[8],
         perf_dsp[9], perf_dsp[10], perf_dsp[11]);
  }

  if (!(perf_v2_kernel && perf_v2_dsp)) {
    fastrpc_perf_update(dev, handle, sc);
  }
bail:
  DESTROY_REMOTE_ARGS();
  if (crc_local) {
    free(crc_local);
    crc_local = NULL;
  }
  if (crc_remote) {
    free(crc_remote);
    crc_remote = NULL;
  }
  if (perf_kernel) {
    free(perf_kernel);
    perf_kernel = NULL;
  }
  if (perf_dsp) {
    free(perf_dsp);
    perf_dsp = NULL;
  }
  if (wake_lock) {
    // Keep holding wake-lock for reverse RPC calls to keep CPU awake for any
    // further processing
    if (!IS_REVERSE_RPC_CALL(handle, sc)) {
      fastrpc_wake_unlock();
      wake_lock = 0;
    }
  }
  if (trace_enabled) {
    /* Write end trace marker. Store return value to satisfy warn_unused_result,
     * but don't check it since trace writes are optional and failures are non-critical. */
    ssize_t ret __attribute__((unused));
    ret = write(trace_marker_fd, INVOKE_END_TRACE_STR, invoke_end_trace_strlen);
  }
  if (nErr != AEE_SUCCESS) {
    if ((nErr == -1) && (errno == ECONNRESET)) {
      nErr = AEE_ECONNRESET;
    }
    // FARF(ERROR, "Error 0x%x: %s failed for handle 0x%x on domain %d (sc
    // 0x%x)\n", nErr, __func__, (int)handle, domain, sc);
  }
  return nErr;
}

int remote_handle_invoke(remote_handle handle, uint32_t sc, remote_arg *pra) {
  int domain = -1, nErr = AEE_SUCCESS, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "Entering %s, handle %u sc %X remote_arg %p\n",
       __func__, handle, sc, pra);
  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x , scalar 0x%x", __func__,
                         (int)handle, sc);
  VERIFYC(handle != (remote_handle)-1, AEE_EINVHANDLE);

  domain = get_current_domain();
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS ==
         (nErr = remote_handle_invoke_domain(domain, handle, sc, pra)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    if (is_process_exiting(domain)) {
      return 0;
    }
    /*
     * handle_info or so name cannot be obtained from remote handles which
     * are used for non-domain calls.
     */
    if (0 == check_rpc_error(nErr)) {
      if (get_logger_state(domain)) {
        FARF(ERROR,
             "Error 0x%x: %s failed for handle 0x%x, method %d on domain %d "
             "(sc 0x%x) (errno %s)\n",
             nErr, __func__, (int)handle, REMOTE_SCALARS_METHOD(sc), domain, sc,
             strerror(errno));
      }
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle64_invoke(remote_handle64 local, uint32_t sc,
                           remote_arg *pra) {
  remote_handle64 remote = 0;
  int nErr = AEE_SUCCESS, domain = -1, ref = 0;
  struct handle_info *h = (struct handle_info*)local;

  if (IS_STATICPD_HANDLE(local)) {
     nErr = AEE_EINVHANDLE;
     FARF(ERROR, "Error 0x%x: %s cannot be called for staticPD handle 0x%"PRIx64"\n",
                 nErr, __func__, local);
     goto bail;
  }

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x , scalar 0x%x", __func__,
                         (int)local, sc);
  VERIFYC(local != (remote_handle64)-1, AEE_EINVHANDLE);

  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(local, &domain)));
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = get_handle_remote(local, &remote)));
  VERIFY(AEE_SUCCESS ==
         (nErr = remote_handle_invoke_domain(domain, remote, sc, pra)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    if (is_process_exiting(domain)) {
      return 0;
    }
    if (0 == check_rpc_error(nErr)) {
      if (get_logger_state(domain)) {
        if (nErr == DSP_AEE_EOFFSET + AEE_ENOSUCHMOD) {
          FARF(ALWAYS,
               "Warning: %s module %s is not supported on DSP domain %d, handle 0x%" PRIx64
               ", method %d (sc 0x%x) (errno %s)\n",
               __func__, h->name, domain, local, REMOTE_SCALARS_METHOD(sc), sc,
               strerror(errno));
        } else {
          FARF(ERROR,
               "Error 0x%x: %s failed for module %s, handle 0x%" PRIx64
               ", method %d on domain %d (sc 0x%x) (errno %s)\n",
               nErr, __func__, h->name, local, REMOTE_SCALARS_METHOD(sc), domain, sc,
               strerror(errno));
        }
      }
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int listener_android_geteventfd(int domain, int *fd);
int remote_handle_open_domain(int domain, const char *name, remote_handle *ph,
                              uint64_t *t_spawn, uint64_t *t_load) {
  char dlerrstr[255];
  int dlerr = 0, nErr = AEE_SUCCESS;
  int dev = -1;
  char *pdname_uri = NULL;
  int name_len = 0;
  remote_handle64 handle = INVALID_HANDLE;

  FASTRPC_ATRACE_BEGIN_L("%s called with domain %d, name %s, handle 0x%x",
                         __func__, domain, name, ph);
  /* If the total reference count exceeds one then exit the application. */
  if (total_dsp_lib_refcnt > MAX_LIB_INSTANCE_ALLOWED) {
    FARF(ERROR,
         "Error: aborting due to %d instances of libxdsprpc. Only %d allowed\n",
         total_dsp_lib_refcnt, MAX_LIB_INSTANCE_ALLOWED);
    deinit_fastrpc_dsp_lib_refcnt();
    exit(EXIT_FAILURE);
  }
  if (!strncmp(name, ITRANSPORT_PREFIX "geteventfd",
                   strlen(ITRANSPORT_PREFIX "geteventfd"))) {
    FARF(RUNTIME_RPC_HIGH, "getting event fd");
    return listener_android_geteventfd(domain, (int *)ph);
  }
  if (!strncmp(name, ITRANSPORT_PREFIX "attachguestos",
                   strlen(ITRANSPORT_PREFIX "attachguestos"))) {
    FARF(RUNTIME_RPC_HIGH, "setting attach mode to guestos : %d", domain);
    *ph = ATTACHGUESTOS_HANDLE;
    hlist_lookup(domain)->dsppd = ROOT_PD;
    return AEE_SUCCESS;
  }
  if (!strncmp(name, ITRANSPORT_PREFIX "createstaticpd",
                   strlen(ITRANSPORT_PREFIX "createstaticpd"))) {
    FARF(RUNTIME_RPC_HIGH, "creating static pd on domain: %d", domain);
    name_len = strlen(name);
    VERIFYC(NULL !=
                (pdname_uri = (char *)calloc(name_len + 1, sizeof(char))),
            AEE_ENOMEMORY);
    strlcpy(pdname_uri, name, name_len + 1);
    char *pdName = pdname_uri + strlen(ITRANSPORT_PREFIX "createstaticpd:");

    /*
     * Support sessions feature for static PDs.
     * For eg, the same app can call 'remote_handle64_open' with
     * "createstaticpd:sensorspd&_dom=adsp&_session=0" and
     * "createstaticpd:oispd&_dom=adsp&_session=1" to create a session
     * on both static PDs. pdName is matched by prefix below, so a
     * trailing "&_dom=...&_session=..." suffix never affects the match;
     * dsppdname is set from the matched literal, not from pdName, so it
     * never ends up storing a dynamic domain's suffix.
     *
     * No length check against pdName here: every destination literal
     * below ("audiopd", "securepd", ...) is a short hardcoded string
     * copied with its own exact length, well within dsppdname's
     * MAX_DSPPD_NAMELEN -- never pdName itself, which still carries the
     * full "&_dom=...&_session=..." suffix and can legitimately exceed
     * that length for a dynamic domain's URI without that being a
     * problem for anything actually written.
     */
    if (!strncmp(pdName, "audiopd", strlen("audiopd"))) {
      *ph = AUDIOPD_HANDLE;
      hlist_lookup(domain)->dsppd = AUDIO_STATICPD;
      strlcpy(hlist_lookup(domain)->dsppdname, "audiopd", strlen("audiopd") + 1);
    } else if (!strncmp(pdName, "securepd", strlen("securepd"))) {
      FARF(ALWAYS, "%s: attaching to securePD\n", __func__);
      *ph = SECUREPD_HANDLE;
      hlist_lookup(domain)->dsppd = SECURE_STATICPD;
      strlcpy(hlist_lookup(domain)->dsppdname, "securepd", strlen("securepd") + 1);
    } else if (!strncmp(pdName, "sensorspd", strlen("sensorspd"))) {
      *ph = SENSORPD_HANDLE;
      hlist_lookup(domain)->dsppd = SENSORS_STATICPD;
      strlcpy(hlist_lookup(domain)->dsppdname, "sensorspd", strlen("sensorspd") + 1);
    } else if (!strncmp(pdName, "rootpd", strlen("rootpd"))) {
      *ph = ROOTPD_HANDLE;
      hlist_lookup(domain)->dsppd = GUEST_OS_SHARED;
      strlcpy(hlist_lookup(domain)->dsppdname, "rootpd", strlen("rootpd") + 1);
    } else if (!strncmp(pdName, "oispd", strlen("oispd"))) {
      *ph = OISPD_HANDLE;
      hlist_lookup(domain)->dsppd = OIS_STATICPD;
      strlcpy(hlist_lookup(domain)->dsppdname, "oispd", strlen("oispd") + 1);
    }
    if (pdname_uri) {
      free(pdname_uri);
      pdname_uri = NULL;
    }
    return AEE_SUCCESS;
  }
  if (!strncmp(name, ITRANSPORT_PREFIX "attachuserpd",
	       strlen(ITRANSPORT_PREFIX "attachuserpd"))) {
    FARF(RUNTIME_RPC_HIGH, "setting attach mode to userpd : %d", domain);
    hlist_lookup(domain)->dsppd = USERPD;
    return AEE_SUCCESS;
  }
  PROFILE_ALWAYS(t_spawn,
                 VERIFY(AEE_SUCCESS == (nErr = domain_init(domain, &dev)));
                 VERIFYM(-1 != dev, AEE_ERPC, "open dev failed\n"););
  PROFILE_ALWAYS(
      t_load,
      if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
        nErr = remotectl1_open1(handle, name, (int *)ph, dlerrstr,
                                sizeof(dlerrstr), &dlerr);
        if (nErr) {
          FARF(ALWAYS,
               "Warning 0x%x: %s: remotectl1 domains not supported for domain "
               "%d\n",
               nErr, __func__, domain);
          fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                     _const_remotectl1_handle, NULL, NULL);

          // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls
          // are non-domain calls
          hlist_lookup(domain)->remotectlhandle = INVALID_HANDLE;
          VERIFY(AEE_SUCCESS ==
                 (nErr = remotectl_open(name, (int *)ph, dlerrstr,
                                        sizeof(dlerrstr), &dlerr)));
        }
      } else {
        VERIFY(AEE_SUCCESS ==
               (nErr = remotectl_open(name, (int *)ph, dlerrstr,
                                      sizeof(dlerrstr), &dlerr)));
      } VERIFY(AEE_SUCCESS == (nErr = dlerr)););
bail:
  if (dlerr != 0) {
    FARF(ERROR,
         "Error 0x%x: %s: dynamic loading failed for %s on domain %d (dlerror "
         "%s) (errno %s)\n",
         nErr, __func__, name, domain, dlerrstr, strerror(errno));
  }
  if (pdname_uri) {
    free(pdname_uri);
    pdname_uri = NULL;
  }
  if (nErr == AEE_ECONNRESET) {
      if (!hlist_lookup(domain)->domainsCount && !hlist_lookup(domain)->nondomainsCount) {
      /* Close session if there are no open remote handles */
      hlist_lookup(domain)->disable_exit_logs = 1;
      domain_deinit(domain);
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle_open(const char *name, remote_handle *ph) {
  int nErr = 0, domain = -1, ref = 0;
  uint64_t t_spawn = 0, t_load = 0;
  remote_handle64 local;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "Entering %s, name %s\n", __func__, name);
  FASTRPC_ATRACE_BEGIN_L("%s for %s", __func__, name);

  if (!name || !ph) {
    FARF(ERROR, "%s: Invalid input", __func__);
    return AEE_EBADPARM;
  }

  domain = get_current_domain();
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_open_domain(domain, name, ph,
                                                          &t_spawn, &t_load)));
  fastrpc_update_module_list(NON_DOMAIN_LIST_PREPEND, domain, *ph, &local, name);
bail:
  if (nErr) {
    if (*ph) {
      remote_handle64_close(*ph);
    } else {
      FASTRPC_PUT_REF(domain);
    }
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for %s (errno %s)", nErr, __func__, name,
          strerror(errno));
    }
  } else {
    FARF(ALWAYS,
         "%s: Successfully opened handle 0x%x for %s on domain %d (spawn time "
         "%" PRIu64 " us, load time %" PRIu64 " us)",
         __func__, (int)(*ph), name, domain, t_spawn, t_load);
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle64_open(const char *name, remote_handle64 *ph) {
  remote_handle h = 0;
  remote_handle64 remote = 0, local;
  int domain = -1, nErr = 0, ref = 0;
  uint64_t t_spawn = 0, t_load = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));
  FARF(RUNTIME_RPC_HIGH, "Entering %s, name %s\n", __func__, name);
  FASTRPC_ATRACE_BEGIN_L("%s for %s", __func__, name);

  if (!name || !ph) {
    FARF(ERROR, "%s: Invalid input", __func__);
    return AEE_EBADPARM;
  }

  domain = get_domain_from_name(name, DOMAIN_NAME_IN_URI);
  VERIFYC(domain >= 0, AEE_EBADPARM);
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_open_domain(domain, name, &h,
                                                          &t_spawn, &t_load)));
  /* Returning local handle to "geteventd" call causes bad fd error when daemon
     polls on it, hence return remote handle (which is the actual fd) for
     "geteventd" call*/
  if (!strncmp(name, ITRANSPORT_PREFIX "geteventfd",
                   strlen(ITRANSPORT_PREFIX "geteventfd")) ||
                   IS_STATICPD_HANDLE(h)) {
    *ph = h;
  } else {
    fastrpc_update_module_list(DOMAIN_LIST_PREPEND, domain, h, &local, name);
    get_handle_remote(local, &remote);
    *ph = local;
  }
bail:
  if (nErr) {
    if (h)
      remote_handle_close(h);
    else
      FASTRPC_PUT_REF(domain);
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for %s (errno %s)\n", nErr, __func__,
           name, strerror(errno));
    }
  } else {
    FARF(ALWAYS,
         "%s: Successfully opened handle 0x%" PRIx64 " (remote 0x%" PRIx64
         ") for %s on domain %d (spawn time %" PRIu64 " us, load time %" PRIu64
         " us), num handles %u",
         __func__, (*ph), remote, name, domain, t_spawn, t_load,
         hlist_lookup(domain)->domainsCount);
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle_close_domain(int domain, remote_handle h) {
  char *dlerrstr = NULL;
  int dlerr = 0, nErr = AEE_SUCCESS;
  size_t err_str_len = MAX_DLERRSTR_LEN * sizeof(char);
  uint64_t t_close = 0;
  remote_handle64 handle = INVALID_HANDLE;

  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x", __func__, (int)h);
  VERIFYC(h != (remote_handle)-1, AEE_EINVHANDLE);
  VERIFYC(NULL != (dlerrstr = (char *)calloc(1, err_str_len)), AEE_ENOMEMORY);
  PROFILE_ALWAYS(
      &t_close,
      if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
        nErr = remotectl1_close1(handle, h, dlerrstr, err_str_len, &dlerr);
        if (nErr) {
          FARF(ALWAYS,
               "Warning 0x%x: %s: remotectl1 domains not supported for domain "
               "%d\n",
               nErr, __func__, domain);
          fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                     _const_remotectl1_handle, NULL, NULL);

          // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls
          // are non-domain calls
          hlist_lookup(domain)->remotectlhandle = INVALID_HANDLE;
          nErr = remotectl_close(h, dlerrstr, err_str_len, &dlerr);
        } else if (nErr)
          goto bail;
      } else {
        VERIFY(AEE_SUCCESS ==
               (nErr = remotectl_close(h, dlerrstr, err_str_len, &dlerr)));
      } VERIFY(AEE_SUCCESS == (nErr = dlerr)););
bail:
  if (nErr != AEE_SUCCESS) {
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR,
           "Error 0x%x: %s failed for handle 0x%x, domain %d (dlerr %s) (errno "
           "%s)\n",
           nErr, __func__, h, domain, dlerrstr, strerror(errno));
    }
  } else {
    FARF(RUNTIME_RPC_HIGH, "%s: closed module with handle 0x%x (skel unload time %" PRIu64 " us)",
         __func__, h, t_close);
  }
  if (dlerrstr) {
    free(dlerrstr);
    dlerrstr = NULL;
  }
  if (domain != -1)
    print_open_handles(domain);
  FASTRPC_ATRACE_END();
  return nErr;
}

int remote_handle_close(remote_handle h) {
  int nErr = AEE_SUCCESS, domain = get_current_domain(), ref = 1;

  FARF(RUNTIME_RPC_HIGH, "Entering %s, handle %lu\n", __func__, h);

  VERIFY(AEE_SUCCESS == (nErr = remote_handle_close_domain(domain, h)));
  FASTRPC_PUT_REF(domain);
  fastrpc_update_module_list(NON_DOMAIN_LIST_DEQUEUE, domain, h, NULL, NULL);
bail:
  if (nErr != AEE_SUCCESS) {
    if (is_process_exiting(domain)) {
      return 0;
    }
    /*
     * handle_info or so name cannot be obtained from remote handles which
     * are used for non-domain calls.
     */
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for handle 0x%x\n", nErr, __func__, h);
    }
  }
  return nErr;
}

int remote_handle64_close(remote_handle64 handle) {
  remote_handle64 remote = 0;
  int domain = -1, nErr = AEE_SUCCESS, ref = 1;
  bool start_deinit = false;
  struct handle_info *hi = (struct handle_info*)handle;

  if (IS_STATICPD_HANDLE(handle))
     return AEE_SUCCESS;
  FARF(RUNTIME_RPC_HIGH, "Entering %s, handle %llu\n", __func__, handle);
  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%" PRIx64 "\n", __func__,
                         handle);
  VERIFYC(handle != (remote_handle64)-1, AEE_EINVHANDLE);
  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(handle, &domain)));
  VERIFY(AEE_SUCCESS == (nErr = get_handle_remote(handle, &remote)));
  set_thread_context(domain);
  /*
   * Terminate remote session if
   *     1. there are no open non-domain handles AND
   *     2. there are no open multi-domain handles, OR
   *        only 1 multi-domain handle is open (for perf reason,
   *        skip closing of it)
   */
  if (hlist_lookup(domain)->domainsCount <= 1 && !hlist_lookup(domain)->nondomainsCount)
    start_deinit = true;
  /*
   * If session termination is not initiated and the remote handle is valid,
   * then close the remote handle on DSP.
   */
  if (!start_deinit && remote) {
    VERIFY(AEE_SUCCESS ==
           (nErr = remote_handle_close_domain(domain, (remote_handle)remote)));
  }
  FARF(ALWAYS, "%s: closed module %s with handle 0x%" PRIx64 " remote handle 0x%" PRIx64
		", num of open handles: %u",
         __func__, hi->name, handle, remote, hlist_lookup(domain)->domainsCount - 1);
bail:
  if (nErr != AEE_EINVHANDLE && IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
    /*
     * Always dequeue the local handle and drop the domain ref here, even if
     * remote_handle_close_domain() above failed (e.g. AEE_ENOSUCH from a dead
     * PD). Otherwise domainsCount gets stuck too high and start_deinit can
     * never become true on later closes.
     */
    fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain, handle, NULL, NULL);
    FASTRPC_PUT_REF(domain);
    if (start_deinit) {
        hlist_lookup(domain)->disable_exit_logs = 1;
        domain_deinit(domain);
    }
    if (nErr != AEE_SUCCESS) {
      if (is_process_exiting(domain))
        return 0;
      if (0 == check_rpc_error(nErr)) 
        FARF(ERROR,
           "Error 0x%x: %s close module %s failed for handle 0x%" PRIx64
           " remote handle 0x%" PRIx64 " (errno %s), num of open handles: %u\n",
           nErr, __func__, hi->name, handle, remote, strerror(errno),
           hlist_lookup(domain)->domainsCount);
    }
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

static int manage_adaptive_qos(int domain, uint32_t enable) {
  int nErr = AEE_SUCCESS;
  remote_handle64 handle = INVALID_HANDLE;

  /* If adaptive QoS is already enabled/disabled, then just return */
  if ((enable && hlist_lookup(domain)->adaptive_qos) ||
      (!enable && !hlist_lookup(domain)->adaptive_qos))
    return nErr;

  if (hlist_lookup(domain)->dev != -1) {
    /* If session is already open on DSP, then make rpc call directly to user PD
     */
    if ((handle = get_remotectl1_handle(domain)) != INVALID_HANDLE) {
      nErr = remotectl1_set_param(handle, RPC_ADAPTIVE_QOS, &enable, 1);
      if (nErr) {
        FARF(ALWAYS,
             "Warning 0x%x: %s: remotectl1 domains not supported for domain "
             "%d\n",
             nErr, __func__, domain);
        fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain,
                                   _const_remotectl1_handle, NULL, NULL);

        // Set remotectlhandle to INVALID_HANDLE, so that all subsequent calls
        // are non-domain calls
        hlist_lookup(domain)->remotectlhandle = INVALID_HANDLE;
        nErr = remotectl_set_param(RPC_ADAPTIVE_QOS, &enable, 1);
      }
    } else {
      nErr = remotectl_set_param(RPC_ADAPTIVE_QOS, &enable, 1);
    }
    if (nErr) {
      FARF(ERROR,
           "Error: %s: remotectl_set_param failed to reset adaptive QoS on DSP "
           "to %d on domain %d",
           __func__, enable, domain);
      goto bail;
    } else {
      hlist_lookup(domain)->adaptive_qos = ((enable == RPC_ADAPTIVE_QOS) ? 1 : 0);
    }
  } else {
    /* If session is not created already, then just set process attribute */
    hlist_lookup(domain)->adaptive_qos = ((enable == RPC_ADAPTIVE_QOS) ? 1 : 0);
  }

  if (enable)
    FARF(ALWAYS, "%s: Successfully enabled adaptive QoS on domain %d", __func__,
         domain);
  else
    FARF(ALWAYS, "%s: Disabled adaptive QoS on domain %d", __func__, domain);
bail:
  return nErr;
}

static int manage_poll_qos(int domain, remote_handle64 h, uint32_t enable) {
  int nErr = AEE_SUCCESS, dev = -1;
  const unsigned int MAX_POLL_TIMEOUT = 10000;
  struct fastrpc_ioctl_set_option op = {0};
  struct handle_list *hl = hlist_lookup(domain);

  /* Handle will be -1 in non-domains invocation. Create DSP session if
   * necessary  */
  if (h == INVALID_HANDLE) {
    if (!hl || hl->dev == -1) {
      VERIFY(AEE_SUCCESS == (nErr = domain_init(domain, &dev)));
      VERIFYM(-1 != dev, AEE_ERPC, "open dev failed\n");
      hl = hlist_lookup(domain);
    }
  }
  /* If the multi-domain handle is valid, then verify that session is created
   * already */
  VERIFYC((NULL != hl) && (-1 != (dev = hl->dev)), AEE_ERPC);

  /* Update polling mode in kernel */
  op.req = FASTRPC_POLL_MODE;
  op.value = enable;
  nErr = ioctl_control(dev, FASTRPC_POLL_MODE, &op);
  if (nErr) {
    nErr = convert_kernel_to_user_error(nErr, errno);
    goto bail;
  }

  /* Update polling mode in DSP */
  if (h == INVALID_HANDLE) {
    VERIFY(AEE_SUCCESS ==
           (nErr = adsp_current_process_poll_mode(enable, MAX_POLL_TIMEOUT)));
  } else {
    remote_handle64 handle = get_adsp_current_process1_handle(domain);
    VERIFY(AEE_SUCCESS ==
           (nErr = adsp_current_process1_poll_mode(handle, enable, MAX_POLL_TIMEOUT)));
  }
  FARF(ALWAYS,
       "%s: poll mode updated to %u for domain %d, handle 0x%" PRIx64 "\n",
       __func__, enable, domain, h);
bail:
  if (nErr) {
    FARF(ERROR,
         "Error 0x%x (errno %d): %s failed for domain %d, handle 0x%" PRIx64
         ", enable %u (%s)\n",
         nErr, errno, __func__, domain, h, enable, strerror(errno));
  }
  return nErr;
}

static inline int enable_process_state_notif_on_dsp(int domain) {
  int nErr = AEE_SUCCESS;
  remote_handle64 notif_handle = 0;

  fastrpc_notif_domain_init(domain);
  if ((notif_handle = get_adsp_current_process1_handle(domain)) !=
      INVALID_HANDLE) {
    VERIFY(AEE_SUCCESS ==
           (nErr = adsp_current_process1_enable_notifications(notif_handle)));
  } else {
    VERIFY(AEE_SUCCESS == (nErr = adsp_current_process_enable_notifications()));
  }
bail:
  return nErr;
}

/* fastrpc_set_qos_latency:
        Send user QoS latency requirement to DSP.
        DSP will use required features to meet the requirement.
        FastRPC driver tries to meet latency requirement but may not be
   guaranteed.
*/
static int fastrpc_set_qos_latency(int domain, remote_handle64 h,
                                   uint32_t latency) {
  int nErr = 0;

  if (h == (remote_handle64)-1) {
    nErr = adsp_current_process_setQoS(latency);
  } else {
    remote_handle64 handle = get_adsp_current_process1_handle(domain);
    nErr = adsp_current_process1_setQoS(handle, latency);
  }
  return nErr;
}

// Notify kernel to enable/disable wakelock control between calls to DSP
static int update_kernel_wakelock_status(int domain, int dev,
                                         uint32_t wl_enable) {
  int nErr = AEE_SUCCESS;
  struct fastrpc_ctrl_wakelock wl = {0};

  wl.enable = wl_enable;
  nErr = ioctl_control(dev, DSPRPC_CONTROL_WAKELOCK, &wl);
  if (nErr) {
    if (errno == EBADRQC || errno == ENOTTY || errno == ENXIO ||
        errno == EINVAL || nErr == AEE_EUNSUPPORTED) {
      VERIFY_WPRINTF(
          "Warning: %s: kernel does not support wakelock management (%s)",
          __func__, strerror(errno));
      hlist_lookup(domain)->wake_lock_enable = 0;
      fastrpc_wake_lock_deinit();
      return AEE_SUCCESS;
    }
    FARF(ERROR,
         "Error 0x%x: %s failed for domain %d, dev %d, wakelock control %d "
         "errno %s",
         nErr, __func__, domain, dev, wl_enable, strerror(errno));
  } else
    FARF(ALWAYS,
         "%s: updated kernel wakelock control status to %d for domain %d",
         __func__, wl_enable, domain);
  return nErr;
}

// Update wakelock status in userspace and notify kernel if necessary
static int wakelock_control(int domain, remote_handle64 h, uint32_t wl_enable) {
  int nErr = AEE_SUCCESS;

  if (hlist_lookup(domain)->wake_lock_enable == wl_enable)
    goto bail;

  if (wl_enable) {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_wake_lock_init()));
  } else {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_wake_lock_deinit()));
  }
  if (IS_SESSION_OPEN_ALREADY(domain))
    VERIFY(AEE_SUCCESS == (nErr = update_kernel_wakelock_status(
                               domain, hlist_lookup(domain)->dev, wl_enable)));
  hlist_lookup(domain)->wake_lock_enable = wl_enable;
bail:
  if (nErr)
    FARF(ERROR, "Error 0x%x: %s failed for domain %d, handle 0x%x, enable %d",
         nErr, __func__, domain, h, wl_enable);
  else {
    if (wl_enable)
      FARF(ALWAYS, "%s: enabled wakelock control for domain %d", __func__,
           domain);
    else
      FARF(ALWAYS, "%s: disable wakelock control for domain %d", __func__,
           domain);
  }
  return nErr;
}

// Make IOCTL call to kill remote process
static int fastrpc_dsp_process_clean(int domain) {
  int nErr = AEE_SUCCESS, dev;

  VERIFYM(IS_SESSION_OPEN_ALREADY(domain), AEE_ERPC,
          "Session not open for domain %d", domain);
  dev = hlist_lookup(domain)->dev;
  nErr = ioctl_control(dev, DSPRPC_REMOTE_PROCESS_KILL, NULL);
bail:
  if (nErr)
    FARF(ERROR, "Error 0x%x: %s failed for domain %d (errno: %s) ", nErr,
         __func__, domain, strerror(errno));
  return nErr;
}

int remote_handle_control_domain(int domain, remote_handle64 h, uint32_t req,
                                 void *data, uint32_t len) {
  int nErr = AEE_SUCCESS;

  FARF(RUNTIME_RPC_HIGH, "Entering %s, domain %d, handle %llu, req %d, data %p, size %d\n",
       __func__, domain, h, req, data, len);

  switch (req) {
  case DSPRPC_CONTROL_LATENCY: {
    struct remote_rpc_control_latency *lp =
        (struct remote_rpc_control_latency *)data;
    VERIFYC(lp, AEE_EBADPARM);
    VERIFYC(len == sizeof(struct remote_rpc_control_latency), AEE_EBADPARM);

    switch (lp->enable) {
    /* Only one of PM QoS or adaptive QoS can be enabled */
    case RPC_DISABLE_QOS: {
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_adaptive_qos(domain, RPC_DISABLE_QOS)));
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_poll_qos(domain, h, 0)));
      /* Error ignored, currently meeting qos requirement is optional. Consider
       * to error out in later targets */
      fastrpc_set_qos_latency(domain, h, FASTRPC_QOS_MAX_LATENCY_USEC);
      break;
    }
    case RPC_PM_QOS: {
      FARF(ALWAYS, "Warning: PM QoS is not supported; ignoring request and returning "
           "success for backward compatibility");
      break;
    }
    case RPC_ADAPTIVE_QOS: {
      /* Enable adaptive QoS */
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_adaptive_qos(domain, RPC_ADAPTIVE_QOS)));
      /* Error ignored, currently meeting qos requirement is optional. Consider
       * to error out in later targets */
      fastrpc_set_qos_latency(domain, h, lp->latency);
      break;
    }
    case RPC_POLL_QOS: {
      VERIFY(AEE_SUCCESS ==
             (nErr = manage_poll_qos(domain, h, 1)));
      break;
    }
    default:
      nErr = AEE_EBADPARM;
      FARF(ERROR, "Error: %s: Bad enable parameter %d passed for QoS control",
           __func__, lp->enable);
      goto bail;
    }
    FARF(ALWAYS,
         "%s: requested QOS %d, latency %u for domain %d handle 0x%" PRIx64
         "\n",
         __func__, lp->enable, lp->latency, domain, h);
    break;
  }
  case DSPRPC_GET_DSP_INFO: {
    int dev = -1;
    struct remote_dsp_capability *cap = (struct remote_dsp_capability *)data;

    (void)dev;
    VERIFYC(cap, AEE_EBADPARM);
    cap->domain = normalize_effec_domain_id(cap->domain);
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(cap->domain), AEE_EBADPARM);

    nErr = fastrpc_get_cap(cap->domain, cap->attribute_ID, &cap->capability);
    VERIFY(AEE_SUCCESS == nErr);
    break;
  }
  case DSPRPC_CONTROL_WAKELOCK: {
    struct remote_rpc_control_wakelock *wp =
        (struct remote_rpc_control_wakelock *)data;

    VERIFYC(wp, AEE_EBADPARM);
    VERIFYC(len == sizeof(struct remote_rpc_control_wakelock), AEE_EBADPARM);
    VERIFY(AEE_SUCCESS == (nErr = wakelock_control(domain, h, wp->enable)));
    break;
  }
  case DSPRPC_GET_DOMAIN: {
    remote_rpc_get_domain_t *pGet = (remote_rpc_get_domain_t *)data;

    VERIFYC(pGet, AEE_EBADPARM);
    VERIFYC(len == sizeof(remote_rpc_get_domain_t), AEE_EBADPARM);
    pGet->domain = domain;
    break;
  }
  default:
    nErr = AEE_EUNSUPPORTED;
    FARF(RUNTIME_RPC_LOW,
         "Error: %s: remote handle control called with unsupported request ID "
         "%d",
         __func__, req);
    break;
  }
bail:
  if (nErr != AEE_SUCCESS)
    FARF(ERROR,
         "Error 0x%x: %s failed for request ID %d on domain %d (errno %s)",
         nErr, __func__, req, domain, strerror(errno));
  return nErr;
}

int remote_handle_control(uint32_t req, void *data, uint32_t len) {
  int domain = -1, nErr = AEE_SUCCESS, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_HIGH, "Entering %s, req %d, data %p, size %d\n", __func__, req,
       data, len);

  domain = get_current_domain();
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS == (nErr = remote_handle_control_domain(
                             domain, INVALID_HANDLE, req, data, len)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
         __func__, req, strerror(errno));
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
           __func__, req, strerror(errno));
    }
  }
  return nErr;
}

int remote_handle64_control(remote_handle64 handle, uint32_t req, void *data,
                            uint32_t len) {
  int nErr = AEE_SUCCESS, domain = -1, ref = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_HIGH, "Entering %s, handle %llu, req %d, data %p, size %d\n",
       __func__, handle, req, data, len);

  VERIFY(AEE_SUCCESS == (nErr = get_domain_from_handle(handle, &domain)));
  FASTRPC_GET_REF(domain);
  VERIFY(AEE_SUCCESS ==
         (nErr = remote_handle_control_domain(domain, handle, req, data, len)));
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
         __func__, req, strerror(errno));
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
           __func__, req, strerror(errno));
    }
  }
  return nErr;
}

static int close_domain_session(int domain) {
  QNode *pn = NULL;
  char dlerrstr[255];
  int dlerr = 0, nErr = AEE_SUCCESS, mut_locked = 0;
  remote_handle64 proc_handle = 0;

  FARF(ALWAYS,
       "%s: user requested to close fastrpc session on domain %d, dev %d\n",
       __func__, domain, hlist_lookup(domain)->dev);
  VERIFY(hlist_lookup(domain)->dev != -1);
  proc_handle = get_adsp_current_process1_handle(domain);
  if (proc_handle != INVALID_HANDLE) {
    adsp_current_process1_exit(proc_handle);
  } else {
    adsp_current_process_exit();
  }
  pthread_mutex_lock(&hlist_lookup(domain)->lmut);
  mut_locked = 1;

  while (!QList_IsEmpty(&hlist_lookup(domain)->nql)) {
    struct handle_info *hi;
    pn = QList_Pop(&hlist_lookup(domain)->nql);
    hi = STD_RECOVER_REC(struct handle_info, qn, pn);
    VERIFYC(NULL != hi, AEE_EINVHANDLE);
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    mut_locked = 0;
    remote_handle_close(hi->remote);
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    mut_locked = 1;
  }

  while (!QList_IsEmpty(&hlist_lookup(domain)->rql)) {
    struct handle_info *hi;
    pn = QList_Pop(&hlist_lookup(domain)->rql);
    hi = STD_RECOVER_REC(struct handle_info, qn, pn);
    VERIFYC(NULL != hi, AEE_EINVHANDLE);
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    mut_locked = 0;
    close_reverse_handle(hi->local, dlerrstr, sizeof(dlerrstr), &dlerr);
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    mut_locked = 1;
  }

  while (!QList_IsEmpty(&hlist_lookup(domain)->ql)) {
    struct handle_info *hi;
    pn = QList_Pop(&hlist_lookup(domain)->ql);
    hi = STD_RECOVER_REC(struct handle_info, qn, pn);
    VERIFYC(NULL != hi, AEE_EINVHANDLE);
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    mut_locked = 0;
    remote_handle64_close(hi->local);
    pthread_mutex_lock(&hlist_lookup(domain)->lmut);
    mut_locked = 1;
  }
  pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
  mut_locked = 0;
bail:
  if (mut_locked) {
    pthread_mutex_unlock(&hlist_lookup(domain)->lmut);
    mut_locked = 0;
  }
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d (errno %s)", nErr,
         __func__, domain, strerror(errno));
  }
  return nErr;
}

int get_unsigned_pd_attribute(uint32_t domain, int *unsigned_module) {
  int nErr = AEE_SUCCESS;

  VERIFYC(hlist_table_ready, AEE_EBADPARM);
  VERIFYC(unsigned_module, AEE_EBADPARM);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  *unsigned_module = hlist_lookup(domain)->unsigned_module;
bail:
  return nErr;
}

static int set_unsigned_pd_attribute(int domain, int enable) {
  int nErr = AEE_SUCCESS;

  VERIFYC(hlist_table_ready, AEE_EBADPARM);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  if (hlist_lookup(domain)->dev != -1) {
    if (hlist_lookup(domain)->unsigned_module == enable) {
      FARF(HIGH, "%s: %s session already open on domain %d , enable %d ",
           __func__, hlist_lookup(domain)->unsigned_module ? "Unsigned" : "Signed",
           domain, enable);
    } else {
      nErr = AEE_EALREADYLOADED;
      FARF(ERROR,
           "Error 0x%x: %s: %s session already open on domain %d , enable %d ",
           nErr, __func__,
           hlist_lookup(domain)->unsigned_module ? "Unsigned" : "Signed", domain,
           enable);
    }
    goto bail;
  }
  hlist_lookup(domain)->unsigned_module = enable ? 1 : 0;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d", nErr, __func__, domain);
  }
  return nErr;
}

static int store_domain_thread_params(int domain, int thread_priority,
                                      int stack_size) {
  int nErr = AEE_SUCCESS;

  VERIFYC(hlist_table_ready, AEE_EBADPARM);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  if (thread_priority != -1) {
    if ((thread_priority < MIN_THREAD_PRIORITY) ||
        (thread_priority > MAX_THREAD_PRIORITY)) {
      nErr = AEE_EBADPARM;
      FARF(ERROR,
           "%s: Thread priority %d is invalid! Should be between %d and %d",
           __func__, thread_priority, MIN_THREAD_PRIORITY, MAX_THREAD_PRIORITY);
      goto bail;
    } else {
      hlist_lookup(domain)->th_params.thread_priority = (uint32_t)thread_priority;
    }
  }
  if (stack_size != -1) {
    if ((stack_size < MIN_UTHREAD_STACK_SIZE) ||
        (stack_size > MAX_UTHREAD_STACK_SIZE)) {
      nErr = AEE_EBADPARM;
      FARF(ERROR, "%s: Stack size %d is invalid! Should be between %d and %d",
           __func__, stack_size, MIN_UTHREAD_STACK_SIZE,
           MAX_UTHREAD_STACK_SIZE);
      goto bail;
    } else
      hlist_lookup(domain)->th_params.stack_size = (uint32_t)stack_size;
  }
  hlist_lookup(domain)->th_params.reqID = FASTRPC_THREAD_PARAMS;
  hlist_lookup(domain)->th_params.update_requested = 1;
  if (hlist_lookup(domain)->dev != -1) {
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_set_remote_uthread_params(domain)));
    FARF(ALWAYS,
         "Dynamically set remote user thread priority to %d and stack size to "
         "%d for domain %d",
         thread_priority, stack_size, domain);
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for domain %d for thread priority %d, stack "
         "size %d",
         nErr, __func__, domain, thread_priority, stack_size);
  }
  return nErr;
}

/* Set attribute to enable/disable pd dump */
static int set_pd_dump_attribute(int domain, int enable) {
  int nErr = AEE_SUCCESS;

  VERIFYC(hlist_table_ready, AEE_ERPC);
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
  if (hlist_lookup(domain)->dev != -1) {
    nErr = AEE_ERPC;
    FARF(ERROR,
         "%s: Session already open on domain %d ! Request unsigned offload "
         "before making any RPC calls",
         __func__, domain);
    goto bail;
  }
  hlist_lookup(domain)->pd_dump = enable ? true : false;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed to enable %d for domain %d", nErr,
         __func__, enable, domain);
  }
  return nErr;
}

/* Set userpd memlen for a requested domain */
static int store_domain_pd_initmem_size(int domain, uint32_t pd_initmem_size) {
  int nErr = AEE_SUCCESS;

  if ((pd_initmem_size < MIN_PD_INITMEM_SIZE) ||
      (pd_initmem_size > MAX_PD_INITMEM_SIZE)) {
    nErr = AEE_EBADPARM;
    goto bail;
  } else {
    hlist_lookup(domain)->pd_initmem_size = pd_initmem_size;
  }
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: %s failed for domain %d for pd init mem size :0x%x", nErr,
         __func__, domain, pd_initmem_size);
  }
  return nErr;
}

/*
 * A caller's domain_name buffer is only guaranteed to hold domain_name_len
 * bytes (per the struct docs: "length, without NULL character"), not a NUL
 * terminator -- get_domain_from_name() takes no length and calls strlen()
 * internally, so passing the raw pointer directly is an out-of-bounds read
 * for a buffer sized to exactly domain_name_len. Copy at most that many
 * bytes (clamped to the local buffer) and NUL-terminate before use.
 */
static void safe_domain_name_copy(char *dst, size_t dst_size,
                                   const char *src, uint32_t src_len) {
  size_t n = src_len < (dst_size - 1) ? src_len : (dst_size - 1);
  memcpy(dst, src, n);
  dst[n] = '\0';
}

/* Set remote session parameters like thread stack size, running on unsigned PD,
 * killing remote process PD etc */
int remote_session_control(uint32_t req, void *data, uint32_t datalen) {
  int nErr = AEE_SUCCESS, domain = DEFAULT_DOMAIN_ID, ref = 0, ii = 0;

  VERIFY(AEE_SUCCESS == (nErr = fastrpc_init_once()));

  FARF(RUNTIME_RPC_HIGH, "Entering %s, req %d, data %p, size %d\n", __func__, req, data,
       datalen);
  FASTRPC_GET_REF(domain);
  switch (req) {
  case FASTRPC_THREAD_PARAMS: {
    struct remote_rpc_thread_params *params =
        (struct remote_rpc_thread_params *)data;
    if (!params) {
      nErr = AEE_EBADPARM;
      FARF(RUNTIME_RPC_LOW, "%s: Thread params struct passed is %p", __func__,
           params);
      goto bail;
    }
    VERIFYC(datalen == sizeof(struct remote_rpc_thread_params), AEE_EBADPARM);
    if (params->domain != -1) {
      params->domain = normalize_effec_domain_id(params->domain);
      if (!IS_VALID_EFFECTIVE_DOMAIN_ID(params->domain)) {
        nErr = AEE_EBADPARM;
        FARF(RUNTIME_RPC_LOW, "%s: Invalid domain ID %d passed", __func__,
             params->domain);
        goto bail;
      }
      VERIFY(AEE_SUCCESS ==
             (nErr = store_domain_thread_params(params->domain, params->prio,
                                                params->stack_size)));
    } else {
      /* If domain is -1, then set parameters for all domains */
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        VERIFY(AEE_SUCCESS == (nErr = store_domain_thread_params(
                                   ii, params->prio, params->stack_size)));
      }
    }
    FARF(ALWAYS,
         "%s DSP info request for domain %d, thread priority %d, stack size %d",
         __func__, params->domain, params->prio, params->stack_size);
    break;
  }
  case DSPRPC_CONTROL_UNSIGNED_MODULE: {
    // Handle the unsigned module offload request
    struct remote_rpc_control_unsigned_module *um =
        (struct remote_rpc_control_unsigned_module *)data;
    VERIFYC(datalen == sizeof(struct remote_rpc_control_unsigned_module),
            AEE_EBADPARM);
    VERIFYC(um != NULL, AEE_EBADPARM);
    if (um->domain != -1) {
      um->domain = normalize_effec_domain_id(um->domain);
      VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(um->domain),
              AEE_EBADPARM);
      VERIFY(AEE_SUCCESS ==
             (nErr = set_unsigned_pd_attribute(um->domain, um->enable)));
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        VERIFY(AEE_SUCCESS ==
               (nErr = set_unsigned_pd_attribute(ii, um->enable)));
      }
    }
    FARF(ALWAYS, "%s Unsigned PD enable %d request for domain %d", __func__,
         um->enable, um->domain);
    break;
  }
  case FASTRPC_RELATIVE_THREAD_PRIORITY: {
    int thread_priority = DEFAULT_UTHREAD_PRIORITY, rel_thread_prio = 0;
    struct remote_rpc_relative_thread_priority *params =
        (struct remote_rpc_relative_thread_priority *)data;

    VERIFYC(datalen == sizeof(struct remote_rpc_relative_thread_priority),
            AEE_EBADPARM);
    VERIFYC(params, AEE_EBADPARM);
    rel_thread_prio = params->relative_thread_priority;

    // handle thread priority overflow and out-of-range conditions
    if (rel_thread_prio < 0) {
      thread_priority = ((~rel_thread_prio) >= DEFAULT_UTHREAD_PRIORITY - 1)
                            ? MIN_THREAD_PRIORITY
                            : (thread_priority + rel_thread_prio);
    } else {
      thread_priority =
          (rel_thread_prio > (MAX_THREAD_PRIORITY - DEFAULT_UTHREAD_PRIORITY))
              ? MAX_THREAD_PRIORITY
              : (thread_priority + rel_thread_prio);
    }
    if (params->domain != -1) {
      params->domain = normalize_effec_domain_id(params->domain);
      VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(params->domain),
              AEE_EBADPARM);
      VERIFY(AEE_SUCCESS == (nErr = store_domain_thread_params(
                                 params->domain, thread_priority, -1)));
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        VERIFY(AEE_SUCCESS ==
               (nErr = store_domain_thread_params(ii, thread_priority, -1)));
      }
    }
    FARF(ALWAYS, "%s DSP thread priority request for domain %d, priority %d",
         __func__, req, params->domain, thread_priority);
    break;
  }
  case FASTRPC_REMOTE_PROCESS_KILL: {
    struct remote_rpc_process_clean_params *dp =
        (struct remote_rpc_process_clean_params *)data;

    VERIFYC(datalen == sizeof(struct remote_rpc_process_clean_params),
            AEE_EBADPARM);
    VERIFYC(dp, AEE_EBADPARM);
    domain = normalize_effec_domain_id(dp->domain);
    dp->domain = domain;
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_dsp_process_clean(domain)));
    FARF(ALWAYS, "%s Remote process kill request for domain %d", __func__,
         domain);
    break;
  }
  case FASTRPC_SESSION_CLOSE: {
    struct remote_rpc_session_close *sclose =
        (struct remote_rpc_session_close *)data;
    if (!sclose) {
      nErr = AEE_EBADPARM;
      FARF(RUNTIME_RPC_LOW,
           "Error: %s: session close data pointer passed is NULL\n", __func__);
      goto bail;
    }
    VERIFYC(datalen == sizeof(struct remote_rpc_session_close), AEE_EBADPARM);
    if (sclose->domain != -1) {
      sclose->domain = normalize_effec_domain_id(sclose->domain);
      VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(sclose->domain),
              AEE_EBADPARM);
      VERIFY(AEE_SUCCESS == (nErr = close_domain_session(sclose->domain)));
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        VERIFY(AEE_SUCCESS == (nErr = close_domain_session(ii)));
      }
    }
    FARF(ALWAYS, "%s Fastrpc session close request for domain %d\n", __func__,
         sclose->domain);
    break;
  }
  case FASTRPC_CONTROL_PD_DUMP: {
    // Handle pd dump enable/disable request
    struct remote_rpc_control_pd_dump *pddump =
        (struct remote_rpc_control_pd_dump *)data;
    VERIFYC(datalen == sizeof(struct remote_rpc_control_pd_dump), AEE_EBADPARM);
    VERIFYC(pddump != NULL, AEE_EBADPARM);
    if (pddump->domain != -1) {
      pddump->domain = normalize_effec_domain_id(pddump->domain);
      VERIFY(AEE_SUCCESS ==
             (nErr = set_pd_dump_attribute(pddump->domain, pddump->enable)));
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        VERIFY(AEE_SUCCESS ==
               (nErr = set_pd_dump_attribute(ii, pddump->enable)));
      }
    }
    FARF(ALWAYS, "%s PD dump request to enable(%d) for domain %d", __func__,
         pddump->enable, pddump->domain);
    break;
  }
  case FASTRPC_REMOTE_PROCESS_EXCEPTION: {
    // Trigger non fatal exception in the User PD of DSP.
    remote_rpc_process_exception *dp = (remote_rpc_process_exception *)data;
    remote_handle64 handle = 0;
    int ret = 0;

    VERIFYC(datalen == sizeof(remote_rpc_process_exception), AEE_EBADPARM);
    VERIFYC(dp, AEE_EBADPARM);
    domain = normalize_effec_domain_id(dp->domain);
    dp->domain = domain;
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
    /*
     * If any request causes exception on DSP, DSP returns AEE_EBADSTATE for the
     * request thread.
     */
    if ((handle = get_adsp_current_process1_handle(domain)) != INVALID_HANDLE) {
      ret = adsp_current_process1_exception(handle);
    } else {
      ret = adsp_current_process_exception();
    }
    ret = (ret == AEE_SUCCESS) ? AEE_ERPC : ret;
    VERIFYC(ret == (int)(DSP_AEE_EOFFSET + AEE_EBADSTATE), ret);
    FARF(ALWAYS,
         "%s Remote process exception request for domain %d, handle 0x%" PRIx64
         "\n",
         __func__, domain, handle);
    break;
  }
  case FASTRPC_REMOTE_PROCESS_TYPE: {
    struct remote_process_type *typ = (struct remote_process_type *)data;
    int ret_val = -1;
    VERIFYC(datalen == sizeof(struct remote_process_type), AEE_EBADPARM);
    VERIFYC(typ, AEE_EBADPARM);
    domain = normalize_effec_domain_id(typ->domain);
    typ->domain = domain;
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
    ret_val = hlist_lookup(domain)->unsigned_module;
    if (ret_val != PROCESS_TYPE_UNSIGNED && ret_val != PROCESS_TYPE_SIGNED) {
      typ->process_type = -1;
      nErr = AEE_EBADPARM;
    } else {
      typ->process_type = ret_val;
    }
    break;
  }
  case FASTRPC_REGISTER_STATUS_NOTIFICATIONS: {
    // Handle DSP PD notification request
    struct remote_rpc_notif_register *notif =
        (struct remote_rpc_notif_register *)data;
    VERIFYC(datalen == sizeof(struct remote_rpc_notif_register), AEE_EBADPARM);
    VERIFYC(notif != NULL, AEE_EBADPARM);
    domain = normalize_effec_domain_id(notif->domain);
    notif->domain = domain;
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADPARM);
    if (domain != -1) {
      VERIFYC(is_status_notif_version2_supported(domain), AEE_EUNSUPPORTED);
      VERIFY(AEE_SUCCESS == (nErr = fastrpc_notif_register(domain, notif)));
    } else {
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        VERIFYC(is_status_notif_version2_supported(ii), AEE_EUNSUPPORTED);
        VERIFY(AEE_SUCCESS == (nErr = fastrpc_notif_register(ii, notif)));
      }
    }
    FARF(RUNTIME_RPC_HIGH, "%s Register PD status notification request for domain %d\n",
         __func__, domain);
    break;
  }
  case FASTRPC_PD_INITMEM_SIZE: {
    // Handle DSP  User PD init memory size request
    struct remote_rpc_pd_initmem_size *params =
        (struct remote_rpc_pd_initmem_size *)data;
    VERIFYC(datalen == sizeof(struct remote_rpc_pd_initmem_size), AEE_EBADPARM);
    VERIFYC(params != NULL, AEE_EBADPARM);
    if (params->domain != -1) {
      params->domain = normalize_effec_domain_id(params->domain);
      if (!IS_VALID_EFFECTIVE_DOMAIN_ID(params->domain)) {
        nErr = AEE_EBADPARM;
        FARF(ERROR, "%s: Invalid domain ID %d passed", __func__,
             params->domain);
        goto bail;
      }
      if (hlist_lookup(params->domain)->unsigned_module) {
        nErr = AEE_EUNSUPPORTED;
        FARF(ERROR, "Configuring User PD init mem length is not supported for "
                    "unsigned PDs");
        goto bail;
      }
      VERIFY(AEE_SUCCESS == (nErr = store_domain_pd_initmem_size(
                                 params->domain, params->pd_initmem_size)));
    } else {
      /* If domain is -1, then set parameters for all domains */
      FOR_EACH_EFFECTIVE_DOMAIN_ID(ii) {
        if (hlist_lookup(ii)->unsigned_module) {
          FARF(ALWAYS,
               "Warning: %s: Configuring User PD init mem length for domain %d "
               "is not supported for unsigned PDs",
               __func__, ii);
        } else {
          VERIFY(AEE_SUCCESS == (nErr = store_domain_pd_initmem_size(
                                     ii, params->pd_initmem_size)));
        }
      }
    }
    FARF(ALWAYS,
         "%s DSP userpd memlen request for domain %d, userpd memlen 0x%x",
         __func__, params->domain, params->pd_initmem_size);
    break;
  }
  case FASTRPC_RESERVE_NEW_SESSION: {
    remote_rpc_reserve_new_session_t *sess =
        (remote_rpc_reserve_new_session_t *)data;
    int jj = 0;
    struct handle_list *hl = NULL;

    VERIFYC(datalen == sizeof(remote_rpc_reserve_new_session_t), AEE_EBADPARM);
    char domain_name_buf[32];

    VERIFYC(sess && sess->domain_name != NULL && sess->domain_name_len > 0 &&
                sess->session_name && sess->session_name_len > 0,
            AEE_EBADPARM);
    safe_domain_name_copy(domain_name_buf, sizeof(domain_name_buf),
                           sess->domain_name, sess->domain_name_len);
    domain = get_domain_from_name(domain_name_buf, DOMAIN_NAME_STAND_ALONE);
    VERIFYC(IS_VALID_DOMAIN_ID(domain), AEE_EBADPARM);
    // Set effective_domain_id and session_id to invalid IDs
    sess->effective_domain_id = (uint32_t)-1;
    sess->session_id = (uint32_t)-1;
    /* Session 0 is the default session and can't be reserved. Take the
     * lowest unreserved session from 1 upwards, creating its entry if it
     * doesn't exist yet. Running out of sessions is reported by the kernel
     * when the session is opened; MAX_SESSIONS_LIMIT only stops a runaway
     * loop. */
    for (jj = 1; jj < MAX_SESSIONS_LIMIT; jj++) {
      hl = hlist_get_or_create_session(domain, jj);
      if (!hl)
        break;
      pthread_mutex_lock(&hl->init);
      if (!hl->is_session_reserved) {
        hl->is_session_reserved = true;
        sess->effective_domain_id = (uint32_t)hl->domain;
        sess->session_id = (uint32_t)jj;
        strlcpy(hl->sessionname, sess->session_name,
                    STD_MIN(sess->session_name_len, (MAX_DSPPD_NAMELEN - 1)));
        pthread_mutex_unlock(&hl->init);
        break;
      }
      pthread_mutex_unlock(&hl->init);
    }
    VERIFYC(sess->effective_domain_id != (uint32_t)-1, AEE_ENOSESSION);
    break;
  }
  case FASTRPC_GET_EFFECTIVE_DOMAIN_ID: {
    remote_rpc_effective_domain_id_t *effec_domain_id =
        (remote_rpc_effective_domain_id_t *)data;
    int effec_domain = -1;
    char domain_name_buf[32];

    VERIFYC(datalen == sizeof(remote_rpc_effective_domain_id_t), AEE_EBADPARM);
    VERIFYC(effec_domain_id && effec_domain_id->domain_name &&
                effec_domain_id->domain_name_len > 0 &&
                effec_domain_id->session_id < MAX_SESSIONS_LIMIT,
            AEE_EBADPARM);
    safe_domain_name_copy(domain_name_buf, sizeof(domain_name_buf),
                           effec_domain_id->domain_name, effec_domain_id->domain_name_len);
    domain = get_domain_from_name(domain_name_buf, DOMAIN_NAME_STAND_ALONE);
    VERIFYC(IS_VALID_DOMAIN_ID(domain), AEE_EBADPARM);
    /* Works with or without an earlier FASTRPC_RESERVE_NEW_SESSION: the id
     * is domain + NUM_DOMAINS * session, and a session that has no entry
     * yet gets one created. This does not reserve the session. */
    effec_domain = fastrpc_get_or_create_effec_domain(
        domain, (int)effec_domain_id->session_id);
    VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(effec_domain), AEE_ENOSESSION);
    effec_domain_id->effective_domain_id = (uint32_t)effec_domain;
    break;
  }
  case FASTRPC_GET_URI: {
    remote_rpc_get_uri_t *rpc_uri = (remote_rpc_get_uri_t *)data;
    int ret_val = -1;
    char domain_name_buf[32];
    char *module_uri_buf = NULL;

    VERIFYC(datalen == sizeof(remote_rpc_get_uri_t), AEE_EBADPARM);
    VERIFYC(rpc_uri && rpc_uri->domain_name && rpc_uri->domain_name_len > 0 &&
                rpc_uri->session_id < MAX_SESSIONS_LIMIT,
            AEE_EBADPARM);
    safe_domain_name_copy(domain_name_buf, sizeof(domain_name_buf),
                           rpc_uri->domain_name, rpc_uri->domain_name_len);
    domain = get_domain_from_name(domain_name_buf, DOMAIN_NAME_STAND_ALONE);
    VERIFYC(IS_VALID_DOMAIN_ID(domain), AEE_EBADPARM);
    VERIFYC(rpc_uri->module_uri != NULL && rpc_uri->module_uri_len > 0,
            AEE_EBADPARM);
    VERIFYC(rpc_uri->uri != NULL, AEE_EBADPARM);
    VERIFYC(rpc_uri->uri_len > rpc_uri->module_uri_len, AEE_EBADSIZE);
    /*
     * module_uri_len is documented as the length "without NULL character" --
     * the caller's buffer isn't guaranteed to be NUL-terminated there, so it
     * can't be passed directly to the %s below.
     */
    VERIFYC(NULL != (module_uri_buf = (char *)calloc(1, rpc_uri->module_uri_len + 1)),
            AEE_ENOMEMORY);
    memcpy(module_uri_buf, rpc_uri->module_uri, rpc_uri->module_uri_len);
    module_uri_buf[rpc_uri->module_uri_len] = '\0';
    {
      const char *dom_name = get_domain_name_str(domain);
      ret_val = snprintf(0, 0, "%s%s%s%s%d", module_uri_buf,
                         FASTRPC_DOMAIN_URI, dom_name,
                         FASTRPC_SESSION_URI, rpc_uri->session_id);
      if (rpc_uri->uri_len <= ret_val) {
        nErr = AEE_EBADSIZE;
        FARF(ERROR,
             "ERROR 0x%x: %s Session URI length %u is not enough, need %u "
             "characters",
             nErr, __func__, rpc_uri->uri_len, ret_val);
      }
      ret_val = snprintf(rpc_uri->uri, rpc_uri->uri_len, "%s%s%s%s%d",
                         module_uri_buf, FASTRPC_DOMAIN_URI,
                         dom_name, FASTRPC_SESSION_URI,
                         rpc_uri->session_id);
    }
    free(module_uri_buf);
    if (ret_val < 0) {
      nErr = AEE_EBADSIZE;
      FARF(ERROR, "ERROR 0x%x: %s Invalid Session URI length %u", nErr,
           __func__, rpc_uri->uri_len);
    }
    break;
  }
  case FASTRPC_CONTEXT_CREATE:
    VERIFYC(datalen == sizeof(fastrpc_context_create)
              && data, AEE_EBADPARM);
    {
      /* Normalize any legacy effective domain IDs in the array to dynamic */
      fastrpc_context_create *ctx = (fastrpc_context_create *)data;
      if (ctx->effec_domain_ids && ctx->num_domain_ids > 0) {
        uint32_t ci;
        for (ci = 0; ci < ctx->num_domain_ids; ci++)
          ctx->effec_domain_ids[ci] = (uint32_t)normalize_effec_domain_id(
                                        (int)ctx->effec_domain_ids[ci]);
      }
    }
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_create_context(data)));
    break;
  case FASTRPC_CONTEXT_DESTROY:
  {
    fastrpc_context_destroy *dest = (fastrpc_context_destroy *)data;

    VERIFYC(datalen == sizeof(fastrpc_context_destroy)
              && dest && !dest->flags, AEE_EBADPARM);
    VERIFY(AEE_SUCCESS == (nErr = fastrpc_destroy_context(dest->ctx)));
    break;
  }
  default:
    nErr = AEE_EUNSUPPORTED;
    FARF(ERROR, "ERROR 0x%x: %s Unsupported request ID %d", nErr, __func__,
         req);
    break;
  }
bail:
  FASTRPC_PUT_REF(domain);
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
         __func__, req, strerror(errno));
    if (0 == check_rpc_error(nErr)) {
      FARF(ERROR, "Error 0x%x: %s failed for request ID %d (errno %s)", nErr,
           __func__, req, strerror(errno));
    }
  }
  return nErr;
}

/*
 * get_domain_name_str - return domain name string for a domain ID.
 * For legacy IDs uses SUBSYSTEM_NAME[]; for dynamic logical IDs queries Level-1.
 */
static const char *get_domain_name_str(int domain_id) {
  if (IS_DYNAMIC_DOMAIN_ID(domain_id)) {
    static __thread char name_buf[MAX_DOMAIN_NAMELEN];
    fastrpc_domain_internal d;

    if (!fastrpc_get_domain_by_logical_id(domain_id, &d))
      return "unknown";
    strlcpy(name_buf, d.domain_info.name, sizeof(name_buf));
    return name_buf;
  }
  if (domain_id >= 0 && domain_id < NUM_DOMAINS)
    return SUBSYSTEM_NAME[domain_id];
  return "unknown";
}

/*
 * legacy_uri_to_logical_id - look up the Level-1 dynamic logical_id for a
 * legacy URI token (e.g. "&_dom=cdsp"). If the Level-1 table has been
 * populated and the legacy domain has a dynamic equivalent, return that
 * logical_id so both legacy and dynamic URI paths converge on the same
 * session. Falls back to fallback_id if Level-1 is not yet populated.
 */
static int legacy_uri_to_logical_id(const char *legacy_name, int fallback_id) {
  fastrpc_domain_internal d;

  if (!fastrpc_get_domain_by_name(legacy_name, strlen(legacy_name), &d))
    return fallback_id;
  return (int)d.domain_info.id;
}

/*
 * strstr(uri, "&_dom=cdsp") also matches "&_dom=cdsp2" (prefix overmatch),
 * silently misrouting an unrecognized/typo'd instance to instance 0
 * instead of failing. Require the match to end the token: next char must
 * be '\0' or not a digit (a real trailing instance number would continue
 * the digits, e.g. the "cdsp1" case already has its own exact macro).
 */
static bool uri_has_domain_token(const char *uri, const char *token) {
  const char *p = strstr(uri, token);

  if (!p)
    return false;
  p += strlen(token);
  return *p == '\0' || !isdigit((unsigned char)*p);
}

static int try_uri_domain_match(const char *uri) {
  char dom_tok[MAX_DOMAIN_NAMELEN + 8]; /* "&_dom=" + name + NUL */
  int logical_id;

  /*
   * Legacy URI tokens: convert to dynamic logical_id via Level-1 table.
   * Both "&_dom=cdsp" and "&_dom=nsp1000" then resolve to logical_id=1000,
   * producing the same effective domain ID (100000 for session 0).
   * Fallback to legacy domain ID if Level-1 is not yet populated.
   */
  if (uri_has_domain_token(uri, ADSP_DOMAIN))  return legacy_uri_to_logical_id("adsp",  ADSP_DOMAIN_ID);
  if (uri_has_domain_token(uri, MDSP_DOMAIN))  return legacy_uri_to_logical_id("mdsp",  MDSP_DOMAIN_ID);
  if (uri_has_domain_token(uri, SDSP_DOMAIN))  return legacy_uri_to_logical_id("sdsp",  SDSP_DOMAIN_ID);
  if (uri_has_domain_token(uri, CDSP1_DOMAIN)) return legacy_uri_to_logical_id("cdsp1", CDSP1_DOMAIN_ID);
  if (uri_has_domain_token(uri, CDSP_DOMAIN))  return legacy_uri_to_logical_id("cdsp",  CDSP_DOMAIN_ID);
  if (uri_has_domain_token(uri, GDSP0_DOMAIN)) return legacy_uri_to_logical_id("gdsp0", GDSP0_DOMAIN_ID);
  if (uri_has_domain_token(uri, GDSP1_DOMAIN)) return legacy_uri_to_logical_id("gdsp1", GDSP1_DOMAIN_ID);

  /*
   * Try dynamic domain names from Level-1 table ("&_dom=hpass5001" etc.).
   * Iterate whatever was actually discovered instead of a hardcoded
   * instance range, so a newly discovered instance (e.g. a 5th NSP core)
   * is matched here with no code change.
   */
  for (logical_id = fastrpc_next_logical_domain(-1); logical_id >= 0;
       logical_id = fastrpc_next_logical_domain(logical_id)) {
    fastrpc_domain_internal d;

    if (!fastrpc_get_domain_by_logical_id(logical_id, &d)) continue;
    snprintf(dom_tok, sizeof(dom_tok), "&_dom=%s", d.domain_info.name);
    if (strstr(uri, dom_tok)) return logical_id;
  }
  return INVALID_DOMAIN_ID;
}

/* Resolves a domain name/URI to a domain id.
 * @uri: standalone domain name, or a module URI containing "&_dom="/"&_session="
 * @type: DOMAIN_NAME_STAND_ALONE or DOMAIN_NAME_IN_URI
 * @returns: logical/legacy domain id, or an effective domain id for
 *           DOMAIN_NAME_IN_URI; INVALID_DOMAIN_ID on no match.
 */
int get_domain_from_name(const char *uri, uint32_t type) {
  int domain = DEFAULT_DOMAIN_ID;
  const char *session_uri = NULL;
  int session_id = 0;
  int effec_domain = -1;

  if (uri && type == DOMAIN_NAME_STAND_ALONE) {
    /*
     * Query Level-1 table first — handles both dynamic ("hpass5001",
     * "nsp1002") and legacy ("adsp", "cdsp", "cdsp1") names, always
     * returning the logical_id:
     *   logical_id=5000 ("adsp")    → effec = 5000*100+session = 500000+session
     *   logical_id=5001 ("hpass5001") → effec = 500100+session
     * For a legacy-aliased domain, this still resolves to the same
     * Level-2 session entry that remote_handle64_open("lib.so&_dom=adsp")
     * would use, since normalize_effec_domain_id() (called at every API
     * entry point) converts an old app's legacy numeric effective ID
     * (e.g. 3 for cdsp) to the same dynamic session.
     */
    fastrpc_domain_internal dinfo;

    if (fastrpc_get_domain_by_name(uri, strlen(uri), &dinfo)) {
      domain = dinfo.domain_info.id; /* always logical_id */
      return domain;
    }
    /*
     * Level-1 not yet populated or name not found — fall back to static map.
     * Exact match only (not strncmp/prefix) -- a prefix match would let
     * "adsp<garbage>" silently resolve as plain "adsp".
     */
    if (!strcmp(uri, ADSP_DOMAIN_NAME)) {
      domain = ADSP_DOMAIN_ID;
    } else if (!strcmp(uri, MDSP_DOMAIN_NAME)) {
      domain = MDSP_DOMAIN_ID;
    } else if (!strcmp(uri, SDSP_DOMAIN_NAME)) {
      domain = SDSP_DOMAIN_ID;
    } else if (!strcmp(uri, CDSP1_DOMAIN_NAME)) {
      domain = CDSP1_DOMAIN_ID;
    } else if (!strcmp(uri, CDSP_DOMAIN_NAME)) {
      domain = CDSP_DOMAIN_ID;
    } else if (!strcmp(uri, GDSP0_DOMAIN_NAME)) {
      domain = GDSP0_DOMAIN_ID;
    } else if (!strcmp(uri, GDSP1_DOMAIN_NAME)) {
      domain = GDSP1_DOMAIN_ID;
    } else {
      domain = INVALID_DOMAIN_ID;
      FARF(ERROR, "Invalid domain name: %s\n", uri);
      return domain;
    }
  }
  if (uri && type == DOMAIN_NAME_IN_URI) {
    domain = try_uri_domain_match(uri);
    if (domain == INVALID_DOMAIN_ID) {
      FARF(ERROR, "Invalid domain name: %s\n", uri);
      goto bail;
    }
    if (NULL != (session_uri = strstr(uri, FASTRPC_SESSION_URI))) {
      session_uri = session_uri + strlen(FASTRPC_SESSION_URI);
      // Get Session ID from URI
      session_id = strtol(session_uri, NULL, 10);
      /* The URI may name a session that was reserved earlier, or one the
       * client never reserved (it built the URI itself). Both resolve to
       * the same entry; a session that doesn't exist yet is created. */
      if (session_id >= 0 && session_id < MAX_SESSIONS_LIMIT &&
          (effec_domain = fastrpc_get_or_create_effec_domain(
               domain, session_id)) >= 0) {
        domain = effec_domain;
      } else {
        domain = INVALID_DOMAIN_ID;
        FARF(ERROR, "Invalid domain name: %s\n", uri);
      }
    } else if (IS_DYNAMIC_DOMAIN_ID(domain)) {
      /* No "&_session=" token: default to session 0. For dynamic domains
       * the logical_id itself is not a valid effective domain id (unlike
       * legacy, where the legacy id already is the session-0 effective
       * id), so it must still be converted. */
      if ((effec_domain = fastrpc_get_or_create_effec_domain(domain, 0)) >= 0) {
        domain = effec_domain;
      } else {
        domain = INVALID_DOMAIN_ID;
        FARF(ERROR, "Invalid domain name: %s\n", uri);
      }
    }
  }
bail:
  VERIFY_IPRINTF("%s: %d\n", __func__, domain);
  return domain;
}

int fastrpc_get_pd_type(int domain) {
	struct handle_list *hl = hlist_lookup(domain);
	if (hl && (hl->dev != -1)) {
		return hl->dsppd;
	} else {
		return -1;
	}
}

int get_current_domain(void) {
  intptr_t tls_val;
  int domain = -1;

  /* user hint to pick default domain id
   * first try tlskey before using default domain
   */
  tls_val = (intptr_t)pthread_getspecific(tlsKey);
  if (tls_val != 0) {
    /* Undo the "+1" offset applied in set_thread_context(). */
    domain = (int)(tls_val - 1);
  }
  if (!IS_VALID_EFFECTIVE_DOMAIN_ID(domain)) {
    // use default domain if thread tlskey not found
    domain = DEFAULT_DOMAIN_ID;
  }
  return normalize_effec_domain_id(domain);
}

bool is_process_exiting(int domain) {
  int nErr = 0, state = 1;
  struct handle_list *hl = NULL;

  (void)nErr;
  VERIFYC(IS_VALID_EFFECTIVE_DOMAIN_ID(domain), AEE_EBADDOMAIN);
  hl = hlist_lookup(domain);
  VERIFYC(hl != NULL, AEE_EBADDOMAIN);
  pthread_mutex_lock(&hl->mut);
  state = hl->state;
  pthread_mutex_unlock(&hl->mut);
  if (state != FASTRPC_DOMAIN_STATE_INIT)
    return true;
  else
    return false;
bail:
  return true;
}

int remote_set_mode(uint32_t mode) {
  int i;
  struct handle_list *hl;
  FOR_EACH_EFFECTIVE_DOMAIN_ID(i) {
    hl = hlist_lookup(i);
    if (!hl)
      continue;
    hl->mode = mode;
    hl->setmode = 1;
  }
  return AEE_SUCCESS;
}

PL_DEP(fastrpc_apps_user);
PL_DEP(gpls);
PL_DEP(apps_std);
PL_DEP(rpcmem);
PL_DEP(listener_android);

static int attach_guestos(int domain) {
  int attach;
  int dom = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);

  /* Dynamic domain IDs (HPASS/NSP): always USERPD */
  if (IS_DYNAMIC_DOMAIN_ID(dom))
    return USERPD;

  switch (dom) {
  case MDSP_DOMAIN_ID:
  case ADSP_DOMAIN_ID:
  case CDSP_DOMAIN_ID:
  case SDSP_DOMAIN_ID:
  case CDSP1_DOMAIN_ID:
  case GDSP0_DOMAIN_ID:
  case GDSP1_DOMAIN_ID:
    attach = USERPD;
    break;
  default:
    attach = ROOT_PD;
    break;
  }
  return attach;
}

static void domain_deinit(int domain) {
  int olddev;
  remote_handle64 handle = 0;
  uint64_t t_kill;

  if (!hlist_lookup(domain)) {
    return;
  }
  olddev = hlist_lookup(domain)->dev;
  FARF(RUNTIME_RPC_HIGH, "%s for domain %d: dev %d", __func__, domain, olddev);
  if (olddev != -1) {

    FASTRPC_ATRACE_BEGIN_L("%s called for handle 0x%x, domain %d, dev %d",
                           __func__, handle, domain, olddev);

    handle = get_adsp_current_process1_handle(domain);
    if (handle != INVALID_HANDLE) {
      adsp_current_process1_exit(handle);
    } else {
      adsp_current_process_exit();
    }

    pthread_mutex_lock(&hlist_lookup(domain)->mut);
    hlist_lookup(domain)->state = FASTRPC_DOMAIN_STATE_DEINIT;
    pthread_mutex_unlock(&hlist_lookup(domain)->mut);

    dspsignal_domain_deinit(domain);
    listener_android_domain_deinit(domain);
    hlist_lookup(domain)->first_revrpc_done = 0;
    fastrpc_notif_domain_deinit(domain);
    fastrpc_clear_handle_list(MULTI_DOMAIN_HANDLE_LIST_ID, domain);
    fastrpc_clear_handle_list(REVERSE_HANDLE_LIST_ID, domain);
    if (domain == normalize_effec_domain_id(DEFAULT_DOMAIN_ID)) {
      fastrpc_clear_handle_list(NON_DOMAIN_HANDLE_LIST_ID, domain);
    }
    fastrpc_perf_deinit();
    trace_marker_deinit(domain);
    deinitFileWatcher(domain);
    adspmsgd_stop(domain);
    fastrpc_mem_close(domain);
    apps_mem_deinit(domain);
    hlist_lookup(domain)->state = 0;
    hlist_lookup(domain)->ref = 0;

    hlist_lookup(domain)->cphandle = 0;
    hlist_lookup(domain)->msghandle = 0;
    hlist_lookup(domain)->remotectlhandle = 0;
    hlist_lookup(domain)->listenerhandle = 0;
    hlist_lookup(domain)->dev = -1;
    hlist_lookup(domain)->info = -1;
    hlist_lookup(domain)->dsppd = attach_guestos(domain);
    memset(hlist_lookup(domain)->dsppdname, 0, MAX_DSPPD_NAMELEN);
    memset(hlist_lookup(domain)->sessionname, 0, MAX_DSPPD_NAMELEN);
    PROFILE_ALWAYS(&t_kill, close_device_node(domain, olddev););
    FARF(RUNTIME_RPC_HIGH, "%s: closed device %d on domain %d (kill time %" PRIu64 " us)",
         __func__, olddev, domain, t_kill);
    FARF(ALWAYS, "%s done for domain %d.", __func__, domain);
    FASTRPC_ATRACE_END();
  }
  hlist_lookup(domain)->proc_sharedbuf_cur_addr = NULL;
  if (hlist_lookup(domain)->proc_sharedbuf) {
    rpcmem_free_internal(hlist_lookup(domain)->proc_sharedbuf);
    hlist_lookup(domain)->proc_sharedbuf = NULL;
  }
  // Free the session, on session deinit
  pthread_mutex_lock(&hlist_lookup(domain)->init);
  hlist_lookup(domain)->is_session_reserved = false;
  pthread_mutex_unlock(&hlist_lookup(domain)->init);
  pthread_mutex_lock(&hlist_lookup(domain)->mut);
  hlist_lookup(domain)->state = FASTRPC_DOMAIN_STATE_CLEAN;
  pthread_mutex_unlock(&hlist_lookup(domain)->mut);
}

/*
 * get_dynamic_device_names - build device node paths from the raw /dev
 * basename captured at discovery time (dev_basename, e.g. "cdsp3",
 * "gdsp0"), instead of a fixed per-instance table -- so any dynamically
 * discovered instance can be opened, not just the ones hardcoded here
 * when this table was last written.
 */
static void get_dynamic_device_names(int domain,
                                     const char **secure_name,
                                     const char **non_secure_name)
{
  static __thread char non_secure_buf[sizeof("/dev/fastrpc-") + MAX_DOMAIN_NAMELEN];
  static __thread char secure_buf[sizeof("/dev/fastrpc-") + MAX_DOMAIN_NAMELEN + sizeof("-secure") - 1];
  fastrpc_domain_internal d;

  if (!fastrpc_get_domain_by_logical_id(domain, &d) || !d.dev_basename[0]) {
    FARF(ERROR, "ERROR: %s: no device basename for logical_id %d", __func__, domain);
    *secure_name = NULL;
    *non_secure_name = NULL;
    return;
  }

  snprintf(non_secure_buf, sizeof(non_secure_buf), "/dev/fastrpc-%s", d.dev_basename);
  snprintf(secure_buf, sizeof(secure_buf), "/dev/fastrpc-%s-secure", d.dev_basename);
  *non_secure_name = non_secure_buf;
  *secure_name = secure_buf;
}

void get_domain_device_names(int domain_id, const char **secure_name, const char **non_secure_name) {
  int domain = domain_id;

  /* Dynamic domain: derive device node from the discovered basename */
  if (IS_DYNAMIC_DOMAIN_ID(domain)) {
    get_dynamic_device_names(domain, secure_name, non_secure_name);
    return;
  }

  switch (domain) {
  case ADSP_DOMAIN_ID:
    *secure_name = ADSPRPC_SECURE_DEVICE;
    *non_secure_name = ADSPRPC_DEVICE;
    break;
  case SDSP_DOMAIN_ID:
    *secure_name = SDSPRPC_SECURE_DEVICE;
    *non_secure_name = SDSPRPC_DEVICE;
    break;
  case MDSP_DOMAIN_ID:
    *secure_name = MDSPRPC_SECURE_DEVICE;
    *non_secure_name = MDSPRPC_DEVICE;
    break;
  case CDSP_DOMAIN_ID:
    *secure_name = CDSPRPC_SECURE_DEVICE;
    *non_secure_name = CDSPRPC_DEVICE;
    break;
  case CDSP1_DOMAIN_ID:
    *secure_name = CDSP1RPC_SECURE_DEVICE;
    *non_secure_name = CDSP1RPC_DEVICE;
    break;
  case GDSP0_DOMAIN_ID:
    *secure_name = GDSP0RPC_SECURE_DEVICE;
    *non_secure_name = GDSP0RPC_DEVICE;
    break;
  case GDSP1_DOMAIN_ID:
    *secure_name = GDSP1RPC_SECURE_DEVICE;
    *non_secure_name = GDSP1RPC_DEVICE;
    break;
  default:
    FARF(ERROR, "ERROR: %s Invalid domain %d", __func__, domain);
    *secure_name = NULL;
    *non_secure_name = NULL;
    break;
  }
}

int open_device_node(int domain_id) {
  int dev = -1, nErr = 0;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);
  int sess_id = GET_SESSION_ID_FROM_DOMAIN_ID(domain_id);
  const char *secure_dev = NULL, *non_secure_dev = NULL;

  if (IS_VALID_DOMAIN_ID(domain)) {
    get_domain_device_names(domain, &secure_dev, &non_secure_dev);
    dev = open(secure_dev, O_NONBLOCK);
    if (dev >= 0) {
      FARF(ALWAYS,
           "%s: opened secure device node %s -> fd %d for domain %d, sess %d",
           __func__, secure_dev, dev, domain_id, sess_id);
    }
    if ((dev < 0) && ((errno == ENOENT) || (errno == EACCES))) {
      FARF(RUNTIME_RPC_HIGH,
           "Device node %s open failed for domain %d (errno %s),"
           "falling back to node %s \n",
           secure_dev, domain, strerror(errno), non_secure_dev);
      dev = open(non_secure_dev, O_NONBLOCK);
      if (dev >= 0) {
        FARF(ALWAYS,
             "%s: opened non-secure device node %s -> fd %d for domain %d, sess %d",
             __func__, non_secure_dev, dev, domain_id, sess_id);
      }
    }
  }
  if (dev < 0)
    FARF(ERROR,
         "Error 0x%x: %s failed for domain ID %d, sess ID %d secure dev : %s, "
         "dev : %s. (errno %d, %s) (Either the remote processor is down, or "
         "application does not have permission to access the remote "
         "processor\n",
         nErr, __func__, domain_id, sess_id, secure_dev,
         non_secure_dev, errno, strerror(errno));
  return dev;
}

static int close_device_node(int domain_id, int dev) {
  int nErr = 0;
  nErr = close(dev);
  FARF(ALWAYS, "%s: closed dev %d on domain %d", __func__, dev, domain_id);
  return nErr;
}

static int get_process_attrs(int domain) {
  int attrs = 0;

  attrs = fastrpc_get_property_int(FASTRPC_PROCESS_ATTRS, 0);
  attrs |= fastrpc_get_property_int(FASTRPC_PROCESS_ATTRS_PERSISTENT, 0);
  fastrpc_trace = fastrpc_get_property_int(FASTRPC_DEBUG_TRACE, 0);
  attrs |= hlist_lookup(domain)->adaptive_qos ? FASTRPC_MODE_ADAPTIVE_QOS : 0;
  attrs |= hlist_lookup(domain)->unsigned_module ? FASTRPC_MODE_UNSIGNED_MODULE : 0;
  attrs |= (hlist_lookup(domain)->pd_dump | fastrpc_config_is_pddump_enabled())
               ? FASTRPC_MODE_ENABLE_PDDUMP
               : 0;
  attrs |= fastrpc_get_property_int(FASTRPC_DEBUG_PDDUMP, 0)
               ? FASTRPC_MODE_DEBUG_PDDUMP
               : 0;
  attrs |= (fastrpc_config_is_perfkernel_enabled() |
            fastrpc_get_property_int(FASTRPC_PERF_KERNEL, 0))
               ? FASTRPC_MODE_PERF_KERNEL
               : 0;
  attrs |= (fastrpc_config_is_perfdsp_enabled() |
            fastrpc_get_property_int(FASTRPC_PERF_ADSP, 0))
               ? FASTRPC_MODE_PERF_DSP
               : 0;
  attrs |= fastrpc_config_is_log_iregion_enabled()
               ? FASTRPC_MODE_ENABLE_IREGION_LOG
               : 0;
  attrs |= fastrpc_config_is_qtf_tracing_enabled()
               ? FASTRPC_MODE_ENABLE_QTF_TRACING
               : 0;
  attrs |= (fastrpc_config_get_caller_level() << 13) &
           FASTRPC_MODE_CALLER_LEVEL_MASK;
  attrs |= fastrpc_config_is_uaf_enabled() ? FASTRPC_MODE_ENABLE_UAF : 0;
  attrs |= fastrpc_config_is_debug_logging_enabled()
               ? FASTRPC_MODE_ENABLE_DEBUG_LOGGING
               : 0;
#ifdef SYSTEM_RPC_LIBRARY
  attrs |= FASTRPC_MODE_SYSTEM_PROCESS;
#endif
  attrs |= fastrpc_config_is_sysmon_reserved_bit_enabled()
               ? FASTRPC_MODE_SYSMON_RESERVED_BIT
               : 0;
  attrs |= fastrpc_config_is_logpacket_enabled() ? FASTRPC_MODE_LOG_PACKET : 0;
  attrs |= (fastrpc_config_get_leak_detect() << 19) &
           FASTRPC_MODE_ENABLE_LEAK_DETECT;
  attrs |= (fastrpc_config_get_caller_stack_num() << 21) &
           FASTRPC_MODE_CALLER_STACK_NUM;
  return attrs;
}

static void get_process_testsig(apps_std_FILE *fp, uint64_t *ptrlen) {
  int nErr = 0;
  uint64_t len = 0;
  char testsig[PROPERTY_VALUE_MAX];

  if (fp == NULL || ptrlen == NULL)
    return;

  if (fastrpc_get_property_string(FASTRPC_DEBUG_TESTSIG, testsig, NULL)) {
    FARF(RUNTIME_RPC_HIGH, "testsig file loading is %s", testsig);
    nErr = apps_std_fopen_with_env(ADSP_LIBRARY_PATH, ";", testsig, "r", fp);
    if (nErr == AEE_SUCCESS && *fp != -1)
      nErr = apps_std_flen(*fp, &len);
  }
  if (nErr)
    len = 0;
  *ptrlen = len;
  return;
}

/*
 * try_open_shell_file() - Search all known DSP locations for the given shell
 * file name and open it.
 *
 * Search order for the given @shell_name:
 *   1. Every ';'-separated directory in DSP_LIBS_LOCATION. For each directory
 *      fopen_from_dirlist() tries "<dir>/<subsystem>/<name>" first and then
 *      "<dir>/<name>".
 *   2. VENDOR_DSP_LOCATION "<name>"
 *   3. VENDOR_DSP_LOCATION "<subsystem>/<name>"
 *
 * Args
 *	@domain		: DSP domain ID (not effective domain ID)
 *	@shell_name	: Shell file name to look for
 *	@fh		: [out] Handle of the opened file
 *	@out_path	: [out] NULL-safe; on success, malloc'd absolute path that
 *			  was actually opened. Caller must free() it.
 *
 * Return	: AEE_SUCCESS if the file was opened, error code otherwise
 */
static int try_open_shell_file(int domain, const char *shell_name,
                               apps_std_FILE *fh, char **out_path) {
  char *absName = NULL;
  size_t absNameLen = 0;
  int nErr = AEE_SUCCESS;

  if (out_path)
    *out_path = NULL;

  /*
   * domain is the DSP domain (GET_DOMAIN_FROM_EFFEC_DOMAIN_ID() of the
   * effective id) -- legacy or dynamic. fopen_from_dirlist() takes it
   * explicitly and resolves the path name via the Level-1 table for
   * dynamic domains, rather than indexing SUBSYSTEM_NAME[] directly.
   */
  VERIFYC(IS_VALID_DOMAIN_ID(domain) && shell_name && fh, AEE_EBADPARM);

  /* 1. Search the configured DSP library locations. */
  nErr = fopen_from_dirlist(DSP_LIBS_LOCATION, ";", "r", shell_name, domain, fh,
                            out_path);
  if (!nErr)
    goto bail;

  /* 2. Fallback to /vendor/dsp/<shell_name> */
  absNameLen = strlen(VENDOR_DSP_LOCATION) + strlen(shell_name) + 1;
  VERIFYC(NULL != (absName = (char *)malloc(absNameLen)), AEE_ENOMEMORY);
  strlcpy(absName, VENDOR_DSP_LOCATION, absNameLen);
  strlcat(absName, shell_name, absNameLen);

  nErr = apps_std_fopen(absName, "r", fh);
  if (!nErr) {
    if (out_path) {
      *out_path = absName;   /* ownership transferred to caller */
      absName = NULL;
    }
    goto bail;
  }

  /* 3. Fallback to /vendor/dsp/<subsystem>/<shell_name>
   * Use get_domain_name_str() to handle both legacy (SUBSYSTEM_NAME[]) and
   * dynamic domain IDs ("hpass5001" etc.) without out-of-bounds access. */
  free(absName);
  absName = NULL;
  {
    const char *subsys = get_domain_name_str(domain);
    /* "<vendor>" + "<subsys>" + "/" + "<name>" + NUL */
    absNameLen = strlen(VENDOR_DSP_LOCATION) + strlen(subsys) +
                 1 + strlen(shell_name) + 1;
    VERIFYC(NULL != (absName = (char *)malloc(absNameLen)), AEE_ENOMEMORY);
    strlcpy(absName, VENDOR_DSP_LOCATION, absNameLen);
    strlcat(absName, subsys, absNameLen);
    strlcat(absName, "/", absNameLen);
    strlcat(absName, shell_name, absNameLen);
  }

  nErr = apps_std_fopen(absName, "r", fh);
  if (!nErr && out_path) {
    *out_path = absName;   /* ownership transferred to caller */
    absName = NULL;
  }
bail:
  free(absName);
  if (nErr != AEE_SUCCESS) {
    /*
     * Do not log the search paths here: DSP_LIBS_LOCATION can be up to
     * PATH_MAX and this helper is called once per candidate name. The final
     * error path in open_shell() logs the paths once.
     */
    FARF(RUNTIME_RPC_HIGH, "%s: 0x%x: '%s' not found for domain %d", __func__,
         nErr, shell_name, domain);
  }
  return nErr;
}

static int open_shell(int domain_id, int dev, apps_std_FILE *fh, int unsigned_shell) {
  char *shell_absName = NULL;
  char *shell_baseName = NULL;
  char *resolved_path = NULL;
  char domain_str[16] = {0};
  size_t shell_absNameLen = 0, shell_baseNameLen = 0;
  int nErr = AEE_SUCCESS;
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain_id);
  const char *shell_name = SIGNED_SHELL;
  (void)dev;

  if (1 == unsigned_shell) {
    shell_name = UNSIGNED_SHELL;
  }

  bool is_mdsp = (domain == MDSP_DOMAIN_ID);
  if (!is_mdsp && IS_DYNAMIC_DOMAIN_ID(domain)) {
    fastrpc_domain_internal d;
    is_mdsp = fastrpc_get_domain_by_logical_id(domain, &d) &&
              d.domain_info.type == FASTRPC_MDSP;
  }
  if (is_mdsp) {
    return nErr;
  }
  /*
   * Guard the out-param: apps_std_fopen() dereferences it unconditionally on
   * success, so a NULL here would fault instead of returning an error.
   */
  VERIFYC(fh != NULL, AEE_EBADPARM);

  /*
   * cap_logical_id identifies the physical domain for both the HPASS/LPASS
   * capability lookup below and the shell-path cache -- a legacy-aliased
   * domain (e.g. "adsp") shares the same cache slot as its dynamic logical
   * id (hpass5000), since both resolve to the same physical shell.
   */
  int cap_logical_id = IS_DYNAMIC_DOMAIN_ID(domain)
                            ? domain
                            : fastrpc_get_logical_id_for_legacy(domain);

  /*
   * Cache hit: this physical domain already had its shell resolved by an
   * earlier session (open_shell() runs once per session, not once per
   * physical domain, so this skips the entire DSP_LIBS_LOCATION/vendor
   * search -- and the DSP_IMAGE_CONFIG capability query below it -- for
   * every session after the first). Falls through to the full search below
   * if the cached path no longer opens (file genuinely moved/removed).
   */
  if (cap_logical_id >= 0) {
    char cached_path[PATH_MAX];

    if (fastrpc_get_cached_shell_path(cap_logical_id, unsigned_shell,
                                       cached_path, sizeof(cached_path))) {
      nErr = apps_std_fopen(cached_path, "r", fh);
      if (!nErr) {
        FARF(RUNTIME_RPC_HIGH, "%s: cache hit, opened '%s' directly for domain %d",
             __func__, cached_path, domain);
        return nErr;
      }
      FARF(ALWAYS, "%s: cached shell path '%s' no longer valid for domain %d, "
           "re-resolving", __func__, cached_path, domain);
    }
  }

  /*
   * Prefer the legacy domain ID for shell naming/path lookup when this
   * dynamic domain has one, so targets shipping only the legacy-named
   * shell (e.g. fastrpc_shell_3 for nsp1000/cdsp) keep working.
   */
  int shell_id = domain;
  if (IS_DYNAMIC_DOMAIN_ID(domain)) {
    fastrpc_domain_internal d;

    if (fastrpc_get_domain_by_logical_id(domain, &d) && d.legacy_domain_id >= 0)
      shell_id = d.legacy_domain_id;
    /* else: new domain (e.g. nsp1002) with no legacy — keep logical_id */
  }

  snprintf(domain_str, sizeof(domain_str), "%d", shell_id);

  /*
   * Preferred name is the domain-suffixed one, e.g. "fastrpc_shell_3" /
   * "fastrpc_shell_unsigned_3".
   */
  shell_absNameLen = strlen(shell_name) + strlen(domain_str) + 1;
  VERIFYC(NULL != (shell_absName = (char *)malloc(shell_absNameLen)),
          AEE_ENOMEMORY);
  strlcpy(shell_absName, shell_name, shell_absNameLen);
  strlcat(shell_absName, domain_str, shell_absNameLen);

  /*
   * Pass shell_id (not domain) so path 3's subsystem directory also uses
   * the legacy name when available.
   */
  nErr = try_open_shell_file(shell_id, shell_absName, fh, &resolved_path);
  if (!nErr) {
    FARF(RUNTIME_RPC_HIGH, "Successfully opened %s, domain %d", shell_absName,
         domain);
    goto bail;
  }

  /*
   * Some targets ship the shell without the domain ID suffix. Retry with the
   * base name, i.e. "fastrpc_shell" / "fastrpc_shell_unsigned". Both
   * SIGNED_SHELL and UNSIGNED_SHELL end with a trailing '_', so the base
   * name is shell_name with that trailing '_' dropped.
   */
  VERIFYC(strlen(shell_name) > 1, AEE_EBADPARM);
  shell_baseNameLen = strlen(shell_name) - 1;
  VERIFYC(NULL != (shell_baseName = (char *)malloc(shell_baseNameLen + 1)),
          AEE_ENOMEMORY);
  strlcpy(shell_baseName, shell_name, shell_baseNameLen + 1);

  nErr = try_open_shell_file(shell_id, shell_baseName, fh, &resolved_path);
  if (!nErr) {
    FARF(RUNTIME_RPC_HIGH, "Successfully opened %s, domain %d", shell_baseName,
         domain);
    goto bail;
  }

  /*
   * Fallback to cdsp's shell for other instances of the SAME cdsp family
   * (cdsp1, cdsp2, cdsp3, ... -- all compute cores sharing one image with
   * cdsp, whether or not they have a legacy alias). Restricted by
   * dev_basename prefix rather than FASTRPC_NSP type alone: gdsp shares
   * that type but is a different DSP family with its own distinct image
   * -- if gdsp0/gdsp1's own shell search genuinely fails, fail loudly
   * instead of silently loading cdsp's shell onto it.
   */
  if (IS_DYNAMIC_DOMAIN_ID(domain)) {
    fastrpc_domain_internal dinfo;
    bool have_dinfo = fastrpc_get_domain_by_logical_id(domain, &dinfo);
    int base_shell_id = -1;

    if (have_dinfo && dinfo.domain_info.type == FASTRPC_NSP &&
        !strncmp(dinfo.dev_basename, "cdsp", 4) &&
        shell_id != CDSP_DOMAIN_ID) {
      base_shell_id = CDSP_DOMAIN_ID;

      FARF(ALWAYS,
           "%s: NSP fallback: domain %d (shell_id=%d) → retrying '%s' "
           "with primary shell_id=%d",
           __func__, domain, shell_id, shell_baseName, base_shell_id);

      nErr = try_open_shell_file(base_shell_id, shell_baseName, fh, &resolved_path);
      if (!nErr)
        FARF(ALWAYS,
             "Opened shell '%s' via NSP fallback (domain=%d → cdsp/)",
             shell_baseName, domain);

    }
  }

  /*
   * adsp family (LPASS/HPASS, distinguished by capability not name):
   * fall back to plain "adsp"'s shell for instances with no shell of
   * their own (adsp1, adsp2, ... -- all sharing the same image as plain
   * adsp unless they ship an override). Mirrors the cdsp/NSP fallback
   * above; no image-config-specific subdir search.
   */
  if (IS_DYNAMIC_DOMAIN_ID(domain)) {
    fastrpc_domain_internal dinfo;
    bool have_dinfo = fastrpc_get_domain_by_logical_id(domain, &dinfo);

    if (have_dinfo && (dinfo.domain_info.type == FASTRPC_HPASS ||
                       dinfo.domain_info.type == FASTRPC_LPASS) &&
        !strncmp(dinfo.dev_basename, "adsp", 4) &&
        shell_id != ADSP_DOMAIN_ID) {
      FARF(ALWAYS,
           "%s: adsp fallback: domain %d (shell_id=%d) → retrying '%s' "
           "with primary shell_id=%d",
           __func__, domain, shell_id, shell_baseName, ADSP_DOMAIN_ID);

      nErr = try_open_shell_file(ADSP_DOMAIN_ID, shell_baseName, fh, &resolved_path);
      if (!nErr)
        FARF(ALWAYS,
             "Opened shell '%s' via adsp fallback (domain=%d → adsp/)",
             shell_baseName, domain);
    }
  }

bail:
  /*
   * Log before releasing the name buffers, so the exact names attempted can be
   * printed. Either buffer may be NULL if its allocation failed or if the
   * first candidate already succeeded.
   */
  if (nErr != AEE_SUCCESS) {
    bool is_sdsp = (domain == SDSP_DOMAIN_ID);
    if (!is_sdsp && IS_DYNAMIC_DOMAIN_ID(domain)) {
      fastrpc_domain_internal d;
      is_sdsp = fastrpc_get_domain_by_logical_id(domain, &d) &&
                d.domain_info.type == FASTRPC_SDSP;
    }
    if (is_sdsp && fh != NULL) {
      nErr = AEE_SUCCESS;
      *fh = -1;
    } else {
      FARF(ERROR,
           "Error 0x%x: %s failed for domain %d, tried '%s' and '%s', search "
           "paths used are %s and %s (errno %s)\n",
           nErr, __func__, domain,
           shell_absName ? shell_absName : "<not built>",
           shell_baseName ? shell_baseName : "<not built>",
           DSP_LIBS_LOCATION, VENDOR_DSP_LOCATION, strerror(errno));
    }
  } else if (cap_logical_id >= 0 && resolved_path) {
    fastrpc_set_cached_shell_path(cap_logical_id, unsigned_shell, resolved_path);
  }
  free(resolved_path);
  free(shell_absName);
  free(shell_baseName);
  return nErr;
}

/*
 * Request kernel to do optimizations to reduce RPC overhead.
 *
 * Provide the max user concurrency level to kernel which will
 * enable appropriate optimizations based on that info.
 * Args
 *	@domain	: Remote subsystem ID
 *
 * Return	: 0 on success
 */
/*
 * dom may be a legacy bare id or a dynamic logical id (see
 * GET_DOMAIN_FROM_EFFEC_DOMAIN_ID). The legacy id list below only covers
 * NSP instances that predate dynamic discovery -- any NSP reached via its
 * dynamic logical id (e.g. cdsp2/cdsp3) is recognized via the Level-1
 * table's type instead.
 */
static bool is_nsp_domain(int dom) {
  if (dom == CDSP_DOMAIN_ID || dom == CDSP1_DOMAIN_ID ||
      dom == GDSP0_DOMAIN_ID || dom == GDSP1_DOMAIN_ID)
    return true;
  if (IS_DYNAMIC_DOMAIN_ID(dom)) {
    fastrpc_domain_internal d;
    return fastrpc_get_domain_by_logical_id(dom, &d) &&
           d.domain_info.type == FASTRPC_NSP;
  }
  return false;
}

static int fastrpc_enable_kernel_optimizations(int domain) {
  int nErr = AEE_SUCCESS, dev = hlist_lookup(domain)->dev,
      dom = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);
  const uint32_t max_concurrency = 25;

  if (!is_nsp_domain(dom) || (hlist_lookup(domain)->dsppd != USERPD))
    goto bail;
  errno = 0;

  nErr = ioctl_optimization(dev, max_concurrency);
  // TODO:Bharath
  if ((nErr == -1 || nErr == (DSP_AEE_EOFFSET + AEE_ERPC)) &&
      (errno == ENOTTY || errno == EINVAL || errno == EBADRQC)) {
    /*
     * Kernel optimizations not supported. Ignore IOCTL failure
     * TODO: kernel cleanup to return ENOTTY for all unsupported IOCTLs
     */
    nErr = 0;
  }
bail:
  if (nErr) {
    FARF(RUNTIME_RPC_ERROR, "Error 0x%x: %s failed for domain %d (%s)\n", nErr, __func__,
         domain, strerror(errno));
  }
  /*
   * Since this is a performance optimization, print error but ignore
   * failure until the feature is stable enough.
   */
  return 0;
}

void print_process_attrs(int domain) {
  bool dbgMode = false, crc = false, signedMd = false, unsignedMd = false,
       qos = false, configPDdump = false;
  bool debugPDdump = false, KernelPerf = false, DSPperf = false,
       iregion = false, qtf = false, uaf = false;
  int one_mb = 1024 * 1024;
  int pd_initmem_size = 0;
  bool logpkt = false;

  if (IS_DEBUG_MODE_ENABLED(hlist_lookup(domain)->procattrs))
    dbgMode = true;
  if (IS_CRC_CHECK_ENABLED(hlist_lookup(domain)->procattrs))
    crc = true;
  if (hlist_lookup(domain)->unsigned_module) {
    unsignedMd = true;
    pd_initmem_size = 5 * one_mb;
  } else {
    signedMd = true;
    pd_initmem_size = hlist_lookup(domain)->pd_initmem_size;
  }
  if (hlist_lookup(domain)->adaptive_qos)
    qos = true;
  if (hlist_lookup(domain)->pd_dump | fastrpc_config_is_pddump_enabled())
    configPDdump = true;
  if (fastrpc_get_property_int(FASTRPC_DEBUG_PDDUMP, 0))
    debugPDdump = true;
  if (hlist_lookup(domain)->procattrs & FASTRPC_MODE_PERF_KERNEL)
    KernelPerf = true;
  if (hlist_lookup(domain)->procattrs & FASTRPC_MODE_PERF_DSP)
    DSPperf = true;
  if (fastrpc_config_is_log_iregion_enabled())
    iregion = true;
  if (fastrpc_config_is_qtf_tracing_enabled())
    qtf = true;
  if (fastrpc_config_is_uaf_enabled())
    uaf = true;
  if (fastrpc_config_is_logpacket_enabled())
    logpkt = true;
  FARF(ALWAYS,
       "Info: Created user PD on domain %d, dbg_trace 0x%x, enabled attr=> RPC "
       "timeout:%d, Dbg Mode:%s, CRC:%s, Unsigned:%s, Signed:%s, Adapt QOS:%s, "
       "PD dump: (Config:%s, Dbg:%s), Perf: (Kernel:%s, DSP:%s), Iregion:%s, "
       "QTF:%s, UAF:%s userPD initmem len:0x%x, Log pkt: %s",
       domain, fastrpc_trace, fastrpc_config_get_rpctimeout(),
       (dbgMode ? "Y" : "N"), (crc ? "Y" : "N"), (unsignedMd ? "Y" : "N"),
       (signedMd ? "Y" : "N"), (qos ? "Y" : "N"), (configPDdump ? "Y" : "N"),
       (debugPDdump ? "Y" : "N"), (KernelPerf ? "Y" : "N"),
       (DSPperf ? "Y" : "N"), (iregion ? "Y" : "N"), (qtf ? "Y" : "N"),
       (uaf ? "Y" : "N"), pd_initmem_size, (logpkt ? "Y" : "N"));
  return;
}

static int remote_init(int domain) {
  int nErr = AEE_SUCCESS, ioErr = 0;
  int dev = -1;
  struct fastrpc_proc_sess_info sess_info = {0};
  apps_std_FILE fh = -1;
  int pd_type = 0, errno_save = 0;
  uint32_t info = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain);
  int shared_buf_support = 0;
  char *file = NULL;
  int flags = 0, filelen = 0, memlen = 0, filefd = -1;

  FARF(RUNTIME_RPC_HIGH, "starting %s for domain %d", __func__, domain);
  /*
   * is_proc_sharedbuf_supported_dsp call should be made before
   * mutex lock (hlist_lookup(domain)->mut), Since remote_get_info is also locked
   * by the same mutex
   */
  shared_buf_support = is_proc_sharedbuf_supported_dsp(domain);
  set_thread_context(domain);
  pd_type = hlist_lookup(domain)->dsppd;
  VERIFYC(pd_type > DEFAULT_UNUSED && pd_type < MAX_PD_TYPE, AEE_EBADITEM);
  if (hlist_lookup(domain)->dev == -1) {
    dev = open_device_node(domain);
    VERIFYC(dev >= 0, AEE_ECONNREFUSED);
    // Set session relation info using FASTRPC_INVOKE2_SESS_INFO
    sess_info.domain_id = info;
    sess_info.pd_type = pd_type;
    sess_info.session_id = GET_SESSION_ID_FROM_DOMAIN_ID(domain);

    nErr = ioctl_session_info(dev, &sess_info);
    if (nErr == AEE_SUCCESS) {
      info = sess_info.domain_id;
      // Skip setting session related info in multiple ioctl calls, if
      // FASTRPC_CONTROL_SESS_INFO is supported
      goto set_sess_info_supported;
    } else {
      // Fallback to previous approach, if FASTRPC_CONTROL_SESS_INFO is not
      // supported
      FARF(RUNTIME_RPC_HIGH,
           "%s: FASTRPC_CONTROL_SESS_INFO not supported with error %d",
           __func__, nErr);
    }

    if (pd_type == SENSORS_STATICPD || pd_type == GUEST_OS_SHARED) {
      struct fastrpc_ctrl_smmu smmu = {0};
      smmu.sharedcb = 1;
      if (ioctl_control(dev, DSPRPC_SMMU_SUPPORT, &smmu)) {
        FARF(RUNTIME_RPC_HIGH, "%s: DSPRPC_SMMU_SUPPORT not supported",
             __func__);
      }
    }
    nErr = ioctl_getinfo(dev, &info);
  set_sess_info_supported:
    hlist_lookup(domain)->info = -1;
    if (nErr == AEE_SUCCESS) {
      hlist_lookup(domain)->info = info;
    } else if (errno == EACCES) {
      FARF(ERROR,
           "Error %d: %s: app does not have access to fastrpc device of domain "
           "%d (%s)",
           nErr, __func__, domain, strerror(errno));
      goto bail;
    } else if (errno == ECONNREFUSED || (errno == ENODEV)) {
      nErr = AEE_ECONNREFUSED;
      FARF(ERROR, "Error %d: %s: fastRPC device driver is disabled (%s)", nErr,
           __func__, strerror(errno));
      goto bail;
    } else if (nErr) {
      FARF(ERROR, "Error 0x%x: %s: failed to setup fastrpc session in kernel",
           nErr, __func__);
      goto bail;
    }

    // Set session id
    if (IS_EXTENDED_DOMAIN_ID(domain))
      VERIFY(AEE_SUCCESS == (nErr = ioctl_setmode(dev, FASTRPC_SESSION_ID1)));

    FARF(RUNTIME_RPC_HIGH, "%s: device %d opened with info 0x%x (attach %d)",
         __func__, dev, hlist_lookup(domain)->info, pd_type);
    // keep the memory we used to allocate
    if (pd_type == ROOT_PD || pd_type == GUEST_OS_SHARED ||
        pd_type == SECURE_STATICPD) {
      FARF(RUNTIME_RPC_HIGH,
           "%s: attaching to guest OS/Secure PD (attach %d) for domain %d",
           __func__, pd_type, domain);
      if (pd_type == SECURE_STATICPD) {
        file = calloc(1, (int)(strlen(hlist_lookup(domain)->dsppdname) + 1));
        VERIFYC(file, AEE_ENOMEMORY);
        strlcpy((char *)file, hlist_lookup(domain)->dsppdname,
                    strlen(hlist_lookup(domain)->dsppdname) + 1);
        filelen = strlen(hlist_lookup(domain)->dsppdname) + 1;
      }
      flags = FASTRPC_INIT_ATTACH;
      ioErr = ioctl_init(dev, flags, 0, (unsigned char *)file, filelen, -1, 0, 0, 0, 0);
      if (file) {
        free(file);
        file = NULL;
      }
      VERIFYC((!ioErr || errno == ENOTTY || errno == ENXIO || errno == EINVAL),
              AEE_ERPC);
    } else if (pd_type == AUDIO_STATICPD || pd_type == OIS_STATICPD) {
      FARF(RUNTIME_RPC_HIGH, "%s: creating static user PD for domain %d",
           __func__, domain);
      file =
          rpcmem_alloc_internal(0, RPCMEM_HEAP_DEFAULT,
                                (int)(strlen(hlist_lookup(domain)->dsppdname) + 1));
      VERIFYC(file, AEE_ENORPCMEMORY);
      strlcpy((char *)file, hlist_lookup(domain)->dsppdname,
                  strlen(hlist_lookup(domain)->dsppdname) + 1);
      filelen = strlen(hlist_lookup(domain)->dsppdname) + 1;
      flags = FASTRPC_INIT_CREATE_STATIC;
      // 3MB of remote heap for dynamic loading is available only for Audio PD.
      if (pd_type == AUDIO_STATICPD) {
        memlen = 3 * 1024 * 1024;
      }
      ioErr =
          ioctl_init(dev, flags, 0, (unsigned char *)file, filelen, -1, 0, memlen, 0, 0);
      if (ioErr) {
        nErr = convert_kernel_to_user_error(ioErr, errno);
        goto bail;
      }
    } else if (pd_type == SENSORS_STATICPD) {
      FARF(RUNTIME_RPC_HIGH, "%s: attaching to sensors PD for domain %d",
           __func__, domain);
      flags = FASTRPC_INIT_ATTACH_SENSORS;
      ioErr = ioctl_init(dev, flags, 0, (unsigned char *)0, 0, -1, 0, 0, 0, 0);
      VERIFYC((!ioErr || errno == ENOTTY || errno == ENXIO || errno == EINVAL),
              AEE_ERPC);
    } else if (pd_type == USERPD) {
      uint64_t len = 0;
      int readlen = 0, eof;
      apps_std_FILE fsig = -1;
      uint64_t siglen = 0;

#ifndef VIRTUAL_FASTRPC
#if !defined(SYSTEM_RPC_LIBRARY)
      open_shell(domain, dev, &fh, hlist_lookup(domain)->unsigned_module);
#endif
#endif

      hlist_lookup(domain)->procattrs = get_process_attrs(domain);
      if (IS_DEBUG_MODE_ENABLED(hlist_lookup(domain)->procattrs))
        get_process_testsig(&fsig, &siglen);

      flags = FASTRPC_INIT_CREATE;
      if (fh != -1) {
        VERIFY(AEE_SUCCESS == (nErr = apps_std_flen(fh, &len)));
        filelen = len + siglen;
        VERIFYC(filelen && filelen < INT_MAX, AEE_EFILE);
        file = rpcmem_alloc_internal(0, RPCMEM_HEAP_DEFAULT, (size_t)filelen);
        VERIFYC(file, AEE_ENORPCMEMORY);
        VERIFY(AEE_SUCCESS ==
               (nErr = apps_std_fread(fh, (unsigned char *)file, len, &readlen, &eof)));
        VERIFYC((int)len == readlen, AEE_EFILE);
        filefd = rpcmem_to_fd_internal((void *)file);
        filelen = (int)len;
        VERIFYC(filefd != -1, AEE_ERPC);
      } else {
        siglen = 0;
        fsig = -1;
      }

      if (!(FASTRPC_MODE_UNSIGNED_MODULE & hlist_lookup(domain)->procattrs)) {
        memlen = hlist_lookup(domain)->pd_initmem_size;
      } else {
        if (hlist_lookup(domain)->pd_initmem_size != DEFAULT_PD_INITMEM_SIZE)
          FARF(ERROR, "Setting user PD initial memory length is not supported "
                      "for unsigned PD, using default size\n");
      }
      errno = 0;

      if (shared_buf_support) {
        fastrpc_process_pack_params(dev, domain);
      }
      if (hlist_lookup(domain)->procattrs) {
        if (siglen && fsig != -1) {
          VERIFY(AEE_SUCCESS ==
                 (nErr = apps_std_fread(fsig, (unsigned char *)(file + len), siglen,
                                        &readlen, &eof)));
          VERIFYC(siglen == (uint64_t)readlen, AEE_EFILE);
          filelen = len + siglen;
        }
      }
      ioErr = ioctl_init(dev, flags, hlist_lookup(domain)->procattrs, (unsigned char *)file,
                         filelen, filefd, NULL, memlen, -1, siglen);
      if (ioErr) {
        nErr = ioErr;
        if (errno == ECONNREFUSED) {
          nErr = AEE_ECONNREFUSED;
          FARF(ERROR,
               "Error 0x%x: %s: untrusted app trying to offload to signed "
               "remote process (errno %d, %s). Try offloading to unsignedPD "
               "using remote_session_control",
               nErr, __func__, errno, strerror(errno));
        }
        goto bail;
      }
      print_process_attrs(domain);
    } else {
      FARF(ERROR, "Error: %s called for unknown mode %d", __func__, pd_type);
    }
    hlist_lookup(domain)->dev = dev;
    dev = -1;
    hlist_lookup(domain)->disable_exit_logs = 0;
  }
bail:
  // errno is being set to 0 in apps_std_fclose and we need original errno to
  // return proper error to user call
  errno_save = errno;
  if (file) {
    rpcmem_free_internal(file);
    file = NULL;
  }
  if (dev >= 0) {
    close_device_node(domain, dev);
  }
  if (fh != -1) {
    apps_std_fclose(fh);
  }
  if (nErr != AEE_SUCCESS) {
    errno = errno_save;
    if ((nErr == -1) && (errno == ECONNRESET)) {
      nErr = AEE_ECONNRESET;
    }
    FARF(ERROR, "Error 0x%x: %s failed for domain %d, errno %s, ioErr %d\n",
         nErr, __func__, domain, strerror(errno), ioErr);
  }
  FARF(RUNTIME_RPC_HIGH, "Done with %s, err: 0x%x, dev: %d", __func__, nErr,
       hlist_lookup(domain)->dev);
  return nErr;
}

__attribute__((destructor)) static void close_dev(void) {
  int i;

  FARF(RUNTIME_RPC_HIGH, "%s: unloading library %s", __func__,
       fastrpc_library[DEFAULT_DOMAIN_ID]);
  FOR_EACH_EFFECTIVE_DOMAIN_ID(i) {
    domain_deinit(i);
  }
  deinit_fastrpc_dsp_lib_refcnt();
  pl_deinit();
  PL_DEINIT(fastrpc_apps_user);
}

remote_handle64 get_adsp_current_process1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;

  if (hlist_lookup(domain)->cphandle) {
    return hlist_lookup(domain)->cphandle;
  }
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_update_module_list(
                             DOMAIN_LIST_PREPEND, domain,
                             _const_adsp_current_process1_handle, &local, NULL)));
  hlist_lookup(domain)->cphandle = local;
  return hlist_lookup(domain)->cphandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: adsp current process handle failed. domain %d (errno "
         "%s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_adspmsgd_adsp1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;

  if (hlist_lookup(domain)->msghandle) {
    return hlist_lookup(domain)->msghandle;
  }
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_update_module_list(
                             DOMAIN_LIST_PREPEND, domain,
                             _const_adspmsgd_adsp1_handle, &local, NULL)));
  hlist_lookup(domain)->msghandle = local;
  return hlist_lookup(domain)->msghandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR,
         "Error 0x%x: get adsp msgd handle failed. domain %d (errno %s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_adsp_listener1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;

  if (hlist_lookup(domain)->listenerhandle) {
    return hlist_lookup(domain)->listenerhandle;
  }
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_update_module_list(
                             DOMAIN_LIST_PREPEND, domain,
                             _const_adsp_listener1_handle, &local, NULL)));
  hlist_lookup(domain)->listenerhandle = local;
  return hlist_lookup(domain)->listenerhandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for domain %d (errno %s)\n", nErr,
         __func__, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_remotectl1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;

  // If remotectlhandle is 0 allocate handle, else return handle even though
  // INVALID_HANDLE handle
  if (hlist_lookup(domain)->remotectlhandle) {
    return hlist_lookup(domain)->remotectlhandle;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(DOMAIN_LIST_PREPEND, domain,
                                            _const_remotectl1_handle, &local, NULL)));
  hlist_lookup(domain)->remotectlhandle = local;
  return hlist_lookup(domain)->remotectlhandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: remotectl1 handle failed. domain %d (errno %s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

remote_handle64 get_adsp_perf1_handle(int domain) {
  remote_handle64 local;
  int nErr = AEE_SUCCESS;

  if (hlist_lookup(domain)->adspperfhandle) {
    return hlist_lookup(domain)->adspperfhandle;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(DOMAIN_LIST_PREPEND, domain,
                                            _const_adsp_perf1_handle, &local, NULL)));
  hlist_lookup(domain)->adspperfhandle = local;
  return hlist_lookup(domain)->adspperfhandle;
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: adsp_perf1 handle failed. domain %d (errno %s)\n",
         nErr, domain, strerror(errno));
  }
  return INVALID_HANDLE;
}

static int domain_init(int domain, int *dev) {
  int nErr = AEE_SUCCESS, dom = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(domain), mut_locked = 0;
  remote_handle64 panic_handle = 0;
  struct err_codes *err_codes_to_send = NULL;

  pthread_mutex_lock(&hlist_lookup(domain)->mut);
  mut_locked = 1;
  if (hlist_lookup(domain)->state != FASTRPC_DOMAIN_STATE_CLEAN) {
    *dev = hlist_lookup(domain)->dev;
    pthread_mutex_unlock(&hlist_lookup(domain)->mut);
    mut_locked = 0;
    return AEE_SUCCESS;
  }

  QList_Ctor(&hlist_lookup(domain)->ql);
  QList_Ctor(&hlist_lookup(domain)->nql);
  QList_Ctor(&hlist_lookup(domain)->rql);
  hlist_lookup(domain)->is_session_reserved = true;
  VERIFY(AEE_SUCCESS == (nErr = remote_init(domain)));
  if (hlist_lookup(domain)->wake_lock_enable) {
    VERIFY(AEE_SUCCESS ==
           (nErr = update_kernel_wakelock_status(
                domain, hlist_lookup(domain)->dev, hlist_lookup(domain)->wake_lock_enable)));
  }
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_mem_open(domain)));
  VERIFY(AEE_SUCCESS == (nErr = apps_mem_init(domain)));

  if (is_nsp_domain(dom)) {
    panic_handle = get_adsp_current_process1_handle(domain);
    if (panic_handle != INVALID_HANDLE) {
      int ret = -1;
      /* If error codes are available in debug config, send panic error codes to
       * dsp to crash. */
      err_codes_to_send = fastrpc_config_get_errcodes();
      if (err_codes_to_send) {
        ret = adsp_current_process1_panic_err_codes(
            panic_handle, err_codes_to_send->err_code,
            err_codes_to_send->num_err_codes);
        if (AEE_SUCCESS == ret) {
          FARF(ALWAYS, "%s : panic error codes sent successfully\n", __func__);
        } else {
          FARF(ERROR, "Error 0x%x: %s : panic error codes send failed\n", ret,
               __func__);
        }
      }
    } else {
      FARF(ALWAYS, "%s : current process handle is not valid\n", __func__);
    }
  }
  VERIFY(AEE_SUCCESS == (nErr = fastrpc_enable_kernel_optimizations(domain)));
  build_dsp_search_path_cache_for_domain(domain);
  initFileWatcher(domain); // Ignore errors
  trace_marker_init(domain);

  // If client notifications are registered, initialize notification thread and
  // enable notifications on domains
  if (fastrpc_notif_flag) {
    int ret = 0;
    ret = enable_process_state_notif_on_dsp(domain);
    if (ret == (int)(DSP_AEE_EOFFSET + AEE_EUNSUPPORTED)) {
      VERIFY_WPRINTF("Warning: %s: DSP does not support notifications",
                     __func__);
    }
    VERIFYC(ret == AEE_SUCCESS ||
                ret == (int)(DSP_AEE_EOFFSET + AEE_EUNSUPPORTED),
            ret);
  }
  fastrpc_perf_init(hlist_lookup(domain)->dev, domain);
  get_dsp_dma_reverse_rpc_map_capability(domain);
  hlist_lookup(domain)->state = FASTRPC_DOMAIN_STATE_INIT;
  hlist_lookup(domain)->ref = 0;
  pthread_mutex_unlock(&hlist_lookup(domain)->mut);
  mut_locked = 0;
  VERIFY(AEE_SUCCESS == (nErr = listener_android_domain_init(
                             domain, hlist_lookup(domain)->th_params.update_requested,
                             &hlist_lookup(domain)->th_params.r_sem)));
  bool is_sdsp_domain = (dom == SDSP_DOMAIN_ID);
  if (!is_sdsp_domain && IS_DYNAMIC_DOMAIN_ID(dom)) {
    fastrpc_domain_internal sdsp_d;
    is_sdsp_domain = fastrpc_get_domain_by_logical_id(dom, &sdsp_d) &&
                     sdsp_d.domain_info.type == FASTRPC_SDSP;
  }
  if (!is_sdsp_domain && hlist_lookup(domain)->dsppd == ROOT_PD) {
    remote_handle64 handle = 0;
    handle = get_adspmsgd_adsp1_handle(domain);
    if (handle != INVALID_HANDLE) {
      adspmsgd_init(handle, 0x10); // enable PD exception logging
    }
  }
bail:
  if (nErr != AEE_SUCCESS) {
    if (mut_locked) {
      pthread_mutex_unlock(&hlist_lookup(domain)->mut);
      mut_locked = 0;
    }
    domain_deinit(domain);
    if (hlist_table_ready) {
      FARF(ERROR, "Error 0x%x: %s (%d) failed for domain %d (errno %s)\n", nErr,
           __func__, hlist_lookup(domain)->dev, domain, strerror(errno));
    }
    *dev = -1;
    return nErr;
  }
  if (hlist_table_ready) {
    FARF(RUNTIME_RPC_LOW, "Done %s with dev %d, err %d", __func__,
         hlist_lookup(domain)->dev, nErr);
    *dev = hlist_lookup(domain)->dev;
    return nErr;
  } else {
    *dev = -1;
    FARF(ERROR,
         "Error 0x%x: Unable to get dev as hlist is NULL for domain %d\n", nErr,
         __func__, domain);
    return nErr;
  }
}

static void fastrpc_apps_user_deinit(void) {
  int i;
  struct handle_list *hl, *tmp;

  FARF(RUNTIME_RPC_HIGH, "%s called\n", __func__);
  if (tlsKey != INVALID_KEY) {
    pthread_key_delete(tlsKey);
    tlsKey = INVALID_KEY;
  }
  fastrpc_clear_handle_list(NON_DOMAIN_HANDLE_LIST_ID, DEFAULT_DOMAIN_ID);
  if (hlist_table_ready) {
    FOR_EACH_EFFECTIVE_DOMAIN_ID(i) {
      fastrpc_clear_handle_list(MULTI_DOMAIN_HANDLE_LIST_ID, i);
      fastrpc_clear_handle_list(REVERSE_HANDLE_LIST_ID, i);
    }
    listener_android_deinit();
    pthread_mutex_lock(&info.mut);
    HASH_ITER(hh, info.tbl, hl, tmp) {
      HASH_DEL(info.tbl, hl);
      hlist_free_node(hl);
    }
    pthread_mutex_unlock(&info.mut);
    pthread_mutex_destroy(&info.mut);
    hlist_table_ready = false;
  }
  fastrpc_domain_status_notif_deinit();
  fastrpc_domain_table_deinit();
  fastrpc_context_table_deinit();
  fastrpc_config_deinit();
  deinit_process_signals();
  fastrpc_notif_deinit();
  apps_mem_table_deinit();
  fastrpc_wake_lock_deinit();
  fastrpc_log_deinit();
  fastrpc_mem_deinit();
  PL_DEINIT(apps_std);
  PL_DEINIT(rpcmem);
  PL_DEINIT(gpls);

  FARF(ALWAYS, "%s done\n", __func__);
  return;
}

static void exit_thread(void *value) {
  remote_handle64 handle = 0;
  int domain;
  int nErr = AEE_SUCCESS;
  struct handle_list *hl = NULL;

  /* The thread-local value is not needed: the loop below visits every
   * session that exists. */
  (void)value;

  if (!hlist_table_ready) {
    FARF(CRITICAL, "%s: Invalid hlist", __func__);
    return;
  }

  FOR_EACH_EFFECTIVE_DOMAIN_ID(domain) {
    hl = hlist_lookup(domain);
    if (hl && hl->dev != -1) {
      if ((handle = get_adsp_current_process1_handle(domain)) !=
          INVALID_HANDLE) {
        nErr = adsp_current_process1_thread_exit(handle);
        if (nErr) {
          FARF(RUNTIME_RPC_HIGH, "%s: nErr:0x%x, dom:%d, h:0x%" PRIx64,
               __func__, nErr, domain, handle);
        }
      } else if (domain == normalize_effec_domain_id(DEFAULT_DOMAIN_ID)) {
        nErr = adsp_current_process_thread_exit();
        if (nErr) {
          FARF(RUNTIME_RPC_HIGH, "%s: nErr:0x%x, dom:%d", __func__, nErr, domain);
        }
      }
    }
  }
  // Set tlsKey to NULL, so that exit_thread won't be called recursively
  pthread_setspecific(tlsKey, (void *)NULL);
}

const char* get_dsp_search_path() {
  int domain = GET_DOMAIN_FROM_EFFEC_DOMAIN_ID(get_current_domain());
  return get_dsp_search_path_for_domain(domain);
}

/*
 * Called only once by fastrpc_init_once
 * Initializes the data structures
 */

static int fastrpc_apps_user_init(void) {
  int nErr = AEE_SUCCESS;

  pthread_mutexattr_init(&hlist_mutexattr);
  pthread_mutexattr_settype(&hlist_mutexattr, PTHREAD_MUTEX_RECURSIVE);

  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(gpls)));
  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(rpcmem)));
  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(apps_std)));
  VERIFY(AEE_SUCCESS == (nErr = pthread_key_create(&tlsKey, exit_thread)));
#ifdef PARSE_YAML
  configure_dsp_paths();
#endif
  fastrpc_mem_init();
  fastrpc_context_table_init();
  fastrpc_log_init();
  fastrpc_config_init();
  pthread_mutex_init(&update_notif_list_mut, 0);
  HASH_TABLE_INIT(struct handle_list);
  hlist_table_ready = true;
  /* Create the legacy entries: sessions 0 and 1 of every domain, i.e.
   * effective ids 0..NUM_DOMAINS_EXTEND-1. Other sessions are created when
   * they are reserved or first used. */
  {
    int d, s;
    for (s = 0; s < NUM_DOMAINS_EXTEND / NUM_DOMAINS; s++) {
      FOR_EACH_DOMAIN_ID(d) {
        VERIFYC(NULL != hlist_get_or_create_session(d, s), AEE_ENOMEMORY);
      }
    }
  }

  /* Build Level-1 domain table by probing device nodes, then create
   * Level-2 session entries for any dynamic domains discovered. */
  fastrpc_domain_table_init();
  fastrpc_domain_status_notif_init();
  fastrpc_discover_domains();
  {
    /* Create 2 sessions (0 and 1) for every dynamic domain actually found
     * in the Level-1 table -- iterated directly instead of a hardcoded
     * instance range, so any family/count discovered at runtime (including
     * ones added after this was written) gets pre-created the same way. */
    int logical_id, s;

    for (logical_id = fastrpc_next_logical_domain(-1); logical_id >= 0;
         logical_id = fastrpc_next_logical_domain(logical_id)) {
      for (s = 0; s < 2; s++) {
        /* hlist_get_or_create_session uses GET_EFFECTIVE_DOMAIN_ID
         * which now handles dynamic IDs correctly */
        VERIFYC(NULL != hlist_get_or_create_session(logical_id, s),
                AEE_ENOMEMORY);
      }
    }
  }

  listener_android_init();
  GenCrc32Tab(POLY32, crc_table);
  fastrpc_notif_init();
  apps_mem_table_init();
bail:
  /*
                   print address of static variable
                   to know duplicate instance of libcdsprpc.so
  */
  if (nErr) {
    FARF(ERROR,
         "Error 0x%x: %s failed. default domain:%x and &fastrpc_trace:%p \n",
         nErr, __func__, DEFAULT_DOMAIN_ID, &fastrpc_trace);
    fastrpc_apps_user_deinit();
  } else {
    FARF(ALWAYS, "%s done. default domain:%x and &fastrpc_trace:%p\n", __func__,
         DEFAULT_DOMAIN_ID, &fastrpc_trace);
  }
  return nErr;
}

PL_DEFINE(fastrpc_apps_user, fastrpc_apps_user_init, fastrpc_apps_user_deinit);

static void frpc_init(void) { PL_INIT(fastrpc_apps_user); }

int fastrpc_init_once(void) {
  static pthread_once_t frpc = PTHREAD_ONCE_INIT;
  int nErr = AEE_SUCCESS;
  VERIFY(AEE_SUCCESS == (nErr = pthread_once(&frpc, (void *)frpc_init)));
bail:
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error %x: fastrpc init once failed\n", nErr);
  }
  return nErr == AEE_SUCCESS ? _pl_fastrpc_apps_user()->nErr : nErr;
}

static int rpcmem_init_me(void) {
  rpcmem_init_internal();
  return AEE_SUCCESS;
}

static void rpcmem_deinit_me(void) {
  rpcmem_deinit_internal();
  return;
}

/*
 * check_multilib_util() - Debug utility to terminate the process when multiple
 * instances of dsp library linked to the process by reading process env
 *                         variable values.
 * Return : void.
 */

static void check_multilib_util(void) {
  int nErr = 0, ret = -1, ii = 0;
  const char *env_name = fastrpc_dsp_lib_refcnt[DEFAULT_DOMAIN_ID];

  /* Set env variable of default domain id to 1. */
  ret = setenv(env_name, "1", 1);
  if (ret != 0 && errno == ENOMEM) {
    nErr = ERRNO;
    FARF(ERROR, "Error 0x%x: setenv failed for %s (domain %u), errno is %s\n",
         nErr, env_name, DEFAULT_DOMAIN_ID, strerror(ERRNO));
    /*
     * If failed to set env variable then free the memory allocated to
     * env variables and exit the application.
     */
    deinit_fastrpc_dsp_lib_refcnt();
    exit(EXIT_FAILURE);
  } else {
    total_dsp_lib_refcnt = 1;
  }

  /* Get the values of all env variables and increment the refcount accordingly.
   */
  FOR_EACH_DOMAIN_ID(ii) {
    char *env_val = NULL;
    env_name = fastrpc_dsp_lib_refcnt[ii];

    /* Skip the check to get default domain env variable value as we already set
     * its value. */
    if (ii == DEFAULT_DOMAIN_ID)
      continue;

    env_val = getenv(env_name);
    if (NULL != env_val) {
      /*
       * Add env value to total reference count to check
       * the total number of dsp library instances loaded.
       */
      total_dsp_lib_refcnt += atoi(env_val);

      /* If the total reference count exceeds one then show warning, application
       * will abort on first handle open. */
      if (total_dsp_lib_refcnt > MAX_LIB_INSTANCE_ALLOWED) {
        FARF(ERROR,
             "Warning: %s %d instances of libxdsprpc (already loaded %s). Only "
             "%d allowed\n",
             fastrpc_library[DEFAULT_DOMAIN_ID], total_dsp_lib_refcnt,
             fastrpc_library[ii], MAX_LIB_INSTANCE_ALLOWED);
      }
    }
  }
}

__CONSTRUCTOR_ATTRIBUTE__
static void multidsplib_env_init(void) {
  const char *local_fastrpc_lib_refcnt[NUM_DOMAINS] = {
      "FASTRPC_ADSP_REFCNT", "FASTRPC_MDSP_REFCNT", "FASTRPC_SDSP_REFCNT",
      "FASTRPC_CDSP_REFCNT", "FASTRPC_CDSP1_REFCNT", "FASTRPC_GDSP0_REFCNT", "FASTRPC_GDSP1_REFCNT"};
  char buf[64] = {0};
  size_t env_name_len = 0;
  char *env_name = NULL;
  int ii = 0;

  pid_t pid = getpid();

  /* Initialize all global array with env variable names along with process id.
   */
  FOR_EACH_DOMAIN_ID(ii) {
    snprintf(buf, sizeof(buf), "%s_%d", local_fastrpc_lib_refcnt[ii], pid);
    env_name_len = (sizeof(char) * strlen(buf)) + 1;
    env_name = malloc(env_name_len);
    if (env_name) {
      strlcpy(env_name, buf, env_name_len);
      fastrpc_dsp_lib_refcnt[ii] = env_name;
    } else {
      FARF(ERROR,
           "Error %d: %s: env variable allocation for %zu bytes failed, domain "
           "%d\n",
           ENOMEM, __func__, env_name_len, ii);
      deinit_fastrpc_dsp_lib_refcnt();
      exit(EXIT_FAILURE);
    }
  }
  check_multilib_util();
  FARF(ALWAYS, "%s: %s loaded", __func__, fastrpc_library[DEFAULT_DOMAIN_ID]);
}

PL_DEFINE(rpcmem, rpcmem_init_me, rpcmem_deinit_me);
