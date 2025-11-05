// Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef __QCOM_FASTRPC_H__
#define __QCOM_FASTRPC_H__

#include <linux/types.h>

/**
 * FastRPC IOCTL functions
 **/
#define FASTRPC_IOCTL_ALLOC_DMA_BUFF		_IOWR('R', 1, struct fastrpc_alloc_dma_buf)
#define FASTRPC_IOCTL_FREE_DMA_BUFF		_IOWR('R', 2, __u32)
#define FASTRPC_IOCTL_INVOKE			_IOWR('R', 3, struct fastrpc_invoke)
#define FASTRPC_IOCTL_INIT_ATTACH		_IO('R', 4)
#define FASTRPC_IOCTL_INIT_CREATE		_IOWR('R', 5, struct fastrpc_init_create)
#define FASTRPC_IOCTL_MMAP			_IOWR('R', 6, struct fastrpc_req_mmap)
#define FASTRPC_IOCTL_MUNMAP			_IOWR('R', 7, struct fastrpc_req_munmap)
#define FASTRPC_IOCTL_INIT_ATTACH_SNS		_IO('R', 8)
#define FASTRPC_IOCTL_INIT_CREATE_STATIC	_IOWR('R', 9, struct fastrpc_init_create_static)
#define FASTRPC_IOCTL_MEM_MAP			_IOWR('R', 10, struct fastrpc_mem_map)
#define FASTRPC_IOCTL_MEM_UNMAP			_IOWR('R', 11, struct fastrpc_mem_unmap)
#define FASTRPC_IOCTL_GET_DSP_INFO		_IOWR('R', 13, struct fastrpc_ioctl_capability)

/**
 * @enum fastrpc_map_flags for fastrpc_mmap and fastrpc_munmap
 * @brief Types of maps with cache maintenance
 */
enum fastrpc_map_flags {
    /**
     * Map memory pages with RW- permission and CACHE WRITEBACK.
     * Driver will clean cache when buffer passed in a FastRPC call.
     * Same remote virtual address will be assigned for subsequent
     * FastRPC calls.
     */
    FASTRPC_MAP_STATIC,

    /** Reserved for compatibility with deprecated flag */
    FASTRPC_MAP_RESERVED,

    /**
     * Map memory pages with RW- permission and CACHE WRITEBACK.
     * Mapping tagged with a file descriptor. User is responsible for
     * maintenance of CPU and DSP caches for the buffer. Get virtual address
     * of buffer on DSP using HAP_mmap_get() and HAP_mmap_put() functions.
     */
    FASTRPC_MAP_FD,

    /**
     * Mapping delayed until user calls HAP_mmap() and HAP_munmap()
     * functions on DSP. User is responsible for maintenance of CPU and DSP
     * caches for the buffer. Delayed mapping is useful for users to map
     * buffer on DSP with other than default permissions and cache modes
     * using HAP_mmap() and HAP_munmap() functions.
     */
    FASTRPC_MAP_FD_DELAYED,

    /** Reserved for compatibility **/
    FASTRPC_MAP_RESERVED_4,
    FASTRPC_MAP_RESERVED_5,
    FASTRPC_MAP_RESERVED_6,
    FASTRPC_MAP_RESERVED_7,
    FASTRPC_MAP_RESERVED_8,
    FASTRPC_MAP_RESERVED_9,
    FASTRPC_MAP_RESERVED_10,
    FASTRPC_MAP_RESERVED_11,
    FASTRPC_MAP_RESERVED_12,
    FASTRPC_MAP_RESERVED_13,
    FASTRPC_MAP_RESERVED_14,
    FASTRPC_MAP_RESERVED_15,

    /**
     * This flag is used to skip CPU mapping,
     * otherwise behaves similar to FASTRPC_MAP_FD_DELAYED flag.
     */
    FASTRPC_MAP_FD_NOMAP,

    /** Update FASTRPC_MAP_MAX when adding new value to this enum **/
};

/* Max value of fastrpc_map_flags, used to validate range of supported flags */
#define FASTRPC_MAP_MAX FASTRPC_MAP_FD_NOMAP + 1

enum fastrpc_proc_attr {
  FASTRPC_MODE_DEBUG = 0x1,
  FASTRPC_MODE_PTRACE = 0x2,
  FASTRPC_MODE_CRC = 0x4,
  FASTRPC_MODE_UNSIGNED_MODULE = 0x8,
  FASTRPC_MODE_ADAPTIVE_QOS = 0x10,
  FASTRPC_MODE_SYSTEM_PROCESS = 0x20,
  FASTRPC_MODE_PRIVILEGED = 0x40, // this attribute will be populated in kernel
};

struct fastrpc_invoke_args {
	__u64 ptr; /* pointer to invoke address*/
	__u64 length; /* size*/
	__s32 fd; /* fd */
	__u32 attr; /* invoke attributes */
};

struct fastrpc_invoke {
	__u32 handle;
	__u32 sc;
	__u64 args;
};

struct fastrpc_init_create {
	__u32 filelen;	/* elf file length */
	__s32 filefd;	/* fd for the file */
	__u32 attrs;
	__u32 siglen;
	__u64 file;	/* pointer to elf file */
};

struct fastrpc_init_create_static {
	__u32 namelen;	/* length of pd process name */
	__u32 memlen;
	__u64 name;	/* pd process name */
};

struct fastrpc_alloc_dma_buf {
	__s32 fd;	/* fd */
	__u32 flags; /* flags to map with */
	__u64 size;	/* size */
};

struct fastrpc_req_mmap {
	__s32 fd;
	__u32 flags;	/* flags for dsp to map with */
	__u64 vaddrin;	/* optional virtual address */
	__u64 size;	/* size */
	__u64 vaddrout;	/* dsp virtual address */
};

struct fastrpc_mem_map {
	__s32 version;
	__s32 fd;		/* fd */
	__s32 offset;		/* buffer offset */
	__u32 flags;		/* flags defined in enum fastrpc_map_flags */
	__u64 vaddrin;		/* buffer virtual address */
	__u64 length;		/* buffer length */
	__u64 vaddrout;		/* [out] remote virtual address */
	__s32 attrs;		/* buffer attributes used for SMMU mapping */
	__s32 reserved[4];
};

struct fastrpc_req_munmap {
	__u64 vaddrout;	/* address to unmap */
	__u64 size;	/* size */
};

struct fastrpc_mem_unmap {
	__s32 version;
	__s32 fd;		/* fd */
	__u64 vaddr;		/* remote process (dsp) virtual address */
	__u64 length;		/* buffer size */
	__s32 reserved[5];
};

struct fastrpc_ioctl_capability {
	__u32 domain; /* domain of the PD*/
	__u32 attribute_id; /* attribute id*/
	__u32 capability;   /* dsp capability */
	__u32 reserved[4];
};

#endif /* __QCOM_FASTRPC_H__ */
