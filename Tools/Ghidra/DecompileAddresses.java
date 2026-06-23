// Headless helper for local reverse-engineering notes.
// Usage:
//   analyzeHeadless <project_dir> <project_name> -import SimCopter.exe -postScript DecompileAddresses.java 0x4abc20 0x4abd70

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class DecompileAddresses extends GhidraScript {
	@Override
	public void run() throws Exception {
		DecompInterface decompiler = new DecompInterface();
		decompiler.setOptions(new DecompileOptions());
		decompiler.openProgram(currentProgram);

		String[] args = getScriptArgs();
		if (args.length == 0) {
			println("No addresses provided.");
			return;
		}

		for (String arg : args) {
			Address address = toAddr(arg);
			Function function = getFunctionContaining(address);
			if (function == null) {
				println("No function at " + arg);
				continue;
			}

			DecompileResults results = decompiler.decompileFunction(function, 30, new ConsoleTaskMonitor());
			println("===== " + function.getName() + " @ " + function.getEntryPoint() + " =====");
			if (!results.decompileCompleted()) {
				println("Decompile failed: " + results.getErrorMessage());
				continue;
			}
			println(results.getDecompiledFunction().getC());
		}

		decompiler.dispose();
	}
}
