/* Regression fixture for the actual vendored OpenCore map compactor.
 * The 38 descriptors below are the captured 10J3250 panic map, not invented
 * memory ranges. check-opencore-memory-map.py supplies the exact source function.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef void VOID;
typedef UINTN EFI_STATUS;
typedef UINT8 BOOLEAN;
typedef struct {
    UINT32 Type, Pad;
    UINT64 PhysicalStart, VirtualStart, NumberOfPages, Attribute;
} EFI_MEMORY_DESCRIPTOR;
enum { EfiLoaderCode=1, EfiLoaderData=2, EfiBootServicesCode=3,
       EfiBootServicesData=4, EfiRuntimeServicesCode=5,
       EfiRuntimeServicesData=6, EfiConventionalMemory=7 };
#define IN
#define OUT
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_NOT_FOUND 14
#define EFI_MEMORY_RUNTIME UINT64_C(0x8000000000000000)
#define EFI_PAGES_TO_SIZE(p) ((UINT64)(p)<<12)
#define NEXT_MEMORY_DESCRIPTOR(d,s) ((EFI_MEMORY_DESCRIPTOR *)((UINT8 *)(d)+(s)))
#define CopyMem memmove
#ifndef VIM_TEST_OPENCORE_FUNCTION
#error Run this fixture through check-opencore-memory-map.py
#endif
#include VIM_TEST_OPENCORE_FUNCTION
typedef struct { EFI_MEMORY_DESCRIPTOR d; UINT64 opaque; } Record;
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR)==40, "UEFI descriptor prefix");
_Static_assert(sizeof(Record)==48, "actual captured descriptor stride");
#define ROW(t,p,v,n,a) {{t,0,UINT64_C(p),UINT64_C(v),UINT64_C(n),UINT64_C(a)},0}

static const Record captured[] = {
    ROW(7, 0x0, 0x0, 0xa0, 0xf),
    ROW(7, 0x100000, 0x0, 0x712, 0xf),
    ROW(6, 0x812000, 0xffffff8002004000, 0x1, 0x800000000000000f),
    ROW(7, 0x813000, 0x0, 0x7d9b9, 0xf),
    ROW(7, 0x7e1cc000, 0xffffff8002005000, 0x1, 0xf),
    ROW(7, 0x7e1cd000, 0x0, 0x1a95, 0xf),
    ROW(6, 0x7fc62000, 0xffffff8002006000, 0x100, 0x800000000000000f),
    ROW(5, 0x7fd62000, 0xffffff8002106000, 0xc8, 0x800000000000000f),
    ROW(6, 0x7fe2a000, 0xffffff80021ce000, 0x1, 0x800000000000000f),
    ROW(5, 0x7fe2b000, 0xffffff80021cf000, 0x2, 0x800000000000000f),
    ROW(6, 0x7fe2d000, 0xffffff80021d1000, 0x4, 0x800000000000000f),
    ROW(5, 0x7fe31000, 0xffffff80021d5000, 0x1, 0x800000000000000f),
    ROW(6, 0x7fe32000, 0xffffff80021d6000, 0x3, 0x800000000000000f),
    ROW(5, 0x7fe35000, 0xffffff80021d9000, 0x1, 0x800000000000000f),
    ROW(6, 0x7fe36000, 0xffffff80021da000, 0x3, 0x800000000000000f),
    ROW(5, 0x7fe39000, 0xffffff80021dd000, 0x2, 0x800000000000000f),
    ROW(6, 0x7fe3b000, 0xffffff80021df000, 0x3, 0x800000000000000f),
    ROW(5, 0x7fe3e000, 0xffffff80021e2000, 0x1, 0x800000000000000f),
    ROW(6, 0x7fe3f000, 0xffffff80021e3000, 0x3, 0x800000000000000f),
    ROW(5, 0x7fe42000, 0xffffff80021e6000, 0x7, 0x800000000000000f),
    ROW(6, 0x7fe49000, 0xffffff80021ed000, 0x5, 0x800000000000000f),
    ROW(5, 0x7fe4e000, 0xffffff80021f2000, 0x1, 0x800000000000000f),
    ROW(6, 0x7fe4f000, 0xffffff80021f3000, 0x4, 0x800000000000000f),
    ROW(5, 0x7fe53000, 0xffffff80021f7000, 0x1, 0x800000000000000f),
    ROW(6, 0x7fe54000, 0xffffff80021f8000, 0x4, 0x800000000000000f),
    ROW(5, 0x7fe58000, 0xffffff80021fc000, 0x1, 0x800000000000000f),
    ROW(6, 0x7fe59000, 0xffffff80021fd000, 0x4, 0x800000000000000f),
    ROW(5, 0x7fe5d000, 0xffffff8002201000, 0x2, 0x800000000000000f),
    ROW(6, 0x7fe5f000, 0xffffff8002203000, 0x3, 0x800000000000000f),
    ROW(0, 0x7fe62000, 0x0, 0x80, 0xf),
    ROW(9, 0x7fee2000, 0x0, 0x12, 0xf),
    ROW(10, 0x7fef4000, 0x0, 0x80, 0xf),
    ROW(3, 0x7ff74000, 0x0, 0x8, 0xf),
    ROW(6, 0x7ff7c000, 0xffffff8002206000, 0x84, 0x800000000000000f),
    ROW(0, 0xe0000000, 0x0, 0x10000, 0x1),
    ROW(11, 0xffc00000, 0xffffff800228a000, 0x400, 0x8000000000000001),
    ROW(7, 0x100000000, 0x0, 0x80000, 0xf),
    ROW(1, 0x140000000, 0x0, 0x4, 0xf),
};

int main(void)
{
    assert(sizeof(captured)/sizeof(captured[0])==38);
    const EFI_MEMORY_DESCRIPTOR *whole=&captured[36].d, *selector=&captured[37].d;
    assert(whole->Type==7 && selector->Type==1 && selector->NumberOfPages==4);
    assert(whole->PhysicalStart==UINT64_C(0x100000000));
    assert(selector->PhysicalStart==UINT64_C(0x140000000));
    /* Reconstruct the legitimate final split: conventional / loaded selector /
     * conventional. The faulty actual compactor produces the captured overlap.
     */
    Record tail[]={captured[36],captured[37],captured[36]};
    tail[0].d.NumberOfPages=(selector->PhysicalStart-whole->PhysicalStart)/4096;
    tail[2].d.PhysicalStart=selector->PhysicalStart+selector->NumberOfPages*4096;
    tail[2].d.NumberOfPages=whole->NumberOfPages-tail[0].d.NumberOfPages-selector->NumberOfPages;
    for(unsigned i=0;i<3;i++)tail[i].opaque=UINT64_C(0xcafe000000000000)+i;
    Record original[3]; memcpy(original,tail,sizeof(tail));
    UINTN size=sizeof(tail);
    assert(OcShrinkMemoryMap(&size,&tail[0].d,sizeof(Record))==EFI_SUCCESS);
    assert(tail[0].d.PhysicalStart==whole->PhysicalStart);
    assert(tail[0].d.NumberOfPages==whole->NumberOfPages);
    assert(tail[0].d.Attribute==whole->Attribute && tail[0].opaque==original[0].opaque);
