/* Native/virtual owned-process IOAccel pbuffer admission. Use -c with the
 * finite public control. No injection, guessed structure offsets or data. */
#pragma D option quiet
pid$target:IOKit:IOAccelCreateSurface:entry
{self->surface=1;printf("CREATE service=%x id=%x flags=%x\n",arg0,arg1,arg2);}
pid$target:IOKit:IOAccelCreateSurface:return
/self->surface/
{printf("CREATE result=%x\n",(uint32_t)arg1);self->surface=0;}
proc:::exit /pid==$target/ {exit(0);}
tick-20s {exit(1);}
