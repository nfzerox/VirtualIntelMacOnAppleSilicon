// Finite coverage checks for the actual hand-off range tracker. No VM changes.
#include "hvdbt-write-span.h"
#include <assert.h>
#include <stdio.h>

static void covered(const HvdbtWriteSpan *span,uint64_t first,uint64_t last)
{
    assert(span->count && span->count<=4);
    assert(span->ranges[0].first==span->first);
    assert(span->ranges[span->count-1].last==span->last);
    bool found=false;
    for(unsigned i=0;i<span->count;i++) {
        assert(span->ranges[i].first<=span->ranges[i].last);
        if(i)assert(span->ranges[i-1].last<span->ranges[i].first);
        found|=span->ranges[i].first<=first&&span->ranges[i].last>=last;
    }
    assert(found);
}

int main(void)
{
    const uint64_t page = 4096, ram = UINT64_C(6) << 30;
    HvdbtWriteSpan span = {0};
    for (unsigned i = 0; i < 65536; i++)
        hvdbt_write_span_note(&span, 0x1234, 16, page);
    assert(span.pending && span.first == 0x1000 && span.last == 0x1000);
    puts("PASS repeated writes stay one page, not an overflow-to-all-RAM request");
    span.pending = false;
    hvdbt_write_span_note(&span, 0x2ff8, 8192, page);
    assert(span.first == 0x2000 && span.last == 0x4000);
    hvdbt_write_span_note(&span, 0x1000, 1, page);
    assert(span.first == 0x1000 && span.last == 0x4000);
    puts("PASS unaligned and cross-page writes retain every touched page");
    span.pending = false;
    for (unsigned i = 0; i < 65; i++)
        hvdbt_write_span_note(&span, 0x100000 + i * page, 64, page);
    assert(span.first == 0x100000 && span.last == 0x140000);
    assert(span.count==1);
    printf("PASS 65 pages use one %llu-byte range rather than %llu bytes of RAM\n",
        (unsigned long long)(span.last - span.first + page), (unsigned long long)ram);
    span.pending=false;
    hvdbt_write_span_note(&span,0x1000,1,page);
    hvdbt_write_span_note(&span,UINT64_C(4)<<30,1,page);
    assert(span.count==2);
    assert(span.ranges[0].last-span.ranges[0].first+page+
           span.ranges[1].last-span.ranges[1].first+page==8192);
    covered(&span,0x1000,0x1000);
    covered(&span,UINT64_C(4)<<30,UINT64_C(4)<<30);
    puts("PASS distant writes scan 8KB, not the enclosing 4GB gap");
    uint64_t random = 1;
    for (unsigned epoch = 0; epoch < 1000; epoch++) {
        span.pending = false;
        uint64_t first = ram, last = 0;
        uint64_t seen_first[100],seen_last[100];
        for (unsigned i = 0; i < 100; i++) {
            random = random * UINT64_C(6364136223846793005) + 1;
            uint64_t address = random % ram;
            uint64_t length = 1 + (random >> 32) % 65536;
            if (length > ram - address) length = ram - address;
            uint64_t a = address & ~(page - 1), b = (address + length - 1) & ~(page - 1);
            if (a < first) first = a;
            if (b > last) last = b;
            hvdbt_write_span_note(&span, address, length, page);
            assert(span.pending && span.first == first && span.last == last);
            seen_first[i]=a;seen_last[i]=b;
            for(unsigned j=0;j<=i;j++)covered(&span,seen_first[j],seen_last[j]);
        }
    }
    puts("PASS 100000 scattered writes and epoch resets conservatively cover every page");
    return 0;
}
