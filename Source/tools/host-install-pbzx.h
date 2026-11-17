#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef bool (*VIMArchiveProgress)(uint64_t inputBytes, uint64_t decodedBytes, void *context);

// Stream a PBZX member into a private archive consumer, without a temporary
// decoded file. Both descriptors are borrowed. An incomplete output belongs
// to the caller's unpublished job. Return -1/errno on failure or cancellation.
int VIMDecodePBZX(int input, int output, VIMArchiveProgress progress, void *context);

// The same decoder can read an entry from a seekable installer archive
// without materializing the compressed member or feeding a second pipe.
typedef ssize_t (*VIMArchiveRead)(void *context, void *buffer, size_t count);
int VIMDecodePBZXFromReader(VIMArchiveRead read, void *input, int output, VIMArchiveProgress progress, void *context);

// Stream the Apple Archive inside an Intel Big Sur xmtr IMG4 payload.
int VIMDecodeIntelMTree(int input, int output, VIMArchiveProgress progress, void *context);
