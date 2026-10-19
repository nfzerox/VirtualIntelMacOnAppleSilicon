// Read-only policy investigation: decompile named functions matching supplied terms.
// @category VirtualIntelMac
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.util.Locale;

public class DecompileMatching extends GhidraScript {
    public void run() throws Exception {
        String[] terms = getScriptArgs();
        if (terms.length == 0) throw new IllegalArgumentException("provide name substrings");
        DecompInterface decompiler = new DecompInterface();
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        try {
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                String name = function.getName(true);
                boolean matched = false;
                for (String term : terms) if (name.toLowerCase(Locale.ROOT).contains(term.toLowerCase(Locale.ROOT))) matched = true;
                if (!matched) continue;
                println("===== " + name + " @ " + function.getEntryPoint() + " =====");
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (result.decompileCompleted()) println(result.getDecompiledFunction().getC());
                else println("DECOMPILE FAILED: " + result.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
