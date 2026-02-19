// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause
#ifndef _FASTRPC_IOCTL_DRM_H
#define _FASTRPC_IOCTL_DRM_H

#include <linux/types.h>
#include <drm/drm.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADSPRPC_DEVICE "/dev/accel/accel0"
#define SDSPRPC_DEVICE "/dev/accel/accel0"  /* Map to ADSP for now */
#define MDSPRPC_DEVICE "/dev/accel/accel0"  /* Map to ADSP for now */
#define CDSPRPC_DEVICE "/dev/accel/accel0"
#define CDSP1RPC_DEVICE "/dev/accel/accel0"  /* Map to CDSP for now */
#define GDSP0RPC_DEVICE "/dev/accel/accel0"  /* Map to CDSP for now */
#define GDSP1RPC_DEVICE "/dev/accel/accel0"  /* Map to CDSP for now */
#define ADSPRPC_SECURE_DEVICE "/dev/accel/accel0"
#define SDSPRPC_SECURE_DEVICE "/dev/accel/accel0"
#define MDSPRPC_SECURE_DEVICE "/dev/accel/accel0"
#define CDSPRPC_SECURE_DEVICE "/dev/accel/accel0"
#define CDSP1RPC_SECURE_DEVICE "/dev/accel/accel0"
#define GDSP0RPC_SECURE_DEVICE "/dev/accel/accel0"
#define GDSP1RPC_SECURE_DEVICE "/dev/accel/accel0"

#define FASTRPC_ATTR_NOVA (256)

/* Secure and default device nodes */
#if DEFAULT_DOMAIN_ID==ADSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/accel/accel0"
	#define DEFAULT_DEVICE "/dev/accel/accel0"
#elif DEFAULT_DOMAIN_ID==MDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/accel/accel0"
	#define DEFAULT_DEVICE "/dev/accel/accel0"
#elif DEFAULT_DOMAIN_ID==SDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/accel/accel0"
	#define DEFAULT_DEVICE "/dev/accel/accel0"
#elif DEFAULT_DOMAIN_ID==CDSP_DOMAIN_ID
	#define SECURE_DEVICE "/dev/accel/accel0"
	#define DEFAULT_DEVICE "/dev/accel/accel0"
#else
	#define SECURE_DEVICE "/dev/accel/accel0"
	#define DEFAULT_DEVICE "/dev/accel/accel0"
#endif

#define INITIALIZE_REMOTE_ARGS(total)	int *pfds = NULL; \
					unsigned *pattrs = NULL; \
					args = (struct qda_invoke_args*) calloc(sizeof(struct qda_invoke_args), total);	\
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

#define FASTRPC_INVOKE2_STATUS_NOTIF		2
#define FASTRPC_INVOKE2_KERNEL_OPTIMIZATIONS	1
#ifndef FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK
#define FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK  1
#endif

/* DRM command numbers for QDA IOCTLs */
#define DRM_QDA_QUERY	0x00
#define DRM_QDA_GEM_CREATE		0x01
#define DRM_QDA_GEM_MMAP_OFFSET	0x02
#define DRM_QDA_INIT_ATTACH		0x03
#define DRM_QDA_INIT_CREATE		0x04
#define DRM_QDA_MAP			0x05
#define DRM_QDA_MUNMAP			0x06
#define DRM_QDA_INVOKE			0x07

/* IOMMU domain types for memory allocation */
#define QDA_IOMMU_DOMAIN_32BIT_IPA	1
#define QDA_IOMMU_DOMAIN_39BIT_IPA	2

/* Default domain type - use NONE if 39BIT is not available */
#define QDA_DEFAULT_IOMMU_DOMAIN	QDA_IOMMU_DOMAIN_32BIT_IPA

/* Process attributes for initialization */
#define QDA_PROC_MODE_DEBUG		(1 << 0)
#define QDA_PROC_MODE_PRIVILEGED	(1 << 6)

/* Map request types */
#define QDA_MAP_REQUEST_LEGACY		1  /* Legacy MMAP operation */
#define QDA_MAP_REQUEST_ATTR		2  /* FD-based MEM_MAP operation with attributes */

/* Unmap request types */
#define QDA_MUNMAP_REQUEST_LEGACY	1  /* Legacy MUNMAP operation */
#define QDA_MUNMAP_REQUEST_ATTR	2  /* FD-based MEM_UNMAP operation */

struct fastrpc_invoke_args {
	__u64 ptr; /* pointer to invoke address*/
	__u64 length; /* size*/
	__s32 fd; /* fd */
	__u32 attr; /* invoke attributes */
};

/**
 * struct drm_qda_gem_create - Create a GEM buffer object
 * @size: Size of the buffer to allocate (in bytes)
 * @domain_type: IOMMU domain type for allocation
 * @handle: [out] Handle to the created GEM object
 *
 * This structure is used with DRM_IOCTL_QDA_GEM_CREATE to allocate
 * memory buffers that can be used by the AI accelerator.
 */
struct drm_qda_gem_create {
	__u64 size;
	__u32 handle;
};

struct drm_qda_gem_mmap_offset {
    __u32 handle;   // Input: GEM handle
    __u32 pad;      // Padding for alignment
    __u64 offset;   // Output: mmap offset
};

/**
 * struct qda_invoke_args - Individual argument descriptor for invoke
 * @ptr: Pointer to argument data or value
 * @length: Length of the argument data
 * @fd: File descriptor (for GEM objects) or -1 for direct data
 * @attr: Argument attributes
 */
