// Find code/data references to one or more addresses in the existing SimCopter Ghidra project.
// Usage with analyzeHeadless: -postScript FindDataReferences.java 0x00506360 ...

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class FindDataReferences extends GhidraScript {
    @Override
    protected void run() throws Exception {
        for (String raw : getScriptArgs()) {
            Address target = toAddr(raw);
            long unsignedValue = Integer.toUnsignedLong(getInt(target));
            println("TARGET " + target + " value=0x" + Long.toHexString(unsignedValue) +
                " (" + unsignedValue + ")");
            Reference[] refs = getReferencesTo(target);
            if (refs.length == 0) {
                println("  no references");
                continue;
            }
            for (Reference ref : refs) {
                Address source = ref.getFromAddress();
                Function function = getFunctionContaining(source);
                CodeUnit unit = getInstructionAt(source);
                if (unit == null) {
                    unit = getDataAt(source);
                }
                println("  " + source + " " +
                    (function != null ? function.getName() : "<data>") + " " +
                    (unit != null ? unit.toString() : ""));
            }
        }
    }
}
