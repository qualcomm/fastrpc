// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef PARSE_YAML
#include <yaml.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef CONFIG_BASE_DIR
#define CONFIG_BASE_DIR "/usr/share/qcom/"
#endif
#define QCOM_BASE_DIR CONFIG_BASE_DIR
#define MACHINE_MODEL_PATH "/sys/firmware/devicetree/base/model"

#ifdef ANDROID
#define FW_BASE_PATH "/vendor/firmware_mnt/image/"
#else
#define FW_BASE_PATH "/lib/firmware/"
#endif

#ifdef PARSE_YAML
static char* cached_dsp_library_path = NULL;
#endif

static char dsp_base_path[PATH_MAX] = "";

static const char* pd_shell_any[] = {
    "fastrpc_shell_?*",
};

static const char* pd_shell_unsigned[] = {
    "fastrpc_shell_unsigned_?*",
};


static void die(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    exit(2);
}
static void* xmalloc(size_t n) { void* p = malloc(n); if (!p) die("OOM"); return p; }
static char* xstrdup(const char* s) { if (!s) return NULL; size_t n=strlen(s)+1; char* p=xmalloc(n); memcpy(p,s,n); return p; }
static bool is_dir(const char* p) { struct stat st; return p && stat(p, &st)==0 && S_ISDIR(st.st_mode); }
static bool path_exists(const char* p) { struct stat st; return p && stat(p, &st)==0; }
static bool is_chardev(const char* p) { struct stat st; return p && stat(p, &st)==0 && S_ISCHR(st.st_mode); }

static void join2(char* out, size_t sz, const char* a, const char* b) {
    snprintf(out, sz, "%s/%s", a, b);
}

static int read_text(const char* path, char* out, size_t out_sz) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return -errno;
    }

    size_t n = fread(out, 1, out_sz-1, f);
    int err = ferror(f);
    fclose(f);

    if (n == 0 && err) return -EIO;

    out[n] = '\0';
    if (n > 0 && out[n-1] == '\n') out[n-1] = '\0';

    return (int)n;
}

static bool get_remoteproc_firmware_path(
    const char* rproc,
    char* out,
    size_t out_sz
)
{
    char sysfs_path[PATH_MAX];
    char fw_rel[PATH_MAX];

    if (!rproc || !*rproc)
        return false;

    snprintf(sysfs_path, sizeof(sysfs_path),
             "/sys/class/remoteproc/%s/firmware",
             rproc);

    if (read_text(sysfs_path, fw_rel, sizeof(fw_rel)) < 0)
        return false;

    snprintf(out, out_sz, FW_BASE_PATH "%s", fw_rel);
    return true;
}

typedef struct { char** v; size_t n; } strv_t;
static void vec_push(strv_t* sv, const char* s) {
    sv->v = (char**)realloc(sv->v, (sv->n + 1) * sizeof(char*));
    if (!sv->v) die("OOM");
    sv->v[sv->n++] = xstrdup(s);
}

static void vec_free(strv_t* sv) {
    for (size_t i=0;i<sv->n;i++) free(sv->v[i]);
    free(sv->v); sv->v=NULL; sv->n=0;
}

static void split_colon_paths(const char* in, strv_t* out) {
    if (!in || !*in) return;
    char* dup = xstrdup(in);
    char* save=NULL; char* tok = strtok_r(dup, ":", &save);
    while (tok) { if (*tok) vec_push(out, tok); tok = strtok_r(NULL, ":", &save); }
    free(dup);
}

static bool dir_has_any(const char* dir, const char* const pats[], size_t np) {
    if (!is_dir(dir)) return false;
    DIR* d = opendir(dir); if (!d) return false;
    bool hit=false; struct dirent* de;
    while (!hit && (de=readdir(d)) != NULL) {
        if (de->d_name[0]=='.') continue;
        for (size_t i=0;i<np;i++)
            if (fnmatch(pats[i], de->d_name, FNM_CASEFOLD)==0) { hit=true; break; }
    }
    closedir(d);
    return hit;
}

static bool extract_crm_build_id(
    const char* file,
    char* out,
    size_t out_sz
) {
    FILE* f = fopen(file, "rb");
    if (!f)
        return false;

    char buf[4096];
    char acc[512];
    size_t acc_len = 0;

    while (!feof(f)) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        for (size_t i = 0; i < n; i++) {
            if (isprint((unsigned char)buf[i])) {
                if (acc_len < sizeof(acc) - 1)
                    acc[acc_len++] = buf[i];
            } else {
                acc[acc_len] = '\0';
                acc_len = 0;

                if (strstr(acc, "CRMBuilds/") || strstr(acc, "CRMBuild/")) {

                    const char* p =
                        strstr(acc, "CRMBuilds/") ?
                        strstr(acc, "CRMBuilds/") :
                        strstr(acc, "CRMBuild/");

                    strncpy(out, p, out_sz - 1);
                    out[out_sz - 1] = '\0';
                    fclose(f);
                    return true;
                }
            }
        }
    }

    fclose(f);
    return false;
}

