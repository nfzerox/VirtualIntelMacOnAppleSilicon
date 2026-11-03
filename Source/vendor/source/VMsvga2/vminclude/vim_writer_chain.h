/* Actual kernel coordinator state; caller holds ONE external lock. No
 * allocation, timers, pointers or inferred GPU completion. CPU restoration
 * and claim race at startRestore()/claim(), not at a later source unlock.
 * The kernel retains every published ticket until this gate retires. */
#ifndef VIM_WRITER_CHAIN_H
#define VIM_WRITER_CHAIN_H
struct VIMWriterChainState {
    enum {Limit=64};
    struct Item {uint64_t ticket;bool complete;} items[Limit];
    uint64_t epoch;
    unsigned count;
    bool writer,restoring,failed,closed;
    void reset(uint64_t initial) {
        epoch=initial;count=0;writer=true;restoring=failed=closed=false;
        for(unsigned i=0;i<Limit;++i){items[i].ticket=0;items[i].complete=false;}
    }
    uint64_t latest() const {return count?items[count-1].ticket:0;}
    bool claim() {
        if(writer || restoring || failed || closed || !count || count>=Limit || epoch==~uint64_t(0))return false;
        writer=true;return true;
    }
    bool append(uint64_t ticket) {
        if(!writer || restoring || failed || closed || !ticket || count>=Limit || epoch==~uint64_t(0))return false;
        for(unsigned i=0;i<count;++i)if(items[i].ticket==ticket)return false;
        items[count].ticket=ticket;items[count].complete=false;++count;++epoch;return true;
    }
    bool complete(uint64_t ticket,bool success) {
        for(unsigned i=0;i<count;++i)if(items[i].ticket==ticket){
            items[i].complete=true;if(!success)failed=true;return true;
        }
        return false;
    }
    bool releaseWriter(bool drained_write=false) {
        if(!writer || restoring || failed || closed)return false;
        if(drained_write){if(epoch==~uint64_t(0)){failed=true;return false;}++epoch;}
        writer=false;return true;
    }
    bool ready() const {
        if(writer || restoring || failed || closed)return false;
        for(unsigned i=0;i<count;++i)if(!items[i].complete)return false;
        return true;
    }
    bool startRestore() {if(!ready())return false;restoring=true;return true;}
};
#endif
