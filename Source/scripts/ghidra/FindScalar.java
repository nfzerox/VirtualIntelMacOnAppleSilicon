// Locate private GL enums in already analyzed donor code, without guesses.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
public class FindScalar extends GhidraScript {
    public void run() throws Exception {
        long wanted=Long.decode(getScriptArgs()[0]);
        InstructionIterator instructions=currentProgram.getListing().getInstructions(true);
        while(instructions.hasNext() && !monitor.isCancelled()) {
            Instruction i=instructions.next();
            for(int n=0;n<i.getNumOperands();++n) for(Object o:i.getOpObjects(n))
                if(o instanceof Scalar && ((Scalar)o).getUnsignedValue()==wanted) {
                    Function f=getFunctionContaining(i.getAddress());
                    println(i.getAddress()+" "+i+" FUNCTION "+(f==null?"?":f.getName()+" "+f.getEntryPoint()));
                }
        }
    }
}
