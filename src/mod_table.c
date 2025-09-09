// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FARF_ERROR
#define FARF_ERROR 1
#endif
#define FARF_LOW 1

#ifndef VERIFY_PRINT_ERROR_ALWAYS
#define VERIFY_PRINT_ERROR_ALWAYS
#endif // VERIFY_PRINT_ERROR_ALWAYS

#include "mod_table.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "HAP_pls.h"
#include "fastrpc_trace.h"
#include "mutex.h"
#include "platform_libs.h"
#include "remote64.h"
#include "sbuf_parser.h"
#include "uthash.h"
#include "verify.h"
#include <assert.h>
#include <errno.h>

/*
 * Handle size needed is about ~250 bytes
 * Allocating 256 bytes per handle to avoid misalignment memory access error
 * and for cache alignment performance.
 */
#define MAX_REV_HANDLES 20
#define REV_HANDLE_SIZE 256
static uint8_t rev_handle_table[MAX_REV_HANDLES][REV_HANDLE_SIZE];
RW_MUTEX_T rev_handle_table_lock;
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#ifdef WINNT
#include <Windows.h>
#include <assert.h>
#include <winbase.h>

#define RTLD_NOW 0

static __inline HMODULE DLOPEN(LPCSTR name, int mode) {
  UNREFERENCED_PARAMETER(mode);
  return LoadLibraryExA(name, 0, 0);
}

static __inline FARPROC DLSYM(LPVOID handle, LPCSTR name) {
  return GetProcAddress((HMODULE)handle, name);
}

static __inline int DLCLOSE(LPVOID handle) {
  int nErr = AEE_SUCCESS;

  VERIFYC(0 < FreeLibrary((HMODULE)handle), AEE_EBADPARM);

bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: dlclose failed for %x\n", nErr, handle);
  }
  return nErr;
}

static __inline const char *DLERROR(VOID) {
  static const char errMsg[] = "dl Error";
  return errMsg;
}
#else
#include <dlfcn.h>

#define DLOPEN dlopen
#define DLCLOSE dlclose
#define DLSYM dlsym
#define DLERROR dlerror
#endif

extern int errno;

/**
 * structure for the mod table
 *
 * you need to define a rw_mutex type and its read/write lock/unlock api's
 * which are under the RW_MUTEX namespace.
 *
 * this library defines 2 functions for opening modules, open_static and
 * open_dynamic.  Both return a handle that should be closed via close.
 *
 * you can also register a const handle, an invoke function for a known handle
 * value.  since handle keys are allocated, you should pick handle values that
 * are not going to be returned by malloc (0, or odd).
 */
struct static_mod_table {
  RW_MUTEX_T mut;
  struct static_mod *staticModOverrides;
  struct static_mod *staticMods;
  struct const_mod *constMods;
  bool bInit;
};

struct open_mod_table {
  RW_MUTEX_T mut;
  struct open_mod *openMods;
  struct static_mod_table *smt;
};

typedef int (*invoke_fn)(uint32_t, remote_arg *);
typedef int (*handle_invoke_fn)(remote_handle64, uint32_t, remote_arg *);
struct static_mod {
  invoke_fn invoke;
  handle_invoke_fn handle_invoke;
  UT_hash_handle hh;
  char uri[1];
};

struct const_mod {
  invoke_fn invoke;
  handle_invoke_fn handle_invoke;
  uint32_t key;
  remote_handle64 h64;
  UT_hash_handle hh;
  char uri[1];
};

struct parsed_uri {
  const char *file;
  const char *sym;
  const char *ver;
  int filelen;
  int symlen;
  int verlen;
};

struct open_mod {
  void *dlhandle;
  invoke_fn invoke;
  handle_invoke_fn handle_invoke;
  uint64_t key;
  UT_hash_handle hh;
  remote_handle64 h64;
  struct parsed_uri vals;
  int refs;
  char uri[1];
};

static int static_mod_table_ctor(struct static_mod_table *me) {
  if (me->bInit == 0) {
    RW_MUTEX_CTOR(me->mut);
    RW_MUTEX_CTOR(rev_handle_table_lock);
    me->staticMods = 0;
    me->staticModOverrides = 0;
    me->bInit = 1;
  }
  return 0;
}

static void static_mod_table_dtor_imp(struct static_mod_table *me) {
  struct static_mod *sm, *stmp;
  struct const_mod *dm, *ftmp;
  if (me->bInit != 0) {
    if (me->staticMods || me->constMods || me->staticModOverrides) {
      RW_MUTEX_LOCK_WRITE(me->mut);
      HASH_ITER(hh, me->staticMods, sm, stmp) {
        if (me->staticMods) {
          HASH_DEL(me->staticMods, sm);
        }
        free(sm);
        sm = NULL;
      }
      HASH_ITER(hh, me->staticModOverrides, sm, stmp) {
        if (me->staticModOverrides) {
          HASH_DEL(me->staticModOverrides, sm);
        }
        free(sm);
        sm = NULL;
      }
      HASH_ITER(hh, me->constMods, dm, ftmp) {
        if (me->constMods) {
          HASH_DEL(me->constMods, dm);
        }
        free(dm);
        dm = NULL;
      }
      RW_MUTEX_UNLOCK_WRITE(me->mut);
    }
    RW_MUTEX_DTOR(me->mut);
    RW_MUTEX_DTOR(rev_handle_table_lock);
    me->staticMods = 0;
    me->staticModOverrides = 0;
    me->bInit = 0;
  }
}

