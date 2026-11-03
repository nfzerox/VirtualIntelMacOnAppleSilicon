/* Run with -p WINDOWSERVER_PID -s THIS_FILE MOUSE_OBSERVER_PID.
 * The mouse observer's ABI is our explicit integer-only source contract.
 * Native CoreGraphics functions are timestamped only: no guessed arguments.
 * CGXUpdateDisplay's real return is verified in matching 13F1911 CoreGraphics.
 * No all-GL/DGSubmit probes: held steps need only a few low-frequency points.
 * A diagnostic ordering trace, not an uninstrumented latency/FPS benchmark.
 */
#pragma D option quiet
#pragma D option bufsize=4m
#pragma D option dynvarsize=2m
BEGIN {began=timestamp;budget=512;printf("READY held-drag event/update ordering target=%d\n",$target);}
pid$1:opengl-drag-input-events*:VIMDragEventObserved:entry /budget>0/
{
    printf("%llu HID_CALLBACK type=%u x=%d y=%d event_ns=%llu callback_mach=%llu\n",
        (unsigned long long)timestamp,(uint32_t)arg0,(int32_t)arg1,(int32_t)arg2,
        (unsigned long long)arg3,(unsigned long long)arg4);budget--;
}
pid$target:CoreGraphics:dragWindow:entry /budget>0/
{printf("%llu DRAG\n",(unsigned long long)timestamp);budget--;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /budget>0/
{printf("%llu UPDATE_BEGIN\n",(unsigned long long)timestamp);budget--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /budget>0/
{printf("%llu UPDATE_END\n",(unsigned long long)timestamp);budget--;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /budget>0/
{printf("%llu GL_FINISH_ENTRY\n",(unsigned long long)timestamp);budget--;}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {printf("DONE ordering metadata budget=%d\n",budget);}
