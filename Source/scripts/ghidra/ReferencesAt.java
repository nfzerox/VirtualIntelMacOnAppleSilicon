// Read-only reference traversal for Objective-C selector pointers.
// @category VirtualIntelMac
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
public class ReferencesAt extends GhidraScript {
    private void refs(Address address, int depth) throws Exception {
        if (depth > 2 || monitor.isCancelled()) return;
        for (Reference ref : currentProgram.getReferenceManager().getReferencesTo(address)) {
            Function f = getFunctionContaining(ref.getFromAddress());
            println("depth=" + depth + " " + ref.getFromAddress() + " -> " + address +
                " function=" + (f == null ? "<data>" : f.getEntryPoint() + " " + f.getName()));
            if (f == null) refs(ref.getFromAddress(), depth + 1);
        }
    }
    public void run() throws Exception {
        for (String arg : getScriptArgs()) refs(toAddr(Long.parseUnsignedLong(arg.replaceFirst("^0[xX]", ""),16)),0);
    }
}