#ifdef VIM_EXPECT_UPSTREAM_BUG
    assert(size==2*sizeof(Record));
    assert(memcmp(&tail[1],&original[1],sizeof(Record))==0);
    puts("REPRODUCED: actual upstream compactor counts stale selector inside merged high RAM");
#else
    assert(size==sizeof(Record));
    puts("PASS: merged high RAM counted once; no stale selector descriptor");
#endif

    /* An ordinary nonmerging map, including opaque tails, is untouched. */
    Record separate[]={ROW(0,0x1000,0,1,0xf),ROW(6,0x9000,0,1,0x800000000000000f)};
    separate[0].opaque=17;separate[1].opaque=42;
    Record before[2];memcpy(before,separate,sizeof(before));size=sizeof(separate);
    OcShrinkMemoryMap(&size,&separate[0].d,sizeof(Record));
    assert(size==sizeof(separate) && memcmp(separate,before,size)==0);

    /* Mid-list merging already worked: preserve the following reserved record. */
    Record middle[]={ROW(7,0x1000,0,1,0xf),ROW(1,0x2000,0,1,0xf),ROW(0,0x5000,0,1,0xf)};
    middle[2].opaque=99;size=sizeof(middle);
    OcShrinkMemoryMap(&size,&middle[0].d,sizeof(Record));
    assert(size==2*sizeof(Record) && middle[0].d.NumberOfPages==2);
    assert(middle[1].d.Type==0 && middle[1].d.PhysicalStart==0x5000 && middle[1].opaque==99);
    size=sizeof(Record);Record single=separate[0];
    assert(OcShrinkMemoryMap(&size,&single.d,sizeof(Record))==EFI_NOT_FOUND);
    assert(memcmp(&single,&separate[0],sizeof(Record))==0);
    puts("PASS: nonmerged, middle merge, reserved/runtime and opaque-stride records");
    return 0;
}

