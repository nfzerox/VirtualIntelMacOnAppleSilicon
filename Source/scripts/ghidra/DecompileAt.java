// Decompile functions at hexadecimal virtual addresses and list their callers.
// @category VirtualIntelMac

import java.util.Iterator;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class DecompileAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            throw new IllegalArgumentException("provide one or more hexadecimal addresses");
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException(decompiler.getLastMessage());
        }

        try {
            for (String value : args) {
                long offset = Long.parseUnsignedLong(value.replaceFirst("^0[xX]", ""), 16);
                Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(offset);
                Function function = getFunctionAt(address);
                if (function == null) {
                    function = getFunctionContaining(address);
                }
                if (function == null) {
                    println("NO FUNCTION AT " + address);
                    continue;
                }

                println("===== " + function.getName(true) + " @ " + function.getEntryPoint() + " =====");
                Iterator<Reference> references = currentProgram.getReferenceManager()
                    .getReferencesTo(function.getEntryPoint());
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Function caller = getFunctionContaining(reference.getFromAddress());
                    println("CALLER " + reference.getFromAddress() + " " +
                        (caller == null ? "<none>" : caller.getName(true)));
                }

                DecompileResults result = decompiler.decompileFunction(function, 180, monitor);
                if (!result.decompileCompleted()) {
                    println("DECOMPILE FAILED: " + result.getErrorMessage());
                    continue;
                }
                println(result.getDecompiledFunction().getC());
            }
        }
        finally {
            decompiler.dispose();
        }
    }
}
