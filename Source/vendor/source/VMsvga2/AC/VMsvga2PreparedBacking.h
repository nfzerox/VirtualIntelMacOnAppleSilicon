/* Prepared RAM ownership for the private PassthroughOpenGL research driver.
 * Retaining an IOMemoryDescriptor does NOT retain its prepare/wire count.
 * One owner balances one successful prepare at its LAST release, independent
 * of a CGS surface's attachment/client lifetime. Retain this owner for a GPU
 * loan, not merely memory(). A pin is not CPU exclusion or GPU completion. */
#ifndef VIM_PREPARED_BACKING_H
#define VIM_PREPARED_BACKING_H
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSData.h>

class VMsvga2PreparedBacking: public OSObject
{
    OSDeclareDefaultStructors(VMsvga2PreparedBacking);
private:
    IOMemoryDescriptor* m_memory;
    IOLock* m_pages_lock;
    OSData* m_pages;
    IOMemoryMap* m_access_map;
    void* m_access_object;
    bool m_access_checked;
public:
    static VMsvga2PreparedBacking* withMemory(IOMemoryDescriptor*,IOReturn*);
    IOMemoryDescriptor* memory() const { return m_memory; } // borrowed while owner retained
    OSData* copyPages(uint32_t,IOReturn*); // retained immutable prefix; caller ALSO retains this owner
    IOMemoryMap* copyAccessMap(void**,IOReturn*); // exact-profile whole object, retained map/key lifetime
    void free();
};
#endif
