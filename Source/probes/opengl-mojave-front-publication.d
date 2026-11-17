/* Run with -Z -c on the matching finite owned fixture. Read no pixel data.
 * Native state offsets below are observation-only, for 18G9323 x86_64;
 * they are NOT a driver dependency or an attempted host binary hook. */
#pragma D option quiet
pid$target::VIMFrontPhase:entry { self->phase=arg0; }
pid$target:AppKit:NSCGLSurfaceFlush:entry /self->phase/
{ @consumer[self->phase,arg4]=count(); }
pid$target:GLEngine:glFlush_Exec:entry,
pid$target:GLEngine:glFinish_Exec:entry,
pid$target:GLEngine:glSwap_Exec:entry
/self->phase/
{
    this->state=(uint8_t *)copyin(arg0+0xc356,3);
    printf("NATIVE phase=%u boundary=%s back=%u front=%u pending=%u\n",
        self->phase,probefunc,this->state[0],this->state[1],this->state[2]);
}
END { printa("CONSUMER phase=%u options=%x count=%@u\n",@consumer); }
