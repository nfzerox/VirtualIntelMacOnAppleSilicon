/* Cold reference-only observation of CGL's GLI share-group ABI. */
#pragma D option quiet
pid$target::gliGetInteger:entry
/arg1==0x3a2/
{self->out=arg2;printf("native share-group selector=%x\n",arg1);}
pid$target::gliGetInteger:return
/self->out/
{printf("native share-group status=%d nonnull=%d\n",arg1,*(uintptr_t *)copyin(self->out,sizeof(uintptr_t))!=0);self->out=0;}
tick-10s {exit(0);}
