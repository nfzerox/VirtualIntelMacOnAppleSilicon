#pragma once
#include <stdbool.h>
#include <stdint.h>

// Cold media preparation only. Wrap a raw HFS+ volume in an Apple_HFS GPT
// partition, preserving every filesystem byte, blessing and volume UUID.
// Destination must not exist. Returns -1/errno; cancellation removes only
// the newly created partial output, never the input volume.
int VIMWrapHFSInstallerDisk(const char *source, const char *destination,
    bool (*progress)(uint64_t completed, uint64_t total, void *context), void *context);

// Copy only the HFS+ partition of our own read-only mounted restore image.
// The caller obtains the device from that mount, never a user-selected disk.
// No host authorization or filesystem reconstruction: preserve all bytes.
int VIMCopyHFSInstallerVolume(const char *device, const char *destination,
    bool (*progress)(uint64_t completed, uint64_t total, void *context), void *context);
