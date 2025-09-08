// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FASTRPC_INTERNAL_UPSTREAM_H
#define FASTRPC_INTERNAL_UPSTREAM_H

#include <sys/ioctl.h>
#include <linux/types.h>

/* File only compiled  when support to upstream kernel is required*/


/**
 * FastRPC IOCTL functions
 **/
#define FASTRPC_IOCTL_ALLOC_DMA_BUFF		_IOWR('R', 1, struct fastrpc_ioctl_alloc_dma_buf)
#define FASTRPC_IOCTL_FREE_DMA_BUFF		_IOWR('R', 2, __u32)
#define FASTRPC_IOCTL_INVOKE			_IOWR('R', 3, struct fastrpc_ioctl_invoke)
#define FASTRPC_IOCTL_INIT_ATTACH		_IO('R', 4)
#define FASTRPC_IOCTL_INIT_CREATE		_IOWR('R', 5, struct fastrpc_ioctl_init_create)
#define FASTRPC_IOCTL_MMAP			_IOWR('R', 6, struct fastrpc_ioctl_req_mmap)
#define FASTRPC_IOCTL_MUNMAP			_IOWR('R', 7, struct fastrpc_ioctl_req_munmap)
#define FASTRPC_IOCTL_INIT_ATTACH_SNS		_IO('R', 8)
#define FASTRPC_IOCTL_INIT_CREATE_STATIC	_IOWR('R', 9, struct fastrpc_ioctl_init_create_static)
#define FASTRPC_IOCTL_MEM_MAP			_IOWR('R', 10, struct fastrpc_ioctl_mem_map)
#define FASTRPC_IOCTL_MEM_UNMAP			_IOWR('R', 11, struct fastrpc_ioctl_mem_unmap)
#define FASTRPC_IOCTL_GET_DSP_INFO		_IOWR('R', 13, struct fastrpc_ioctl_capability)

#define ADSPRPC_DEVICE "/dev/fastrpc-adsp"
#define SDSPRPC_DEVICE "/dev/fastrpc-sdsp"
#define MDSPRPC_DEVICE "/dev/fastrpc-mdsp"
#define CDSPRPC_DEVICE "/dev/fastrpc-cdsp"
#define CDSP1RPC_DEVICE "/dev/fastrpc-cdsp1"
#define GDSP0RPC_DEVICE "/dev/fastrpc-gdsp0"
#define GDSP1RPC_DEVICE "/dev/fastrpc-gdsp1"
#define ADSPRPC_SECURE_DEVICE "/dev/fastrpc-adsp-secure"
#define SDSPRPC_SECURE_DEVICE "/dev/fastrpc-sdsp-secure"
#define MDSPRPC_SECURE_DEVICE "/dev/fastrpc-mdsp-secure"
#define CDSPRPC_SECURE_DEVICE "/dev/fastrpc-cdsp-secure"
#define CDSP1RPC_SECURE_DEVICE "/dev/fastrpc-cdsp1-secure"
#define GDSP0RPC_SECURE_DEVICE "/dev/fastrpc-gdsp0-secure"
#define GDSP1RPC_SECURE_DEVICE "/dev/fastrpc-gdsp1-secure"

#define FASTRPC_ATTR_NOVA (256)

/* Secure and default device nodes */
#if DEFAULT_DOMAIN_ID==ADSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-adsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-adsp"
#elif DEFAULT_DOMAIN_ID==MDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-mdsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-mdsp"
#elif DEFAULT_DOMAIN_ID==SDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-sdsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-sdsp"
#elif DEFAULT_DOMAIN_ID==CDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/fastrpc-cdsp-secure"
	#define DEFAULT_DEVICE "/dev/fastrpc-cdsp"
#else
	#define SECURE_DEVICE ""
	#define DEFAULT_DEVICE ""
#endif

#define INITIALIZE_REMOTE_ARGS(total)	int *pfds = NULL; \
					unsigned *pattrs = NULL; \
					args = (struct fastrpc_invoke_args*) calloc(sizeof(*args), total);	\
					if(args==NULL) { 	\
						goto bail;	\
					}

#define DESTROY_REMOTE_ARGS()		if(args) {	\
						free(args);	\
					}

#define set_args(i, pra, len, filedesc, attrs)	args[i].ptr = (uint64_t)pra; \
						args[i].length = len;	\
						args[i].fd = filedesc;	\
						args[i].attr = attrs;

#define set_args_ptr(i, pra)		args[i].ptr = (uint64_t)pra
#define set_args_len(i, len)		args[i].length = len
#define set_args_attr(i, attrs)		args[i].attr = attrs
#define set_args_fd(i, filedesc)	args[i].fd = filedesc
#define get_args_ptr(i)			args[i].ptr
#define get_args_len(i)			args[i].length
#define get_args_attr(i)		args[i].attr
#define get_args_fd(i)			args[i].fd
#define append_args_attr(i, attrs)	args[i].attr |= attrs
#define get_args()			args
#define is_upstream()			1