struct qda_invoke_args {
	__u64 ptr;
	__u64 length;
	__s32 fd;
	__u32 attr;
};

/* Compatibility typedef for existing code */
typedef struct qda_invoke_args fastrpc_invoke_args;

/**
 * struct qda_invoke - Invoke a function on the accelerator
 * @handle: Handle to the function/method to invoke
 * @sc: Scalars parameter describing argument layout
 * @args: Pointer to array of qda_invoke_args structures
 *
 * This structure is used with DRM_IOCTL_QDA_INVOKE to execute
 * functions on the AI accelerator.
 */
struct qda_invoke {
	__u32 handle;
	__u32 sc;
	__u64 args;
};

/**
 * struct qda_mem_map - Map memory for accelerator access
 * @request: Request type (QDA_MAP_REQUEST_LEGACY or QDA_MAP_REQUEST_ATTR)
 * @flags: Mapping flags
 * @fd: File descriptor of the buffer to map
 * @attrs: SMMU attributes (used for QDA_MAP_REQUEST_ATTR)
 * @offset: Offset within buffer (used for QDA_MAP_REQUEST_ATTR)
 * @reserved: Reserved for alignment/future use
 * @vaddrin: Input virtual address (optional)
 * @size: Size of the mapping
 * @vaddrout: [out] Output DSP virtual address
 */
struct qda_mem_map {
	__u32 request;	/* Request type: QDA_MAP_REQUEST_* */
	__u32 flags;	/* Flags for DSP to map with */
	__s32 fd;	/* File descriptor */
	__u32 attrs;	/* SMMU attributes (for ATTR request) */
	__u32 offset;	/* Offset within buffer (for ATTR request) */
	__u32 reserved;	/* Reserved for alignment/future use */
	__u64 vaddrin;	/* Optional virtual address */
	__u64 size;	/* Size of mapping */
	__u64 vaddrout;	/* DSP virtual address (output) */
};

/**
 * struct qda_mem_unmap - Unmap memory from accelerator
 * @request: Request type (QDA_MUNMAP_REQUEST_LEGACY or QDA_MUNMAP_REQUEST_ATTR)
 * @fd: File descriptor (used for ATTR request)
 * @vaddr: Virtual address to unmap (used for ATTR request)
 * @vaddrout: DSP virtual address to unmap (used for LEGACY request)
 * @size: Size of the mapping to unmap
 */
struct qda_mem_unmap {
	__u32 request;	/* Request type: QDA_MUNMAP_REQUEST_* */
	__s32 fd;	/* File descriptor (for ATTR request) */
	__u64 vaddr;	/* Virtual address (for ATTR request) */
	__u64 vaddrout;	/* DSP virtual address (for LEGACY request) */
	__u64 size;	/* Size of mapping */
};

/**
 * struct qda_init_create - Initialize and create a process on accelerator
 * @filelen: Length of the ELF file
 * @filefd: File descriptor containing the ELF file (GEM object)
 * @attrs: Process attributes (debug, privileged, etc.)
 * @siglen: Length of signature data
 * @file: Pointer to ELF file data (if not using filefd)
 */
struct qda_init_create {
	__u32 filelen;
	__s32 filefd;
	__u32 attrs;
	__u32 siglen;
	__u64 file;
};

/**
 * @brief internal data structures used in remote handle control
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

/**
 * struct drm_qda_device_info - Device information query
 * @dsp_name: Name of DSP (e.g., "adsp", "cdsp", "cdsp1", "gdsp0", "gdsp1")
 * @capabilities: Capability flags
 *
 * This structure is used with DRM_IOCTL_QDA_QUERY to query
 * device type and capabilities, allowing userspace to identify which
 * DSP a device node represents. The kernel provides the DSP name directly
 * as a null-terminated string.
 */
struct drm_qda_query {
	__u8 dsp_name[16];
	__u32 capabilities;
};

/* DRM IOCTLs for QDA */
#define DRM_IOCTL_QDA_QUERY	DRM_IOR(DRM_COMMAND_BASE + DRM_QDA_QUERY, struct drm_qda_query)
#define DRM_IOCTL_QDA_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_GEM_CREATE, struct drm_qda_gem_create)
#define DRM_IOCTL_QDA_GEM_MMAP_OFFSET	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_GEM_MMAP_OFFSET, struct drm_qda_gem_mmap_offset)
#define DRM_IOCTL_QDA_INIT_ATTACH	DRM_IO(DRM_COMMAND_BASE + DRM_QDA_INIT_ATTACH)
#define DRM_IOCTL_QDA_INIT_CREATE	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_INIT_CREATE, struct qda_init_create)
#define DRM_IOCTL_QDA_MAP		DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_MAP, struct qda_mem_map)
#define DRM_IOCTL_QDA_MUNMAP		DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_MUNMAP, struct qda_mem_unmap)
#define DRM_IOCTL_QDA_INVOKE		DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_INVOKE, struct qda_invoke_args)

/* Helper macros for building scalars parameter */
#define QDA_SCALARS(method, in, out) \
	(((method & 0x1f) << 24) | ((in & 0xff) << 16) | ((out & 0xff) << 8))

#define QDA_BUILD_SCALARS(attr, method, in, out, oin, oout) \
	(((attr & 0x07) << 29) | ((method & 0x1f) << 24) | \
	 ((in & 0xff) << 16) | ((out & 0xff) << 8) | \
	 ((oin & 0x0f) << 4) | (oout & 0x0f))

// Backward compatibility canges

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

#ifdef __cplusplus
}
#endif

#endif /* _FASTRPC_IOCTL_DRM_H */
