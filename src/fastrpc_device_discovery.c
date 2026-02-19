// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "fastrpc_device_discovery.h"
#include "fastrpc_internal.h"
#include "fastrpc_ioctl_drm.h"
#include "HAP_farf.h"
#include "AEEStdErr.h"


// Device mapping entry
struct device_mapping {
    int domain_id;
    char device_path[64];
    char dsp_type_name[16];
    bool valid;
    atomic_uint_fast64_t access_count;
    struct timespec last_discovery;
};

// Discovery state
struct discovery_state {
    struct device_mapping devices[NUM_DOMAINS_EXTEND];
    pthread_mutex_t mutex;
    pthread_t inotify_thread;
    int inotify_fd;
    int inotify_wd;
    atomic_bool initialized;
    atomic_bool running;
    struct timespec init_time;
};

static struct discovery_state g_discovery = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .inotify_fd = -1,
    .inotify_wd = -1,
    .initialized = false,
    .running = false,
};

// Forward declarations
static int discover_devices_locked(void);
static void *inotify_monitor_thread(void *arg);

/**
 * Map DSP type name to domain ID
 */
static int dsp_type_name_to_domain(const char *dsp_name) {
    if (!dsp_name)
        return -1;
    
    if (strcmp(dsp_name, "adsp") == 0)
        return ADSP_DOMAIN_ID;
    else if (strcmp(dsp_name, "cdsp") == 0)
        return CDSP_DOMAIN_ID;
    else if (strcmp(dsp_name, "cdsp1") == 0)
        return CDSP1_DOMAIN_ID;
    else if (strcmp(dsp_name, "mdsp") == 0)
        return MDSP_DOMAIN_ID;
    else if (strcmp(dsp_name, "sdsp") == 0)
        return SDSP_DOMAIN_ID;
    else if (strcmp(dsp_name, "gdsp0") == 0)
        return GDSP0_DOMAIN_ID;
    else if (strcmp(dsp_name, "gdsp1") == 0)
        return GDSP1_DOMAIN_ID;
    
    return -1;
}

/**
 * Query device type via IOCTL
 */
static int query_device_type(const char *dev_path, char *dsp_type_name, size_t name_len) {
    int fd, ret;
    struct drm_qda_query info = {0};
    
    fd = open(dev_path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        FARF(RUNTIME_RPC_HIGH, "Failed to open %s: %s", dev_path, strerror(errno));
        return -1;
    }
    
    ret = ioctl(fd, DRM_IOCTL_QDA_QUERY, &info);
    close(fd);
    
    if (ret < 0) {
        FARF(ERROR, "Failed to query device info for %s: %s", 
             dev_path, strerror(errno));
        return AEE_ERPC;
    }
    
    // Kernel provides DSP name directly as a string
    strlcpy(dsp_type_name, (const char *)info.dsp_name, name_len);
    
    FARF(RUNTIME_RPC_HIGH, "Device %s: name=%s caps=0x%x", 
         dev_path, dsp_type_name, info.capabilities);
    
    return AEE_SUCCESS;
}

/**
 * Discover all DSP devices (must be called with mutex held)
 */
static int discover_devices_locked(void) {
    DIR *dir;
    struct dirent *entry;
    int discovered = 0;
    struct timespec now;
    
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    FARF(ALWAYS, "Discovering DSP devices in /dev/accel/");
    
    // Clear old mappings
    for (int i = 0; i < NUM_DOMAINS_EXTEND; i++) {
        g_discovery.devices[i].valid = false;
    }
    
    dir = opendir("/dev/accel");
    if (!dir) {
        FARF(ERROR, "Failed to open /dev/accel: %s", strerror(errno));
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "accel", 5) == 0) {
            char dev_path[64];
            char dsp_type_name[16];
            int domain;
            
            snprintf(dev_path, sizeof(dev_path), "/dev/accel/%s", entry->d_name);
            
            if (query_device_type(dev_path, dsp_type_name, sizeof(dsp_type_name)) == AEE_SUCCESS) {
                domain = dsp_type_name_to_domain(dsp_type_name);
                
                if (domain >= 0 && domain < NUM_DOMAINS_EXTEND) {
                    strlcpy(g_discovery.devices[domain].device_path, dev_path,
                           sizeof(g_discovery.devices[domain].device_path));
                    strlcpy(g_discovery.devices[domain].dsp_type_name, dsp_type_name,
                           sizeof(g_discovery.devices[domain].dsp_type_name));
                    g_discovery.devices[domain].valid = true;
                    g_discovery.devices[domain].domain_id = domain;
                    g_discovery.devices[domain].last_discovery = now;
                    
                    discovered++;
                    FARF(ALWAYS, "Mapped %s -> domain %d (%s)", 
                         dev_path, domain, dsp_type_name);
                }
            }
        }
    }
    
    closedir(dir);
    FARF(ALWAYS, "Discovery complete: %d devices mapped", discovered);
    return AEE_SUCCESS;
}

