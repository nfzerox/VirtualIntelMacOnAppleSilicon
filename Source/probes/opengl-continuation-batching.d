/* Actual Dock window-level atomicity gate for a transient scheduler test.
 * No object layouts or writes. Require POSITIVE level/admission coverage and
 * true deferral results; an empty interval is not a passing control.
 */
#pragma D option quiet
BEGIN {began=timestamp;printf("READY compositor continuation batching target=%d\n",$target);}
pid$target:CoreGraphics:WSWindowSetLevel:entry /!self->level/ {self->admission=0;}
pid$target:CoreGraphics:WSWindowSetLevel:entry {self->level++;@levels=count();}
pid$target:CoreGraphics:WSWindowSetLevel:return /self->level/ {self->level--;}
pid$target:CoreGraphics:begin_deferred_update_cycle:entry /self->level/ {self->admission++;@admissions=count();}
pid$target:CoreGraphics:begin_deferred_update_cycle:return /self->level/ {@deferrals[self->admission,arg1&255]=count();}
pid$target:CoreGraphics:begin_deferred_update_cycle:return /self->level && self->admission==1/ {@first[arg1&255]=count();}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {@updates=count();}
pid$target:AppleIntelHD5000GraphicsGLDriver*:glrFlushContextToken:entry {@intel=count();}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID compositor continuation batching probe error\n");exit(2);}
END {
    printf("Actual window-level calls / nested admissions\n");printa(@levels);printa(@admissions);
    printf("FIRST admission deferred bool (0=immediate,1=deferred)\n");printa(@first);
    printf("All admission ordinal / deferred bool\n");printa(@deferrals);
    printf("Actual update calls\n");printa(@updates);
    printf("Actual native Intel submits (required for physical oracle)\n");printa(@intel);
    printf("END compositor continuation batching\n");
}
