/* Local downstream hand-off experiment; not an upstream contribution.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HVDBT_WRITE_SPAN_H
#define HVDBT_WRITE_SPAN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct HvdbtWriteSpan {
    uint64_t first, last; /* Inclusive page starts in QEMU's RAM address space. */
    bool pending;
    unsigned count;
    struct { uint64_t first, last; } ranges[4];
} HvdbtWriteSpan;

/* Nonempty writes have already been clipped to the RAMBlock by the caller.
 * Keep four sorted conservative ranges. On overflow merge the closest pair;
 * no touched page can be dropped. The enclosing span still bounds ONE TLB
 * walk, but dirty bitmap scans need not visit untouched gigabyte-sized gaps. */
static inline void hvdbt_write_span_note(HvdbtWriteSpan *span, uint64_t address,
                                       uint64_t length, uint64_t page_size)
{
    uint64_t mask = ~(page_size - 1);
    uint64_t first = address & mask;
    uint64_t last = (address + length - 1) & mask;
    uint64_t starts[5], ends[5];
    unsigned count = span->pending ? span->count : 0;
    unsigned pos = 0;
    while (pos < count && span->ranges[pos].first < first) {
        pos++;
    }
    for (unsigned i = 0; i < count + 1; i++) {
        if (i == pos) {
            starts[i] = first;
            ends[i] = last;
        } else {
            unsigned from = i < pos ? i : i - 1;
            starts[i] = span->ranges[from].first;
            ends[i] = span->ranges[from].last;
        }
    }
    count++;
    for (unsigned i = 1; i < count; ) {
        if (starts[i] <= ends[i - 1] || starts[i] - ends[i - 1] <= page_size) {
            if (ends[i] > ends[i - 1]) {
                ends[i - 1] = ends[i];
            }
            for (unsigned j = i; j + 1 < count; j++) {
                starts[j] = starts[j + 1];
                ends[j] = ends[j + 1];
            }
            count--;
        } else {
            i++;
        }
    }
    if (count > 4) {
        unsigned best = 1;
        for (unsigned i = 2; i < count; i++) {
            if (starts[i] - ends[i - 1] < starts[best] - ends[best - 1]) {
                best = i;
            }
        }
        ends[best - 1] = ends[best];
        for (unsigned j = best; j + 1 < count; j++) {
            starts[j] = starts[j + 1];
            ends[j] = ends[j + 1];
        }
        count--;
    }
    for (unsigned i = 0; i < count; i++) {
        span->ranges[i].first = starts[i];
        span->ranges[i].last = ends[i];
    }
    span->count = count;
    if (!span->pending || first < span->first) {
        span->first = first;
    }
    if (!span->pending || last > span->last) {
        span->last = last;
    }
    span->pending = true;
}

#endif