/**
 * Inotify monitoring thread
 */
static void *inotify_monitor_thread(void *arg) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    ssize_t len;
    char *ptr;
    struct pollfd pfd;
    
    FARF(ALWAYS, "Device discovery: inotify monitor thread started");
    
    pfd.fd = g_discovery.inotify_fd;
    pfd.events = POLLIN;
    
    while (atomic_load(&g_discovery.running)) {
        int ret = poll(&pfd, 1, 1000);  // 1 second timeout
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            FARF(ERROR, "poll() failed: %s", strerror(errno));
            break;
        }
        
        if (ret == 0) continue;  // Timeout
        
        len = read(g_discovery.inotify_fd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR) continue;
            FARF(ERROR, "read() from inotify fd failed: %s", strerror(errno));
            break;
        }
        
        // Process events
        for (ptr = buf; ptr < buf + len;
             ptr += sizeof(struct inotify_event) + event->len) {
            
            event = (const struct inotify_event *)ptr;
            
            if (event->len && strncmp(event->name, "accel", 5) == 0) {
                if (event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM)) {
                    FARF(ALWAYS, "Device change detected: %s (mask 0x%x)", 
                         event->name, event->mask);
                    
                    // Rediscover devices
                    pthread_mutex_lock(&g_discovery.mutex);
                    discover_devices_locked();
                    pthread_mutex_unlock(&g_discovery.mutex);
                    
                    // Debounce: wait for rapid changes to settle
                    usleep(100000);  // 100ms
                }
            }
        }
    }
    
    FARF(ALWAYS, "Device discovery: inotify monitor thread exiting");
    return NULL;
}

/**
 * Initialize inotify monitoring
 */
