// Flexible headless exploration helper for SimCopter reverse engineering.
// Writes clean UTF-8 output to a file (avoids console encoding issues).
//
// Usage:
//   analyzeHeadless <proj_dir> <proj> -process SimCopter.exe -noanalysis \
//     -postScript ReverseExplore.java <outFile> <command> [args...]
//
// Commands:
//   strings <substr>          List defined strings containing substr + referencing funcs.
//   xrefsto <hexaddr>         References to an address + containing function.
//   decompile <hexaddr>...    Decompile functions containing the given addresses.
//   func <name>...            Decompile functions by name.
//   callers <hexaddr>         Functions that call the function containing addr.
//   bytes <hexaddr> <count>   Hex/dword dump.

import java.io.PrintWriter;
import java.io.OutputStreamWriter;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.ConsoleTaskMonitor;

public class ReverseExplore extends GhidraScript {
	PrintWriter out;
	DecompInterface decompiler;

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 2) {
			println("Need <outFile> <command> ...");
			return;
		}
		out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(args[0]), StandardCharsets.UTF_8));
		String cmd = args[1];
		try {
			switch (cmd) {
				case "strings": cmdStrings(args[2]); break;
				case "xrefsto": cmdXrefsTo(args[2]); break;
				case "decompile": for (int i = 2; i < args.length; i++) decompileAt(toAddr(args[i])); break;
				case "func": for (int i = 2; i < args.length; i++) decompileFunc(args[i]); break;
				case "callers": cmdCallers(args[2]); break;
				case "bytes": cmdBytes(args[2], Integer.parseInt(args[3])); break;
				default: out.println("Unknown command: " + cmd);
			}
		} finally {
			out.flush();
			out.close();
			if (decompiler != null) decompiler.dispose();
		}
		println("Wrote " + args[0]);
	}

	DecompInterface dec() {
		if (decompiler == null) {
			decompiler = new DecompInterface();
			decompiler.setOptions(new DecompileOptions());
			decompiler.openProgram(currentProgram);
		}
		return decompiler;
	}

	void cmdStrings(String substr) {
		String needle = substr.toLowerCase();
		Listing listing = currentProgram.getListing();
		DataIterator it = listing.getDefinedData(true);
		int count = 0;
		while (it.hasNext()) {
			Data d = it.next();
			if (d == null) continue;
			String type = d.getDataType().getName().toLowerCase();
			if (!(type.contains("string") || type.contains("char") || type.contains("unicode"))) continue;
			Object val = d.getValue();
			if (val == null) continue;
			String s = val.toString();
			if (!s.toLowerCase().contains(needle)) continue;
			count++;
			out.println("STRING @ " + d.getAddress() + " : " + escape(s));
			ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
			int rc = 0;
			while (refs.hasNext()) {
				Reference r = refs.next();
				Address from = r.getFromAddress();
				Function f = getFunctionContaining(from);
				out.println("    <- " + from + (f != null ? "  in " + f.getName() + " @ " + f.getEntryPoint() : "  (no func)"));
				rc++;
			}
			if (rc == 0) out.println("    (no references)");
		}
		out.println("=== " + count + " strings matched '" + substr + "' ===");
	}

	void cmdXrefsTo(String hex) {
		Address addr = toAddr(hex);
		out.println("References to " + addr + ":");
		ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
		while (refs.hasNext()) {
			Reference r = refs.next();
			Address from = r.getFromAddress();
			Function f = getFunctionContaining(from);
			out.println("    " + from + "  " + r.getReferenceType() + (f != null ? "  in " + f.getName() + " @ " + f.getEntryPoint() : ""));
		}
	}

	void cmdCallers(String hex) {
		Address addr = toAddr(hex);
		Function target = getFunctionContaining(addr);
		if (target == null) { out.println("No function at " + hex); return; }
		out.println("Callers of " + target.getName() + " @ " + target.getEntryPoint() + ":");
		ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target.getEntryPoint());
		while (refs.hasNext()) {
			Reference r = refs.next();
			Function f = getFunctionContaining(r.getFromAddress());
			out.println("    " + r.getFromAddress() + (f != null ? "  in " + f.getName() + " @ " + f.getEntryPoint() : ""));
		}
	}

	void decompileFunc(String name) {
		FunctionIterator it = currentProgram.getListing().getFunctions(true);
		boolean found = false;
		while (it.hasNext()) {
			Function f = it.next();
			if (f.getName().equals(name)) { decompileFunction(f); found = true; }
		}
		if (!found) out.println("No function named " + name);
	}

	void decompileAt(Address addr) {
		Function f = getFunctionContaining(addr);
		if (f == null) { out.println("No function at " + addr); return; }
		decompileFunction(f);
	}

	void decompileFunction(Function f) {
		out.println("===== " + f.getName() + " @ " + f.getEntryPoint() + " =====");
		DecompileResults res = dec().decompileFunction(f, 60, new ConsoleTaskMonitor());
		if (!res.decompileCompleted()) { out.println("Decompile failed: " + res.getErrorMessage()); return; }
		out.println(res.getDecompiledFunction().getC());
	}

	void cmdBytes(String hex, int count) {
		Address addr = toAddr(hex);
		StringBuilder sb = new StringBuilder();
		try {
			for (int i = 0; i < count; i++) {
				if (i % 16 == 0) { if (i > 0) out.println(sb.toString()); sb.setLength(0); sb.append(addr.add(i)).append(": "); }
				int b = currentProgram.getMemory().getByte(addr.add(i)) & 0xff;
				sb.append(String.format("%02x ", b));
			}
			out.println(sb.toString());
			out.println("--- as little-endian dwords ---");
			for (int i = 0; i + 4 <= count; i += 4) {
				int v = currentProgram.getMemory().getInt(addr.add(i));
				out.println(addr.add(i) + ": " + v + "  (0x" + Integer.toHexString(v) + ")");
			}
		} catch (MemoryAccessException e) {
			out.println("Memory access error: " + e.getMessage());
		}
	}

	String escape(String s) {
		return s.replace("\n", "\\n").replace("\r", "\\r");
	}
}
