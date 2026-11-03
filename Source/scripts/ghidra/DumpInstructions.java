// Read-only instruction/byte evidence for a bounded address range.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
public class DumpInstructions extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();
        Address start=toAddr(args[0]),end=toAddr(args[1]);
        InstructionIterator items=currentProgram.getListing().getInstructions(start,true);
        while(items.hasNext() && !monitor.isCancelled()) {
            Instruction item=items.next();if(item.getAddress().compareTo(end)>0) break;
            StringBuilder bytes=new StringBuilder();
            for(byte b:item.getBytes()) bytes.append(String.format("%02x ",b&255));
            println(item.getAddress()+" "+bytes+" "+item);
        }
    }
}
