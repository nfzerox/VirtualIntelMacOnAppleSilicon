/* Finite UART/read-event metadata only. Never records text, tokens or data. */
#pragma D option quiet
#pragma D option bufsize=1m
BEGIN {budget=128;printf("READY clipboard read-event metadata\n");}
syscall::read:entry /execname=="guest-clipboard" && uid!=0 && budget>0/
{self->reading=1;self->descriptor=arg0;}
syscall::read:return /self->reading/
{budget--;printf("pid=%d read fd=%d result=%d errno=%d\n",pid,self->descriptor,arg0,errno);self->reading=0;}
syscall::kevent:entry /execname=="guest-clipboard" && uid!=0 && budget>0/
{self->events=1;}
syscall::kevent:return /self->events/
{budget--;printf("pid=%d kevent result=%d errno=%d\n",pid,arg0,errno);self->events=0;}
tick-12s {exit(0);}
