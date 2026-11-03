/* Read-only, finite, x86_64 10J3250/10K549 QuartzCore policy comparison.
 * Offsets are diagnostic-only, verified in each actual fe_accel_new binary:
 * class at0x60, capability bytes at0x120, per-stage limits at0xb8/0xe8.
 * Run with -Z -c on the owned finite opengl-ci-effect fixture, not arbitrary
 * host processes. No policy changes, injected calls or pixel capture.
 */
#pragma D option quiet
BEGIN {budget=256;printf("READY Snow Core Image policy\n");}
pid$target:QuartzCore:fe_accel_new:return /arg1 && budget>0/
{
    budget--;
    printf("ACCEL class=%u flags=%x,%x\n",
        *(uint32_t *)copyin(arg1+0x60,4),
        *(uint8_t *)copyin(arg1+0x120,1),*(uint8_t *)copyin(arg1+0x121,1));
    tracemem(copyin(arg1+0xb8,0x60),0x60);
}
pid$target:QuartzCore:fe_accel_get:entry /budget>0/
{self->query=1;self->key=arg1;self->parameter=arg2;}
pid$target:QuartzCore:fe_accel_get:return /self->query && budget>0/
{budget--;printf("PROPERTY %u parameter=%u result=%u\n",self->key,self->parameter,arg1);self->query=0;}
pid$target:QuartzCore:fe_accel_buffer_new:entry /budget>0/
{self->buffer=1;self->format=arg1;}
pid$target:QuartzCore:fe_accel_buffer_new:return /self->buffer && budget>0/
{budget--;printf("BUFFER format=%u result=%x\n",self->format,arg1);self->buffer=0;}
tick-15s {exit(0);}
