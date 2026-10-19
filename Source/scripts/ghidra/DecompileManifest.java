// Export matched donor functions using original symbol-bearing image addresses.
// @category VirtualIntelMac
import java.nio.file.*;
import java.io.*;
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.FlowOverride;
import ghidra.app.cmd.function.CreateFunctionCmd;
public class DecompileManifest extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        // Analysis-only recovery pass: unresolved shared-cache stubs can acquire
        // a false noreturn signature, truncating every caller's decompilation.
        // Keep this output separate: genuine throwing paths also become returning
        // and must still be checked against assembly before deriving patches.
        boolean conservativeReturns = args.length > 2 && args[2].equals("conservative-returns");
        if (conservativeReturns) {
            int changed = 0;
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.hasNoReturn()) { f.setNoReturn(false); changed++; }
            }
            println("Analysis-only: cleared " + changed + " noreturn annotations");
            for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
                if (ins.getFlowOverride() == FlowOverride.CALL_RETURN)
                    ins.setFlowOverride(FlowOverride.NONE);
            }
        }
        DecompInterface d = new DecompInterface();
        if (!d.openProgram(currentProgram)) throw new IOException(d.getLastMessage());
        try (PrintWriter out = new PrintWriter(args[1])) {
            for (String row : Files.readAllLines(Path.of(args[0]))) {
                if (monitor.isCancelled()) break;
                String[] fields = row.split("\t", 2);
                if (args.length > 3 && !fields[1].contains(args[3])) continue;
                Address address = toAddr(fields[0]);
                Function f = getFunctionAt(address);
                if (f == null) { disassemble(address); f = createFunction(address, null); }
                if (f != null && conservativeReturns) {
                    disassemble(address);
                    CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
                }
                out.println("\n/* === " + fields[1] + " @ " + address + " === */");
                if (f == null) out.println("/* MISSING FUNCTION */");
                else {
                    DecompileResults r = d.decompileFunction(f, 30, monitor);
                    out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
                        : "/* FAILED: " + r.getErrorMessage() + " */");
                }
                out.flush();
            }
        } finally { d.dispose(); }
    }
}
