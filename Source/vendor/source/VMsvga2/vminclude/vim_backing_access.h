/* Shared normal GPU loans versus exclusive whole-VM-object CPU gates.
 * Caller serializes every operation with one lock. No allocation, pointers
 * on the wire, polling, or implicit destructor. Zero-initialize before use.
 * Unknown normal aliases conflict with EVERY exclusive gate. */
#ifndef VIM_BACKING_ACCESS_H
#define VIM_BACKING_ACCESS_H
struct VIMBackingAccessRegistry {
    enum {Limit=256,Conflict=0,Admitted=1,Full=-1,Duplicate=-2};
    struct Borrow {void* key;void* owner;} shared[Limit];
    void* exclusive[Limit];
    int borrow(void* key,void* owner) {
        if(!owner)return Duplicate;
        unsigned available=Limit;bool conflict=false;
        for(unsigned i=0;i<Limit;++i){
            if(shared[i].owner==owner)return Duplicate;
            if(!shared[i].owner && available==Limit)available=i;
            if(exclusive[i] && (!key || exclusive[i]==key))conflict=true;
        }
        if(conflict)return Conflict;
        if(available==Limit)return Full;
        shared[available].key=key;shared[available].owner=owner;return Admitted;
    }
    bool promote(void* key,void* owner) {
        if(!key || !owner)return false;
        unsigned own=Limit,available=Limit;
        for(unsigned i=0;i<Limit;++i){
            if(exclusive[i]==key)return false;
            if(!exclusive[i] && available==Limit)available=i;
            if(shared[i].owner==owner){if(shared[i].key!=key)return false;own=i;}
            else if(shared[i].owner && (!shared[i].key || shared[i].key==key))return false;
        }
        if(own==Limit || available==Limit)return false;
        shared[own].key=0;shared[own].owner=0;exclusive[available]=key;return true;
    }
    void returnBorrow(void* owner) {
        for(unsigned i=0;i<Limit;++i)if(shared[i].owner==owner){shared[i].key=0;shared[i].owner=0;return;}
    }
    void releaseExclusive(void* key) {
        for(unsigned i=0;i<Limit;++i)if(exclusive[i]==key){exclusive[i]=0;return;}
    }
};
#endif
