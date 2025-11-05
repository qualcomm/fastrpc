// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _APPS_STD_H
#define _APPS_STD_H
#include <AEEStdDef.h>
#include <remote.h>

#ifndef __QAIC_HEADER
#define __QAIC_HEADER(ff) ff
#endif //__QAIC_HEADER

#ifndef __QAIC_HEADER_EXPORT
#define __QAIC_HEADER_EXPORT
#endif // __QAIC_HEADER_EXPORT

#ifndef __QAIC_HEADER_ATTRIBUTE
#define __QAIC_HEADER_ATTRIBUTE
#endif // __QAIC_HEADER_ATTRIBUTE

#ifndef __QAIC_IMPL
#define __QAIC_IMPL(ff) ff
#endif //__QAIC_IMPL

#ifndef __QAIC_IMPL_EXPORT
#define __QAIC_IMPL_EXPORT
#endif // __QAIC_IMPL_EXPORT

#ifndef __QAIC_IMPL_ATTRIBUTE
#define __QAIC_IMPL_ATTRIBUTE
#endif // __QAIC_IMPL_ATTRIBUTE
#ifdef __cplusplus
extern "C" {
#endif
#if !defined(__QAIC_STRING1_OBJECT_DEFINED__) && !defined(__STRING1_OBJECT__)
#define __QAIC_STRING1_OBJECT_DEFINED__
#define __STRING1_OBJECT__
typedef struct _cstring1_s {
   char* data;
   int dataLen;
} _cstring1_t;

#endif /* __QAIC_STRING1_OBJECT_DEFINED__ */
/**
    * standard library functions remoted from the apps to the dsp
    */
typedef int apps_std_FILE;
enum apps_std_SEEK {
   APPS_STD_SEEK_SET,
   APPS_STD_SEEK_CUR,
   APPS_STD_SEEK_END,
   _32BIT_PLACEHOLDER_apps_std_SEEK = 0x7fffffff
};
typedef enum apps_std_SEEK apps_std_SEEK;
typedef struct apps_std_DIR apps_std_DIR;
struct apps_std_DIR {
   uint64_t handle;
};
typedef struct apps_std_DIRENT apps_std_DIRENT;
struct apps_std_DIRENT {
   int ino;
   char name[255];
};
typedef struct apps_std_STAT apps_std_STAT;
struct apps_std_STAT {
   uint64_t tsz;
   uint64_t dev;
   uint64_t ino;
   uint32_t mode;
   uint32_t nlink;
   uint64_t rdev;
   uint64_t size;
   int64_t atime;
   int64_t atimensec;
   int64_t mtime;
   int64_t mtimensec;
   int64_t ctime;
   int64_t ctimensec;
};
/**
       * @retval, if operation fails errno is returned
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fopen)(const char* name, const char* mode, apps_std_FILE* psout) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_freopen)(apps_std_FILE sin, const char* name, const char* mode, apps_std_FILE* psout) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fflush)(apps_std_FILE sin) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fclose)(apps_std_FILE sin) __QAIC_HEADER_ATTRIBUTE;
/**
       * @param,  bEOF, if read or write bytes <= bufLen bytes then feof() is called
       *          and the result is returned in bEOF, otherwise bEOF is set to 0.
       * @retval, if read or write return 0 for non zero length buffers, ferror is checked
       *          and a non zero value is returned in case of error with no rout parameters
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fread)(apps_std_FILE sin, unsigned char* buf, int bufLen, int* bytesRead, int* bEOF) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fwrite)(apps_std_FILE sin, const unsigned char* buf, int bufLen, int* bytesWritten, int* bEOF) __QAIC_HEADER_ATTRIBUTE;
/**
       * @param, pos, this buffer is filled up to MIN(posLen, sizeof(fpos_t))
       * @param, posLenReq, returns sizeof(fpos_t)
       * @retval, if operation fails errno is returned
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fgetpos)(apps_std_FILE sin, unsigned char* pos, int posLen, int* posLenReq) __QAIC_HEADER_ATTRIBUTE;
/**
       * @param, if size of pos doesn't match the system size an error is returned.
       *         fgetpos can be used to query the size of fpos_t
       * @retval, if operation fails errno is returned
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fsetpos)(apps_std_FILE sin, const unsigned char* pos, int posLen) __QAIC_HEADER_ATTRIBUTE;
/**
       * @retval, if operation fails errno is returned
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_ftell)(apps_std_FILE sin, int* pos) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fseek)(apps_std_FILE sin, int offset, apps_std_SEEK whence) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_flen)(apps_std_FILE sin, uint64_t* len) __QAIC_HEADER_ATTRIBUTE;
/**
       * @retval, only fails if transport fails
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_rewind)(apps_std_FILE sin) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_feof)(apps_std_FILE sin, int* bEOF) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_ferror)(apps_std_FILE sin, int* err) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_clearerr)(apps_std_FILE sin) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_print_string)(const char* str) __QAIC_HEADER_ATTRIBUTE;
/**
       * @param val, must contain space for NULL
       * @param valLenReq, length required with NULL
       * @retval, if fails errno is returned
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_getenv)(const char* name, char* val, int valLen, int* valLenReq) __QAIC_HEADER_ATTRIBUTE;
/**
       * @retval, if fails errno is returned
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_setenv)(const char* name, const char* val, int override) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_unsetenv)(const char* name) __QAIC_HEADER_ATTRIBUTE;
/**
       * This function will try to open a file given directories in envvarname separated by
       * delim.
       * so given environment variable FOO_PATH=/foo;/bar
       *    fopen_wth_env("FOO_PATH", ";", "path/to/file", "rw", &out);
       * will try to open /foo/path/to/file, /bar/path/to/file
       * if the variable is unset, it will open the file directly
       *
       * @param envvarname, name of the environment variable containing the path
       * @param delim, delimiator string, such as ";"
       * @param name, name of the file
       * @param mode, mode
       * @param psout, output handle
       * @retval, 0 on success errno or -1 on failure
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fopen_with_env)(const char* envvarname, const char* delim, const char* name, const char* mode, apps_std_FILE* psout) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fgets)(apps_std_FILE sin, unsigned char* buf, int bufLen, int* bEOF) __QAIC_HEADER_ATTRIBUTE;
/**
       * This method will return the paths that are searched when looking for a file.
       *  The paths are defined by the environment variable (separated by delimiters)
       *  that is passed to the method.
       *
       * @param envvarname, name of the environment variable containing the path
       * @param delim, delimiator string, such as ";"
       * @param name, name of the file
       * @param paths, Search paths
       * @param numPaths, Actual number of paths found
       * @param maxPathLen, The max path length
       * @retval, 0 on success errno or -1 on failure
       *
       */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_get_search_paths_with_env)(const char* envvarname, const char* delim, _cstring1_t* paths, int pathsLen, uint32_t* numPaths, uint16_t* maxPathLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fileExists)(const char* path, bool* exists) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fsync)(apps_std_FILE sin) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fremove)(const char* name) __QAIC_HEADER_ATTRIBUTE;
/**
        * This function decrypts the file using the provided open file descriptor, closes the
        * original descriptor and return a new file descriptor.
        * @retval, if operation fails errno is returned
        */
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fdopen_decrypt)(apps_std_FILE sin, apps_std_FILE* psout) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_opendir)(const char* name, apps_std_DIR* dir) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_closedir)(const apps_std_DIR* dir) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_readdir)(const apps_std_DIR* dir, apps_std_DIRENT* dirent, int* bEOF) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_mkdir)(const char* name, int mode) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_rmdir)(const char* name) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_stat)(const char* name, apps_std_STAT* stat) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_ftrunc)(apps_std_FILE sin, int offset) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_frename)(const char* oldname, const char* newname) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fopen_fd)(const char* name, const char* mode, int* fd, int* len) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fclose_fd)(int fd) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(apps_std_fopen_with_env_fd)(const char* envvarname, const char* delim, const char* name, const char* mode, int* fd, int* len) __QAIC_HEADER_ATTRIBUTE;
#ifdef __cplusplus
}
#endif
#endif //_APPS_STD_H
