/* Attach to passthrough-opengl-interface while VIM_GL_TRACE_WAIT is active.
 * sudo dtrace -q -s opengl-gld-counts.d -p PID; end after the probe exits. */
pid$target::*gld*:entry
{
    @[probemod, probefunc] = count();
}
proc:::exit
/pid == $target/
{
    exit(0);
}
