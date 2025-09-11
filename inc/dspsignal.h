// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef DSPSIGNAL_H
#define DSPSIGNAL_H

/** @file
    Internal FastRPC CPU-DSP signaling API.

    dspsignals are low-level userspace to userspace signals intended to be used
    as building blocks for client-visible inter-processor communication
    constructs. The key properties are:

    - 32-bit per-process signal identifiers, grouped into address
      spaces for different applications. Clients are responsible for
      managing their signal numbers.

    - Power efficient interrupt-based signaling. The implementation
      does not use shared memory polling or similar constructs -
      sending a signal involves triggering an interrupt on the
      receiving endpoint. This results in some key secondary
      properties:

        - Frameworks that aim for lower possible latency should use
          shared memory polling, WFE, or other similar mechanisms as
          the fast path and dspsignals as the longer-latency
          power-efficient fallback mechanism when appropriate.

        - Signaling latency is typically comparable to (but lower
          than) 50% of synchronous FastRPC call latency without
          polling.

        - Signaling latency will vary widely depending on the
          low-power modes the endpoints can use. Clients can influence
          this through DSP power voting and CPU PM QoS settings.

    - Reliable signal delivery. Every signal will eventually be
      delivered to the other endpoint and clients are not expected to
      implement retry loops.

    - Multiple instances of the same signal may get coalesced into
      one. In other words, if the signal sender calls dspsignal_send()
      multiple times in the loop, the client may see fewer
      dspsignal_wait() calls complete. It will however see at least
      one wait call complete after the last send call returns.

    - Signals may be delivered out of order

    - Clients may receive stale or spurious signals after reusing a
      signal ID. Due to this clients should not assume a specific
      event happened when receiving a signal, but should use another
      mechanism such as a shared memory structure to confirm it
      actually did.

    The signal APIs are intended for internal FastRPC use only.

    On process or subsystem restart the CPU client must destroy all
    signal instances and re-create them as needed.

    The CPU client process is expected to have an existing FastRPC
    session with the target DSP before creating signals. On the DSP
    signals can only be used in FastRPC user PDs.
*/

#include <AEEStdDef.h>
#include <stdint.h>
#include <stdlib.h>

/** Infinite timeout */
#define DSPSIGNAL_TIMEOUT_NONE UINT32_MAX

/** Remote domain ID for the application CPU */
#define DSPSIGNAL_DOMAIN_CPU 0xffff

/** Signal range currently supported: [0..1023]:
    - [0..255]: dspqueue signals
    - [256..1023]: reserved
*/
#define DSPSIGNAL_DSPQUEUE_MIN 0
#define DSPSIGNAL_DSPQUEUE_MAX 255
#define DSPSIGNAL_RESERVED_MIN (DSPSIGNAL_DSPQUEUE_MAX + 1)
#define DSPSIGNAL_RESERVED_MAX 1023
#define DSPSIGNAL_NUM_SIGNALS (DSPSIGNAL_RESERVED_MAX + 1)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a signal instance for use.
 *
 * @param [in] domain The remote processor/DSP the signal is used
 *                     with. Use CDSP_DOMAIN_ID for cDSP on the CPU,
 *                     DSPSIGNAL_DOMAIN_CPU for the main application
 *                     CPU on the DSP.
 * @param [in] id Signal ID. The ID must be unique within the process and
 *                           within the range specified above (i.e.
 * <DSPSIGNAL_NUM_SIGNALS)
 * @param [in] flags Signal flags, currently set to zero
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOMEMORY: Not enough memory available
 *         - AEE_EITEMBUSY: Signal ID already in use
 *         - AEE_EUNSUPPORTED: Signals not supported with the given remote
 * endpoint or support not present in the FastRPC driver
 *         - AEE_EBADPARM: Bad parameters, e.g. Invalid domain or signal ID
 */
AEEResult dspsignal_create(int domain, uint32_t id, uint32_t flags);

/**
 * Destroys a signal currently in use. This call will cancel all
 * pending wait operations on the signal.
 *
 * @param [in] domain The remote processor/DSP the signal is used
 *                     with. Use CDSP_DOMAIN_ID for cDSP on the CPU,
 *                     DSPSIGNAL_DOMAIN_CPU for the main application
 *                     CPU on the DSP.
 * @param [in] id Signal ID.
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCH: Unknown signal ID
 *         - AEE_EBADPARM: Bad parameters, e.g. Invalid domain
 */
AEEResult dspsignal_destroy(int domain, uint32_t id);

/**
 * Send a signal. After the call returns a signal will be delivered
 * to the endpoint asynchronously. The system may merge multiple
 * signals in flight, but is guaranteed to deliver at least one signal
 * to the remote endpoint after a call to this function returns.
 *
 * @param [in] domain The remote processor/DSP the signal is used
 *                     with. Use CDSP_DOMAIN_ID for cDSP on the CPU,
 *                     DSPSIGNAL_DOMAIN_CPU for the main application
 *                     CPU on the DSP.
 * @param [in] id Signal ID.
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCH: Unknown signal ID
 *         - AEE_EBADPARM: Bad parameters, e.g. Invalid domain
 */
AEEResult dspsignal_signal(int domain, uint32_t id);

/**
 * Wait for a signal, with an optional timeout.
 *
 * Only one thread can wait on a signal at a time. Otherwise
 * results are undefined.
 *
 * @param [in] domain The remote processor/DSP the signal is used
 *                     with. Use CDSP_DOMAIN_ID for cDSP on the CPU,
 *                     DSPSIGNAL_DOMAIN_CPU for the main application
 *                     CPU on the DSP.
 * @param [in] id Signal ID.
 * @param [in] timeout_usec Timeout in microseconds. Use DSPSIGNAL_TIMEOUT_NONE
 *                          for an infinite wait.
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCH: Unknown signal ID
 *         - AEE_EBADPARM: Bad parameters, e.g. Invalid domain
 *         - AEE_EEXPIRED: Timeout
 *         - AEE_EINTERRUPTED: Wait interrupted or cancelled
 */
AEEResult dspsignal_wait(int domain, uint32_t id, uint32_t timeout_usec);

/**
 * Cancel all pending wait operations on a signal on the local
 * processor. The signal cannot be used afterwards and must be
 * destroyed and re-created before using.
 *
 * @param [in] domain The remote processor/DSP the signal is used
 *                     with. Use CDSP_DOMAIN_ID for cDSP on the CPU,
 *                     DSPSIGNAL_DOMAIN_CPU for the main application
 *                     CPU on the DSP.
 * @param [in] id Signal ID.
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCH: Unknown signal ID
 *         - AEE_EBADPARM: Bad parameters, e.g. Invalid domain
 */
AEEResult dspsignal_cancel_wait(int domain, uint32_t id);

/**
 * Deinitialize allocated resources for a signal.
 * Should be call once all signals have been destroy for
 * a given domain.
 */
void dspsignal_domain_deinit(int domain);

/**
 * Destroy all allocated signals for the process.
 */
void deinit_process_signals();

#ifdef __cplusplus
}
#endif

#endif // DSPSIGNAL_H