static int open_mod_table_ctor_imp(void *ctx, void *data) {
  struct open_mod_table *me = (struct open_mod_table *)data;
  RW_MUTEX_CTOR(me->mut);
  me->openMods = 0;
  me->smt = (struct static_mod_table *)ctx;
  return 0;
}

static int open_mod_handle_close(struct open_mod *mod, remote_handle64 h);

static void open_mod_table_dtor_imp(void *data) {
  struct open_mod_table *me = (struct open_mod_table *)data;
  struct open_mod *dm, *ftmp;
  if (me->openMods) {
    RW_MUTEX_LOCK_WRITE(me->mut);
    HASH_ITER(hh, me->openMods, dm, ftmp) {
      if (me->openMods) {
        HASH_DEL(me->openMods, dm);
      }
      if (dm->h64) {
        (void)open_mod_handle_close(dm, dm->h64);
      }
      if (dm->dlhandle) {
        DLCLOSE(dm->dlhandle);
      }
      FARF(RUNTIME_RPC_HIGH, "%s: closed reverse module %s with handle 0x%x", __func__,
           dm->uri, (uint32_t)dm->key);
      dm->key = 0;
    }
    RW_MUTEX_UNLOCK_WRITE(me->mut);
  }
  RW_MUTEX_DTOR(me->mut);
  me->openMods = 0;
}
static int open_mod_table_open_from_static(struct open_mod_table *me,
                                           struct static_mod **tbl,
                                           const char *uri,
                                           remote_handle *handle);

static int open_mod_table_open_static_override(struct open_mod_table *me,
                                               const char *uri,
                                               remote_handle *handle) {
  FARF(RUNTIME_RPC_HIGH, "open_mod_table_open_static_override");
  return open_mod_table_open_from_static(me, &me->smt->staticModOverrides, uri,
                                         handle);
}

static int open_mod_table_open_static(struct open_mod_table *me,
                                      const char *uri, remote_handle *handle) {
  FARF(RUNTIME_RPC_HIGH, "open_mod_table_open_static");
  return open_mod_table_open_from_static(me, &me->smt->staticMods, uri, handle);
}

static int static_mod_add(struct static_mod_table *me, struct static_mod **tbl,
                          const char *uri,
                          int (*invoke)(uint32_t sc, remote_arg *pra),
                          int (*handle_invoke)(remote_handle64, uint32_t sc,
                                               remote_arg *pra)) {
  int nErr = AEE_SUCCESS;
  struct static_mod *sm = 0;
  int len = strlen(uri) + 1;
  VERIFYC(NULL != (sm = ((struct static_mod *)calloc(
                       1, sizeof(struct static_mod) + len))),
          AEE_ENOMEMORY);
  strlcpy(sm->uri, uri, len);
  sm->invoke = invoke;
  sm->handle_invoke = handle_invoke;
  RW_MUTEX_LOCK_WRITE(me->mut);
  HASH_ADD_STR(*tbl, uri, sm);
  RW_MUTEX_UNLOCK_WRITE(me->mut);
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: static module addition failed\n", nErr);
    if (sm) {
      free(sm);
      sm = NULL;
    }
  }
  return nErr;
}

static int static_mod_table_register_static_override(
    struct static_mod_table *me, const char *uri,
    int (*pfn)(uint32_t sc, remote_arg *pra)) {
  return static_mod_add(me, &me->staticModOverrides, uri, pfn, 0);
}
static int static_mod_table_register_static_override1(
    struct static_mod_table *me, const char *uri,
    int (*pfn)(remote_handle64, uint32_t sc, remote_arg *pra)) {
  return static_mod_add(me, &me->staticModOverrides, uri, 0, pfn);
}
static int
static_mod_table_register_static(struct static_mod_table *me, const char *uri,
                                 int (*pfn)(uint32_t sc, remote_arg *pra)) {
  return static_mod_add(me, &me->staticMods, uri, pfn, 0);
}
static int static_mod_table_register_static1(
    struct static_mod_table *me, const char *uri,
    int (*pfn)(remote_handle64, uint32_t sc, remote_arg *pra)) {
  return static_mod_add(me, &me->staticMods, uri, 0, pfn);
}

static int static_mod_table_register_const_handle(
    struct static_mod_table *me, remote_handle local, remote_handle64 remote,
    const char *uri, int (*invoke)(uint32_t sc, remote_arg *pra),
    int (*handle_invoke)(remote_handle64, uint32_t sc, remote_arg *pra)) {
  int nErr = AEE_SUCCESS;
  int len = strlen(uri) + 1;
  struct const_mod *dm = 0, *dmOld;
  VERIFYC(NULL != (dm = ((struct const_mod *)calloc(1, sizeof(struct open_mod) +
                                                           len))),
          AEE_ENOMEMORY);
  dm->key = local;
  dm->invoke = invoke;
  dm->handle_invoke = handle_invoke;
  dm->h64 = remote;
  strlcpy(dm->uri, uri, len);

  RW_MUTEX_LOCK_WRITE(me->mut);
  HASH_FIND_INT(me->constMods, &local, dmOld);
  if (dmOld == 0) {
    HASH_ADD_INT(me->constMods, key, dm);
  }
  RW_MUTEX_UNLOCK_WRITE(me->mut);
  nErr = dmOld != 0 ? -1 : nErr;
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: failed to register const handle in modtable\n",
                   nErr);
    if (dm) {
      free(dm);
      dm = NULL;
    }
  }
  return nErr;
}

