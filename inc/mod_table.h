// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef MOD_TABLE_H
#define MOD_TABLE_H

#include "AEEStdDef.h"
#include "remote64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * multi-domain support
 *
 * multi domain modules return remote_handl64 on open/close, but mod_table
 * creates uint32_t handles as the "remote" facing handle.  These fit nicely
 * into the transport layer.
 *
 */

/**
 * register a static component for invocations
 * this can be called at any time including from a static constructor
 *
 * name, name of the interface to register
 * pfn, function pointer to the skel invoke function
 *
 * for example:
 *   __attribute__((constructor)) static void my_module_ctor(void) {
 *      mod_table_register_static("my_module", my_module_skel_invoke);
 *   }
 *
 */
int mod_table_register_static(const char *name,
                              int (*pfn)(uint32_t sc, remote_arg *pra));

/**
 * same as register_static, but module with user defined handle lifetimes.
 */
int mod_table_register_static1(const char *uri,
                               int (*pfn)(remote_handle64 h, uint32_t sc,
                                          remote_arg *pra));

/**
 * register a static component for invocations
 * this can be called at any time including from a static constructor
 *
 * overrides will be tried first, then dynamic modules, then regular
 * static modules.  This api should only be use by system components
 * that will never be upgradable.
 *
 * name, name of the interface to register
 * pfn, function pointer to the skel invoke function
 *
 * for example:
 *   __attribute__((constructor)) static void my_module_ctor(void) {
 *      mod_table_register_static("my_module", my_module_skel_invoke);
 *   }
 *
 */
int mod_table_register_static_override(const char *name,
                                       int (*pfn)(uint32_t sc,
                                                  remote_arg *pra));

/**
 * same as register_static, but module with user defined handle lifetimes.
 */
int mod_table_register_static_override1(const char *uri,
                                        int (*pfn)(remote_handle64,
                                                   uint32_t sc,
                                                   remote_arg *pra));

/**
 * Open a module and get a handle to it
 *
 * in_name, name of module to open
 * handle, Output handle
 * dlerr, Error String (if an error occurs)
 * dlerrorLen, Length of error String (if an error occurs)
 * pdlErr, Error identifier
 */
int mod_table_open(const char *in_name, remote_handle *handle, char *dlerr,
                   int dlerrorLen, int *pdlErr);

/**
 * invoke a handle in the mod table
 *
 * handle, handle to invoke
 * sc, scalars, see remote.h for documentation.
 * pra, args, see remote.h for documentation.
 */
int mod_table_invoke(remote_handle handle, uint32_t sc, remote_arg *pra);

/**
 * Closes a handle in the mod table
 *
 * handle, handle to close
 * errStr, Error String (if an error occurs)
 * errStrLen, Length of error String (if an error occurs)
 * pdlErr, Error identifier
 */
int mod_table_close(remote_handle handle, char *errStr, int errStrLen,
                    int *pdlErr);

/**
 * internal use only
 */
int mod_table_register_const_handle(remote_handle handle, const char *in_name,
                                    int (*pfn)(uint32_t sc, remote_arg *pra));
/**
 * @param remote, the handle we should expect from the transport layer
 * @param local, the local handle that will be passed to pfn
 */
int mod_table_register_const_handle1(remote_handle remote,
                                     remote_handle64 local, const char *uri,
                                     int (*pfn)(remote_handle64 h, uint32_t sc,
                                                remote_arg *pra));

#ifdef __cplusplus
}
#endif

#endif // MOD_TABLE_H
