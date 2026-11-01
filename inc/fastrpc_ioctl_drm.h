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

/*
 * The invoke path fills these with a CPU pointer in .offset and an fd in
 * .handle; ioctl_invoke() translates each entry to a GEM handle + offset.
 */
#define INITIALIZE_REMOTE_ARGS(total)	int *pfds = NULL; \
					unsigned *pattrs = NULL; \
					args = (struct drm_qda_fastrpc_invoke_args*) calloc(sizeof(struct drm_qda_fastrpc_invoke_args), total);	\
					if(args==NULL) { 	\
						goto bail;	\
					}

#define DESTROY_REMOTE_ARGS()		if(args) {	\
						free(args);	\
					}

#define set_args(i, pra, len, filedesc, attrs)	args[i].offset = (uint64_t)pra; \
						args[i].length = len;	\
						args[i].handle = (uint32_t)(filedesc);	\
						args[i].attr = attrs;

#define set_args_ptr(i, pra)		args[i].offset = (uint64_t)pra
#define set_args_len(i, len)		args[i].length = len
#define set_args_attr(i, attrs)		args[i].attr = attrs
#define set_args_fd(i, filedesc)	args[i].handle = (uint32_t)(filedesc)
#define get_args_ptr(i)			args[i].offset
#define get_args_len(i)			args[i].length
#define get_args_attr(i)		args[i].attr
#define get_args_fd(i)			args[i].handle
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

/* DRM command numbers for QDA IOCTLs (mirrors kernel uapi qda_accel.h) */
#define DRM_QDA_QUERY			0x00
#define DRM_QDA_GEM_CREATE		0x01
#define DRM_QDA_GEM_MMAP_OFFSET		0x02
#define DRM_QDA_REMOTE_INVOKE		0x03
#define DRM_QDA_REMOTE_SESSION_CREATE	0x04
#define DRM_QDA_REMOTE_MMAP		0x05
#define DRM_QDA_REMOTE_MUNMAP		0x06
#define DRM_QDA_REMOTE_SESSION_ATTACH	0x07
#define DRM_QDA_SET_OPTION		0x08

/* IOMMU domain types for memory allocation */
#define QDA_IOMMU_DOMAIN_32BIT_IPA	1
#define QDA_IOMMU_DOMAIN_39BIT_IPA	2

/* Default domain type - use NONE if 39BIT is not available */
#define QDA_DEFAULT_IOMMU_DOMAIN	QDA_IOMMU_DOMAIN_32BIT_IPA

/* Process attributes for initialization */
#define QDA_PROC_MODE_DEBUG		(1 << 0)
#define QDA_PROC_MODE_PRIVILEGED	(1 << 6)

/* Request types for drm_qda_mem_map.request */
#define QDA_REQUEST_MMAP	1	/* Map a whole buffer */
#define QDA_REQUEST_MEM_MAP	2	/* Map a region of a buffer, with attributes */

/* Request types for drm_qda_mem_unmap.request */
#define QDA_REQUEST_MUNMAP	1
#define QDA_REQUEST_MEM_UNMAP	2

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
	__u32 pad;
};

struct drm_qda_gem_mmap_offset {
	__u64 offset;
	__u32 handle;
	__u32 pad;
};

/**
 * struct drm_qda_fastrpc_invoke_args - FastRPC invocation argument descriptor
 * @offset: Byte offset within the buffer at which the argument starts
 * @length: Length of the argument data in bytes
 * @handle: GEM handle of the buffer holding the argument
 * @attr: Argument attributes and flags
 *
 * The driver never accepts DMA-BUF fds directly; userspace imports any fd to
 * a GEM handle before filling @handle.
 */
struct drm_qda_fastrpc_invoke_args {
	__u64 offset;
	__u64 length;
	__u32 handle;
	__u32 attr;
};

/* Compatibility typedef for existing code */
typedef struct drm_qda_fastrpc_invoke_args fastrpc_invoke_args;

/* struct drm_qda_invoke_args - Dynamic FastRPC invocation parameters */
struct drm_qda_invoke_args {
	__u32 handle;
	__u32 sc;
	__u64 args;
};

/* Process attribute flags for drm_qda_init_create.attrs */
#define QDA_INIT_ATTR_UNSIGNED_MODULE	(1U << 3)

/* struct drm_qda_init_create - DSP process creation parameters */
struct drm_qda_init_create {
	__u32 filelen;
	__u32 filehandle;
	__u32 attrs;
	__u32 siglen;
	__u64 file;
};

/* struct drm_qda_mem_map - Parameters for mapping a buffer into the DSP */
struct drm_qda_mem_map {
	__u32 request;
	__u32 flags;
	__u32 handle;
	__u32 attrs;
	__u32 offset;
	__s32 dsp_handle;
	__u64 vaddrin;
	__u64 size;
	__u64 vaddrout;
};