static int open_mod_handle_open(struct open_mod *mod, const char *name,
                                remote_handle64 *ph) {
  int nErr = AEE_SUCCESS;
  remote_arg args[3] = {0};
  int32_t len = strlen(name) + 1;
  args[0].buf.pv = &len;
  args[0].buf.nLen = sizeof(len);
  args[1].buf.pv = (void *)name;
  args[1].buf.nLen = len;
  nErr = mod->handle_invoke(0, REMOTE_SCALARS_MAKEX(0, 0, 2, 0, 0, 1), args);
  if (!nErr) {
    *ph = args[2].h64;
  }
  FARF(RUNTIME_RPC_HIGH, "allocated %x", *ph);
  return nErr;
}

static int open_mod_handle_close(struct open_mod *mod, remote_handle64 h) {
  int nErr;
  remote_arg args[1] = {0};
  args[0].h64 = h;
  FARF(RUNTIME_RPC_HIGH, "releasing %x", h);
  nErr = mod->handle_invoke(0, REMOTE_SCALARS_MAKEX(0, 1, 0, 0, 1, 0), args);
  return nErr;
}

static int notqmark(struct sbuf *buf) { return sbuf_notchar(buf, '?'); }
static int notandoreq(struct sbuf *buf) { return sbuf_notchars(buf, "&="); }
static int notand(struct sbuf *buf) { return sbuf_notchar(buf, '&'); }

static int parse_uri(const char *uri, int urilen, struct parsed_uri *out) {
  // "file:///librhtest_skel.so?rhtest_skel_handle_invoke&_modver=1.0"
  int nErr = 0;
  char *name, *value;
  int nameLen, valueLen;
  struct sbuf buf;
  FARF(RUNTIME_RPC_HIGH, "parse_uri %s %d", uri, urilen);
  memset(out, 0, sizeof(*out));
  // initialize
  sbuf_parser_init(&buf, uri, urilen);

  // parse until question mark
  VERIFYC(sbuf_string(&buf, "file://"), AEE_EBADPARM);

  // ignore the starting /
  (void)sbuf_string(&buf, "/");

  out->file = sbuf_cur(&buf);
  VERIFY(sbuf_many1(&buf, notqmark));
  out->filelen = sbuf_cur(&buf) - out->file;
  FARF(RUNTIME_RPC_HIGH, "file:%.*s %d", out->filelen, out->file, out->filelen);
  VERIFY(sbuf_char(&buf, '?'));
  out->sym = sbuf_cur(&buf);
  VERIFY(sbuf_many1(&buf, notand));
  out->symlen = sbuf_cur(&buf) - out->sym;
  assert(out->sym + out->symlen <= uri + urilen);
  FARF(RUNTIME_RPC_HIGH, "sym:%.*s %d", out->symlen, out->sym, out->symlen);

  if (!sbuf_end(&buf) && sbuf_char(&buf, '&')) {
    // parse each query
    while (!sbuf_end(&buf)) {
      // record where the name starts
      name = sbuf_cur(&buf);

      // name is valid until '=' or '&'
      VERIFY(sbuf_many1(&buf, notandoreq));
      nameLen = sbuf_cur(&buf) - name;

      value = 0;
      valueLen = 0;
      // if the next char is a '=' then we also get a value
      if (sbuf_char(&buf, '=')) {
        value = sbuf_cur(&buf);

        // value is until the next query that starts with '&'
        VERIFY(sbuf_many1(&buf, notand));
        valueLen = sbuf_cur(&buf) - value;
      }
      // expect '&' or end
      sbuf_char(&buf, '&');
      if (!strncmp(name, "_modver", nameLen)) {
        out->ver = value;
        out->verlen = valueLen;
      }
    }
  }
bail:
  if (out->filelen) {
    FARF(RUNTIME_RPC_HIGH, "parse_uri file: %.*s", out->filelen, out->file);
  }
  if (out->symlen) {
    FARF(RUNTIME_RPC_HIGH, "parse_uri sym: %.*s", out->symlen, out->sym);
  }
  if (out->verlen) {
    FARF(RUNTIME_RPC_HIGH, "parse_uri version: %.*s", out->verlen, out->ver);
  }
  FARF(RUNTIME_RPC_HIGH, "parse_uri done: %s %d err:%x", uri, urilen, nErr);
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: parseuri failed for uri %s, urilen %d\n", nErr,
                   uri, urilen);
  }
  return nErr;
}

/*
 * Iterate through the list of reverse handles and search for the next available
 * handle. 'key' is only set when handle have been taken and is set to 0 when
 * handle is free. Returns 0 if it found a free handle, otherwise return -1 if
 * it fails.
 */
