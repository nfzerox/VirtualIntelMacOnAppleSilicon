/* Finite owned public-CI fixture: distinguish valid output from actual GPU
 * filter allocation. No private structure offsets or policy mutation. */
#pragma D option quiet
BEGIN {budget=256;printf("READY owned CI GPU activity\n");}
pid$target:QuartzCore:fe_accel_new:entry,
pid$target:QuartzCore:fe_accel_buffer_new:entry,
pid$target:GLEngine:VIMDispatch_program_string_ARB:entry,
pid$target:GLEngine:VIMDispatch_draw_arrays:entry,
pid$target:GLEngine:VIMDispatch_begin:entry
{@calls[probefunc]=count();}
pid$target:QuartzCore:fe_accel_buffer_new:return
{@buffers[arg1!=0]=count();}
/* Native9J/9L x64 fe_accel_get reads its property in esi, parameter in edx
 * and returns a scalar in eax. No Snow policy-record offsets are reused. */
pid$target:QuartzCore:fe_accel_get:entry
/budget>0/
{self->property=arg1;self->parameter=arg2;self->query=1;}
pid$target:QuartzCore:fe_accel_get:return
/budget>0 && self->query/
{budget--;@properties[self->property,self->parameter,(uint32_t)arg1]=count();self->query=0;}
pid$target:GLEngine:VIMDispatch_get_integerv:entry
/budget>0 && arg2/
{self->integer_key=arg1;self->integer=arg2;}
pid$target:GLEngine:VIMDispatch_get_integerv:return
/budget>0 && self->integer/
{budget--;@integers[self->integer_key,*(int32_t *)copyin(self->integer,4)]=count();self->integer=0;}
pid$target:GLEngine:VIMDispatch_get_programiv_ARB:entry
/budget>0 && arg3/
{self->program_key=arg2;self->program=arg3;}
pid$target:GLEngine:VIMDispatch_get_programiv_ARB:return
/budget>0 && self->program/
{budget--;@programs[self->program_key,*(int32_t *)copyin(self->program,4)]=count();self->program=0;}
proc:::exit /pid==$target/ {exit(0);}
tick-20s {exit(1);}
END {printf("CALLS\n");printa(@calls);printf("BUFFER returned-nonnull count\n");printa(@buffers);
    printf("CI PROPERTY parameter result count\n");printa(@properties);
    printf("GL INTEGER key value count\n");printa(@integers);
    printf("GL PROGRAM key value count\n");printa(@programs);}
