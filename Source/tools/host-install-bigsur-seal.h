#pragma once

#include <stdbool.h>
#include <stdint.h>

// Read an already decoded AppleArchive from input and publish its two Intel
// seal inputs into a private, open directory. Both descriptors are borrowed.
// Returns 0 or -1/errno. On failure, removes only files created by this call.
int VIMExtractIntelSealArchive(int input, int directory,
    bool (*progress)(uint64_t bytesWritten, void *context), void *context);