static inline int next_available_rev_handle(uint32_t *handle_idx) {
  int nErr = AEE_EUNKNOWN;
  struct open_mod *dm = 0;
  unsigned int ii;

  for (ii = 0; ii < MAX_REV_HANDLES; ++ii) {
    dm = (struct open_mod *)&(rev_handle_table[ii][0]);
    if (dm->key == 0) {
      *handle_idx = ii;
      nErr = 0;
      break;
    }
  }
  if (nErr)
    FARF(ERROR,
         "Error 0x%x: %s: max number of reverse RPC handles (%u) open already",
         nErr, __func__, MAX_REV_HANDLES);
  return nErr;
}

uint32_t is_reverse_handle_opened(struct open_mod_table *me,
                                remote_handle *handle, const char *uri) {
  int ii = 0;
  uint32_t keyfound = 0;
  struct open_mod *dmOld;
  RW_MUTEX_LOCK_WRITE(me->mut);
  for (ii = 0; ii < MAX_REV_HANDLES; ++ii) {
    dmOld = (struct open_mod *)&(rev_handle_table[ii][0]);
    if (dmOld->key != 0) {
      if (!strncmp(dmOld->uri, uri, MAX(strlen(dmOld->uri), strlen(uri)))) {
        keyfound = 1;
        break;
      }
    }
  }
  if (keyfound) {
    *handle = dmOld->key;
    dmOld->refs++;
    FARF(
        ALWAYS,
        "%s: reverse module %s already found with handle 0x%x (idx %u) refs %d",
        __func__, uri, *handle, ii, dmOld->refs);
  }
  RW_MUTEX_UNLOCK_WRITE(me->mut);
  return keyfound;
}

