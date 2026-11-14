/* SPDX-License-Identifier: MIT - Original AtlanticOS/XNU RAM trace boundary. */
#ifndef _PEXPERT_ATLANTIC_LOG_H_
#define _PEXPERT_ATLANTIC_LOG_H_
#include <atlantic/early_log.h>

#if CONFIG_ATLANTIC_RAM_CONSOLE
#include <stdbool.h>
#if !CONFIG_ATLANTIC_FIRMWARE || defined(MACH_BSD) || defined(IOKIT)
#error RAM console requires the explicit firmware-only XNU profile
#endif
void atlantic_console_memory_putc(char character, bool poll);
#endif

/* Private boot diagnostics, not native console initialization or serial I/O.
 * The caller must satisfy the normal-RAM and writable-buffer contract of
 * atl_early_log_snapshot. No address/ownership proof is implied by this API. */
enum AtlanticEarlyLogStatus atlantic_xnu_log_snapshot(void *destination,
    size_t capacity, struct AtlanticEarlyLogSnapshot *metadata);
#endif
