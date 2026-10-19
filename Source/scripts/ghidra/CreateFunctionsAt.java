// @category VirtualIntelMac
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class CreateFunctionsAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        for (String value : getScriptArgs()) {
            Address address = toAddr(value);
            if (getFunctionAt(address) == null) {
                disassemble(address);
                createFunction(address, null);
            }
        }
    }
}
