// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef DSPQUEUE_SHARED_H
#define DSPQUEUE_SHARED_H

#include "dspqueue.h"
#include <stdbool.h>
#include <stdint.h>

/* Shared memory queue definitions.

   Each queue is allocated as a single shared ION buffer. The buffer consists
   of:
   * struct dspqueue_header
     - request packet queue header. Used for messages from the host CPU to the
   DSP.
     - response packet queue header. Used for messages from the DSP to the host
   CPU.
   * read/write states for each packet queue, including read/write pointers.
     Each read/write state structure must be on a separate cache line.
   * Request and response packet queues
     - Packet queues are circular buffers consisting packet headers and data
     - Packets are padded to be 64-bit aligned
     - The reader and writer manage read and write positions
     - Packets do not wrap around at the end of the queue. If a packet cannot
   fit before the end of the queue, the entire packet is written at the
   beginning. The 64-bit header is replicated.
*/

/* Header structure for each one-way packet queue.
   All offsets are in bytes to the beginning of the shared memory queue block.
 */
struct dspqueue_packet_queue_header {
	uint32_t queue_offset; /* Queue offset */
	uint32_t queue_length; /* Queue length in bytes */
	uint32_t
	    read_state_offset; /* Read state offset. Contains struct
	                          dspqueue_packet_queue_state, describing the
	                          state of the reader of this queue. */
	uint32_t
	    write_state_offset; /* Write state offset. Contains a struct
	                           dspqueue_packet_queue_state, describing the
	                           state of the writer of this queue. */
};

/* State structure, used to describe the state of the reader or writer of each
   queue. The state structure is at an offset from the start of the header as
   defined in struct dspqueue_packet_queue_header above, and must fit in a
   single cache line. */
struct dspqueue_packet_queue_state {
	volatile uint32_t position; /* Position within the queue in bytes */
	volatile uint32_t packet_count; /* Number of packets read/written */
	volatile uint32_t
	    wait_count; /* Non-zero if the reader/writer is waiting for a
	                   signal for a new packet or more space in the queue
	                   respectively */
};

/* Userspace shared memory queue header */
struct dspqueue_header {
	uint32_t version; /* Initial version 1, 2 if any flags are set and need
	                     to be checked. */
	int32_t error;
	uint32_t flags;
	struct dspqueue_packet_queue_header req_queue;  /* CPU to DSP */
	struct dspqueue_packet_queue_header resp_queue; /* DSP to CPU */
	uint32_t queue_count;
};

/* The version number currently expected if both CPU and DSP sides match */
#define DSPQUEUE_HEADER_CURRENT_VERSION 2

/* Wait counts present in the packet queue header. Set by the CPU, DSP must
   fail initialization if the feature is not supported. */
#define DSPQUEUE_HEADER_FLAG_WAIT_COUNTS 1

/* Use driver signaling. Set by the CPU, DSP must fail initialization
   if the feature is not supported. */
#define DSPQUEUE_HEADER_FLAG_DRIVER_SIGNALING 2

/* Unexpected flags */
#define DSPQUEUE_HEADER_UNEXPECTED_FLAGS                                      \
	~(DSPQUEUE_HEADER_FLAG_WAIT_COUNTS                                    \
	  | DSPQUEUE_HEADER_FLAG_DRIVER_SIGNALING)

/* Maximum queue size in bytes */
#define DSPQUEUE_MAX_QUEUE_SIZE 16777216

/* Maximum number of buffers in a packet */
#define DSPQUEUE_MAX_BUFFERS 64

/* Maximum message size */
#define DSPQUEUE_MAX_MESSAGE_SIZE 65536

/* Default sizes */
#define DSPQUEUE_DEFAULT_REQ_SIZE 65536
#define DSPQUEUE_DEFAULT_RESP_SIZE 16384

/* Maximum number of queues per process. Must ensure the state arrays get cache
   line aligned. Update signal allocations in dspsignal.h if this changes. */
#define DSPQUEUE_MAX_PROCESS_QUEUES 64

/* Process queue information block, used with RPC-based signaling.

   Each participant increments the packet/space count for the
   corresponding queue when there is a new packet or more space
   available and signals the other party. The other party then goes
   through active queues to see which one needs processing.

   This reduces the number of signals to two per process and lets us
   use argumentless FastRPC calls for signaling.
 */
struct dspqueue_process_queue_state {
	uint32_t req_packet_count[DSPQUEUE_MAX_PROCESS_QUEUES];
	uint32_t req_space_count[DSPQUEUE_MAX_PROCESS_QUEUES];
	uint32_t resp_packet_count[DSPQUEUE_MAX_PROCESS_QUEUES];
	uint32_t resp_space_count[DSPQUEUE_MAX_PROCESS_QUEUES];
};

/* Info specific to multi-domain queues */
struct dspqueue_multidomain {
	/* Flag to indicate if queue is multidomain */
	bool is_mdq;

	/* Multi-domain context id associated with queue */
	uint64_t ctx;

	/* Number of domains on which queue was created */
	unsigned int num_domain_ids;

	/* Effective domain ids on which queue was created */
	unsigned int *effec_domain_ids;

	/* Array of queue handles - one for each domain */
	dspqueue_t *queues;

	/* Array of queue ids - one for each domain */
	uint64_t *dsp_ids;
};

/* Signals IDs used with driver signaling. Update the signal allocations in
   dspsignal.h if this changes. */
enum dspqueue_signal {
	DSPQUEUE_SIGNAL_REQ_PACKET = 0,
	DSPQUEUE_SIGNAL_REQ_SPACE,
	DSPQUEUE_SIGNAL_RESP_PACKET,
	DSPQUEUE_SIGNAL_RESP_SPACE,
	DSPQUEUE_NUM_SIGNALS
};

#endif
