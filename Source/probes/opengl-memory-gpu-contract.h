/* Private isolated lab only. No host pointer, public framework or automatic
 * renderer entry point. Prepared pages belong to the calling task's scratch
 * named object. The real source driver/GLI still uses its synchronous loans. */
#ifndef VIM_MEMORY_GPU_CONTRACT_H
#define VIM_MEMORY_GPU_CONTRACT_H
#include <stdint.h>
#define VIM_GATE_PREPARE 2u
#define VIM_GATE_COMMIT 3u
#define VIM_GATE_ABORT_DRAINED 4u
#define VIM_GATE_EVENT_COUNTS 5u
#define VIM_GATE_MAP_ALIAS 6u
#define VIM_GATE_MAX_PAGES 16u
struct VIMGatePages { uint64_t length,serial,pages[VIM_GATE_MAX_PAGES]; };
typedef char VIMGatePagesSize[(sizeof(struct VIMGatePages)==144)?1:-1];
#endif
