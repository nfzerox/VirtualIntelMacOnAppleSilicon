"""Read-only live selector annotation for cache-extracted donor code."""
import lldb
import re

def inspect(target, name):
    context=target.FindFunctions(name).GetContextAtIndex(0)
    symbol=context.GetSymbol()
    process=target.GetProcess()
    end=symbol.GetEndAddress().GetLoadAddress(target)
    for instruction in target.ReadInstructions(symbol.GetStartAddress(),2000):
        address=instruction.GetAddress().GetLoadAddress(target)
        if address>=end:break
        operands=instruction.GetOperands(target)
        match=re.search(r'(0x[0-9a-f]+)\(%rip\), %rsi',operands)
        if not match:continue
        error=lldb.SBError()
        slot=address+instruction.GetByteSize()+int(match.group(1),16)
        pointer=process.ReadPointerFromMemory(slot,error)
        if not error.Success():continue
        name=process.ReadCStringFromMemory(pointer,200,error)
        if error.Success():print(hex(address),name)
