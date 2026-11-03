/* The actual allocation-free kernel admission algorithm, not a mock copy.
 * OS sleeping/interrupt/gate integration still requires a real guest test. */
#include "../vendor/source/VMsvga2/vminclude/vim_backing_access.h"
#include <assert.h>
#include <stdio.h>
int main()
{
    VIMBackingAccessRegistry r={};int a,b,one,two,three;
    assert(r.borrow(&a,&one)==r.Admitted && r.borrow(&a,&two)==r.Admitted);
    assert(!r.promote(&a,&one)); // another normal GPU alias already active
    r.returnBorrow(&two);assert(r.promote(&a,&one));
    assert(r.borrow(&a,&two)==r.Conflict && r.borrow(0,&two)==r.Conflict);
    assert(r.borrow(&b,&two)==r.Admitted); // independently identified object
    assert(r.promote(&b,&two));r.releaseExclusive(&a);r.releaseExclusive(&b);
    assert(r.borrow(&a,&one)==r.Admitted && r.borrow(0,&three)==r.Admitted);
    assert(!r.promote(&a,&one)); // unknown earlier alias prevents ANY new gate
    r.returnBorrow(&three);assert(r.promote(&a,&one));r.releaseExclusive(&a);
    assert(!r.promote(&a,&one)); // no owner => no exclusive reservation
    assert(r.borrow(&a,&one)==r.Admitted && r.borrow(&a,&one)==r.Duplicate);
    assert(!r.promote(&b,&one));r.returnBorrow(&one);
    assert(r.borrow(&b,&two)==r.Admitted && r.promote(&b,&two));
    assert(r.borrow(&a,&one)==r.Admitted && r.borrow(&b,&one)==r.Duplicate);
    r.returnBorrow(&one);r.releaseExclusive(&b);
    int owners[VIMBackingAccessRegistry::Limit+1];
    for(unsigned i=0;i<r.Limit;++i)assert(r.borrow(&a,&owners[i])==r.Admitted);
    assert(r.borrow(&b,&owners[r.Limit])==r.Full);
    for(unsigned i=0;i<r.Limit;++i)r.returnBorrow(&owners[i]);
    for(unsigned i=0;i<r.Limit;++i){
        assert(r.borrow(&owners[i],&owners[i])==r.Admitted);
        assert(r.promote(&owners[i],&owners[i]));
    }
    assert(r.borrow(0,&one)==r.Conflict);
    for(unsigned i=0;i<r.Limit;++i)r.releaseExclusive(&owners[i]);
    for(unsigned i=0;i<r.Limit;++i)assert(!r.shared[i].owner && !r.exclusive[i]);
    puts("PASS backing access: shared aliases, atomic upgrades, unknown borrowers, admission budgets and balanced release");
}
