/* Count actual monitor entries without changing their execution. No opcode,
 * pointer, pixel, output, allocation or clock collection on the normal path.
 * Counts may include fault/retry/exception entries: never call them retired
 * instructions or infer elapsed cost from a histogram alone.
 */
#ifndef ENGINE_X87_HELPER_PROFILE_H
#define ENGINE_X87_HELPER_PROFILE_H
#include "engine_abi.h"
static inline void engine_profile_x87_helper(EngineCtx *context,uint32_t op)
{
    if(!context->profile_x87_helpers)return;
    unsigned bucket;
    if(op==0xffff)bucket=576;
    else if(op<=0x7ff){
        unsigned esc=op>>8,modrm=op&255;
        bucket=modrm>=0xc0?64+esc*64+(modrm&63):esc*8+((modrm>>3)&7);
    }else{context->stat_x87_helpers_unclassified++;return;}
    context->stat_x87_helpers[bucket]++;
}
#endif
