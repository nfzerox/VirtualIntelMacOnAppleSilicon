/* DIAGNOSTIC MUTATION, NOT A PRODUCT FIX. Only the owned ci-effect28 child.
 * Requires dtrace -w -Z -c. Exact10J3250 x86_64 fe_accel_new class offset was
 * verified in its binary; class10 is observed on the physical Intel control.
 * Change one newly created unknown policy record, leaving GL capabilities,
 * limits, renderer identity, shaders and image data untouched. This tests
 * whether its observed zero GPU cost estimate explains CPU filter selection.
 * Never attach this script to WindowServer or another application.
 */
#pragma D option quiet
BEGIN {printf("READY owned Core Image reference-class experiment\n");}
pid$target:QuartzCore:fe_accel_new:return
/execname=="ci-effect28" && arg1 && !changed && *(uint32_t *)copyin(arg1+0x60,4)==0/
{
    this->reference=(uint32_t *)alloca(4);*this->reference=10;
    copyout(this->reference,arg1+0x60,4);changed=1;
    printf("CHANGED owned Core Image class0 to measured Intel class10\n");
}
pid$target:QuartzCore:fe_accel_get:entry {self->key=arg1;}
pid$target:QuartzCore:fe_accel_get:return /changed && self->key==9/
{printf("GPU_SPEED %u\n",arg1);}
pid$target:QuartzCore:fe_accel_buffer_new:return /changed/
{printf("GPU_BUFFER %x\n",arg1);}
tick-15s {exit(0);}