static const char* normalize_build_id(char* s)
{
    if (!s)
        return NULL;

    char* p = strstr(s, "CRMBuilds/");
    if (p) {
        s = p + strlen("CRMBuilds/");
    } else {
        p = strstr(s, "CRMBuild/");
        if (p)
            s = p + strlen("CRMBuild/");
    }

    char* slash = strchr(s, '/');
    if (slash)
        *slash = '\0';

    return s;
}

static bool dsp_name_matches(const char *remoteproc_name,
                             const char *dsp_key)
{
    const char *p = strstr(remoteproc_name, dsp_key);
    if (!p)
        return false;

    size_t key_len = strlen(dsp_key);

    if (!isdigit((unsigned char)dsp_key[key_len - 1])) {
        if (isdigit((unsigned char)p[key_len]))
            return false;
    }

    return true;
}

#ifdef PARSE_YAML
static char* find_dsp_library_path_from_yaml(void)
{
    char machine[256] = "";

    if (read_text(MACHINE_MODEL_PATH, machine, sizeof(machine)) < 0)
        return NULL;
    machine[strcspn(machine, "\n")] = '\0';

    char conf_dir[PATH_MAX];
    snprintf(conf_dir, sizeof(conf_dir), "%s/conf.d", QCOM_BASE_DIR);

    DIR* dir = opendir(conf_dir);
    if (!dir)
        return NULL;

    struct dirent* de;

    while ((de = readdir(dir)) != NULL) {

        if (!strstr(de->d_name, ".yaml"))
            continue;

        char yaml_path[PATH_MAX];
        snprintf(yaml_path, sizeof(yaml_path),
                 "%s/%s", conf_dir, de->d_name);

        FILE* f = fopen(yaml_path, "r");
        if (!f)
            continue;

        yaml_parser_t parser;
        yaml_event_t event;

        if (!yaml_parser_initialize(&parser)) {
            fclose(f);
            continue;
        }

        yaml_parser_set_input_file(&parser, f);

        bool in_machines = false;
        bool machine_match = false;
        bool want_dsp_path = false;
        char dsp_path[PATH_MAX] = "";

        while (yaml_parser_parse(&parser, &event)) {

            if (event.type == YAML_STREAM_END_EVENT) {
                yaml_event_delete(&event);
                break;
            }

            if (event.type == YAML_SCALAR_EVENT) {
                const char* val = (const char*)event.data.scalar.value;

                if (strcmp(val, "machines") == 0) {
                    in_machines = true;
                }
                else if (in_machines && !machine_match) {
                    /* try exact match against DT model */
                    if (strcmp(val, machine) == 0) {
                        machine_match = true;
                    }
                }
                else if (machine_match && strcmp(val, "DSP_LIBRARY_PATH") == 0) {
                    want_dsp_path = true;
                }
                else if (want_dsp_path) {
                    strncpy(dsp_path, val, sizeof(dsp_path) - 1);
                    yaml_event_delete(&event);
                    yaml_parser_delete(&parser);
                    fclose(f);
                    closedir(dir);

                    return xstrdup(dsp_path);
                }
            }

            yaml_event_delete(&event);
        }
        yaml_parser_delete(&parser);
        fclose(f);
    }
    closedir(dir);
    return NULL;
}

static bool get_yaml_dsp_base(char* out, size_t out_sz)
{
    if (!cached_dsp_library_path) {
        cached_dsp_library_path = find_dsp_library_path_from_yaml();
    }

    if (!cached_dsp_library_path)
        return false;

    snprintf(out, out_sz, "%s/%s", QCOM_BASE_DIR, cached_dsp_library_path);
    return is_dir(out);
}

#else

static bool get_yaml_dsp_base(char* out, size_t out_sz)
{
    (void)out;
    (void)out_sz;
    return false;
}
#endif

typedef enum {
    DSP_ADSP=0,
    DSP_CDSP,
    DSP_CDSP1,
    DSP_GDSP0,
    DSP_GDSP1,
    DSP_MDSP,
    DSP_SDSP,
    DSP_COUNT
} dsp_id_t;

