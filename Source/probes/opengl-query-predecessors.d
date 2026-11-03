/* Bounded query-order inventory; not an FPS measurement. Fixed DGMessage
 * op/context prefix is shared by x64/i386. Only counts, no argument payloads.
 * Identify which commands separate error/common-state native observations.
 */
#pragma D option quiet
BEGIN {last[(uint32_t)0]=(uint32_t)0;printf("READY native query predecessors\n");}
pid$target::DGSubmit:entry
{
    this->m=(uint32_t *)copyin(arg1,8);
    this->op=this->m[0];this->context=this->m[1];
}
pid$target::DGSubmit:entry
/this->op==0x10005 || this->op==0x10043/
{@predecessors[this->op,last[this->context]]=count();}
pid$target::DGSubmit:entry
{last[this->context]=this->op;}
tick-12s {exit(0);}
END {printa("query=%x predecessor=%x calls=%@d\n",@predecessors);}
