/* Local native-ARM PVG/Intel-guest experiment; not an upstream contribution.
 * Geometry matches the Intel donor: 4K pages, 32-bit PTEs, 10-bit levels.
 * This is only the page-table walker, not the complete donor adaptation. */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <stdint.h>
#include <string.h>

typedef BOOL (^PageRead)(uint64_t address, uint64_t size, void *out);
typedef void (^PageVisit)(uint64_t offset, uint32_t page, BOOL flag);
static Ivar root_ivar, depth_ivar, holes_ivar, read_ivar;

static void *mapped_page(id self, SEL cmd, uint32_t page, uint64_t length)
{
    return ((void *(*)(id, SEL, uint64_t, uint64_t, BOOL))objc_msgSend)(
        self, sel_registerName("mappedAddressForOffset:length:needWritable:"),
        (uint64_t)page << 12, length, NO);
}

static void fail(NSString *reason)
{
    [NSException raise:NSInternalInconsistencyException format:@"%@", reason];
}

static void walk(uint32_t table, unsigned depth, uint64_t first, uint64_t last,
                 uint64_t prefix, BOOL holes, PageRead read, PageVisit visit)
{
    if (!table) {
        if (!holes) fail(@"Missing Intel PVG page table");
        return;
    }
    unsigned shift = (depth - 1) * 10;
    unsigned lo = (first >> shift) & 1023;
    unsigned hi = (last >> shift) & 1023;
    uint32_t entries[1024];
    unsigned count = hi - lo + 1;
    if (!read(((uint64_t)table << 12) + lo * 4, count * 4, entries))
        fail(@"Reading Intel PVG page table failed");
    for (unsigned i = lo; i <= hi; i++) {
        uint32_t pte = entries[i - lo], page = pte & 0x7fffffff;
        uint64_t child = prefix | ((uint64_t)i << shift);
        if (!page) {
            if (!holes) fail(@"Missing Intel PVG page");
            continue;
        }
        if (depth == 1) {
            visit(child << 12, page, (pte >> 31) != 0);
        } else {
            uint64_t mask = (UINT64_C(1) << shift) - 1;
            walk(page, depth - 1, i == lo ? first : child,
                 i == hi ? last : child | mask, child, holes, read, visit);
        }
    }
}

static void visit_pages(id self, SEL cmd, uint64_t offset, uint64_t length,
                        PageVisit visitor)
{
    uint64_t root;
    unsigned depth;
    BOOL holes;
    memcpy(&root, (char *)self + ivar_getOffset(root_ivar), sizeof(root));
    memcpy(&depth, (char *)self + ivar_getOffset(depth_ivar), sizeof(depth));
    memcpy(&holes, (char *)self + ivar_getOffset(holes_ivar), sizeof(holes));
    if (!length || (offset & 4095) || (length & 4095) ||
        offset > UINT64_MAX - (length - 1) || depth < 1 || depth > 6)
        fail(@"Invalid Intel PVG page-table range");
    uint64_t first = offset >> 12, last = (offset + length - 1) >> 12;
    if (depth < 6 && (last >> (depth * 10)))
        fail(@"Intel PVG range exceeds page-table depth");
    /* ARM init stores the supplied root PFN shifted by 14. Recover the PFN;
     * only this walker consumes that field, and read() receives byte addresses. */
    walk((uint32_t)(root >> 14), depth, first, last, 0, holes,
         object_getIvar(self, read_ivar), visitor);
}

__attribute__((constructor)) static void install(void)
{
    if (!getenv("PVG_INTEL_PAGE_WALKER")) return;
    Class cls = NSClassFromString(@"PGPageTableIterator");
    root_ivar = class_getInstanceVariable(cls, "_root");
    depth_ivar = class_getInstanceVariable(cls, "_depth");
    holes_ivar = class_getInstanceVariable(cls, "_allowHoles");
    read_ivar = class_getInstanceVariable(cls, "_read");
    SEL selector = sel_registerName("visitPagesInRange:length:visitor:");
    Method method = class_getInstanceMethod(cls, selector);
    if (!root_ivar || !depth_ivar || !holes_ivar || !read_ivar || !method) abort();
    class_replaceMethod(cls, selector, (IMP)visit_pages, method_getTypeEncoding(method));
    Class task = NSClassFromString(@"PGLocalTask");
    selector = sel_registerName("mappedAddressForPageNumber:length:");
    method = class_getInstanceMethod(task, selector);
    if (!method) abort();
    class_replaceMethod(task, selector, (IMP)mapped_page, method_getTypeEncoding(method));
}
