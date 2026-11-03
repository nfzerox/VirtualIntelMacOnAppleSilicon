/* Bounded cache of ACTUAL successful native uniform-location replies.
 * A connection is one GL share group. Only positively tracked live program
 * creations are eligible: deleted-but-still-bound programs always use the
 * native query, so later unbinding cannot resurrect a cached deleted name.
 * All access is under that connection's existing submission lock.
 */
#ifndef VIM_PASSTHROUGH_OPENGL_PROGRAM_LOCATIONS_H
#define VIM_PASSTHROUGH_OPENGL_PROGRAM_LOCATIONS_H
#define DG_LOCATION_PROGRAMS 128u
#define DG_LOCATION_ENTRIES 256u
#define DG_LOCATION_NAME 128u
#define DG_LOCATION_CREATE 1u
#define DG_LOCATION_LINK 2u
#define DG_LOCATION_DELETE 3u
#define DG_LOCATION_FORGET 4u
struct DGLocationProgram {uint32_t name;uint64_t generation;};
struct DGLocationEntry {uint64_t generation;uint32_t program;int32_t value;
    unsigned length;char name[DG_LOCATION_NAME];};
struct DGProgramLocations {
    uint64_t serial,forgotten,hits,fills;unsigned exhausted;
    struct DGLocationProgram programs[DG_LOCATION_PROGRAMS];
    struct DGLocationEntry entries[DG_LOCATION_ENTRIES];
};
static unsigned DGLocationProgramIndex(uint32_t name)
{return (name*UINT32_C(2654435761))>>25;}
static uint64_t DGLocationNext(struct DGProgramLocations *s)
{
    /* Fail closed forever at exhaustion, never reuse an in-flight token. */
    if(s->exhausted)return 0;
    if(s->serial==UINT64_MAX){s->exhausted=1;return 0;}
    return ++s->serial;
}
static void DGLocationBorn(struct DGProgramLocations *s,uint32_t program)
{
    if(!program || s->exhausted)return;
    struct DGLocationProgram *p=&s->programs[DGLocationProgramIndex(program)];
    p->name=program;p->generation=DGLocationNext(s);
}
static void DGLocationMutation(struct DGProgramLocations *s,unsigned operation,uint32_t program)
{
    /* Compatibility list-heavy apps must not clear kilobytes on every call. A
     * monotonic eligibility watermark forgets all records in constant time. */
    if(operation==DG_LOCATION_FORGET){s->forgotten=DGLocationNext(s);return;}
    if(!program || s->exhausted)return;
    struct DGLocationProgram *p=&s->programs[DGLocationProgramIndex(program)];
    if(p->name!=program || p->generation<=s->forgotten)return;
    if(operation==DG_LOCATION_DELETE){p->name=0;p->generation=0;}
    else if(operation==DG_LOCATION_LINK)p->generation=DGLocationNext(s);
}
static uint64_t DGLocationGeneration(struct DGProgramLocations *s,uint32_t program)
{
    struct DGLocationProgram *p=&s->programs[DGLocationProgramIndex(program)];
    return program && !s->exhausted && p->name==program && p->generation>s->forgotten?p->generation:0;
}
static unsigned DGLocationIndex(uint32_t program,const char *name,unsigned length)
{
    uint32_t hash=UINT32_C(2166136261)^program;
    for(unsigned i=0;i<length;++i)hash=(hash^(unsigned char)name[i])*UINT32_C(16777619);
    return hash&(DG_LOCATION_ENTRIES-1);
}
static int DGLocationLookup(struct DGProgramLocations *s,uint32_t program,const char *name,
    unsigned length,uint64_t *generation,int32_t *value)
{
    if(!name || length>=DG_LOCATION_NAME){*generation=0;return 0;}
    *generation=DGLocationGeneration(s,program);
    if(!*generation)return 0;
    struct DGLocationEntry *e=&s->entries[DGLocationIndex(program,name,length)];
    if(e->generation!=*generation || e->program!=program || e->length!=length ||
       memcmp(e->name,name,length))return 0;
    *value=e->value;++s->hits;return 1;
}
static void DGLocationStore(struct DGProgramLocations *s,uint32_t program,const char *name,
    unsigned length,uint64_t generation,int32_t value)
{
    if(!name || length>=DG_LOCATION_NAME || !generation || DGLocationGeneration(s,program)!=generation)return;
    struct DGLocationEntry *e=&s->entries[DGLocationIndex(program,name,length)];
    e->generation=generation;e->program=program;e->length=length;e->value=value;
    memcpy(e->name,name,length);++s->fills;
}
#endif