static int open_mod_table_open_dynamic(struct open_mod_table *me,
                                       const char *uri, remote_handle *handle,
                                       char *dlStr, int dlerrorLen,
                                       int *pdlErr) {
  int nErr = AEE_SUCCESS, dlErr = 0;
  struct open_mod *dm = 0, *dmOld;
  int len = strlen(uri);
  int tmplen = len * 2 +
               sizeof("file:///lib_skel.so?_skel_handle_invoke&_modver=1.0") +
               1;
  char *tmp = 0;
  uint32_t handle_idx = 0, keyfound = 0;
  int lock = 0;

  FARF(RUNTIME_RPC_HIGH, "open_mod_table_open_dynamic uri %s", uri);
  VERIFYC(NULL != (tmp = calloc(1, tmplen)), AEE_ENOMEMORY);
  VERIFYC((REV_HANDLE_SIZE >= sizeof(struct open_mod) + len + 1),
          AEE_ENOMEMORY);
  RW_MUTEX_LOCK_WRITE(rev_handle_table_lock);
  lock = 1;
  keyfound = is_reverse_handle_opened(me, handle, uri);
  if (keyfound) {
    goto bail;
  }

  VERIFYC(0 == (nErr = next_available_rev_handle(&handle_idx)), AEE_EINVHANDLE);
  VERIFYC(handle_idx < MAX_REV_HANDLES, AEE_EINVHANDLE);
  dm = (struct open_mod *)&(rev_handle_table[handle_idx][0]);
  memset(dm, 0, REV_HANDLE_SIZE);
  memmove(dm->uri, uri, len + 1);
  FARF(RUNTIME_RPC_HIGH, "calling parse_uri");
  (void)parse_uri(dm->uri, len, &dm->vals);
  FARF(RUNTIME_RPC_HIGH, "done calling parse_uri");
  FARF(RUNTIME_RPC_HIGH, "vals %d %d %d", dm->vals.filelen, dm->vals.symlen,
       dm->vals.verlen);
  if (dm->vals.filelen) {
    int rv = snprintf(tmp, tmplen, "%.*s", dm->vals.filelen, dm->vals.file);
    VERIFYC((rv > 0) && (tmplen >= rv), AEE_EBADPARM);
  } else {
    int rv;
    rv = snprintf(tmp, tmplen, "lib%s_skel.so", uri);
    VERIFYC((rv > 0) && (tmplen >= rv), AEE_EBADPARM);
  }

  FARF(RUNTIME_RPC_HIGH, "calling dlopen for %s", tmp);
  errno = 0;
  dm->dlhandle = DLOPEN(tmp, RTLD_NOW);

  // Only check for system library if original library was not found
  if ((dlErr = dm->dlhandle == 0 ? AEE_EINVHANDLE : 0) && errno == ENOENT) {
    int str_len, ii, rv;

    FARF(RUNTIME_RPC_HIGH, "Couldn't find %s", tmp);
    str_len = strlen(tmp);
    VERIFYC(str_len <= tmplen, AEE_EBADPARM);
    for (ii = str_len - 1; ii >= 0; ii--) {

      // Find index of last character before the extension ".so" starts
      if (tmp[ii] == '.') {
        tmp[ii] = '\0';
        break;
      }
    }
    rv = snprintf(tmp, tmplen, "%s_system.so", tmp);
    VERIFYC((rv > 0) && (tmplen >= rv), AEE_EBADPARM);
    FARF(RUNTIME_RPC_HIGH, "calling dlopen for %s", tmp);
    dm->dlhandle = DLOPEN(tmp, RTLD_NOW);
    dlErr = dm->dlhandle == 0 ? AEE_EINVHANDLE : 0;
    if (dlErr == 0)
      FARF(ALWAYS, "system library %s successfully loaded", tmp);
  }
  FARF(RUNTIME_RPC_HIGH, "got DL handle %p for %s", dm->dlhandle, tmp);
  VERIFY(!(nErr = dlErr));

  if (dm->vals.symlen) {
    int rv = snprintf(tmp, tmplen, "%.*s", dm->vals.symlen, dm->vals.sym);
    VERIFYC((rv > 0) && (tmplen >= rv), AEE_EBADPARM);
  } else {
    int rv = snprintf(tmp, tmplen, "%s_skel_invoke", uri);
    VERIFYC((rv > 0) && (tmplen >= rv), AEE_EBADPARM);
  }

  FARF(RUNTIME_RPC_HIGH, "calling dlsym for %s", tmp);
  if (dm->vals.verlen &&
      0 == strncmp(dm->vals.ver, "1.0", dm->vals.verlen)) {
    dm->handle_invoke = (handle_invoke_fn)DLSYM(dm->dlhandle, tmp);
  } else {
    dm->invoke = (invoke_fn)DLSYM(dm->dlhandle, tmp);
  }
  FARF(RUNTIME_RPC_HIGH, "dlsym returned %p %p", dm->invoke, dm->handle_invoke);
  VERIFYC(!(dlErr = dm->invoke || dm->handle_invoke ? 0 : AEE_ENOSUCHSYMBOL),
          AEE_ENOSUCHSYMBOL);

  dm->key = (uint32_t)(uintptr_t)dm;
  dm->refs = 1;
  if (dm->handle_invoke) {
    VERIFY(AEE_SUCCESS == (nErr = open_mod_handle_open(dm, uri, &dm->h64)));
  }

  RW_MUTEX_LOCK_WRITE(me->mut);
  if (!keyfound) {
    do {
      HASH_FIND_INT(me->openMods, &dm->key, dmOld);
      if (dmOld) {
        dm->key++;
      }
    } while (dmOld);
    RW_MUTEX_LOCK_WRITE(me->smt->mut);
    HASH_FIND_INT(me->smt->constMods, &dm->key, dmOld);
    RW_MUTEX_UNLOCK_WRITE(me->smt->mut);
    if (dmOld == 0) {
      HASH_ADD_INT(me->openMods, key, dm);
    }
    nErr = dmOld != 0 ? -1 : nErr;
    if (nErr == 0) {
      *handle = dm->key;
    }
  }
  RW_MUTEX_UNLOCK_WRITE(me->mut);
bail:
  if (lock) {
    lock = 0;
    RW_MUTEX_UNLOCK_WRITE(rev_handle_table_lock);
  }
  if (nErr == AEE_SUCCESS) {
    FARF(ALWAYS,
         "%s: dynamic reverse module %s opened with handle 0x%x (idx %u)",
         __func__, uri, *handle, handle_idx);
  } else {
    if (dlErr) {
      const char *dlerr = DLERROR();
      if (dlerr != 0) {
        strlcpy(dlStr, dlerr, dlerrorLen);
      }
      FARF(RUNTIME_RPC_HIGH, "dlerror:0x%x:%s", dlErr, dlerr == 0 ? "" : dlerr);
      nErr = 0;
    }
    if (pdlErr) {
      *pdlErr = dlErr;
    }
    if (dm && dm->h64) {
      (void)open_mod_handle_close(dm, dm->h64);
    }
    if (dm && dm->dlhandle) {
      DLCLOSE(dm->dlhandle);
    }
    if (dm) {
      dm->key = 0;
      dm = NULL;
    }
    VERIFY_EPRINTF("Error 0x%x: %s failed for %s, dlerr 0x%x", nErr, __func__,
                   uri, dlErr);
  }

  FARF(RUNTIME_RPC_HIGH,
       "done open_mod_table_open_dynamic for %s rv %x handle: %p %x", uri, nErr,
       *handle, dlErr);
  if (tmp) {
    free(tmp);
    tmp = NULL;
  }
  return nErr;
}

