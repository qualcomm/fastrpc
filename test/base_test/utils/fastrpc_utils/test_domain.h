// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef TEST_DOMAIN_H
#define TEST_DOMAIN_H

#include <stddef.h>

#ifndef NUM_DOMAINS
#define NUM_DOMAINS 8
#endif

/* Keep the base-test domain IDs local to the domain utility. These values
 * match the FastRPC domain IDs without depending on remote.h for them. */
typedef enum {
    TEST_DOMAIN_ADSP_ID = 0,
    TEST_DOMAIN_MDSP_ID = 1,
    TEST_DOMAIN_SDSP_ID = 2,
    TEST_DOMAIN_CDSP_ID = 3,
    TEST_DOMAIN_CDSP1_ID = 4,
    TEST_DOMAIN_GDSP0_ID = 5,
    TEST_DOMAIN_GDSP1_ID = 6,
} test_domain_id_t;

#ifndef TEST_DOMAIN_DEFAULT_ID
#define TEST_DOMAIN_DEFAULT_ID TEST_DOMAIN_CDSP_ID
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Returns non-zero when domain_id is a supported FastRPC domain. */
int test_domain_is_supported(int domain_id);

/* Returns the number of supported domains used for explicit-domain capacity. */
int test_domain_count(void);

/* Discovers running domains with usable FastRPC device nodes. */
int test_domain_discover(int domains[NUM_DOMAINS]);

/* Domain URI and name helpers retained under their existing public names. */
int test_utils_domain_uri(char *buf, size_t buflen);
int test_utils_domain_uri_for(int domain_id, char *buf, size_t buflen);
const char *test_utils_domain_name(void);
const char *test_utils_domain_name_for(int domain_id);

#ifdef __cplusplus
}
#endif

#endif /* TEST_DOMAIN_H */
