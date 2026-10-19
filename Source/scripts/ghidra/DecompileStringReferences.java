// Trace strings through pointer/selector data to their referencing functions.
// @category VirtualIntelMac
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.util.*;

public class DecompileStringReferences extends GhidraScript {
    private Set<Address> visited = new HashSet<>();
    private Set<Function> functions = new LinkedHashSet<>();
    private void references(Address address, int depth) throws Exception {
        if (depth > 3 || !visited.add(address) || monitor.isCancelled()) return;
        for (Reference reference : currentProgram.getReferenceManager().getReferencesTo(address)) {
            Function function = getFunctionContaining(reference.getFromAddress());
            if (function != null) functions.add(function);
            else references(reference.getFromAddress(), depth + 1);
        }
    }
    public void run() throws Exception {
        String[] terms = getScriptArgs();
        if (terms.length == 0) throw new IllegalArgumentException("provide string substrings");
        DataIterator data = currentProgram.getListing().getDefinedData(true);
        while (data.hasNext() && !monitor.isCancelled()) {
            Data item = data.next();
            Object value = item.getValue();
            if (!(value instanceof String)) continue;
            for (String term : terms) if (((String)value).contains(term)) {
                println("STRING " + item.getAddress() + " " + value);
                references(item.getAddress(), 0);
                break;
            }
        }
        DecompInterface decompiler = new DecompInterface();
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        try {
            for (Function function : functions) {
                println("===== " + function.getName(true) + " @ " + function.getEntryPoint() + " =====");
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (result.decompileCompleted()) println(result.getDecompiledFunction().getC());
                else println("DECOMPILE FAILED: " + result.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