static int open_mod_table_open_from_static(struct open_mod_table *me,
                                           struct static_mod **tbl,
                                           const char *uri,
                                           remote_handle *handle) {
  int nErr = AEE_SUCCESS;
  struct static_mod *sm = 0;
  struct open_mod *dm = 0;
  int len = strlen(uri);
  int sz = len * 2 +
           sizeof("file:///lib_skel.so?_skel_handle_invoke&_modver=1.0") + 1;
  uint32_t handle_idx = 0, keyfound = 0;
  int lock = 0;

  VERIFYC((REV_HANDLE_SIZE >= sizeof(struct open_mod) + sz), AEE_ENOMEMORY);

  RW_MUTEX_LOCK_WRITE(rev_handle_table_lock);
  lock = 1;
  keyfound = is_reverse_handle_opened(me, handle, uri);
  if (keyfound) {
    goto bail;
  }
  VERIFYC(0 == (nErr = next_available_rev_handle(&handle_idx)), AEE_EINVHANDLE);
  VERIFYC(handle_idx < MAX_REV_HANDLES, AEE_EINVHANDLE);
  dm = (struct open_mod *)&(rev_handle_table[handle_idx][0]);
  memset(dm, 0, REV_HANDLE_SIZE);
  RW_MUTEX_LOCK_READ(me->mut);
  HASH_FIND_STR(*tbl, uri, sm);
  RW_MUTEX_UNLOCK_READ(me->mut);
  memmove(dm->uri, uri, len);
  if (sm == 0) {
    VERIFY(AEE_SUCCESS == (nErr = parse_uri(uri, len, &dm->vals)));
    FARF(RUNTIME_RPC_HIGH, "file %.*s %d", dm->vals.filelen, dm->vals.file,
         dm->vals.filelen);
    FARF(RUNTIME_RPC_HIGH, "sym %.*s %d", dm->vals.symlen, dm->vals.sym,
         dm->vals.symlen);
    FARF(RUNTIME_RPC_HIGH, "version %.*s %d", dm->vals.verlen, dm->vals.ver,
         dm->vals.verlen);
    if (dm->vals.verlen) {
      int rv = snprintf(dm->uri, sz, "file:///%.*s?%.*s&_modver=%.*s",
                            dm->vals.filelen, dm->vals.file, dm->vals.symlen,
                            dm->vals.sym, dm->vals.verlen, dm->vals.ver);
      VERIFYC((rv > 0) && (sz >= rv), AEE_EBADPARM);
    } else {
      int rv = snprintf(dm->uri, sz, "file://%.*s?%.*s", dm->vals.filelen,
                            dm->vals.file, dm->vals.symlen, dm->vals.sym);
      VERIFYC((rv > 0) && (sz >= rv), AEE_EBADPARM);
    }
    FARF(RUNTIME_RPC_HIGH, "dm->uri:%s", dm->uri);

    RW_MUTEX_LOCK_READ(me->mut);
    HASH_FIND_STR(*tbl, dm->uri, sm);
    RW_MUTEX_UNLOCK_READ(me->mut);
  }
  VERIFYM(0 != sm, AEE_ENOSUCHMOD, "Error %x: static mod is not initialized\n",
          nErr);
  assert(sm->handle_invoke || sm->invoke);
  dm->handle_invoke = sm->handle_invoke;
  dm->invoke = sm->invoke;
  dm->key = (uint32_t)(uintptr_t)dm;
  dm->refs = 1;
  if (dm->handle_invoke) {
    VERIFY(AEE_SUCCESS == (nErr = open_mod_handle_open(dm, uri, &dm->h64)));
  }

  RW_MUTEX_LOCK_WRITE(me->mut);
  if (!keyfound) {
    HASH_ADD_INT(me->openMods, key, dm);
    *handle = dm->key;
  }
  RW_MUTEX_UNLOCK_WRITE(me->mut);
bail:
  if (lock) {
    lock = 0;
    RW_MUTEX_UNLOCK_WRITE(rev_handle_table_lock);
  }
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s failed for %s", nErr, __func__, uri);
  } else {
    FARF(RUNTIME_RPC_HIGH, "%s: reverse module %s opened with handle 0x%x (idx %u)",
         __func__, uri, *handle, handle_idx);
  }
  if (nErr && dm) {
    if (dm->h64) {
      (void)open_mod_handle_close(dm, dm->h64);
    }
    dm->key = 0;
    dm = NULL;
  }
  return nErr;
}

static int open_mod_table_open(struct open_mod_table *me, const char *uri,
                               remote_handle *handle, char *dlerr,
                               int dlerrorLen, int *pdlErr) {
  int nErr = AEE_SUCCESS, dlErr = 0;
  if (pdlErr) {
    *pdlErr = 0;
  }
  if (0 != open_mod_table_open_static_override(me, uri, handle)) {
    VERIFY(AEE_SUCCESS == (nErr = open_mod_table_open_dynamic(
                               me, uri, handle, dlerr, dlerrorLen, &dlErr)));
    if (dlErr != 0) {
      FARF(RUNTIME_RPC_HIGH, "dynammic open failed, trying static");
      if (0 != open_mod_table_open_static(me, uri, handle)) {
        if (pdlErr) {
          *pdlErr = dlErr;
        }
      }
    }
  }
bail:
  FARF(RUNTIME_RPC_HIGH, "done open for %s rv %d handle: %p", uri, nErr,
       *handle);
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: open modtable failed\n", nErr);
  }
  return nErr;
}