static int init_inotify_monitor(void) {
    if (g_discovery.inotify_fd >= 0) {
        return AEE_SUCCESS;  // Already initialized
    }
    
    g_discovery.inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (g_discovery.inotify_fd < 0) {
        FARF(ERROR, "inotify_init1() failed: %s", strerror(errno));
        return AEE_ERPC;
    }
    
    g_discovery.inotify_wd = inotify_add_watch(
        g_discovery.inotify_fd, "/dev/accel",
        IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
    
    if (g_discovery.inotify_wd < 0) {
        FARF(ERROR, "inotify_add_watch() failed: %s", strerror(errno));
        close(g_discovery.inotify_fd);
        g_discovery.inotify_fd = -1;
        return AEE_ERPC;
    }
    
    FARF(ALWAYS, "Inotify watch added for /dev/accel (wd=%d)", 
         g_discovery.inotify_wd);
    
    atomic_store(&g_discovery.running, true);
    
    if (pthread_create(&g_discovery.inotify_thread, NULL, 
                      inotify_monitor_thread, NULL) != 0) {
        FARF(ERROR, "Failed to create inotify thread: %s", strerror(errno));
        close(g_discovery.inotify_fd);
        g_discovery.inotify_fd = -1;
        atomic_store(&g_discovery.running, false);
        return AEE_ERPC;
    }
    
    pthread_setname_np(g_discovery.inotify_thread, "frpc-discovery");
    
    return AEE_SUCCESS;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int fastrpc_discovery_init(void) {
    int nErr = AEE_SUCCESS;
    
    if (atomic_load(&g_discovery.initialized)) {
        return AEE_SUCCESS;  // Already initialized
    }
    
    pthread_mutex_lock(&g_discovery.mutex);
    
    if (atomic_load(&g_discovery.initialized)) {
        pthread_mutex_unlock(&g_discovery.mutex);
        return AEE_SUCCESS;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &g_discovery.init_time);
    
    // Initial discovery
    nErr = discover_devices_locked();
    if (nErr != AEE_SUCCESS) {
        pthread_mutex_unlock(&g_discovery.mutex);
        return nErr;
    }
    
    // Start inotify monitoring
    nErr = init_inotify_monitor();
    if (nErr != AEE_SUCCESS) {
        FARF(ERROR, "Warning: inotify monitoring failed, SSR detection disabled");
        // Continue anyway - discovery still works
    }
    
    atomic_store(&g_discovery.initialized, true);
    pthread_mutex_unlock(&g_discovery.mutex);
    
    FARF(ALWAYS, "Device discovery initialized successfully");
    return AEE_SUCCESS;
}

void fastrpc_discovery_deinit(void) {
    if (!atomic_load(&g_discovery.initialized)) {
        return;
    }
    
    FARF(ALWAYS, "Shutting down device discovery");
    
    // Stop inotify thread
    if (atomic_load(&g_discovery.running)) {
        atomic_store(&g_discovery.running, false);
        pthread_join(g_discovery.inotify_thread, NULL);
    }
    
    // Cleanup inotify
    if (g_discovery.inotify_wd >= 0) {
        inotify_rm_watch(g_discovery.inotify_fd, g_discovery.inotify_wd);
        g_discovery.inotify_wd = -1;
    }
    
    if (g_discovery.inotify_fd >= 0) {
        close(g_discovery.inotify_fd);
        g_discovery.inotify_fd = -1;
    }
    
    atomic_store(&g_discovery.initialized, false);
}

int fastrpc_discovery_get_device_path(int domain_id, char *dev_path, 
                                      size_t path_len) {
    int nErr = AEE_SUCCESS;
    
    if (!dev_path || path_len < 64) {
        return AEE_EBADPARM;
    }
    
    if (domain_id < 0 || domain_id >= NUM_DOMAINS_EXTEND) {
        return AEE_EBADPARM;
    }
    
    // Lazy initialization
    if (!atomic_load(&g_discovery.initialized)) {
        nErr = fastrpc_discovery_init();
        if (nErr != AEE_SUCCESS) {
            return nErr;
        }
    }
    
    pthread_mutex_lock(&g_discovery.mutex);
    
    if (g_discovery.devices[domain_id].valid) {
        strlcpy(dev_path, g_discovery.devices[domain_id].device_path, path_len);
        atomic_fetch_add(&g_discovery.devices[domain_id].access_count, 1);
        nErr = AEE_SUCCESS;
    } else {
        nErr = -1;
    }
    
    pthread_mutex_unlock(&g_discovery.mutex);
    
    return nErr;
}

bool fastrpc_discovery_is_device_available(int domain_id) {
    if (domain_id < 0 || domain_id >= NUM_DOMAINS_EXTEND) {
        return false;
    }
    
    if (!atomic_load(&g_discovery.initialized)) {
        fastrpc_discovery_init();
    }
    
    return g_discovery.devices[domain_id].valid;
}

int fastrpc_discovery_refresh(void) {
    int nErr;
    
    pthread_mutex_lock(&g_discovery.mutex);
    nErr = discover_devices_locked();
    pthread_mutex_unlock(&g_discovery.mutex);
    
    return nErr;
}

int fastrpc_discovery_get_stats(int domain_id, uint64_t *access_count,
                                struct timespec *last_discovery) {
    if (domain_id < 0 || domain_id >= NUM_DOMAINS_EXTEND) {
        return AEE_EBADPARM;
    }
    
    if (access_count) {
        *access_count = atomic_load(&g_discovery.devices[domain_id].access_count);
    }
    
    if (last_discovery) {
        *last_discovery = g_discovery.devices[domain_id].last_discovery;
    }
    
    return AEE_SUCCESS;
}
