/* Bounded native/guest Core Image context negotiation. Run with dtrace -Z;
 * change/restore wallpaper after READY. GLI format attrs are 32-bit words on
 * both widths; the bounded prefix may include words after its zero sentinel.
 * Decode only up to that sentinel, not unrelated trailing stack contents.
 */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN {budget=128;printf("READY Snow filter context negotiation\n");}
pid$target:GLEngine:gliChoosePixelFormat:entry /budget>0/
{budget--;printf("CHOOSE\n");tracemem(copyin(arg1,64),64);self->choose=1;}
pid$target:GLEngine:gliChoosePixelFormat:return /self->choose/
{printf("CHOOSE_RESULT %x\n",arg1);self->choose=0;}
pid$target:GLEngine:gliCreateContext:entry /budget>0/
{budget--;printf("CREATE flags=%x share=%x\n",arg5,arg2);self->create=1;}
pid$target:GLEngine:gliCreateContext:return /self->create/
{printf("CREATE_RESULT %x\n",arg1);self->create=0;}
pid$target:OpenGL:*CGLChoosePixelFormat:return,
pid$target:OpenGL:*CGLCreateContext:return,
pid$target:OpenGL:*CGLSetCurrentContext:return
/budget>0 && arg1/
{budget--;printf("ERROR %s=%x\n",probefunc,arg1);}
pid$target:QuartzCore:fe_accel_new:entry /budget>0/
{self->accel=1;}
pid$target:QuartzCore:fe_accel_new:return /self->accel/
{budget--;printf("CORE_IMAGE_ACCEL %x\n",arg1);self->accel=0;}
pid$target:QuartzCore:fe_cgls_create_context:return,
pid$target:QuartzCore:fe_accel_buffer_new:return
/budget>0/
{budget--;printf("CORE_IMAGE %s=%x\n",probefunc,arg1);}
tick-15s {exit(0);}
