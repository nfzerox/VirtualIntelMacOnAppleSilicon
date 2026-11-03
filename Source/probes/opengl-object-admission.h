/* Caller serializes this bounded kernel-private registry. The key is an
 * actually retained VM object, NEVER a user ID/descriptor/virtual address.
 * NULL reserves the deliberately exclusive old partial-object TIMER control.
 * There are no waits: competing aliases decline, independent objects proceed.
 */
#ifndef VIM_OPENGL_OBJECT_ADMISSION_H
#define VIM_OPENGL_OBJECT_ADMISSION_H
#define VIM_OBJECT_ADMISSION_LIMIT 256u
struct VIMObjectAdmission { void *keys[VIM_OBJECT_ADMISSION_LIMIT]; unsigned exclusive; };
static int VIMObjectReserve(struct VIMObjectAdmission *r,void *key)
{
    if(r->exclusive)return 0;
    unsigned free_slot=VIM_OBJECT_ADMISSION_LIMIT;
    for(unsigned i=0;i<VIM_OBJECT_ADMISSION_LIMIT;++i) {
        if(r->keys[i] && (!key || r->keys[i]==key))return 0;
        if(!r->keys[i] && free_slot==VIM_OBJECT_ADMISSION_LIMIT)free_slot=i;
    }
    if(!key){r->exclusive=1;return 1;}
    if(free_slot==VIM_OBJECT_ADMISSION_LIMIT)return 0;
    r->keys[free_slot]=key;return 1;
}
static int VIMObjectRelease(struct VIMObjectAdmission *r,void *key)
{
    if(!key){if(!r->exclusive)return 0;r->exclusive=0;return 1;}
    for(unsigned i=0;i<VIM_OBJECT_ADMISSION_LIMIT;++i)if(r->keys[i]==key){r->keys[i]=0;return 1;}
    return 0;
}
#endif
