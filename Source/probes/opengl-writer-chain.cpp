/* The production state machine, not a substitute GPU/CPU ownership oracle. */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "../vendor/source/VMsvga2/vminclude/vim_writer_chain.h"
int main()
{
    VIMWriterChainState s;
    s.reset(11);assert(!s.ready() && !s.claim() && s.append(101));
    assert(s.epoch==12 && s.complete(101,true) && !s.ready()); // inline callback BEFORE publish
    assert(s.releaseWriter() && s.ready());
    VIMWriterChainState restoring=s;assert(restoring.startRestore() && !restoring.claim());
    assert(s.claim() && !s.startRestore() && !s.append(101)); // claim wins versus old completion
    assert(s.append(102) && s.releaseWriter() && !s.ready());
    assert(s.complete(101,true) && !s.ready()); // old/duplicate callback cannot open the CPU gate
    assert(s.complete(102,true) && s.startRestore() && !s.claim());
    s.reset(1);assert(s.append(1) && s.releaseWriter() && s.claim() && s.append(2) && s.releaseWriter());
    assert(s.complete(2,true) && !s.ready()); // newest callback before older delivery
    assert(s.complete(1,true) && s.ready() && !s.complete(999,true));
    s.reset(1);assert(s.append(1) && s.releaseWriter() && s.claim());
    assert(s.complete(1,true) && !s.ready() && s.releaseWriter() && s.ready()); // no-write refusal
    s.reset(1);assert(s.releaseWriter() && s.ready()); // initial refusal BEFORE native writes
    s.reset(1);assert(s.append(1) && s.releaseWriter() && s.claim());
    assert(s.complete(1,true) && s.releaseWriter(true) && s.epoch==3 && s.ready()); // REAL drained close
    s.reset(1);assert(s.append(1) && s.releaseWriter() && s.complete(1,false));
    assert(!s.ready() && !s.claim() && !s.startRestore()); // quarantine on a failed completion
    s.reset(1);
    for(unsigned i=0;i<VIMWriterChainState::Limit;++i){
        if(i)assert(s.claim());assert(s.append(i+1) && s.complete(i+1,true) && s.releaseWriter());
    }
    assert(!s.claim() && s.count==VIMWriterChainState::Limit && s.epoch==65 && s.startRestore());
    s.reset(~uint64_t(0));assert(!s.append(1) && !s.releaseWriter(true) && s.failed);
    puts("PASS writer coordinator race orders, inline/late/failed callbacks, exact epochs, cancellation, limits and restoration exclusion");
    return 0;
}