static void open_mod_close(struct open_mod_table *me, struct open_mod *dm) {
  RW_MUTEX_LOCK_WRITE(me->mut);
  FARF(RUNTIME_RPC_HIGH, "%s: uri:%s, refs:%d", __func__, dm->uri, dm->refs);
  dm->refs--;
  if (dm->refs == 0) {
    HASH_DEL(me->openMods, dm);
  } else {
    FARF(RUNTIME_RPC_HIGH, "%s : module %s has pending invokes ref count %d",
         __func__, dm->uri, dm->refs);
    dm = 0;
  }
  RW_MUTEX_UNLOCK_WRITE(me->mut);
  if (dm) {
    if (dm->h64) {
      (void)open_mod_handle_close(dm, dm->h64);
    }
    if (dm->dlhandle) {
      DLCLOSE(dm->dlhandle);
    }
    FARF(ALWAYS, "%s: closed reverse module %s with handle 0x%x", __func__,
         dm->uri, (uint32_t)dm->key);
    dm->key = 0;
    dm = NULL;
  }
}
static int open_mod_table_close(struct open_mod_table *me,
                                remote_handle64 handle, char *errStr,
                                int errStrLen, int *pdlErr) {
  int nErr = AEE_SUCCESS;
  struct open_mod *dm, *del = 0;
  int dlErr = 0, locked = 0;
  // First ensure that the handle is valid
  RW_MUTEX_LOCK_WRITE(me->mut);
  HASH_FIND_INT(me->openMods, &handle, dm);
  locked = 1;
  VERIFYC(dm, AEE_ENOSUCHMOD);
  if (dm) {
    dm->refs--;
    if (dm->refs == 0) {
      del = dm;
      FARF(RUNTIME_RPC_HIGH, "deleting %s %p %d", del->uri, del, dm->refs);
      HASH_DEL(me->openMods, dm);
      del->key = 0;
    } else {
      FARF(RUNTIME_RPC_HIGH, "%s: pending ref: dm->refs %d, for uri: %s",
           __func__, dm->refs, dm->uri);
      dm = 0;
    }
  }
  RW_MUTEX_UNLOCK_WRITE(me->mut);
  locked = 0;
  if (del) {
    if (del->h64) {
      (void)open_mod_handle_close(dm, dm->h64);
    }
    if (del->dlhandle) {
      dlErr = DLCLOSE(del->dlhandle);
    }
    FARF(ALWAYS, "%s: closed reverse module %s with handle 0x%x", __func__,
         del->uri, (uint32_t)handle);
    del = NULL;
  }
bail:
  if (locked) {
    locked = 0;
    RW_MUTEX_UNLOCK_WRITE(me->mut);
  }
  if (dlErr) {
    const char *error = DLERROR();
    nErr = dlErr;
    if (error != 0) {
      strlcpy(errStr, error, errStrLen);
    }
    VERIFY_EPRINTF("Error %x: open modtable close failed. dlerr %s\n", nErr,
                   error);
  }
  if (pdlErr) {
    *pdlErr = dlErr;
  }
  return nErr;
}

static struct open_mod *open_mod_table_get_open(struct open_mod_table *me,
                                                remote_handle handle) {
  struct open_mod *om = 0;
  RW_MUTEX_LOCK_WRITE(me->mut);
  HASH_FIND_INT(me->openMods, &handle, om);
  if (0 != om) {
    om->refs++;
    FARF(RUNTIME_RPC_HIGH, "%s: module %s, increament refs %d", __func__,
         om->uri, om->refs);
  }
  RW_MUTEX_UNLOCK_WRITE(me->mut);
  return om;
}
static struct const_mod *open_mod_table_get_const(struct open_mod_table *me,
                                                  remote_handle handle) {
  struct const_mod *cm = 0;
  RW_MUTEX_LOCK_READ(me->smt->mut);
  HASH_FIND_INT(me->smt->constMods, &handle, cm);
  RW_MUTEX_UNLOCK_READ(me->smt->mut);
  return cm;
}

static int open_mod_table_handle_invoke(struct open_mod_table *me,
                                        remote_handle handle, uint32_t sc,
                                        remote_arg *pra) {
  int nErr = AEE_SUCCESS;
  struct open_mod *om = 0;
  struct const_mod *cm = 0;
  remote_handle64 h = 0;
  invoke_fn invoke = 0;
  handle_invoke_fn handle_invoke = 0;
  cm = open_mod_table_get_const(me, handle);

  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x , scalar 0x%x", __func__,
                         handle, sc);
  if (cm) {
    invoke = cm->invoke;
    handle_invoke = cm->handle_invoke;
    h = cm->h64;
  } else {
    VERIFYC(0 != (om = open_mod_table_get_open(me, handle)), AEE_ENOSUCHMOD);
    invoke = om->invoke;
    handle_invoke = om->handle_invoke;
    h = om->h64;
  }
  if (invoke) {
    VERIFY(AEE_SUCCESS == (nErr = invoke(sc, pra)));
  } else {
    VERIFY(AEE_SUCCESS == (nErr = handle_invoke(h, sc, pra)));
  }
