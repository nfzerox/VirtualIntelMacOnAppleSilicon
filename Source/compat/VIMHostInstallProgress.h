#pragma once
#include <stddef.h>
#include <math.h>

/* Adapt successful phase history to this run's observed pace. A measured
 * current phase can contribute before it finishes (important for Essentials),
 * but an elapsed-only animation is not evidence of completed work. Tiny
 * startup phases are dominated by the UI sampling interval, so wait for a
 * few seconds of paired observations before changing the historical budget.
 * This is an estimate only, never a deadline or installation condition. */
static inline double VIMHostInstallPaceScale(double observedCompleted,double referenceCompleted,
    double elapsed,double fraction,double referenceStage) {
    if(fraction>=.02&&fraction<=1&&referenceStage>0) {
        observedCompleted+=elapsed;
        referenceCompleted+=referenceStage*fraction;
    }
    if(observedCompleted<5||referenceCompleted<5)return 1;
    double scale=observedCompleted/referenceCompleted;
    return isfinite(scale)&&scale>0?scale:1;
}

/* A local tool percentage is not a whole-install ETA. Only combine it with
 * a known remaining-stage budget; unknown time must stay unknown in the UI. */
static inline double VIMHostInstallRemainingSeconds(double elapsed,double fraction,
                                                   double stageSeconds,double futureSeconds) {
    if(futureSeconds<0)return -1;
    if(elapsed>=2&&fraction>=.02&&fraction<=1)
        return elapsed*(1-fraction)/fraction+futureSeconds;
    if(stageSeconds>elapsed)return stageSeconds-elapsed+futureSeconds;
    return -1;
}

/* Presentation only. Catalina's split-volume projection and detached export
 * each take minutes in real Create runs; the generic recipe put all of them
 * in its last 9%. Retain measured progress inside each stage, but give those
 * stages a share consistent with the observed preparation time. */
static inline double VIMHostInstallProgressPosition(double position,int splitCatalina) {
    if(!splitCatalina)return position;
    static const double source[]={0,.14,.76,.80,.83,.87,.91,.94,.95,.96,.98,1};
    static const double display[]={0,.04,.34,.35,.38,.42,.44,.68,.73,.86,.98,1};
    for(size_t i=1;i<sizeof(source)/sizeof(source[0]);++i)
        if(position<=source[i])
            return display[i-1]+(display[i]-display[i-1])*(position-source[i-1])/(source[i]-source[i-1]);
    return position;
}
