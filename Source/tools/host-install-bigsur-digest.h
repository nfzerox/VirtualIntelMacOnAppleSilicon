#pragma once

#include <stdint.h>

// Walk the original Berkeley DB in its signed record order. The callback
// borrows the record bytes only until it returns. Zero continues; a positive
// errno stops. The caller must own the already-authenticated DB path.
typedef int (*VIMBigSurDigestVisitor)(const uint8_t key[16],const uint8_t value[36],void *context);
int VIMVisitBigSurDigests(const char *path,VIMBigSurDigestVisitor visit,void *context);