/* struct drm_qda_mem_unmap - Parameters for removing a DSP mapping */
struct drm_qda_mem_unmap {
	__u32 request;
	__s32 dsp_handle;
	__u64 vaddr;
	__u64 vaddrout;
	__u64 size;
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

/* Query type definitions for drm_qda_query */
#define QDA_QUERY_DSP_NAME		1
#define QDA_QUERY_DSP_CAPABILITIES	2

/**
 * struct drm_qda_capability_query - DSP capability query parameters
 * @attribute_id: DSP attribute to query (input)
 * @capability: Value of the queried attribute (output)
 */
struct drm_qda_capability_query {
	__u32 attribute_id;
	__u32 capability;
};

/**
 * struct drm_qda_query - Device information query structure
 * @query_type: Type of query (input)
 * @dsp_name: Null-terminated name of the DSP (returned when query_type is
 *            QDA_QUERY_DSP_NAME)
 * @cap: DSP capability query parameters (used when query_type is
 *       QDA_QUERY_DSP_CAPABILITIES)
 *
 * This structure is used with DRM_IOCTL_QDA_QUERY to query device attributes
 * based on @query_type. @dsp_name and @cap are mutually exclusive, since
 * only one query is made per call.
 */
struct drm_qda_query {
	__u32 query_type;
	union {
		__u8 dsp_name[16];
		struct drm_qda_capability_query cap;
	};
};

/* Protection domain types for drm_qda_remote_attach */
#define QDA_ATTACH_ROOT_PD	1
#define QDA_ATTACH_SENSORS_PD	2
#define QDA_ATTACH_AUDIO_PD	3

/**
 * struct drm_qda_remote_attach - Attach to a DSP-resident protection domain
 * @pd_type: Protection domain to attach to (QDA_ATTACH_*_PD)
 * @namelen: Length of @name in bytes, including any NUL terminator (only
 *           for QDA_ATTACH_AUDIO_PD; must be 0 otherwise)
 * @name: Pointer to the name of the static PD to attach to (only for
 *        QDA_ATTACH_AUDIO_PD; must be 0 otherwise)
 *
 * Used with DRM_IOCTL_QDA_REMOTE_SESSION_ATTACH. Requires CAP_SYS_ADMIN.
 */
struct drm_qda_remote_attach {
	__u32 pd_type;
	__u32 namelen;
	__u64 name;
};

/* Request IDs for drm_qda_set_option.request_id */
#define QDA_SET_OPTION_POLL_MODE	1

/* Values for drm_qda_set_option.value with QDA_SET_OPTION_POLL_MODE */
#define QDA_POLL_MODE_DISABLE	0
#define QDA_POLL_MODE_ENABLE	1

/* struct drm_qda_set_option - Set a per-session option */
struct drm_qda_set_option {
	__u32 request_id;
	__u32 value;
};

/* DRM IOCTLs for QDA */
#define DRM_IOCTL_QDA_QUERY		DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_QUERY, struct drm_qda_query)
#define DRM_IOCTL_QDA_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_GEM_CREATE, struct drm_qda_gem_create)
#define DRM_IOCTL_QDA_GEM_MMAP_OFFSET	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_GEM_MMAP_OFFSET, struct drm_qda_gem_mmap_offset)
#define DRM_IOCTL_QDA_REMOTE_INVOKE	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_INVOKE, struct drm_qda_invoke_args)
#define DRM_IOCTL_QDA_REMOTE_SESSION_CREATE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_SESSION_CREATE, struct drm_qda_init_create)
#define DRM_IOCTL_QDA_REMOTE_MMAP	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_MMAP, struct drm_qda_mem_map)
#define DRM_IOCTL_QDA_REMOTE_MUNMAP	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_MUNMAP, struct drm_qda_mem_unmap)
#define DRM_IOCTL_QDA_REMOTE_SESSION_ATTACH \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QDA_REMOTE_SESSION_ATTACH, struct drm_qda_remote_attach)
#define DRM_IOCTL_QDA_SET_OPTION	DRM_IOW(DRM_COMMAND_BASE + DRM_QDA_SET_OPTION, struct drm_qda_set_option)

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

/* Set option IOCTL request ID to enable poll mode */
#define FASTRPC_POLL_MODE	1

struct fastrpc_ioctl_set_option {
	__u32 req;		/* request id */
	__u32 value;	/* value */
	__s32 reserved[6];
};

#ifdef __cplusplus
}
#endif

#endif /* _FASTRPC_IOCTL_DRM_H */
