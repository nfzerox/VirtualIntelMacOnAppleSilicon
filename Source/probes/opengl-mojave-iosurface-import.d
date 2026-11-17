/* Finite read-only GLI argument/error survey for one iWork process. Records
 * fixed eight-word IOSurface/3e5 surface requests, never source pixels or an
 * unbounded instruction trace. Run on guest or native Intel with -p PID. */
#pragma D option quiet
#pragma D option bufsize=1m
BEGIN { budget=24;printf("READY IOSurface import pid=%u\n",$target); }
pid$target:GLEngine*:gliSetInteger:entry /(arg1==0x38e || arg1==0x3e5) && budget>0/
{
    self->active_import=1;budget--;
    this->words=(uint32_t *)copyin(arg2,32);
    printf("IMPORT op=%x surface=%u target=%x internal=%x size=%ux%u extra5=%x extra6=%x extra7=%x\n",
        arg1,this->words[0],this->words[1],this->words[2],this->words[3],this->words[4],
        this->words[5],this->words[6],this->words[7]);
    ustack(8);
}
pid$target:GLEngine*:gliSetInteger:return /self->active_import/
{ printf("RESULT %d\n",arg1);self->active_import=0; }
tick-12s { exit(0); }
