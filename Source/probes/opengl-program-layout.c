/* Host-only bounds/mapping regression. Compile with ASan and UBSan. */
#include <assert.h>
#include "../compat/PVGOpenGLProbe/PVGGLProgram.c"
_Thread_local void *PVGGLLegacyListContext;

int main(void) {
    uint8_t policy[2];
    assert(selectPolicy(0,0,policy)&&policy[0]==1&&policy[1]==1);
    assert(selectPolicy(0,0x1405,policy)&&policy[0]==1&&policy[1]==1);
    const uint16_t types[]={0x14ff,0x1401,0x1403,0x1405};
    for(unsigned i=0;i<4;++i)
        assert(selectPolicy(255,types[i],policy)&&policy[0]==(1u<<i)&&policy[1]==4);
    assert(!selectPolicy(1,0x1403,policy));
    assert(!selectPolicy(255,0x1402,policy));
    unsigned char *native=malloc(NativeProgramSpan);
    Program *p=calloc(1,sizeof(*p));
    assert(native && p);
    p->native=native;
    assert(!pthread_mutex_init(&p->lock,NULL));
    for(unsigned iteration=0;iteration<1000;++iteration) {
        for(unsigned i=0;i<NativeProgramSpan;++i)
            native[i]=(unsigned char)(i*37+iteration);
        refresh(p);
        assert(!memcmp(native,p->donor,0x10));
        for(unsigned i=0x10;i<0x20;++i)assert(p->donor[i]==0);
        assert(!memcmp(native+0x10,p->donor+0x20,NativeProgramSpan-0x10));
        assert(!memcmp(native+0x172,p->donor+0x182,2));
        assert(p->donor[DonorProgramSpan-1]==native[NativeProgramSpan-1]);
    }
    pthread_mutex_destroy(&p->lock);
    free(p);free(native);
    puts("program span: 1000 refreshes, complete mapping and late mode field passed");
    return 0;
}
