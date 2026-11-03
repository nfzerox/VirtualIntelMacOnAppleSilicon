/* Finite control-flow metadata only; no message objects or byte contents. */
#pragma D option quiet
BEGIN {printf("READY clipboard lifecycle\n");}
pid$target:guest-clipboard:*receive*:entry {printf("receive entered\n");}
pid$target:guest-clipboard:*stop*:entry {printf("stop caller\n");ustack(12);exit(0);}
tick-15s {exit(0);}