//Utility macros for reading the ioctl structure
#define NOTIF_GETDOMAIN(response)	-1;
#define NOTIF_GETSESSION(response)	-1;
#define NOTIF_GETSTATUS(response)	-1;

#define FASTRPC_INVOKE2_STATUS_NOTIF		2	//TODO: Temporary change (Bharath to fix)
#define FASTRPC_INVOKE2_KERNEL_OPTIMIZATIONS	1	//TODO: Temporary change (Bharath to fix)
#ifndef FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK
#define FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK  1
#endif

struct fastrpc_invoke_args {
	__u64 ptr; /* pointer to invoke address*/
	__u64 length; /* size*/
	__s32 fd; /* fd */
	__u32 attr; /* invoke attributes */
};

struct fastrpc_ioctl_invoke {
	__u32 handle;
	__u32 sc;
	__u64 args;
};

struct fastrpc_ioctl_alloc_dma_buf {
	__s32 fd;	/* fd */
	__u32 flags; /* flags to map with */
	__u64 size;	/* size */
};

struct fastrpc_ioctl_init_create {
	__u32 filelen;	/* elf file length */
	__s32 filefd;	/* fd for the file */
	__u32 attrs;
	__u32 siglen;
	__u64 file;	/* pointer to elf file */
};

struct fastrpc_ioctl_init_create_static {
	__u32 namelen;	/* length of pd process name */
	__u32 memlen;
	__u64 name;	/* pd process name */
};

struct fastrpc_ioctl_req_mmap {
	__s32 fd;
	__u32 flags;	/* flags for dsp to map with */
	__u64 vaddrin;	/* optional virtual address */
	__u64 size;	/* size */
	__u64 vaddrout;	/* dsp virtual address */
};

struct fastrpc_ioctl_mem_map {
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

struct fastrpc_ioctl_req_munmap {
	__u64 vaddrout;	/* address to unmap */
	__u64 size;	/* size */
};

struct fastrpc_ioctl_mem_unmap {
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

/**
  * @brief internal data strcutures used in remote handle control
  *  fastrpc_ctrl_latency -
  *  fastrpc_ctrl_smmu - Allows the PD to use the shared SMMU context banks
  *  fastrpc_ctrl_kalloc - feature to allow the kernel allocate memory
  *                        for signed PD memory needs.
  *  fastrpc_ctrl_wakelock - enabled wake lock in user space and kernel
  *                          improves the response latency time of remote calls
  *  fastrpc_ctrl_pm - timeout (in ms) for which the system should stay awake
  *
  **/
struct fastrpc_ctrl_latency {
	uint32_t enable;
	uint32_t latency;
};

struct fastrpc_ctrl_smmu {
	uint32_t sharedcb;
};

struct fastrpc_ctrl_kalloc {
	uint32_t kalloc_support;
};

struct fastrpc_ctrl_wakelock {
	uint32_t enable;
};

struct fastrpc_ctrl_pm {
	uint32_t timeout;
};

struct fastrpc_ioctl_control {
	__u32 req;
	union {
		struct fastrpc_ctrl_latency lp;
		struct fastrpc_ctrl_smmu smmu;
		struct fastrpc_ctrl_kalloc kalloc;
		struct fastrpc_ctrl_wakelock wl;
		struct fastrpc_ctrl_pm pm;
	};
};

/* Types of context manage requests */
enum fastrpc_mdctx_manage_req {
	/* Setup multidomain context in kernel */
	FASTRPC_MDCTX_SETUP,
	/* Delete multidomain context in kernel */
	FASTRPC_MDCTX_REMOVE,
};

/* Payload for FASTRPC_MDCTX_SETUP type */
struct fastrpc_ioctl_mdctx_setup {
	/* ctx id in userspace */
	__u64 user_ctx;
	/* User-addr to list of domains on which context is being created */
	__u64 domain_ids;
	/* Number of domain ids */
	__u32 num_domains;
	/* User-addr where unique context generated by kernel is copied to */
	__u64 ctx;
	__u32 reserved[9];
};

/* Payload for FASTRPC_MDCTX_REMOVE type */
struct fastrpc_ioctl_mdctx_remove {
	/* kernel-generated context id */
	__u64 ctx;
	__u32 reserved[8];
};

/* Payload for FASTRPC_INVOKE_MDCTX_MANAGE type */
struct fastrpc_ioctl_mdctx_manage {
	/*
	 * Type of ctx manage request.
	 * One of "enum fastrpc_mdctx_manage_req"
	 */
	__u32 req;
	/* To keep struct 64-bit aligned */
	__u32 padding;
	union {
		struct fastrpc_ioctl_mdctx_setup setup;
		struct fastrpc_ioctl_mdctx_remove remove;
	};
};

#endif // FASTRPC_INTERNAL_H
