/* SPDX-License-Identifier: MIT - QEMU translation probe interface. */
#ifndef ATLANTIC_QEMU_TRANSLATION_PROBE_H
#define ATLANTIC_QEMU_TRANSLATION_PROBE_H

#include <atlantic/boot_tree.h>
#include <atlantic/entry.h>

int atl_translation_probe(const struct AtlanticBootInventory *inventory,
                          const struct AtlanticEntryCapture *capture,
                          const uint8_t *tree, uint32_t tree_bytes);

#endif
