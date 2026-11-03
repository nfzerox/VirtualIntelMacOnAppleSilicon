/* Finite metadata-only WindowServer video import trace on Snow Leopard.
 * sudo dtrace -Z -q -s THIS_FILE -p ACTUAL_WINDOWSERVER_PID > LOG 2>&1
 * Play the real movie after READY. No injection, restart, copyout, renderer
 * selection or GL error consumption. Only the known eight-word IOSurface
 * API arguments are copied; no texture/pixel/program contents are recorded.
 */
#pragma D option quiet
#pragma D option bufsize=2m
#pragma D option aggsize=1m

BEGIN { video_budget=64;error_budget=32;printf("READY 15-second GLI video metadata trace target=%d\n",$target); }

pid$target:GLEngine:*gliSetInteger:entry
{
    @keys[(uint32_t)arg1]=count();
    self->set_key=(uint32_t)arg1;
    self->set_active=1;
    self->video=(arg1==0x38e && arg2 && video_budget>0);
}
pid$target:GLEngine:*gliSetInteger:entry
/self->video/
{
    video_budget--;
    this->a=(uint32_t *)copyin(arg2,32);
    printf("%llu IOSURFACE tid=%d ctx=%p id=%u target=%x internal=%x width=%u height=%u format=%x type=%x plane=%u\n",
        (unsigned long long)timestamp,tid,arg0,this->a[0],this->a[1],this->a[2],this->a[3],
        this->a[4],this->a[5],this->a[6],this->a[7]);
}
pid$target:GLEngine:*gliSetInteger:return
/self->set_active && self->video/
{
    printf("%llu IOSURFACE_RESULT tid=%d result=%d\n",(unsigned long long)timestamp,tid,(int32_t)arg1);
}
pid$target:GLEngine:*gliSetInteger:return
/self->set_active && arg1 && error_budget>0/
{
    error_budget--;
    printf("%llu SET_ERROR tid=%d key=%x result=%d\n",(unsigned long long)timestamp,tid,self->set_key,(int32_t)arg1);
}
pid$target:GLEngine:*gliSetInteger:return
{ self->set_active=0;self->video=0;self->set_key=0; }

tick-15sec { exit(0); }
END { printf("GLI set-key call counts (decimal keys)\n");printa(@keys); }