typedef struct {
    const char* name;
    const char* env_name;
    strv_t      cli_paths;
    strv_t      search;

    const char* defaults[6];
    size_t      def_n;

    const char* devnodes[4];
    size_t      dev_n;

    bool        present;
    bool        remote_running;
    char        rproc[32];
    bool        fw_present;
    bool        online;
    bool        offload_capable;
    bool        modules_ok, fastrpc_lib_ok, signed_pd, unsigned_pd;
    bool        fw_build_found;
    bool        shell_build_found;
    bool        build_id_match;
    bool        shell_is_unsigned;
    bool        unsigned_shell_match;
    bool        signed_shell_match;
    char        unsigned_shell_build_id[256];
    char        signed_shell_build_id[256];
    char        fw_build_id[256];
    char        shell_build_id[256];

} dsp_t;

static bool scan_shell_dir_for_build_id(
    dsp_t* d,
    const char* dsp_dir
)
{
    d->shell_build_found = false;
    d->unsigned_shell_match = false;
    d->signed_shell_match   = false;

    d->unsigned_shell_build_id[0] = '\0';
    d->signed_shell_build_id[0]   = '\0';

    if (!is_dir(dsp_dir))
        return false;

    DIR* dir = opendir(dsp_dir);
    if (!dir)
        return false;

    const char* fw_id = normalize_build_id(d->fw_build_id);
    struct dirent* de;

    while ((de = readdir(dir)) != NULL) {

        bool is_unsigned =
            fnmatch(pd_shell_unsigned[0], de->d_name, 0) == 0;
        bool is_signed =
            fnmatch(pd_shell_any[0], de->d_name, 0) == 0;

        if (!is_unsigned && !is_signed)
            continue;

        char shell_path[PATH_MAX];
        join2(shell_path, sizeof(shell_path), dsp_dir, de->d_name);

        char tmp[256];
        if (!extract_crm_build_id(shell_path, tmp, sizeof(tmp)))
            continue;

        const char* sh_id = normalize_build_id(tmp);

        if (fw_id && sh_id && strcmp(fw_id, sh_id) == 0) {
            d->shell_build_found = true;

            if (is_unsigned) {
                d->unsigned_shell_match = true;
                strncpy(d->unsigned_shell_build_id, sh_id,
                        sizeof(d->unsigned_shell_build_id) - 1);
            } else {
                d->signed_shell_match = true;
                strncpy(d->signed_shell_build_id, sh_id,
                        sizeof(d->signed_shell_build_id) - 1);
            }

            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

#ifdef ANDROID
static bool find_android_fw_build_id(
    const dsp_t* d,
    char* out,
    size_t out_sz
)
{
    DIR* dir = opendir(FW_BASE_PATH);
    if (!dir)
        return false;

    char dsp_lc[16];
    snprintf(dsp_lc, sizeof(dsp_lc), "%s", d->name);
    for (char* c = dsp_lc; *c; ++c)
        *c = tolower((unsigned char)*c);

    char pat[32];
    snprintf(pat, sizeof(pat), "%s.*", dsp_lc);

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {

        if (de->d_name[0] == '.')
            continue;

        if (fnmatch(pat, de->d_name, FNM_CASEFOLD) != 0)
            continue;

        char fw_path[PATH_MAX];
        join2(fw_path, sizeof(fw_path),
              FW_BASE_PATH, de->d_name);

        if (!path_exists(fw_path))
            continue;

        if (extract_crm_build_id(fw_path, out, out_sz)) {
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}
#endif

static void scan_yaml_shell_availability(dsp_t* d)
{
    d->signed_pd   = false;
    d->unsigned_pd = false;

    char dsp_base[PATH_MAX];
    if (!get_yaml_dsp_base(dsp_base, sizeof(dsp_base)))
        return;
    if (dsp_base_path[0] == '\0') {
        strncpy(dsp_base_path, dsp_base, sizeof(dsp_base_path) - 1);
    }

    char dsp_lc[16];
    snprintf(dsp_lc, sizeof(dsp_lc), "%s", d->name);
    for (char* c = dsp_lc; *c; c++)
        *c = tolower((unsigned char)*c);

    char dsp_dir[PATH_MAX];
    snprintf(dsp_dir, sizeof(dsp_dir),
             "%s/%s", dsp_base, dsp_lc);

    if (!is_dir(dsp_dir))
        return;

    DIR* ddir = opendir(dsp_dir);
    if (!ddir)
        return;

    struct dirent* de;
    while ((de = readdir(ddir)) != NULL) {

        if (fnmatch(pd_shell_unsigned[0], de->d_name, 0) == 0) {
            d->unsigned_pd = true;
        }
        else if (fnmatch(pd_shell_any[0], de->d_name, 0) == 0) {
            d->signed_pd = true;
        }
    }


    closedir(ddir);
}

#ifdef ANDROID
static void scan_android_shell_availability(dsp_t *d)
{
    char dsp_lc[16];
    snprintf(dsp_lc, sizeof(dsp_lc), "%s", d->name);
    for (char *c = dsp_lc; *c; ++c)
        *c = tolower((unsigned char)*c);

    char dsp_dir[PATH_MAX];
    snprintf(dsp_dir, sizeof(dsp_dir), "/vendor/dsp/%s", dsp_lc);

    d->signed_pd   = dir_has_any(dsp_dir, pd_shell_any, 1);
    d->unsigned_pd = dir_has_any(dsp_dir, pd_shell_unsigned, 1);
}
#endif

static bool find_shell_build_id(dsp_t* d, const char* unused)
{

    if (!d->fw_build_found)
        return false;

    char dsp_lc[16];
    snprintf(dsp_lc, sizeof(dsp_lc), "%s", d->name);
    for (char* c = dsp_lc; *c; ++c)
        *c = tolower((unsigned char)*c);

    char dsp_dir[PATH_MAX];

#ifdef ANDROID
    snprintf(dsp_dir, sizeof(dsp_dir),
             "/vendor/dsp/%s", dsp_lc);
#else
    char dsp_base[PATH_MAX];
    if (!get_yaml_dsp_base(dsp_base, sizeof(dsp_base)))
        return false;

    snprintf(dsp_dir, sizeof(dsp_dir),
             "%s/%s", dsp_base, dsp_lc);
#endif

    if (!scan_shell_dir_for_build_id(d, dsp_dir))
        return false;

    if (d->unsigned_shell_match) {
        d->shell_is_unsigned = true;
        strncpy(d->shell_build_id,
                d->unsigned_shell_build_id,
                sizeof(d->shell_build_id) - 1);
        return true;
    }

    if (d->signed_shell_match) {
        d->shell_is_unsigned = false;
        strncpy(d->shell_build_id,
                d->signed_shell_build_id,
                sizeof(d->shell_build_id) - 1);
        return true;
    }

    return false;
}

static void dsp_init_defaults(dsp_t dsps[DSP_COUNT]) {
    dsps[DSP_ADSP] = (dsp_t){
        .name="ADSP", .env_name="DSP_LIBRARY_PATH",
        .defaults={ "/usr/lib/dsp", "/usr/lib/rfsa/adsp" },
        .def_n=2,
        .devnodes={ "/dev/fastrpc-adsp", "/dev/fastrpc-adsp-secure" },
        .dev_n=2,
    };

    dsps[DSP_CDSP] = (dsp_t){
        .name="CDSP", .env_name="DSP_LIBRARY_PATH",
        .defaults={ "/usr/lib/dsp", "/usr/lib/rfsa/adsp" },
        .def_n=2,
        .devnodes={ "/dev/fastrpc-cdsp", "/dev/fastrpc-cdsp-secure" },
        .dev_n=2,
    };

    dsps[DSP_CDSP1] = (dsp_t){
        .name="CDSP1", .env_name="DSP_LIBRARY_PATH",
        .defaults={ "/usr/lib/dsp", "/usr/lib/rfsa/adsp" },
        .def_n=2,
        .devnodes={ "/dev/fastrpc-cdsp1", "/dev/fastrpc-cdsp1-secure" },
        .dev_n=2,
    };

    dsps[DSP_GDSP0] = (dsp_t){
        .name="GDSP0", .env_name="DSP_LIBRARY_PATH",
        .defaults={ "/usr/lib/dsp", "/usr/lib/rfsa/adsp" },
        .def_n=2,
        .devnodes={ "/dev/fastrpc-gdsp0", "/dev/fastrpc-gdsp0-secure" },
        .dev_n=2,
    };

    dsps[DSP_GDSP1] = (dsp_t){
        .name="GDSP1", .env_name="DSP_LIBRARY_PATH",
        .defaults={ "/usr/lib/dsp", "/usr/lib/rfsa/adsp" },
        .def_n=2,
        .devnodes={ "/dev/fastrpc-gdsp1", "/dev/fastrpc-gdsp1-secure" },
        .dev_n=2,
    };

    dsps[DSP_MDSP] = (dsp_t){
        .name="MDSP", .env_name="DSP_LIBRARY_PATH",
        .defaults={ "/usr/lib/dsp", "/usr/lib/rfsa/adsp" },
        .def_n=2,
        .devnodes={ "/dev/fastrpc-mdsp", "/dev/fastrpc-mdsp-secure" },
        .dev_n=2,
    };

    dsps[DSP_SDSP] = (dsp_t){
        .name="SDSP", .env_name="DSP_LIBRARY_PATH",
        .defaults={ "/usr/lib/dsp", "/usr/lib/rfsa/adsp" },
        .def_n=2,
        .devnodes={ "/dev/fastrpc-sdsp", "/dev/fastrpc-sdsp-secure" },
        .dev_n=2,
    };
}

static void update_remoteproc_states(dsp_t dsps[DSP_COUNT]) {
    const char* rroot = "/sys/class/remoteproc";
    DIR* d = opendir(rroot);
    if (!d) {
        return;
    }

    static const struct {
        const char* key;
        dsp_id_t id;
    } map[] = {
        {"adsp",   DSP_ADSP},
        {"cdsp",   DSP_CDSP},
        {"cdsp1",  DSP_CDSP1},
        {"gpdsp0", DSP_GDSP0},
        {"gpdsp1", DSP_GDSP1},
        {"mdsp",   DSP_MDSP},
        {"sdsp",   DSP_SDSP},
        {"slpi",   DSP_SDSP},
    };

    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "remoteproc", 10) != 0)
            continue;

        char path[PATH_MAX];
        char name[128]  = "";
        char state[64]  = "";

        snprintf(path, sizeof(path), "%s/%s/name", rroot, de->d_name);
        if (read_text(path, name, sizeof(name)) < 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s/state", rroot, de->d_name);
        if (read_text(path, state, sizeof(state)) < 0)
            continue;

        for (char* p = name; *p; ++p)
            *p = (char)tolower((unsigned char)*p);
        for (char* p = state; *p; ++p)
            *p = (char)tolower((unsigned char)*p);

        bool running = (strstr(state, "running") != NULL);

        bool matched = false;
        for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); ++i) {
            if (dsp_name_matches(name, map[i].key)) {

                dsps[map[i].id].present = true;
                dsps[map[i].id].remote_running = running;

                snprintf(dsps[map[i].id].rproc,
                        sizeof(dsps[map[i].id].rproc),
                        "%s",
                        de->d_name);

                matched = true;
                break;
            }
        }
    }

    closedir(d);
}

static void build_search_for(dsp_t* d, const char* dsp_all_env, const char* yaml_base_path) {
    strv_t ordered = {0};

    for (size_t i=0; i<d->cli_paths.n; i++) {
        if (is_dir(d->cli_paths.v[i])) {
            vec_push(&ordered, d->cli_paths.v[i]);
        }
    }

    if (yaml_base_path) {
        char dsp_base[PATH_MAX];
        snprintf(dsp_base, sizeof(dsp_base), "%s/%s", QCOM_BASE_DIR, yaml_base_path);

        char dsp_lc[16];
        snprintf(dsp_lc, sizeof(dsp_lc), "%s", d->name);
        for (char* c = dsp_lc; *c; c++)
            *c = (char)tolower((unsigned char)*c);

        char subdir[PATH_MAX];
        snprintf(subdir, sizeof(subdir), "%s/%s", dsp_base, dsp_lc);
        if (is_dir(subdir)) {
            vec_push(&ordered, subdir);
        }
    }

    strv_t envv = {0};
    if (dsp_all_env && *dsp_all_env) split_colon_paths(dsp_all_env, &envv);
    const char* specific = getenv(d->env_name);
    if (specific && *specific) split_colon_paths(specific, &envv);
    for (size_t i=0; i<envv.n; i++) {
        if (is_dir(envv.v[i])) {
            vec_push(&ordered, envv.v[i]);
        }
    }
    vec_free(&envv);

    for (size_t i=0; i<d->def_n; i++) {
        if (is_dir(d->defaults[i])) {
            vec_push(&ordered, d->defaults[i]);
        }
    }

    d->search = ordered;
}

#ifdef ANDROID
static void add_android_dsp_search_root(dsp_t *d)
{
    if (dsp_base_path[0] == '\0') {
        strncpy(dsp_base_path, "/vendor/dsp",
                sizeof(dsp_base_path) - 1);
    }

    char dsp_lc[16];
    snprintf(dsp_lc, sizeof(dsp_lc), "%s", d->name);
    for (char *c = dsp_lc; *c; ++c)
        *c = tolower((unsigned char)*c);

    char dsp_dir[PATH_MAX];
    snprintf(dsp_dir, sizeof(dsp_dir), "/vendor/dsp/%s", dsp_lc);

    if (is_dir(dsp_dir)) {
        vec_push(&d->search, dsp_dir);
    }
}
#endif

static bool dsp_devnode_ok(const dsp_t* d) {
    for (size_t i=0;i<d->dev_n;i++) if (is_chardev(d->devnodes[i])) return true;
    return false;
}

static const char* offload_block_reason(const dsp_t* d)
{
    if (!d->online)
        return "DSP offline";

    if (!d->modules_ok)
        return "Missing DSP modules";

    if (!d->fastrpc_lib_ok)
        return "Missing FastRPC user-space libs";

    if (!d->fw_build_found)
        return "Missing firmware build ID";

    if (!d->shell_build_found)
        return "Missing shell build ID";

    if (!d->build_id_match)
        return "Firmware/Shell build mismatch";

    if (!dsp_devnode_ok(d))
        return "Missing FastRPC device node";

    return "-";
}

static bool firmware_present_for(dsp_t* d)
{
    d->fw_present = false;
    d->fw_build_found = false;
    d->fw_build_id[0] = '\0';
    char fw_path[PATH_MAX];
    if (!get_remoteproc_firmware_path(d->rproc, fw_path, sizeof(fw_path))) {
        return false;
    }

    if (!path_exists(fw_path)) {
        return false;
    }

#ifdef ANDROID

    if (find_android_fw_build_id(d,
            d->fw_build_id,
            sizeof(d->fw_build_id))) {
        d->fw_build_found = true;
    } else {
        d->fw_build_found = false;
    }
#else
    if (extract_crm_build_id(fw_path,
            d->fw_build_id,
            sizeof(d->fw_build_id))) {
        d->fw_build_found = true;
    }
#endif

    d->fw_present = true;
    return true;
}

static void build_usr_lib_dirs(strv_t* out) {
    const char* ld = getenv("LD_LIBRARY_PATH");
    split_colon_paths(ld, out);

#ifdef ANDROID
    const char* stds[] = {
        "/vendor/lib64"
    };
#else
    const char* stds[] = {
        "/usr/lib"
    };
#endif

    for (size_t i=0;i<sizeof(stds)/sizeof(stds[0]); ++i) if (is_dir(stds[i])) vec_push(out, stds[i]);
}

static void usage(const char* argv0) {
    fprintf(stderr,
      "Usage: %s [OPTIONS]\n"
      "Options:\n"
      "  -l, --lib-path DSP:PATH   Add library search path for specific DSP (repeatable)\n"
      "  -h, --help                Show this help message\n"
      "\n"
      "DSP names: ADSP, CDSP, CDSP1, GDSP0, GDSP1, MDSP, SDSP\n"
      "ENV: DSP_LIBRARY_PATH (global for all DSPs)\n",
      argv0);
}

#ifdef PARSE_YAML
static bool scan_qcom_yaml_and_check_dsp_modules(
    const char* const dsp_module_pats[],
    size_t dsp_module_pats_sz
) {
    char machine_model[256] = "";
    bool found_any = false;
    bool found_signed_pd = false;
    bool found_unsigned_pd = false;

    if (read_text(MACHINE_MODEL_PATH, machine_model, sizeof(machine_model)) < 0) {
        return false;
    }

    char dirpath[PATH_MAX];
    snprintf(dirpath, sizeof(dirpath), "%s/conf.d", QCOM_BASE_DIR);
    DIR* dir = opendir(dirpath);
    if (!dir) {
        return false;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {

        if (!strstr(de->d_name, ".yaml"))
            continue;

        char yaml_path[PATH_MAX];
        snprintf(yaml_path, sizeof(yaml_path), "%s/%s", dirpath, de->d_name);

        FILE* f = fopen(yaml_path, "r");
        if (!f)
            continue;

        yaml_parser_t parser;
        yaml_event_t event;

        if (!yaml_parser_initialize(&parser)) {
            fclose(f);
            continue;
        }

        yaml_parser_set_input_file(&parser, f);

        bool in_machines = false;
        bool match_found = false;
        bool next_is_dsp_path = false;
        char dsp_lib_rel[PATH_MAX] = "";

        /* Parse YAML */
        while (yaml_parser_parse(&parser, &event)) {

            if (event.type == YAML_STREAM_END_EVENT) {
                yaml_event_delete(&event);
                break;
            }

            if (event.type == YAML_SCALAR_EVENT) {
                const char* val = (const char*)event.data.scalar.value;

                if (strcmp(val, "machines") == 0) {
                    in_machines = true;
                } else if (in_machines && !match_found &&
                           strcmp(val, "DSP_LIBRARY_PATH") != 0) {
                    if (strcmp(val, machine_model) == 0) {
                        match_found = true;
                    }
                } else if (match_found && strcmp(val, "DSP_LIBRARY_PATH") == 0) {
                    next_is_dsp_path = true;
                } else if (next_is_dsp_path) {
                    strncpy(dsp_lib_rel, val, sizeof(dsp_lib_rel) - 1);
                    break;
                }
            }

            yaml_event_delete(&event);
        }

        yaml_parser_delete(&parser);
        fclose(f);

        if (!match_found || dsp_lib_rel[0] == '\0')
            continue;

        char dsp_base[PATH_MAX];
        snprintf(dsp_base, sizeof(dsp_base), "%s/%s",
                 QCOM_BASE_DIR, dsp_lib_rel);


        DIR* dsp_dir = opendir(dsp_base);
        if (!dsp_dir) {
            closedir(dir);
            return false;
        }

        struct dirent* sub;
        while ((sub = readdir(dsp_dir)) != NULL) {
            if (sub->d_name[0] == '.')
                continue;

            char subdir[PATH_MAX];
            snprintf(subdir, sizeof(subdir), "%s/%s", dsp_base, sub->d_name);

            if (!is_dir(subdir))
                continue;

            if (dir_has_any(subdir,
                            dsp_module_pats,
                            dsp_module_pats_sz)) {
                found_any = true;
            }
            if (dir_has_any(subdir,
                            pd_shell_any,
                            sizeof(pd_shell_any) / sizeof(pd_shell_any[0]))) {
                found_signed_pd = true;
            }

            if (dir_has_any(subdir,
                            pd_shell_unsigned,
                            sizeof(pd_shell_unsigned) / sizeof(pd_shell_unsigned[0]))) {
                found_unsigned_pd = true;
            }

        }

        closedir(dsp_dir);
        closedir(dir);
        return found_any;
    }

    closedir(dir);
    return false;
}
#endif

#ifndef PARSE_YAML
static bool scan_qcom_yaml_and_check_dsp_modules(
    const char* const dsp_module_pats[],
    size_t dsp_module_pats_sz
)
{
    (void)dsp_module_pats;
    (void)dsp_module_pats_sz;
    return false;
}
#endif

int main(int argc, char** argv) {
    char base_fw_path[PATH_MAX] = FW_BASE_PATH;
    char matched_build_id[256] = "";
    dsp_t dsps[DSP_COUNT];
    memset(dsps, 0, sizeof(dsps));
    dsp_init_defaults(dsps);

    static struct option long_options[] = {
        {"lib-path", required_argument, 0, 'l'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "hl:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'l': {
                char* spec = xstrdup(optarg);
                char* colon = strchr(spec, ':');
                if (!colon) die("Use --lib-path DSP:PATH");
                *colon = '\0';
                const char* dsp = spec;
                const char* path = colon+1;
                dsp_id_t id = DSP_COUNT;
                if (!strcmp(dsp,"ADSP")) id=DSP_ADSP;
                else if (!strcmp(dsp,"CDSP")) id=DSP_CDSP;
                else if (!strcmp(dsp,"CDSP1")) id=DSP_CDSP1;
                else if (!strcmp(dsp,"GDSP0")) id=DSP_GDSP0;
                else if (!strcmp(dsp,"GDSP1")) id=DSP_GDSP1;
                else if (!strcmp(dsp,"MDSP")) id=DSP_MDSP;
                else if (!strcmp(dsp,"SDSP")) id=DSP_SDSP;
                else die("Unknown DSP in --lib-path: %s", dsp);
                vec_push(&dsps[id].cli_paths, path);
                free(spec);
                break;
            }
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 2;
        }
    }

    update_remoteproc_states(dsps);

    const char* dsp_all_env = getenv("DSP_LIBRARY_PATH");
    char yaml_base_rel[PATH_MAX] = "";
    char yaml_base_abs[PATH_MAX] = "";

    if (get_yaml_dsp_base(yaml_base_abs, sizeof(yaml_base_abs))) {
        strncpy(yaml_base_rel, yaml_base_abs + strlen(QCOM_BASE_DIR) + 1,
                sizeof(yaml_base_rel) - 1);
    }

    for (int i = 0; i < DSP_COUNT; i++) {
        build_search_for(&dsps[i], dsp_all_env,
                        yaml_base_rel[0] ? yaml_base_rel : NULL);
    }

#ifdef ANDROID
    for (int i = 0; i < DSP_COUNT; i++) {
        add_android_dsp_search_root(&dsps[i]);
    }
#endif

    const char* dma_heap = "/dev/dma_heap/system";
    bool dma_ok = is_chardev(dma_heap) || path_exists(dma_heap);

    strv_t usr_lib_dirs = {0}; build_usr_lib_dirs(&usr_lib_dirs);
    const char* fastrpc_lib_pats[] = {
        "libadsprpc.so", "libadsprpc.so.1", "libadsprpc.so.1.0.0",
        "libcdsprpc.so", "libcdsprpc.so.1", "libcdsprpc.so.1.0.0",
        "libsdsprpc.so", "libsdsprpc.so.1", "libsdsprpc.so.1.0.0",
        "libadsp_default_listener.so", "libadsp_default_listener.so.1", "libadsp_default_listener.so.1.0.0",
        "libcdsp_default_listener.so", "libcdsp_default_listener.so.1", "libcdsp_default_listener.so.1.0.0",
        "libsdsp_default_listener.so", "libsdsp_default_listener.so.1", "libsdsp_default_listener.so.1.0.0"
    };

    const char* dsp_module_pats[] = {
        "fastrpc_shell_*",
        "fastrpc_shell_unsigned_*"
    };

    bool yaml_has_modules = scan_qcom_yaml_and_check_dsp_modules(
        dsp_module_pats,
        sizeof(dsp_module_pats) / sizeof(dsp_module_pats[0])
    );

    for (int i = 0; i < DSP_COUNT; i++) {
        dsp_t* d = &dsps[i];

        bool fw_ok = firmware_present_for(d);

        d->online = d->present && d->remote_running && fw_ok;

        if (!d->online) {
            continue;
        }
        bool has_shell = false;

        for (size_t r = 0; r < d->search.n; r++) {

            const char* dir = d->search.v[r];
            if (!is_dir(dir)) continue;

            DIR* dp = opendir(dir);
            if (!dp) continue;

            struct dirent* de;
            while ((de = readdir(dp)) != NULL) {

                if (de->d_name[0] == '.') continue;

                for (size_t p = 0;
                    p < sizeof(dsp_module_pats) / sizeof(dsp_module_pats[0]); p++) {
                        if (fnmatch(dsp_module_pats[p], de->d_name, FNM_CASEFOLD) == 0) {
                            has_shell = true;
                            break;
                        }
                }
            }
            closedir(dp);
        }

        bool yaml_has_skel = yaml_has_modules;
        d->modules_ok = has_shell || yaml_has_skel;

#ifdef ANDROID
        scan_android_shell_availability(d);
#else
        scan_yaml_shell_availability(d);
#endif

        if (d->fw_build_found && find_shell_build_id(d, NULL)) {

            const char* fw_id = normalize_build_id(d->fw_build_id);
            const char* sh_id = normalize_build_id(d->shell_build_id);

            d->build_id_match =
                fw_id && sh_id && strcmp(fw_id, sh_id) == 0;

            if (d->build_id_match && matched_build_id[0] == '\0') {
                strncpy(matched_build_id, fw_id,
                        sizeof(matched_build_id) - 1);
            }
        }

        d->fastrpc_lib_ok = true;

        if (i == DSP_CDSP || i == DSP_CDSP1 ||
            i == DSP_GDSP0 || i == DSP_GDSP1 ||
            i == DSP_ADSP) {

            d->fastrpc_lib_ok = false;
            for (size_t r = 0; r < usr_lib_dirs.n && !d->fastrpc_lib_ok; ++r) {

                if (dir_has_any(
                        usr_lib_dirs.v[r],
                        fastrpc_lib_pats,
                        sizeof(fastrpc_lib_pats) /
                        sizeof(fastrpc_lib_pats[0]))) {

                    d->fastrpc_lib_ok = true;
                }
            }
        }

        bool build_ok = false;

        if (d->fw_build_found && d->shell_build_found) {
            build_ok = d->build_id_match;
        }

        d->offload_capable =
            d->online &&
            d->modules_ok &&
            d->fastrpc_lib_ok &&
            build_ok &&
            dsp_devnode_ok(d);

    }

    printf("DSP firmware path : %s\n", base_fw_path);
    printf("DSP build ID      : %s\n",
        matched_build_id[0] ? matched_build_id : "N/A");

    printf("DSP dynamic libs path : %s\n",
        dsp_base_path[0] ? dsp_base_path : "N/A");

    printf("\nDMA-BUF Heap Support:\nSystem heap -> %s\n", dma_ok ? "available" : "unavailable");

    printf("\nDSP Summary:\n");
    printf("%-8s %-10s %-9s %-11s %s\n",
        "DSP", "State", "SignedPD", "UnsignedPD*", "FastRPC Support");
    printf("---------------------------------------------------------------------------------\n");


    for (int i = 0; i < DSP_COUNT; i++) {
        dsp_t* d = &dsps[i];

        if (!d->present) {
            continue;
        }

        const char* state = d->online ? "Online" : "Offline";

        char fastrpc_support[128];
        if (!d->online) {
            snprintf(fastrpc_support, sizeof(fastrpc_support), "-");
        } else if (d->offload_capable) {
            snprintf(fastrpc_support, sizeof(fastrpc_support), "Yes");
        } else {
            snprintf(fastrpc_support, sizeof(fastrpc_support),
                    "No (%s)", offload_block_reason(d));
        }

        printf("%-8s %-10s %-9s %-11s %s\n",
            d->name,
            state,
            d->signed_pd   ? "Yes" : "No",
            d->unsigned_pd ? "Yes" : "No",
            fastrpc_support);


    }

    printf("\n* If Unsigned PD not supported, needs signing to run\n");

#ifdef PARSE_YAML
    if (cached_dsp_library_path) {
        free(cached_dsp_library_path);
        cached_dsp_library_path = NULL;
    }
#endif

    vec_free(&usr_lib_dirs);
    for (int i=0;i<DSP_COUNT;i++) { vec_free(&dsps[i].cli_paths); vec_free(&dsps[i].search); }
    return 0;
}
