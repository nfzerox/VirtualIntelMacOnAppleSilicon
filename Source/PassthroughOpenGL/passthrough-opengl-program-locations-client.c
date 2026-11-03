/* No allocations, environment polling, GL math or guessed query results.
 * Native errors, unknown/deleted objects, Begin/End and long names retain the
 * normal synchronous path. Generation checks also reject an in-flight query
 * fill if another shared context relinks/deletes/recreates the program.
 */
#include "direct-program-location-ops.inc"
static volatile int dg_program_locations_reuse=1; // Owned-process A/B only.
static unsigned DGProgramLocationOperation(uint32_t op)
{return op<974 ? dg_program_location_ops[op] : op==DG_DESTROY ? DG_LOCATION_FORGET : 0;}
static int DGProgramLocationGet(struct DGContext *context,uint32_t program,const char *name,
    GLint *value,uint64_t *generation)
{
    *generation=0;
    if(!context || !program || !name || !dg_program_locations_reuse ||
       context->common_state_blocked ||
       !(context->connection->capabilities&DG_CAP_VALIDATED_UNIFORM_LOCATIONS))return 0;
    size_t length=DGBoundedStringLength(name,DG_LOCATION_NAME);
    if(length>=DG_LOCATION_NAME)return 0;
    struct DGConnection *c=context->connection;
    pthread_mutex_lock(&c->lock);
    int hit=!c->dead && DGLocationLookup(&c->program_locations,program,name,(unsigned)length,generation,value);
    pthread_mutex_unlock(&c->lock);return hit;
}
static void DGProgramLocationPut(struct DGContext *context,uint32_t program,const char *name,
    uint64_t generation,GLint value)
{
    if(!context || !name || !generation || !dg_program_locations_reuse)return;
    size_t length=DGBoundedStringLength(name,DG_LOCATION_NAME);if(length>=DG_LOCATION_NAME)return;
    struct DGConnection *c=context->connection;
    pthread_mutex_lock(&c->lock);
    if(!c->dead)DGLocationStore(&c->program_locations,program,name,(unsigned)length,generation,value);
    pthread_mutex_unlock(&c->lock);
}
