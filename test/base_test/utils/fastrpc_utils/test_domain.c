// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "fastrpc_base_test.h"
#include "remote.h"
#include "test_config.h"
#include "test_domain.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define REMOTEPROC_CLASS_PATH "/sys/class/remoteproc"

typedef struct {
    int domain_id;
    const char *domain_name;
    const char *uri_suffix;
    const char *remoteproc_names[2];
    const char *devnodes[2];
} domain_probe_t;

/* Keep all domain identity and discovery information in one table. */
static const domain_probe_t domain_probes[] = {
    { TEST_DOMAIN_ADSP_ID, ADSP_DOMAIN_NAME, ADSP_DOMAIN,
      { "adsp", NULL },
      { "/dev/fastrpc-adsp", "/dev/fastrpc-adsp-secure" } },
    { TEST_DOMAIN_MDSP_ID, MDSP_DOMAIN_NAME, MDSP_DOMAIN,
      { "mdsp", NULL },
      { "/dev/fastrpc-mdsp", "/dev/fastrpc-mdsp-secure" } },
    { TEST_DOMAIN_SDSP_ID, SDSP_DOMAIN_NAME, SDSP_DOMAIN,
      { "sdsp", "slpi" },
      { "/dev/fastrpc-sdsp", "/dev/fastrpc-sdsp-secure" } },
    { TEST_DOMAIN_CDSP_ID, CDSP_DOMAIN_NAME, CDSP_DOMAIN,
      { "cdsp", NULL },
      { "/dev/fastrpc-cdsp", "/dev/fastrpc-cdsp-secure" } },
    { TEST_DOMAIN_CDSP1_ID, CDSP1_DOMAIN_NAME, CDSP1_DOMAIN,
      { "cdsp1", NULL },
      { "/dev/fastrpc-cdsp1", "/dev/fastrpc-cdsp1-secure" } },
    { TEST_DOMAIN_GDSP0_ID, GDSP0_DOMAIN_NAME, GDSP0_DOMAIN,
      { "gpdsp0", "gdsp0" },
      { "/dev/fastrpc-gdsp0", "/dev/fastrpc-gdsp0-secure" } },
    { TEST_DOMAIN_GDSP1_ID, GDSP1_DOMAIN_NAME, GDSP1_DOMAIN,
      { "gpdsp1", "gdsp1" },
      { "/dev/fastrpc-gdsp1", "/dev/fastrpc-gdsp1-secure" } },
};

#define DOMAIN_PROBE_COUNT (sizeof(domain_probes) / sizeof(domain_probes[0]))
#define DOMAIN_ALIAS_COUNT \
    (sizeof(domain_probes[0].remoteproc_names) / sizeof(domain_probes[0].remoteproc_names[0]))
#define DOMAIN_DEVNODE_COUNT \
    (sizeof(domain_probes[0].devnodes) / sizeof(domain_probes[0].devnodes[0]))

static const domain_probe_t *find_domain_probe(int domain_id)
{
    for (size_t i = 0; i < DOMAIN_PROBE_COUNT; i++) {
        if (domain_probes[i].domain_id == domain_id)
            return &domain_probes[i];
    }

    return NULL;
}

static int domain_probe_index(int domain_id)
{
    for (size_t i = 0; i < DOMAIN_PROBE_COUNT; i++) {
        if (domain_probes[i].domain_id == domain_id)
            return (int)i;
    }

    return -1;
}

int test_domain_is_supported(int domain_id)
{
    return domain_probe_index(domain_id) >= 0;
}

int test_domain_count(void)
{
    return (int)DOMAIN_PROBE_COUNT;
}

_Static_assert(DOMAIN_PROBE_COUNT <= NUM_DOMAINS,
               "domain_ids[] cannot hold every supported domain");

static int read_text_file(const char *path, char *buf, size_t buflen)
{
    FILE *file;
    size_t length;
    int next;

    if (!path || !buf || buflen < 2)
        return -1;

    file = fopen(path, "r");
    if (!file)
        return -1;

    length = fread(buf, 1, buflen - 1, file);
    if (ferror(file)) {
        fclose(file);
        return -1;
    }

    next = fgetc(file);
    fclose(file);
    if (next != EOF)
        return -1;

    buf[length] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static int remoteproc_name_matches(const char *name, const char *key)
{
    return name != NULL && key != NULL && strcmp(name, key) == 0;
}

static int domain_has_devnode(const domain_probe_t *probe)
{
    struct stat st;

    for (size_t i = 0; i < DOMAIN_DEVNODE_COUNT; i++) {
        if (probe->devnodes[i] != NULL && stat(probe->devnodes[i], &st) == 0 && S_ISCHR(st.st_mode))
            return 1;
    }

    return 0;
}

int test_domain_discover(int domains[NUM_DOMAINS])
{
    int running[DOMAIN_PROBE_COUNT] = { 0 };
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    dir = opendir(REMOTEPROC_CLASS_PATH);
    if (!dir) {
        fprintf(stderr, "[test_config] unable to scan %s: %s\n",
                REMOTEPROC_CLASS_PATH, strerror(errno));
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        char name[128];
        char state[64];

        if (strncmp(entry->d_name, "remoteproc", strlen("remoteproc")) != 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s/name", REMOTEPROC_CLASS_PATH, entry->d_name);
        if (read_text_file(path, name, sizeof(name)) != 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s/state", REMOTEPROC_CLASS_PATH, entry->d_name);
        if (read_text_file(path, state, sizeof(state)) != 0)
            continue;

        for (char *p = name; *p; p++)
            *p = (char)tolower((unsigned char)*p);
        for (char *p = state; *p; p++)
            *p = (char)tolower((unsigned char)*p);

        if (strcmp(state, "running") != 0)
            continue;

        for (size_t i = 0; i < DOMAIN_PROBE_COUNT; i++) {
            for (size_t j = 0; j < DOMAIN_ALIAS_COUNT &&
                            domain_probes[i].remoteproc_names[j]; j++) {
                if (remoteproc_name_matches(name, domain_probes[i].remoteproc_names[j])) {
                    running[i] = 1;
                    break;
                }
            }
        }
    }

    closedir(dir);

    for (size_t i = 0; i < DOMAIN_PROBE_COUNT; i++) {
        if (running[i] && domain_has_devnode(&domain_probes[i]))
            domains[count++] = domain_probes[i].domain_id;
    }

    return count;
}

int test_utils_domain_uri_for(int domain_id, char *buf, size_t buflen)
{
    const domain_probe_t *probe = find_domain_probe(domain_id);
    int written;

    if (buf == NULL || buflen == 0)
        return -1;

    buf[0] = '\0';
    if (probe == NULL)
        return -1;

    written = snprintf(buf, buflen, "%s%s", fastrpc_base_test_URI, probe->uri_suffix);
    if (written < 0 || (size_t)written >= buflen) {
        buf[0] = '\0';
        return -1;
    }

    return 0;
}

int test_utils_domain_uri(char *buf, size_t buflen)
{
    return test_utils_domain_uri_for(g_test_config.domain_id, buf, buflen);
}

const char *test_utils_domain_name_for(int domain_id)
{
    const domain_probe_t *probe = find_domain_probe(domain_id);

    return probe != NULL ? probe->domain_name : NULL;
}

const char *test_utils_domain_name(void)
{
    return test_utils_domain_name_for(g_test_config.domain_id);
}
