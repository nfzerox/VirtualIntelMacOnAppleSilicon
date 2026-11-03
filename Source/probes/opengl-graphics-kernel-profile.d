/* Bounded Mavericks WindowServer CPU sample; pass its current PID as $1.
 * Run during an owned animation, not concurrently with frame-rate timing.
 * No injection, input synthesis, pixels or unrelated-process stacks.
 * sudo dtrace -o profile.log -s opengl-graphics-kernel-profile.d PID
 */
#pragma D option quiet
BEGIN {began=timestamp;printf("READY kernel CPU profile target=%d\n",$1);}
profile-97
/pid == $1/
{
    @execution[arg0 ? "kernel" : "user"] = count();
}

profile-97
/pid == $1 && arg0/
{
    @kernel[stack(8)] = count();
}

tick-1s /timestamp-began>=12000000000/
{
    exit(0);
}