bail:
  if (om) {
    open_mod_close(me, om);
  }
  if (nErr != AEE_SUCCESS) {
    FARF(ERROR, "Error 0x%x: %s failed for handle:0x%x, sc:0x%x", nErr,
         __func__, handle, sc);
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

struct mod_table {
  struct static_mod_table smt;
  struct open_mod_table omt;
};

// mod_table object
static struct static_mod_table static_mod_table_obj;

/**
 * register a static component for invocations
 * this can be called at any time including from a static constructor
 *
 * overrides will be tried first, then dynamic modules, then regular
 * static modules.
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
                                       int (*pfn)(uint32_t sc, remote_arg *pra)) {
  if (0 == static_mod_table_ctor(&static_mod_table_obj)) {
    return static_mod_table_register_static_override(&static_mod_table_obj,
                                                     name, pfn);
  }
  return AEE_EUNKNOWN;
}

int mod_table_register_static_override1(const char *name,
                                        int (*pfn)(remote_handle64, uint32_t sc,
                                                   remote_arg *pra)) {
  if (0 == static_mod_table_ctor(&static_mod_table_obj)) {
    return static_mod_table_register_static_override1(&static_mod_table_obj,
                                                      name, pfn);
  }
  return AEE_EUNKNOWN;
}

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
                              int (*pfn)(uint32_t sc, remote_arg *pra)) {
  if (0 == static_mod_table_ctor(&static_mod_table_obj)) {
    return static_mod_table_register_static(&static_mod_table_obj, name, pfn);
  }
  return AEE_EUNKNOWN;
}

int mod_table_register_static1(const char *name,
                               int (*pfn)(remote_handle64, uint32_t sc,
                                          remote_arg *pra)) {
  if (0 == static_mod_table_ctor(&static_mod_table_obj)) {
    return static_mod_table_register_static1(&static_mod_table_obj, name, pfn);
  }
  return AEE_EUNKNOWN;
}

/**
 * Open a module and get a handle to it
 *
 * uri, name of module to open
 * handle, Output handle
 * dlerr, Error String (if an error occurs)
 * dlerrorLen, Length of error String (if an error occurs)
 * pdlErr, Error identifier
 */
int mod_table_open(const char *uri, remote_handle *handle, char *dlerr,
                   int dlerrorLen, int *pdlErr) {
  int nErr = AEE_SUCCESS;
  struct open_mod_table *pomt = 0;

  VERIFYC(NULL != uri, AEE_EBADPARM);
  FASTRPC_ATRACE_BEGIN_L("%s for %s", __func__, uri);
  FARF(RUNTIME_RPC_HIGH, "mod_table_open for %s", uri);
  VERIFY(AEE_SUCCESS ==
         (nErr = HAP_pls_add_lookup((uintptr_t)open_mod_table_ctor_imp, 0,
                                    sizeof(*pomt), open_mod_table_ctor_imp,
                                    (void *)&static_mod_table_obj,
                                    open_mod_table_dtor_imp, (void **)&pomt)));
  VERIFY(AEE_SUCCESS == (nErr = open_mod_table_open(pomt, uri, handle, dlerr,
                                                    dlerrorLen, pdlErr)));
bail:
  FARF(RUNTIME_RPC_HIGH, "mod_table_open for %s nErr: %x", uri, nErr);
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: modtable open failed\n", nErr);
  }
  if (uri) {
    FASTRPC_ATRACE_END();
  }
  return nErr;
}
/**
 * invoke a handle in the mod table
 *
 * handle, handle to invoke
 * sc, scalars, see remote.h for documentation.
 * pra, args, see remote.h for documentation.
 */
int mod_table_invoke(remote_handle handle, uint32_t sc, remote_arg *pra) {
  int nErr = AEE_SUCCESS;
  struct open_mod_table *pomt = 0;
  VERIFY(AEE_SUCCESS ==
         (nErr = HAP_pls_add_lookup((uintptr_t)open_mod_table_ctor_imp, 0,
                                    sizeof(*pomt), open_mod_table_ctor_imp,
                                    (void *)&static_mod_table_obj,
                                    open_mod_table_dtor_imp, (void **)&pomt)));
  VERIFY(AEE_SUCCESS ==
         (nErr = open_mod_table_handle_invoke(pomt, handle, sc, pra)));
bail:
  return nErr;
}

/**
 * Closes a handle in the mod table
 *
 * handle, handle to close
 * errStr, Error String (if an error occurs)
 * errStrLen, Length of error String (if an error occurs)
 * pdlErr, Error identifier
 */
int mod_table_close(remote_handle handle, char *errStr, int errStrLen,
                    int *pdlErr) {
  int nErr = AEE_SUCCESS;
  struct open_mod_table *pomt = 0;

  FASTRPC_ATRACE_BEGIN_L("%s called with handle 0x%x", __func__, (int)handle);
  VERIFY(AEE_SUCCESS ==
         (nErr = HAP_pls_lookup((uintptr_t)open_mod_table_ctor_imp, 0,
                                (void **)&pomt)));
  VERIFY(AEE_SUCCESS == (nErr = open_mod_table_close(pomt, handle, errStr,
                                                     errStrLen, pdlErr)));
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: modtable close failed\n", nErr);
  }
  FASTRPC_ATRACE_END();
  return nErr;
}

/**
 * internal use only
 */
int mod_table_register_const_handle(remote_handle remote, const char *uri,
                                    int (*pfn)(uint32_t sc, remote_arg *pra)) {
  if (0 == static_mod_table_ctor(&static_mod_table_obj)) {
    return static_mod_table_register_const_handle(&static_mod_table_obj, remote,
                                                  0, uri, pfn, 0);
  }
  return AEE_EUNKNOWN;
}
int mod_table_register_const_handle1(remote_handle remote,
                                     remote_handle64 local, const char *uri,
                                     int (*pfn)(remote_handle64, uint32_t sc,
                                                remote_arg *pra)) {
  if (0 == static_mod_table_ctor(&static_mod_table_obj)) {
    return static_mod_table_register_const_handle(&static_mod_table_obj, remote,
                                                  local, uri, 0, pfn);
  }
  return AEE_EUNKNOWN;
}

// Constructor and destructor
static int mod_table_ctor(void) {
  return static_mod_table_ctor(&static_mod_table_obj);
}
static void mod_table_dtor(void) {
  static_mod_table_dtor_imp(&static_mod_table_obj);
  return;
}

PL_DEFINE(mod_table, mod_table_ctor, mod_table_dtor);
