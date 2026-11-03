/* Native IOKit IOAccelCreateSurface(service, surfaceID, mode, outSurface).
 * Signature verified in actual10J3250 IOKit, which forwards ID/mode as two
 * uint64 scalars to selector7 (the matching public Snow surface enum).
 * Observe native requests/results; no mode changes or kernel offsets here.
 * Change and restore display resolution after READY. */
#pragma D option quiet
BEGIN {budget=128;printf("READY native surface-mode negotiation\n");}
pid$target:IOKit:IOAccelCreateSurface:entry /budget>0/
{self->id=arg1;self->mode=arg2;self->active=1;}
pid$target:IOKit:IOAccelCreateSurface:return /self->active && budget>0/
{budget--;printf("CREATE id=%x mode=%x result=%x\n",self->id,self->mode,arg1);self->active=0;}
tick-12s {exit(0);}
