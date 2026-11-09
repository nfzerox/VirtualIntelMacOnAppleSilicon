// Offline named-function analysis. --inline-outlined changes only Ghidra
// metadata, not executable bytes, so machine-outliner register effects are
// visible in callers instead of becoming fictitious C ABI calls.
// @category VirtualIntelMac
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.util.Locale;
import java.util.ArrayList;

public class DecompileMatching extends GhidraScript {
    public void run() throws Exception {
        ArrayList<String> terms = new ArrayList<>();
        boolean inlineOutlined = false;
        for (String arg : getScriptArgs()) {
            if (arg.equals("--inline-outlined")) inlineOutlined = true;
            else terms.add(arg);
        }
        if (terms.isEmpty()) throw new IllegalArgumentException("provide name substrings");
        if (inlineOutlined) {
            int count = 0;
            FunctionIterator helpers = currentProgram.getFunctionManager().getFunctions(true);
            while (helpers.hasNext() && !monitor.isCancelled()) {
                Function function = helpers.next();
                if (function.getName().startsWith("_OUTLINED_FUNCTION_")) {
                    function.setInline(true);
                    ++count;
                }
            }
            println("Marked machine-outliner helpers inline: " + count);
        }
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
