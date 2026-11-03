/* Bounded physical Intel contract survey, NOT an FPS measurement. Match
 * native 13F1911 GLEngine first. Its glViewport_Exec entry takes GLI context,
 * x,y,width,height (verified in its disassembly/decompilation). Attach's first
 * three CGS words are connection/window/surface, not pixel data. Trace a real
 * Finder window and require an actual Intel-driver hit, not display ancestry.
 */
#pragma D option quiet
#pragma D option bufsize=4m
#pragma D option aggsize=8m
BEGIN { printf("READY Intel drawable/view survey\n"); }
pid$target:*Intel*:glrFlushContextToken:entry
{
    intel_seen=1;@intel[probemod]=count();
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/arg2 && (arg1==0x50 || arg1==0x5a)/
{
    this->record=(uint32_t *)copyin(arg2,12);
    surface[arg0]=this->record[2];
    @attachments[arg0,arg1,this->record[0],this->record[1],this->record[2]]=count();
}
pid$target:GLEngine:glViewport_Exec:entry
{
    @viewports[arg0,surface[arg0],(int)arg1,(int)arg2,(int)arg3,(int)arg4]=count();
}
tick-12sec { exit(0); }
END {
    printf("END actual-Intel-driver-observed=%d\n",(int)intel_seen);
    printa(@intel);
    printf("ATTACH GLI-context type connection window surface count\n");printa(@attachments);
    printf("VIEWPORT GLI-context last-surface x y width height count\n");printa(@viewports);
}
